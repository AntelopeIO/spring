#pragma once

#include "finality_test_cluster.hpp"

using mvo = mutable_variant_object;

using namespace eosio::chain;

namespace finality_proof {

   // data relevant to IBC
   struct ibc_block_data_t {
      signed_block_ptr block;
      qc_data_t qc_data;
      action_trace onblock_trace;
      finality_data_t finality_data;
      uint32_t active_finalizer_policy_generation = 0;
      uint32_t last_pending_finalizer_policy_generation = 0;
      uint32_t last_proposed_finalizer_policy_generation = 0;
      digest_type action_mroot; //this is the real action_mroot, as returned from finality_data
      digest_type base_digest;
      digest_type active_finalizer_policy_digest;
      digest_type last_pending_finalizer_policy_digest;
      block_timestamp_type last_pending_finalizer_policy_start_timestamp;
      digest_type last_proposed_finalizer_policy_digest;
      digest_type finality_digest;
      digest_type level_3_commitments_digest;
      digest_type level_2_commitments_digest;
      digest_type finality_leaf;
      digest_type finality_root;
      block_timestamp_type parent_timestamp;
   };

   static digest_type hash_pair(const digest_type& a, const digest_type& b) {
      return fc::sha256::hash(std::pair<const digest_type&, const digest_type&>(a, b));
   }

   //generate a proof of inclusion for a node at index from a list of leaves
   static std::vector<digest_type> generate_proof_of_inclusion(const std::vector<digest_type>& leaves, const size_t index) {
      auto _leaves = leaves;
      auto _index = index;

      std::vector<digest_type> merkle_branches;

      while (_leaves.size()>1){
         std::vector<digest_type> new_level;
         for (size_t i = 0 ; i < _leaves.size() ; i+=2){
            digest_type left = _leaves[i];

            if (i + 1 < _leaves.size() ){
               // Normal case: both children exist and are not at the end or are even
               digest_type right = _leaves[i+1];

               new_level.push_back(hash_pair(left, right));
               if (_index == i || _index == i + 1) {
                 merkle_branches.push_back(_index == i ? right : left);
                 _index = i / 2; // Update index for next level

               }
            } else {
               // Odd number of leaves at this level, and we're at the end
               new_level.push_back(left); // Promote the left (which is also the right in this case)
               if (_index == i) _index = i / 2; // Update index for next level, no sibling to add

            }
         }
         _leaves = std::move(new_level);
      }
      return merkle_branches;
   }

   //extract instant finality data from block header extension, as well as qc data from block extension
   static qc_data_t extract_qc_data(const signed_block_ptr& b) {
      assert(b);
      auto hexts = b->validate_and_extract_header_extensions();
      if (auto f_entry = hexts.find(finality_extension::extension_id()); f_entry != hexts.end()) {
         auto& f_ext   = std::get<finality_extension>(f_entry->second);

         // get the matching qc extension if present
         auto exts = b->validate_and_extract_extensions();
         if (auto entry = exts.find(quorum_certificate_extension::extension_id()); entry != exts.end()) {
            auto& qc_ext = std::get<quorum_certificate_extension>(entry->second);
            return qc_data_t{ std::move(qc_ext.qc), f_ext.qc_claim };
         }
         return qc_data_t{ {}, f_ext.qc_claim };
      }
      return {};
   }

   static bool has_finalizer_policy_diffs(const signed_block_ptr& block){

      // extract new finalizer policy
      finality_extension f_ext = block->extract_header_extension<finality_extension>();

      return f_ext.new_finalizer_policy_diff.has_value();

   }

   static finalizer_policy update_finalizer_policy(const signed_block_ptr block, const finalizer_policy& current_policy){

      // extract new finalizer policy
      finality_extension f_ext = block->extract_header_extension<finality_extension>();

      assert(f_ext.new_finalizer_policy_diff.has_value());

      finalizer_policy active_finalizer_policy =
         current_policy.apply_diff(f_ext.new_finalizer_policy_diff.value());

      return active_finalizer_policy;

   }

   struct policy_count {
      finalizer_policy policy;
      int32_t blocks_since_proposed = 0;
   };

   template<size_t NUM_NODES>
   class proof_test_cluster : public finality_test_cluster<NUM_NODES> {
   public:

      /*****

         The proof_test_cluster class inherits from finality_test_cluster and serves to generate finality proofs for the purpose of IBC and proving finality violations.

         It has its own high-level produce_block function, which hides all the internal consensus details, and returns an extended struct containing data relevant for proof generation.

         It doesn't support forks or rollbacks, and always assumes the happy path in finality progression, which is sufficient for the purpose of generating finality proofs for testing.
         
         It also assumes a single producer pre-transition, resulting in only 2 transition blocks when IF is activated.

      *****/
         
      // cache last proposed, last pending and currently active finalizer policies + digests
      finalizer_policy last_proposed_finalizer_policy;
      digest_type last_proposed_finalizer_policy_digest;

      finalizer_policy last_pending_finalizer_policy;
      digest_type last_pending_finalizer_policy_digest;

      finalizer_policy active_finalizer_policy;
      digest_type active_finalizer_policy_digest;

      block_timestamp_type timestamp;
      block_timestamp_type parent_timestamp;

      block_timestamp_type prev_last_pending_finalizer_policy_start_timestamp;

      // counter to (optimistically) track internal policy changes
      std::unordered_map<digest_type, policy_count> blocks_since_proposed_policy;

      // internal flag to indicate whether or not block is the IF genesis block
      bool is_genesis = true;
      // internal flag to indicate whether or not the transition is complete
      bool is_transition = true;

      // returns finality leaves for construction of merkle proofs
      std::vector<digest_type> get_finality_leaves(const size_t cutoff){
         assert(cutoff>=0 && cutoff<finality_leaves.size());
         return {finality_leaves.begin(), finality_leaves.begin() + cutoff + 1};
      }

      ibc_block_data_t process_result(eosio::testing::produce_block_result_t result){

         signed_block_ptr block = result.block;

         BOOST_REQUIRE(result.onblock_trace->action_traces.size()>0);

         action_trace onblock_trace = result.onblock_trace->action_traces[0];

         block_timestamp_type last_pending_finalizer_policy_start_timestamp;

         for (auto& p : blocks_since_proposed_policy) p.second.blocks_since_proposed++;

         //skip this part on genesis
         if (!is_genesis){
            parent_timestamp = timestamp;
            last_pending_finalizer_policy_start_timestamp = prev_last_pending_finalizer_policy_start_timestamp;
            for (const auto& p : blocks_since_proposed_policy){

               //under the happy path with strong QCs in every block, a policy becomes active 4 blocks after being proposed
               if (p.second.blocks_since_proposed == 2 * eosio::testing::num_chains_to_final && p.first != active_finalizer_policy_digest){
                  active_finalizer_policy = p.second.policy;
                  active_finalizer_policy_digest = p.first;
               }
               //under the happy path with strong QCs in every block, a policy becomes pending 2 blocks after being proposed
               else if (p.second.blocks_since_proposed == eosio::testing::num_chains_to_final && p.first != last_pending_finalizer_policy_digest){

                  last_pending_finalizer_policy = p.second.policy;
                  last_pending_finalizer_policy_digest = p.first;
                  last_pending_finalizer_policy_start_timestamp = block->timestamp;
                  prev_last_pending_finalizer_policy_start_timestamp = block->timestamp;
               }
            }
         }

         timestamp = block->timestamp;

         // if we have policy diffs, process them
         if (has_finalizer_policy_diffs(block)){
            if (is_genesis) {
               // if block is genesis, the initial policy is the last proposed, last pending and currently active
               last_proposed_finalizer_policy        = update_finalizer_policy(block, eosio::chain::finalizer_policy());
               last_proposed_finalizer_policy_digest = fc::sha256::hash(last_proposed_finalizer_policy);
               last_pending_finalizer_policy         = last_proposed_finalizer_policy;
               last_pending_finalizer_policy_digest  = last_proposed_finalizer_policy_digest;
               last_pending_finalizer_policy_start_timestamp = block->timestamp;
               prev_last_pending_finalizer_policy_start_timestamp = block->timestamp;
               active_finalizer_policy               = last_proposed_finalizer_policy;
               active_finalizer_policy_digest        = last_proposed_finalizer_policy_digest;
               blocks_since_proposed_policy[last_proposed_finalizer_policy_digest] = {last_proposed_finalizer_policy, 0};
            } else {
               // if block is not genesis, the new policy is proposed
               last_proposed_finalizer_policy        = update_finalizer_policy(block, last_proposed_finalizer_policy);
               last_proposed_finalizer_policy_digest = fc::sha256::hash(last_proposed_finalizer_policy);
               blocks_since_proposed_policy[last_proposed_finalizer_policy_digest] = {last_proposed_finalizer_policy, 0};
            }
         }

         //process votes and collect / compute the IBC-relevant data
         this->process_votes(1, this->num_needed_for_quorum); //enough to reach quorum threshold

         finality_data_t finality_data = *this->node0.control->head_finality_data();
         digest_type action_mroot = finality_data.action_mroot;
         digest_type base_digest = finality_data.base_digest;

         // compute commitments used for proving finality violations
         digest_type level_3_commitments_digest = fc::sha256::hash(level_3_commitments_t{
            .reversible_blocks_mroot = finality_data.reversible_blocks_mroot,
            .latest_qc_claim_block_num = finality_data.latest_qc_claim_block_num,
            .latest_qc_claim_finality_digest = finality_data.latest_qc_claim_finality_digest,
            .latest_qc_claim_timestamp = finality_data.latest_qc_claim_timestamp,
            .timestamp = timestamp,
            .base_digest = base_digest
         });

         // compute commitments used for proving finalizer policy changes
         digest_type level_2_commitments_digest = fc::sha256::hash(level_2_commitments_t{
            .last_pending_fin_pol_digest = last_pending_finalizer_policy_digest,
            .last_pending_fin_pol_start_timestamp = last_pending_finalizer_policy_start_timestamp,
            .l3_commitments_digest = level_3_commitments_digest
         });

         // during IF transition, finality_root is always set to an empty digest
         digest_type finality_root = digest_type();

         // after transition, finality_root can be obtained from the action_mroot field of the block header
         if (!is_transition) finality_root = block->action_mroot;

         // compute digest for verification purposes
         digest_type finality_digest = fc::sha256::hash(finality_digest_data_v1{
            .active_finalizer_policy_generation       = is_genesis ? 1 : active_finalizer_policy.generation,
            .last_pending_finalizer_policy_generation = is_genesis ? 1 : last_pending_finalizer_policy.generation,
            .finality_tree_digest                     = finality_root,
            .l2_commitments_digest                    = level_2_commitments_digest
         });

         // compute finality leaf
         digest_type finality_leaf = fc::sha256::hash(valid_t::finality_leaf_node_t{
            .block_num = block->block_num(),
            .timestamp = timestamp,
            .parent_timestamp = parent_timestamp,
            .finality_digest = finality_digest,
            .action_mroot = action_mroot
         });

         // add finality leaf to the internal list
         finality_leaves.push_back(finality_leaf);

         if (is_transition && !is_genesis) is_transition = false; // if we are no longer in transition mode, set to false
         if (is_genesis) is_genesis = false; // if IF genesis block, set to false 

         qc_data_t qc_data = extract_qc_data(block);

         // return relevant IBC information
         return ibc_block_data_t{
            .block = block, 
            .qc_data = qc_data, 
            .onblock_trace = onblock_trace, 
            .finality_data = finality_data, 
            .active_finalizer_policy_generation = active_finalizer_policy.generation, 
            .last_pending_finalizer_policy_generation = last_pending_finalizer_policy.generation, 
            .last_proposed_finalizer_policy_generation = last_proposed_finalizer_policy.generation, 
            .action_mroot = action_mroot, 
            .base_digest = base_digest, 
            .active_finalizer_policy_digest = active_finalizer_policy_digest, 
            .last_pending_finalizer_policy_digest = last_pending_finalizer_policy_digest, 
            .last_pending_finalizer_policy_start_timestamp = last_pending_finalizer_policy_start_timestamp,
            .last_proposed_finalizer_policy_digest = last_proposed_finalizer_policy_digest, 
            .finality_digest = finality_digest, 
            .level_3_commitments_digest = level_3_commitments_digest, 
            .level_2_commitments_digest = level_2_commitments_digest, 
            .finality_leaf = finality_leaf,
            .finality_root = finality_root ,
            .parent_timestamp = parent_timestamp 
         };

      }

      // produce and propagate a block, update internal state as needed, and returns relevant IBC data 
      ibc_block_data_t produce_block(){
         eosio::testing::produce_block_result_t result = this->produce_and_push_block_ex();
         return(process_result(result));
      }

      ibc_block_data_t produce_blocks(const uint32_t count){
         ibc_block_data_t result;
         for (uint32_t i = 0 ; i < count ; i++){
            result=produce_block();
         }
         return result; //return last produced block
      }

      proof_test_cluster(finality_cluster_config_t config = {.transition_to_savanna = false})
      : finality_test_cluster<NUM_NODES>(config) {

      }

   private:
      std::vector<digest_type> finality_leaves;

   };

}
