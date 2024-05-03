#include <eosio/chain/abi_serializer.hpp>
#include <eosio/testing/tester.hpp>

#include <fc/variant_object.hpp>

#include <boost/test/unit_test.hpp>

#include <contracts.hpp>
#include <test_contracts.hpp>
#include "fork_test_utilities.hpp"

#include <eosio/chain/exceptions.hpp>

#include "finality_test_cluster.hpp"

using namespace eosio::chain;
using namespace eosio::testing;

using mvo = mutable_variant_object;

static digest_type hash_pair(const digest_type& a, const digest_type& b) {
   return fc::sha256::hash(std::pair<const digest_type&, const digest_type&>(a, b));
}

BOOST_AUTO_TEST_SUITE(svnn_ibc)

   //extract instant finality data from block header extension, as well as qc data from block extension
   qc_data_t extract_qc_data(const signed_block_ptr& b) {
      std::optional<qc_data_t> qc_data;
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

   //generate a proof of inclusion for a node at index from a list of leaves
   std::vector<digest_type> generate_proof_of_inclusion(const std::vector<digest_type> leaves, const size_t index) {
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

   BOOST_AUTO_TEST_CASE(ibc_test) { try {

      // cluster is set up with the head about to produce IF Genesis
      finality_test_cluster cluster { finality_test_cluster::cluster_config_t{.transition_to_savanna = false} };

      // produce IF Genesis block
      auto genesis_block = cluster.produce_and_push_block();

      // ensure out of scope setup and initial cluster wiring is consistent
      BOOST_CHECK_EQUAL(genesis_block->block_num(), 4);

      // check if IF Genesis block contains an IF extension
      std::optional<eosio::chain::block_header_extension> maybe_genesis_if_ext =
         genesis_block->extract_header_extension(eosio::chain::instant_finality_extension::extension_id());
      BOOST_CHECK(maybe_genesis_if_ext.has_value());

      eosio::chain::block_header_extension genesis_if_ext = maybe_genesis_if_ext.value();

      // check that the header extension is of the correct type
      BOOST_CHECK(std::holds_alternative<eosio::chain::instant_finality_extension>(genesis_if_ext));

      // and that it has the expected initial finalizer_policy
      std::optional<eosio::chain::finalizer_policy> maybe_active_finalizer_policy =
         std::get<eosio::chain::instant_finality_extension>(genesis_if_ext).new_finalizer_policy;

      BOOST_CHECK(maybe_active_finalizer_policy.has_value());

      eosio::chain::finalizer_policy active_finalizer_policy = maybe_active_finalizer_policy.value();

      BOOST_CHECK_EQUAL(active_finalizer_policy.finalizers.size(), finality_test_cluster::num_nodes);
      BOOST_CHECK_EQUAL(active_finalizer_policy.generation, 1);

      // compute the digest of the finalizer policy
      auto active_finalizer_policy_digest = fc::sha256::hash(active_finalizer_policy);
      
      auto genesis_block_fd = cluster.node0.control->head_finality_data();

      // verify we have finality data for the IF genesis block
      BOOST_CHECK(genesis_block_fd.has_value());

      // compute IF finality leaf
      auto genesis_base_digest = genesis_block_fd.value().base_digest;
      auto genesis_afp_base_digest = hash_pair(active_finalizer_policy_digest, genesis_base_digest);
      
      auto genesis_block_finality_digest = fc::sha256::hash(eosio::chain::finality_digest_data_v1{
         .active_finalizer_policy_generation      = active_finalizer_policy.generation,
         .finality_tree_digest                    = digest_type(), //nothing to finalize yet
         .active_finalizer_policy_and_base_digest = genesis_afp_base_digest
      });

      // action_mroot computed using the post-IF activation merkle tree rules
      auto genesis_block_action_mroot = genesis_block_fd.value().action_mroot;
      
      // initial finality leaf
      auto genesis_block_leaf = fc::sha256::hash(valid_t::finality_leaf_node_t{
         .block_num = genesis_block->block_num(),
         .finality_digest = genesis_block_finality_digest,
         .action_mroot = genesis_block_action_mroot
      });

      // create the ibc account and deploy the ibc contract to it 
      cluster.node0.create_account( "ibc"_n );
      cluster.node0.set_code( "ibc"_n, eosio::testing::test_contracts::svnn_ibc_wasm());
      cluster.node0.set_abi( "ibc"_n, eosio::testing::test_contracts::svnn_ibc_abi());

      cluster.node0.push_action( "ibc"_n, "setfpolicy"_n, "ibc"_n, mvo()
         ("from_block_num", 1)
         ("policy", mvo() 
            ("generation", active_finalizer_policy.generation)
            ("threshold", active_finalizer_policy.threshold)
            ("last_block_num", 0)
            ("finalizers", active_finalizer_policy.finalizers)
         )
      );

      // Transition block. Finalizers are not expected to vote on this block. 
      auto block_1 = cluster.produce_and_push_block();
      auto block_1_fd = cluster.node0.control->head_finality_data();
      auto block_1_action_mroot = block_1_fd.value().action_mroot;
      auto block_1_finality_digest = cluster.node0.control->get_strong_digest_by_id(block_1->calculate_id());
      auto block_1_leaf = fc::sha256::hash(valid_t::finality_leaf_node_t{
         .block_num = block_1->block_num(),
         .finality_digest = block_1_finality_digest,
         .action_mroot = block_1_action_mroot
      });
      
      // Proper IF Block. From now on, finalizers must vote. Moving forward, the header action_mroot
      // field is reconverted to provide the finality_mroot.
      // The action_mroot is instead provided via the finality data
      auto block_2 = cluster.produce_and_push_block();
      cluster.process_votes(1, cluster.num_needed_for_quorum - 1); //enough to reach quorum threshold
      auto block_2_fd = cluster.node0.control->head_finality_data();
      auto block_2_action_mroot = block_2_fd.value().action_mroot;
      auto block_2_base_digest = block_2_fd.value().base_digest;
      auto block_2_finality_digest = cluster.node0.control->get_strong_digest_by_id(block_2->calculate_id());
      auto block_2_afp_base_digest = hash_pair(active_finalizer_policy_digest, block_2_base_digest);
      auto block_2_leaf = fc::sha256::hash(valid_t::finality_leaf_node_t{
         .block_num = block_2->block_num(),
         .finality_digest = block_2_finality_digest,
         .action_mroot = block_2_action_mroot
      });
      auto block_2_finality_root = block_2->action_mroot;

      // block_3 contains a QC over block_2
      auto block_3 = cluster.produce_and_push_block();
      cluster.process_votes(1, cluster.num_needed_for_quorum - 1);
      auto block_3_fd = cluster.node0.control->head_finality_data();
      auto block_3_action_mroot = block_3_fd.value().action_mroot;
      auto block_3_finality_digest = cluster.node0.control->get_strong_digest_by_id(block_3->calculate_id());
      auto block_3_leaf = fc::sha256::hash(valid_t::finality_leaf_node_t{
         .block_num = block_3->block_num(),
         .finality_digest = block_3_finality_digest,
         .action_mroot = block_3_action_mroot
      });

      // block_4 contains a QC over block_3
      auto block_4 = cluster.produce_and_push_block();
      cluster.process_votes(1, cluster.num_needed_for_quorum - 1);
      auto block_4_fd = cluster.node0.control->head_finality_data();
      auto block_4_base_digest = block_4_fd.value().base_digest;
      auto block_4_afp_base_digest = hash_pair(active_finalizer_policy_digest, block_4_base_digest);

      auto block_4_finality_root = block_4->action_mroot;
      qc_data_t qc_b_4 = extract_qc_data(block_4);

      BOOST_TEST(qc_b_4.qc.has_value());

      // block_5 contains a QC over block_4, which completes the 3-chain for block_2 and
      // serves as a proof of finality for it
      auto block_5 = cluster.produce_and_push_block();
      cluster.process_votes(1, cluster.num_needed_for_quorum - 1);
      auto block_5_fd = cluster.node0.control->head_finality_data();
      auto block_5_base_digest = block_5_fd.value().base_digest;
      auto block_5_afp_base_digest =  hash_pair(active_finalizer_policy_digest, block_5_base_digest);
      auto block_5_finality_root = block_5->action_mroot; 

      // retrieve the QC over block_4 that is contained in block_5
      qc_data_t qc_b_5 = extract_qc_data(block_5);

      BOOST_TEST(qc_b_5.qc.has_value());
      
      // block_5 contains a QC over block_4, which completes the 3-chain for block_2
      // and serves as a proof of finality for it
      auto block_6 = cluster.produce_and_push_block();
      cluster.process_votes(1, cluster.num_needed_for_quorum - 1);

      // retrieve the QC over block_5 that is contained in block_6
      qc_data_t qc_b_6 = extract_qc_data(block_6);

      BOOST_TEST(qc_b_6.qc.has_value());
      
      std::string raw_bitset("03"); //node0 ande node1 signed

      // create a few proofs we'll use to perform tests

      // heavy proof #1. Proving finality of block #2 using block #2 finality root
      mutable_variant_object heavy_proof_1 = mvo()
         ("proof", mvo() 
            ("finality_proof", mvo() //proves finality of block #2
               ("qc_block", mvo()
                  ("major_version", 1)
                  ("minor_version", 0)
                  ("finalizer_policy_generation", 1)
                  ("witness_hash", block_4_afp_base_digest)
                  ("finality_mroot", block_4_finality_root)
               )
               ("qc", mvo()
                  ("signature", qc_b_5.qc.value().data.sig.to_string())
                  ("finalizers", raw_bitset) 
               )
            )
            ("target_block_proof_of_inclusion", mvo() 
               ("target_node_index", 2)
               ("last_node_index", 2)
               ("target",  mvo() //target block #2
                  ("finality_data", mvo() 
                     ("major_version", 1)
                     ("minor_version", 0)
                     ("finalizer_policy_generation", 1)
                     ("witness_hash", block_2_afp_base_digest)
                     ("finality_mroot", block_2_finality_root)
                  )
                  ("dynamic_data", mvo() 
                     ("block_num", block_2->block_num())
                     ("action_proofs", fc::variants())
                     ("action_mroot", block_2_action_mroot)
                  )
               )
               ("merkle_branches", generate_proof_of_inclusion({genesis_block_leaf, block_1_leaf, block_2_leaf}, 2))
            )
         );

      // heavy proof #2. Proving finality of block #2 using block #3 finality root
      mutable_variant_object heavy_proof_2 = mvo()
         ("proof", mvo() 
            ("finality_proof", mvo()  //proves finality of block #3
               ("qc_block", mvo()
                  ("major_version", 1)
                  ("minor_version", 0)
                  ("finalizer_policy_generation", 1)
                  ("witness_hash", block_5_afp_base_digest)
                  ("finality_mroot", block_5_finality_root)
               )
               ("qc", mvo()
                  ("signature", qc_b_6.qc.value().data.sig.to_string())
                  ("finalizers", raw_bitset) 
               )
            )
            ("target_block_proof_of_inclusion", mvo() 
               ("target_node_index", 2)
               ("last_node_index", 3)
               ("target", mvo() //target block #2
                  ("finality_data", mvo() 
                     ("major_version", 1)
                     ("minor_version", 0)
                     ("finalizer_policy_generation", 1)
                     ("witness_hash", block_2_afp_base_digest)
                     ("finality_mroot", block_2_finality_root)
                  )
                  ("dynamic_data", mvo() 
                     ("block_num", block_2->block_num())
                     ("action_proofs", fc::variants())
                     ("action_mroot", block_2_action_mroot)
                  )
               )
               ("merkle_branches", generate_proof_of_inclusion({genesis_block_leaf, block_1_leaf, block_2_leaf, block_3_leaf}, 2))
            )
         );

      // light proof #1. Attempt to prove finality of block #2 with previously proven finality root of block #2
      mutable_variant_object light_proof_1 = mvo()
         ("proof", mvo() 
            ("target_block_proof_of_inclusion", mvo() 
               ("target_node_index", 2)
               ("last_node_index", 2)
               ("target", mvo() 
                  ("finality_data", mvo() 
                     ("major_version", 1)
                     ("minor_version", 0)
                     ("finalizer_policy_generation", 1)
                     ("witness_hash", block_2_afp_base_digest)
                     ("finality_mroot", block_2_finality_root)
                  )
                  ("dynamic_data", mvo() 
                     ("block_num", block_2->block_num())
                     ("action_proofs", fc::variants())
                     ("action_mroot", block_2_action_mroot)
                  )
               )
               ("merkle_branches", generate_proof_of_inclusion({genesis_block_leaf, block_1_leaf, block_2_leaf}, 2))
            )
         );
      

      // verify first heavy proof
      cluster.node0.push_action("ibc"_n, "checkproof"_n, "ibc"_n, heavy_proof_1);

      // now that we stored the proven root, we should be able to verify the same proof without
      // the finality data (aka light proof)
      cluster.node0.push_action("ibc"_n, "checkproof"_n, "ibc"_n, light_proof_1);

      // verify a second proof where the target block is different from the finality block.
      // This also saves a second finality root to the contract, marking the beginning of the cache
      // timer for the older finality root.
      cluster.node0.push_action("ibc"_n, "checkproof"_n, "ibc"_n, heavy_proof_2);

      cluster.produce_blocks(1); //advance 1 block to avoid duplicate transaction

      // we should still be able to verify a proof of finality for block #2 without finality proof,
      // since the previous root is still cached
      cluster.node0.push_action("ibc"_n, "checkproof"_n, "ibc"_n, light_proof_1);

      cluster.produce_blocks(1200); //advance 10 minutes

      // the root is still cached when performing this action, so the action succeeds.
      // However, it also triggers garbage collection,removing the old proven root for block #2,
      // so subsequent calls with the same action data will fail
      cluster.node0.push_action("ibc"_n, "checkproof"_n, "ibc"_n, light_proof_1);

      cluster.produce_blocks(1); //advance 1 block to avoid duplicate transaction

      bool failed = false;

      // Since garbage collection was previously triggered for the merkle root of block #2 which this
      // proof attempts to link to, action will now fail
      try {
         cluster.node0.push_action("ibc"_n, "checkproof"_n, "ibc"_n, light_proof_1);
      }
      catch(const eosio_assert_message_exception& e){
         failed = true;
      }

      // verify action has failed, as expected
      BOOST_CHECK(failed); 

   } FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()