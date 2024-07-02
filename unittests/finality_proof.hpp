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
      digest_type last_proposed_finalizer_policy_digest;
      digest_type finality_digest;
      //digest_type computed_finality_digest;
      digest_type afp_base_digest;
      digest_type finality_leaf;
      digest_type finality_root;
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
      if (auto if_entry = hexts.lower_bound(instant_finality_extension::extension_id()); if_entry != hexts.end()) {
         auto& if_ext   = std::get<instant_finality_extension>(if_entry->second);

         // get the matching qc extension if present
         auto exts = b->validate_and_extract_extensions();
         if (auto entry = exts.lower_bound(quorum_certificate_extension::extension_id()); entry != exts.end()) {
            auto& qc_ext = std::get<quorum_certificate_extension>(entry->second);
            return qc_data_t{ std::move(qc_ext.qc), if_ext.qc_claim };
         }
         return qc_data_t{ {}, if_ext.qc_claim };
      }
      return {};
   }

   static bool has_finalizer_policy_diffs(const signed_block_ptr& block){

      // extract new finalizer policy
      instant_finality_extension if_ext = block->extract_header_extension<instant_finality_extension>();

      return if_ext.new_finalizer_policy_diff.has_value();

   }

   static finalizer_policy update_finalizer_policy(const signed_block_ptr block, const finalizer_policy& current_policy){

      // extract new finalizer policy
      instant_finality_extension if_ext = block->extract_header_extension<instant_finality_extension>();

      assert(if_ext.new_finalizer_policy_diff.has_value());

      finalizer_policy active_finalizer_policy =
         current_policy.apply_diff(if_ext.new_finalizer_policy_diff.value());

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

         for (auto& p : blocks_since_proposed_policy) p.second.blocks_since_proposed++;

         //skip this part on genesis
         if (!is_genesis){
            for (const auto& p : blocks_since_proposed_policy){
               //under the happy path with strong QCs in every block, a policy becomes active 6 blocks after being proposed
               if (p.second.blocks_since_proposed == 6){
                  active_finalizer_policy = p.second.policy;
                  active_finalizer_policy_digest = p.first;
               }
               //under the happy path with strong QCs in every block, a policy becomes pending 3 blocks after being proposed
               else if (p.second.blocks_since_proposed == 3){
                  last_pending_finalizer_policy = p.second.policy;
                  last_pending_finalizer_policy_digest = p.first;
               }
            }
         }

         // if we have policy diffs, process them
         if (has_finalizer_policy_diffs(block)){
            if (is_genesis) {
               // if block is genesis, the initial policy is the last proposed, last pending and currently active
               last_proposed_finalizer_policy        = update_finalizer_policy(block, eosio::chain::finalizer_policy());
               last_proposed_finalizer_policy_digest = fc::sha256::hash(last_proposed_finalizer_policy);
               last_pending_finalizer_policy         = last_proposed_finalizer_policy;
               last_pending_finalizer_policy_digest  = last_proposed_finalizer_policy_digest;
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
         digest_type afp_base_digest = hash_pair(last_pending_finalizer_policy_digest, base_digest);
         //digest_type finality_digest;

         // during IF transition, finality_root is always set to an empty digest
         digest_type finality_root = digest_type();

         // after transition, finality_root can be obtained from the action_mroot field of the block header
         if (!is_transition) finality_root = block->action_mroot;

         // compute digest for verification purposes
         digest_type finality_digest = fc::sha256::hash(finality_digest_data_v1{
               .active_finalizer_policy_generation      = is_genesis ? 1 : active_finalizer_policy.generation,
               .final_on_strong_qc_block_num            = finality_data.final_on_strong_qc_block_num,
               .finality_tree_digest                    = finality_root,
               .last_pending_finalizer_policy_and_base_digest = afp_base_digest
            });

         // compute finality leaf
         digest_type finality_leaf = fc::sha256::hash(valid_t::finality_leaf_node_t{
            .block_num = block->block_num(),
            .finality_digest = finality_digest,
            .action_mroot = action_mroot
         });

         // add finality leaf to the internal list
         finality_leaves.push_back(finality_leaf);

         if (is_transition && !is_genesis) is_transition = false; // if we are no longer in transition mode, set to false
         if (is_genesis) is_genesis = false; // if IF genesis block, set to false 

         qc_data_t qc_data = extract_qc_data(block);

         if (qc_data.qc.has_value()) {
             dlog("qc claim  : ${bn} , sig: ${s}", ("bn", qc_data.qc_claim.block_num)("s", qc_data.qc.value().data.sig.to_string()));
         }

         // return relevant IBC information
         return {block, qc_data, onblock_trace, finality_data, active_finalizer_policy.generation, last_pending_finalizer_policy.generation, last_proposed_finalizer_policy.generation, action_mroot, base_digest, active_finalizer_policy_digest, last_pending_finalizer_policy_digest, last_proposed_finalizer_policy_digest, finality_digest, afp_base_digest, finality_leaf, finality_root };

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
