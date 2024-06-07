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

   // data relevant to finality proofs
   struct finality_block_data_t {
      signed_block_ptr block;
      qc_data_t qc_data;
      action_trace onblock_trace;
      finality_data_t finality_data;
      digest_type action_mroot;
      digest_type base_digest;
      finalizer_policy active_finalizer_policy;
      finalizer_policy last_pending_finalizer_policy;
      finalizer_policy last_proposed_finalizer_policy;
      digest_type finality_digest;
      digest_type computed_finality_digest;
      digest_type afp_base_digest;
      digest_type finality_leaf;
      digest_type finality_root;
   };

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

   //extract instant finality data from block header extension, as well as qc data from block extension
   static qc_data_t extract_qc_data(const signed_block_ptr& b) {
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
   static mvo get_finality_proof(const finality_block_data_t target_block, 
                                 const finality_block_data_t qc_block, 
                                 const uint32_t target_block_index, 
                                 const uint32_t final_block_index, 
                                 const std::string signature, 
                                 const std::string bitset,
                                 const std::vector<digest_type> proof_of_inclusion){

      mutable_variant_object proof = mvo()
            ("finality_proof", mvo()
               ("qc_block", mvo()
                  ("major_version", 1)
                  ("minor_version", 0)
                  ("finalizer_policy_generation", qc_block.active_finalizer_policy.generation)
                  ("final_on_qc_block_num", target_block.block->block_num() - (final_block_index-target_block_index) )
                  ("witness_hash", qc_block.afp_base_digest)
                  ("finality_mroot", qc_block.finality_root)
               )
               ("qc", mvo()
                  ("signature", signature)
                  ("finalizers", bitset) 
               )
            )
            ("target_block_proof_of_inclusion", mvo() 
               ("target_block_index", target_block_index)
               ("final_block_index", final_block_index)
               ("target", fc::variants{"simple_block_data", mvo() 
                  ("major_version", 1)
                  ("minor_version", 0)
                  ("finality_digest", target_block.finality_digest)
                  ("dynamic_data", mvo() 
                     ("block_num", target_block.block->block_num())
                     ("action_proofs", fc::variants())
                     ("action_mroot", target_block.action_mroot)
                  )}
               )
               ("merkle_branches", proof_of_inclusion)
            );

      return proof;

   }

   struct policy_count {
      finalizer_policy policy;
      int32_t blocks_since_proposed;
   };

   struct proof_of_finality {
      finalizer_policy policy;
      std::vector<finality_block_data_t> qc_chain;
      mvo proof;
      bool replace;
   };

   template<size_t NUM_NODES>
   class proof_test_cluster : public finality_test_cluster<NUM_NODES> {
   public:

      // cache last proposed, last pending and currently active finalizer policies + digests
      eosio::chain::finalizer_policy last_proposed_finalizer_policy;
      eosio::chain::finalizer_policy last_pending_finalizer_policy;
      eosio::chain::finalizer_policy active_finalizer_policy;

      std::vector<finality_block_data_t> qc_chain;
      std::vector<proof_of_finality> proofs_of_finality;

      // counter to (optimistically) track internal policy changes
      std::unordered_map<digest_type, policy_count> blocks_since_proposed_policy;

      //node0 always produce blocks. This vector determines if votes from node1, node2, etc. are propagated
      std::vector<bool> vote_propagation;
      
      // internal flag to indicate whether or not block is the IF genesis block
      bool is_genesis = true;
      // internal flag to indicate whether or not the transition is complete
      bool is_transition = true;

      uint32_t genesis_block_num = 0;
      
      std::string bitset;

      // returns finality leaves for construction of merkle proofs
      std::vector<digest_type> get_finality_leaves(const size_t cutoff){
         assert(cutoff>=0 && cutoff<finality_leaves.size());
         return std::vector<digest_type>(finality_leaves.begin(), finality_leaves.begin() + cutoff + 1);
      }

      finality_block_data_t process_result(eosio::testing::produce_block_result_t result){

         signed_block_ptr block = result.block;

         BOOST_REQUIRE(result.onblock_trace->action_traces.size()>0);

         action_trace onblock_trace = result.onblock_trace->action_traces[0];

         for (auto& p : blocks_since_proposed_policy) p.second.blocks_since_proposed++;

         bool replace = true;

         if (is_genesis){
            last_proposed_finalizer_policy = update_finalizer_policy(block, eosio::chain::finalizer_policy());;
            last_pending_finalizer_policy = last_proposed_finalizer_policy;
            active_finalizer_policy = last_proposed_finalizer_policy;
            blocks_since_proposed_policy[fc::sha256::hash(last_proposed_finalizer_policy)] = {active_finalizer_policy, 0};
            genesis_block_num = block->block_num();
         }
         else {
            for (auto& p : blocks_since_proposed_policy){
               if (p.second.blocks_since_proposed == 6){
                  active_finalizer_policy = p.second.policy;
                  //replace = false; //tombstone moment for the previous policy
                  replace = false;
               }
               else if (p.second.blocks_since_proposed == 3){
                  last_pending_finalizer_policy = p.second.policy;
               }
            }
               
            // if we have policy diffs, process them
            if (has_finalizer_policy_diffs(block)){
               // if block is not genesis, the new policy is proposed
               last_proposed_finalizer_policy = update_finalizer_policy(block, last_proposed_finalizer_policy);
               blocks_since_proposed_policy[fc::sha256::hash(last_proposed_finalizer_policy)] = {last_proposed_finalizer_policy, 0};
            }
         }

         //process votes and collect / compute the finality-relevant data

         if (vote_propagation.size() == 0) this->process_votes(1, this->num_needed_for_quorum); //enough to reach quorum threshold
         else this->process_finalizer_votes(vote_propagation); //enough to reach quorum threshold

         finality_data_t finality_data = *this->node0.control->head_finality_data();
         digest_type action_mroot = finality_data.action_mroot;
         digest_type base_digest = finality_data.base_digest;
         digest_type afp_base_digest = hash_pair(fc::sha256::hash(last_pending_finalizer_policy), base_digest);
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

         if (is_transition && !is_genesis) is_transition = false; // if we are no longer in transition mode, set is_transition flag to false
         if (is_genesis) is_genesis = false; // if this was the genesis block, set is_genesis flag to false 

         qc_data_t qc_data = extract_qc_data(block);

         // relevant finality information
         finality_block_data_t finality_block_data{block, qc_data, onblock_trace, finality_data, action_mroot, base_digest, active_finalizer_policy, last_pending_finalizer_policy, last_proposed_finalizer_policy, finality_digest, computed_finality_digest, afp_base_digest, finality_leaf, finality_root };

         if (qc_chain.size()==4) qc_chain.erase(qc_chain.begin());

         qc_chain.push_back(finality_block_data);

         if (qc_chain.size() == 4){

            uint32_t target_block_num = qc_chain[0].block->block_num() - genesis_block_num;

            auto proof = get_finality_proof( qc_chain[0],    
                                             qc_chain[2], 
                                             target_block_num, 
                                             target_block_num, 
                                             qc_chain[3].qc_data.qc.value().data.sig.to_string(),
                                             bitset,
                                             finality_proof::generate_proof_of_inclusion(get_finality_leaves(target_block_num), target_block_num));
   
            if (proofs_of_finality.size()>0 && proofs_of_finality[proofs_of_finality.size()-1].replace) proofs_of_finality.pop_back();

            proofs_of_finality.push_back({active_finalizer_policy, qc_chain, proof, replace});

         }

         return finality_block_data;

      }

      // produce and propagate a block, update internal state as needed, and returns relevant finality data 
      finality_block_data_t produce_block(){
         assert(vote_propagation.size() == 0 || vote_propagation.size() == NUM_NODES-1);
         eosio::testing::produce_block_result_t result = this->produce_and_push_block_ex();
         return(process_result(result));
      }

      finality_block_data_t produce_blocks(const uint32_t count){
         finality_block_data_t result;
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
