#pragma once

#include "finality_test_cluster.hpp"

/*****

   The proof_test_cluster class inherits from finality_test_cluster and serves to generate finality proofs for the purpose of IBC and proving finality violations.

   It has its own high-level produce_block function, which hides all the internal consensus details, and returns an extended struct containing data relevant for proof generation.

   It doesn't support forks or rollbacks, and always assumes the happy path in finality progression, which is sufficient for the purpose of generating finality proofs for testing.
   
   It also assumes a single producer pre-transition, resulting in only 2 transition blocks when IF is activated.

*****/

using mvo = mutable_variant_object;

namespace finality_proof {


   // data relevant to IBC
   struct ibc_block_data_t {
      signed_block_ptr block;
      action_trace onblock_trace;
      finality_data_t finality_data;
      digest_type action_mroot;
      digest_type base_digest;
      digest_type active_finalizer_policy_digest;
      digest_type last_pending_finalizer_policy_digest;
      digest_type last_proposed_finalizer_policy_digest;
      digest_type finality_digest;
      digest_type computed_finality_digest;
      digest_type afp_base_digest;
      digest_type finality_leaf;
      digest_type finality_root;
   };

/*   static digest_type encode_num_in_digest(const digest_type& original_digest, const uint32_t num) {
      block_id_type result = original_digest;
      result._hash[0] &= 0xffffffff00000000;
      result._hash[0] += fc::endian_reverse_u32(num);
      return result;
   }*/

   //generate a proof of inclusion for a node at index from a list of leaves
   static std::vector<digest_type> generate_proof_of_inclusion(const std::vector<digest_type> leaves, const size_t index) {
      auto _leaves = leaves;
      auto _index = index;

      std::vector<digest_type> merkle_branches;

      while (_leaves.size()>1){
         std::vector<digest_type> new_level;
         for (size_t i = 0 ; i < _leaves.size() ; i+=2){
            digest_type left = _leaves[i];

            if (i + 1 < _leaves.size() && (i + 1 != _leaves.size() - 1 || _leaves.size() % 2 == 0)){
               // Normal case: both children exist and are not at the end or are even
               digest_type right = _leaves[i+1];

               new_level.push_back(fc::sha256::hash(std::pair<digest_type, digest_type>(left, right)));
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
         _leaves = new_level;
      }
      return merkle_branches;
   }

   static digest_type hash_pair(const digest_type& a, const digest_type& b) {
      return fc::sha256::hash(std::pair<const digest_type&, const digest_type&>(a, b));
   }

   static bool has_finalizer_policy_diffs(const signed_block_ptr& block){

      // extract new finalizer policy
      instant_finality_extension if_ext = block->extract_header_extension<instant_finality_extension>();

      if (if_ext.new_finalizer_policy_diff.has_value()) return true;
      else return false;

   }

   static eosio::chain::finalizer_policy update_finalizer_policy(const signed_block_ptr block, eosio::chain::finalizer_policy current_policy){

      // extract new finalizer policy
      instant_finality_extension if_ext = block->extract_header_extension<instant_finality_extension>();

      assert(if_ext.new_finalizer_policy_diff.has_value());

      eosio::chain::finalizer_policy active_finalizer_policy =
         current_policy.apply_diff(if_ext.new_finalizer_policy_diff.value());

      return active_finalizer_policy;

   }

   static mvo get_finality_proof(const ibc_block_data_t target_block, 
                                 const ibc_block_data_t qc_block, 
                                 const uint32_t target_block_finalizer_policy_generation, 
                                 const uint32_t qc_block_finalizer_policy_generation, 
                                 const uint32_t target_node_index, 
                                 const uint32_t last_node_index, 
                                 const std::string signature, 
                                 const std::string bitset,
                                 const std::vector<digest_type> proof_of_inclusion){

      mutable_variant_object proof = mvo()
            ("finality_proof", mvo()
               ("qc_block", mvo()
                  ("major_version", 1)
                  ("minor_version", 0)
                  ("finalizer_policy_generation", qc_block_finalizer_policy_generation)
                  ("witness_hash", qc_block.afp_base_digest)
                  ("finality_mroot", qc_block.finality_root)
               )
               ("qc", mvo()
                  ("signature", signature)
                  ("finalizers", bitset) 
               )
            )
            ("target_block_proof_of_inclusion", mvo() 
               ("target_node_index", target_node_index)
               ("last_node_index", last_node_index)
               ("target",  mvo() 
                  ("finality_data", mvo() 
                     ("major_version", 1)
                     ("minor_version", 0)
                     ("finalizer_policy_generation", target_block_finalizer_policy_generation)
                     ("witness_hash", target_block.afp_base_digest)
                     ("finality_mroot", target_block.finality_root)
                  )
                  ("dynamic_data", mvo() 
                     ("block_num", target_block.block->block_num())
                     ("action_proofs", fc::variants())
                     ("action_mroot", target_block.action_mroot)
                  )
               )
               ("merkle_branches", proof_of_inclusion)
            );

      return proof;

   }

   template<size_t NUM_NODES>
   class proof_test_cluster : public finality_test_cluster<NUM_NODES> {
   public:

      // cache last proposed, last pending and currently active finalizer policies + digests
      eosio::chain::finalizer_policy last_proposed_finalizer_policy;
      digest_type last_proposed_finalizer_policy_digest;

      eosio::chain::finalizer_policy last_pending_finalizer_policy;
      digest_type last_pending_finalizer_policy_digest;

      eosio::chain::finalizer_policy active_finalizer_policy;
      digest_type active_finalizer_policy_digest;

      // counter to (optimistically) track internal policy changes
      uint32_t blocks_since_proposed_policy = 0;

      //node0 always produce blocks. This vector determines if votes from node1, node2, etc. are propagated
      std::vector<bool> vote_propagation;
      
      // internal flag to indicate whether or not block is the IF genesis block
      bool is_genesis = true;

      bool is_transition = true;

      // returns finality leaves for construction of merkle proofs
      std::vector<digest_type> get_finality_leaves(const size_t cutoff){
         assert(cutoff>=0 && cutoff<finality_leaves.size());
         return std::vector<digest_type>(finality_leaves.begin(), finality_leaves.begin() + cutoff + 1);
      }

      ibc_block_data_t process_result(eosio::testing::produce_block_result_t result){

         signed_block_ptr block = result.block;

         BOOST_REQUIRE(result.onblock_trace->action_traces.size()>0);

         action_trace onblock_trace = result.onblock_trace->action_traces[0];

         // if we have policy diffs, process them
         if (has_finalizer_policy_diffs(block)){

            if (is_genesis){
               // if block is genesis, the initial policy is the last proposed, last pending and currently active
               last_proposed_finalizer_policy = update_finalizer_policy(block, eosio::chain::finalizer_policy());;
               last_proposed_finalizer_policy_digest = fc::sha256::hash(last_proposed_finalizer_policy);
               last_pending_finalizer_policy = last_proposed_finalizer_policy;
               last_pending_finalizer_policy_digest = last_proposed_finalizer_policy_digest;
               active_finalizer_policy = last_proposed_finalizer_policy;
               active_finalizer_policy_digest = last_proposed_finalizer_policy_digest;
            }
            else {
               // if block is not genesis, the new policy is proposed
               last_proposed_finalizer_policy = update_finalizer_policy(block, active_finalizer_policy);
               last_proposed_finalizer_policy_digest = fc::sha256::hash(last_proposed_finalizer_policy);
               blocks_since_proposed_policy = 0;
            }

         }

         // after 3 QCs, the proposed policy becomes pending
         if (last_pending_finalizer_policy_digest!= last_proposed_finalizer_policy_digest && blocks_since_proposed_policy == 3){
            last_pending_finalizer_policy = last_proposed_finalizer_policy;
            last_pending_finalizer_policy_digest = fc::sha256::hash(last_pending_finalizer_policy);
         }

         // after 3 more QCs (6 total since the policy was proposed) the pending policy becomes active
         if (active_finalizer_policy_digest!= last_pending_finalizer_policy_digest && blocks_since_proposed_policy == 6){
            active_finalizer_policy = last_pending_finalizer_policy;
            active_finalizer_policy_digest = fc::sha256::hash(active_finalizer_policy);
         }

         blocks_since_proposed_policy++;

         //process votes and collect / compute the IBC-relevant data

         if (vote_propagation.size() == 0) this->process_votes(1, this->num_needed_for_quorum); //enough to reach quorum threshold
         else this->process_finalizer_votes(vote_propagation); //enough to reach quorum threshold

         finality_data_t finality_data = *this->node0.control->head_finality_data();
         digest_type action_mroot = finality_data.action_mroot;
         digest_type base_digest = finality_data.base_digest;
         digest_type afp_base_digest = hash_pair(last_pending_finalizer_policy_digest, base_digest);
         digest_type finality_digest;

         if (is_genesis){

            // one-time genesis finality digest computation
            finality_digest = fc::sha256::hash(eosio::chain::finality_digest_data_v1{
               .active_finalizer_policy_generation      = 1,
               .final_on_strong_qc_block_num            = finality_data.final_on_strong_qc_block_num,
               .finality_tree_digest                    = digest_type(), //nothing to finalize yet
               .last_pending_finalizer_policy_and_base_digest = afp_base_digest
            });
         }
         else finality_digest = this->node0.control->get_strong_digest_by_id(block->calculate_id());

         // compute finality leaf
         digest_type finality_leaf = fc::sha256::hash(valid_t::finality_leaf_node_t{
            .block_num = block->block_num(),
            .finality_digest = finality_digest,
            .action_mroot = action_mroot
         });

         // during IF transition, finality_root is always set to an empty digest
         digest_type finality_root = digest_type();

         // after transition, finality_root can be obtained from the action_mroot field of the block header
         if (!is_transition) finality_root = block->action_mroot;

         // compute digest for verification purposes
         digest_type computed_finality_digest = fc::sha256::hash(eosio::chain::finality_digest_data_v1{
               .active_finalizer_policy_generation      = active_finalizer_policy.generation,
               .final_on_strong_qc_block_num            = finality_data.final_on_strong_qc_block_num,
               .finality_tree_digest                    = is_genesis ? digest_type() : finality_root,
               .last_pending_finalizer_policy_and_base_digest = afp_base_digest
            });

         // add finality leaf to the internal list
         finality_leaves.push_back(finality_leaf);

         if (is_transition && !is_genesis) is_transition = false; // if we are no longer in transition mode, set to false
         if (is_genesis) is_genesis = false; // if IF genesis block, set to false 

         // return relevant IBC information
         return {block, onblock_trace, finality_data, action_mroot, base_digest, active_finalizer_policy_digest, last_pending_finalizer_policy_digest, last_proposed_finalizer_policy_digest, finality_digest, computed_finality_digest, afp_base_digest, finality_leaf, finality_root };

      }

      // produce and propagate a block, update internal state as needed, and returns relevant IBC data 
      ibc_block_data_t produce_block(){
         assert(vote_propagation.size() == 0 || vote_propagation.size() == NUM_NODES-1);
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
