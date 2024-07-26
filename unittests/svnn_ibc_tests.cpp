#include <eosio/chain/abi_serializer.hpp>
#include <eosio/testing/tester.hpp>

#include <fc/variant_object.hpp>

#include <boost/test/unit_test.hpp>

#include <contracts.hpp>
#include <test_contracts.hpp>
#include "fork_test_utilities.hpp"

#include <eosio/chain/exceptions.hpp>

#include "finality_proof.hpp"

using namespace eosio::chain;
using namespace eosio::testing;

using mvo = mutable_variant_object;

std::string bitset_to_input_string(const boost::dynamic_bitset<unsigned char>& bitset) {
   static const char* hexchar = "0123456789abcdef";

   boost::dynamic_bitset<unsigned char> bs(bitset);
   bs.resize((bs.size() + 7) & ~0x7);
   assert(bs.size() % 8 == 0);

   std::string result;
   result.resize(bs.size() / 4);
   for (size_t i = 0; i < bs.size(); i += 4) {
      size_t x = 0;
      for (size_t j = 0; j < 4; ++j)
         x += bs[i+j] << j;
      auto slot = i / 4;
      result[slot % 2 ? slot - 1 : slot + 1] = hexchar[x]; // flip the two hex digits for each byte
   }
   return result;
}

std::string binary_to_hex(const std::string& bin) {
   boost::dynamic_bitset<unsigned char> bitset(bin.size());
   for (size_t i = 0; i < bin.size(); ++i) {
       if (bin[i] == '1') {
           bitset.set(bin.size() - 1 - i);
       }
   }
   return bitset_to_input_string(bitset);
}

auto active_finalizers_string = [](const finality_proof::ibc_block_data_t& bd)  {
   return bitset_to_input_string(bd.qc_data.qc.value().active_policy_sig.strong_votes.value());
};

BOOST_AUTO_TEST_SUITE(svnn_ibc)

   BOOST_AUTO_TEST_CASE(ibc_test) { try {

      // cluster is set up with the head about to produce IF Genesis
      finality_proof::proof_test_cluster<4> cluster;

      // produce IF Genesis block
      auto genesis_block_result = cluster.produce_block();

      // ensure out of scope setup and initial cluster wiring is consistent
      BOOST_CHECK_EQUAL(genesis_block_result.block->block_num(), 4u);
      
      BOOST_CHECK_EQUAL(cluster.active_finalizer_policy.finalizers.size(), cluster.num_nodes);
      BOOST_CHECK_EQUAL(cluster.active_finalizer_policy.generation, 1u);

      // create the ibc account and deploy the ibc contract to it 
      cluster.node0.create_account( "ibc"_n );
      cluster.node0.set_code( "ibc"_n, eosio::testing::test_contracts::ibc_wasm());
      cluster.node0.set_abi( "ibc"_n, eosio::testing::test_contracts::ibc_abi());

      cluster.node0.push_action( "ibc"_n, "setfpolicy"_n, "ibc"_n, mvo()
         ("from_block_num", 1)
         ("policy", cluster.active_finalizer_policy)
      );

      // Transition block. Finalizers are not expected to vote on this block.
      // Note : block variable names are identified by ordinal number after IF genesis, and not by their block num
      auto block_1_result = cluster.produce_block(); //block num : 5

      // Proper IF Block. From now on, finalizers must vote.
      // Moving forward, the header action_mroot field is reconverted to provide the finality_mroot.
      // The action_mroot is instead provided via the finality data
      auto block_2_result = cluster.produce_block(); //block num : 6
      
      // block_3 contains a QC over block_2
      auto block_3_result = cluster.produce_block(); //block num : 7

      // block_4 contains a QC over block_3, which completes the 2-chain for block_2 and
      // serves as a proof of finality for it
      auto block_4_result = cluster.produce_block(); //block num : 8

      // block_5 contains a QC over block_4.
      auto block_5_result = cluster.produce_block(); //block num : 9
      auto block_6_result = cluster.produce_block(); //block num : 10

      BOOST_TEST(block_4_result.qc_data.qc.has_value());
      BOOST_TEST(block_5_result.qc_data.qc.has_value());
      BOOST_TEST(block_6_result.qc_data.qc.has_value());
      
      // create a few proofs we'll use to perform tests

      // heavy proof #1. Proving finality of block #2 using block #2 finality root. 

      // Under 2-chains finality, a QC over block #2 (delivered by block #3) and a strong QC on block #3 
      // (delivered by block #4) constitutes a valid 2-chains and results in #2 becoming final.  
      mutable_variant_object heavy_proof_1 = mvo()
         ("proof", mvo() 
            ("finality_proof", mvo() //proves finality of block #2
               ("qc_block", mvo()
                  ("major_version", 1)
                  ("minor_version", 0)
                  ("active_finalizer_policy_generation", 1)
                  ("witness_hash", block_3_result.level_2_commitments_digest)
                  ("finality_mroot", block_3_result.finality_root)
               )
               ("active_policy_qc", mvo()
                  ("signature", block_4_result.qc_data.qc.value().active_policy_sig.sig.to_string())
                  ("finalizers", active_finalizers_string(block_4_result)) 
               )
            )
            ("target_block_proof_of_inclusion", mvo() 
               ("target_block_index", 2)
               ("final_block_index", 2)
               ("target", fc::variants{"extended_block_data", mvo() //target block #2
                  ("finality_data", mvo() 
                     ("major_version", 1)
                     ("minor_version", 0)
                     ("active_finalizer_policy_generation", 1)
                     ("witness_hash", block_2_result.level_2_commitments_digest)
                     ("finality_mroot", block_2_result.finality_root)
                  )
                  ("timestamp", block_2_result.block->timestamp)
                  ("parent_timestamp", block_2_result.parent_timestamp)
                  ("dynamic_data", mvo() 
                     ("block_num", block_2_result.block->block_num())
                     ("action_proofs", fc::variants())
                     ("action_mroot", block_2_result.action_mroot)
                  )}
               )
               ("merkle_branches", finality_proof::generate_proof_of_inclusion(cluster.get_finality_leaves(2), 2))
            )
         );

      // heavy proof #1 again, this time using simple_block_data variant type 
      mutable_variant_object simple_heavy_proof_1 = mvo()
         ("proof", mvo() 
            ("finality_proof", mvo() //proves finality of block #2
               ("qc_block", mvo()
                  ("major_version", 1)
                  ("minor_version", 0)
                  ("active_finalizer_policy_generation", 1)
                  ("witness_hash", block_3_result.level_2_commitments_digest)
                  ("finality_mroot", block_3_result.finality_root)
               )
               ("active_policy_qc", mvo()
                  ("signature", block_4_result.qc_data.qc.value().active_policy_sig.sig.to_string())
                  ("finalizers", active_finalizers_string(block_4_result)) 
               )
            )
            ("target_block_proof_of_inclusion", mvo() 
               ("target_block_index", 2)
               ("final_block_index", 2)
               ("target", fc::variants{"simple_block_data", mvo() //target block #2
                  ("major_version", 1)
                  ("minor_version", 0)
                  ("finality_digest", block_2_result.finality_digest)
                  ("timestamp", block_2_result.block->timestamp)
                  ("parent_timestamp", block_2_result.parent_timestamp)
                  ("dynamic_data", mvo() 
                     ("block_num", block_2_result.block->block_num())
                     ("action_proofs", fc::variants())
                     ("action_mroot", block_2_result.action_mroot)
                  )}
               )
               ("merkle_branches", finality_proof::generate_proof_of_inclusion(cluster.get_finality_leaves(2), 2))
            )
         );

      // heavy proof #2. Proving finality of block #2 using block #3 finality root
      mutable_variant_object heavy_proof_2 = mvo()
         ("proof", mvo() 
            ("finality_proof", mvo()  //proves finality of block #3
               ("qc_block", mvo()
                  ("major_version", 1)
                  ("minor_version", 0)
                  ("active_finalizer_policy_generation", 1)
                  ("witness_hash", block_4_result.level_2_commitments_digest)
                  ("finality_mroot", block_4_result.finality_root)
               )
               ("active_policy_qc", mvo()
                  ("signature", block_5_result.qc_data.qc.value().active_policy_sig.sig.to_string())
                  ("finalizers", active_finalizers_string(block_5_result)) 
               )
            )
            ("target_block_proof_of_inclusion", mvo() 
               ("target_block_index", 2)
               ("final_block_index", 3)
               ("target", fc::variants{"extended_block_data", mvo() //target block #2
                  ("finality_data", mvo() 
                     ("major_version", 1)
                     ("minor_version", 0)
                     ("active_finalizer_policy_generation", 1)
                     ("witness_hash", block_2_result.level_2_commitments_digest)
                     ("finality_mroot", block_2_result.finality_root)
                  )
                  ("timestamp", block_2_result.block->timestamp)
                  ("parent_timestamp", block_2_result.parent_timestamp)
                  ("dynamic_data", mvo() 
                     ("block_num", block_2_result.block->block_num())
                     ("action_proofs", fc::variants())
                     ("action_mroot", block_2_result.action_mroot)
                  )}
               )
               ("merkle_branches", finality_proof::generate_proof_of_inclusion(cluster.get_finality_leaves(3), 2))
            )
         );

      // light proof #1. Attempt to prove finality of block #2 with previously proven finality root of block #2
      mutable_variant_object light_proof_1 = mvo()
         ("proof", mvo() 
            ("target_block_proof_of_inclusion", mvo() 
               ("target_block_index", 2)
               ("final_block_index", 2)
               ("target", fc::variants{"extended_block_data", mvo() 
                  ("finality_data", mvo() 
                     ("major_version", 1)
                     ("minor_version", 0)
                     ("active_finalizer_policy_generation", 1)
                     ("witness_hash", block_2_result.level_2_commitments_digest)
                     ("finality_mroot", block_2_result.finality_root)
                  )
                  ("timestamp", block_2_result.block->timestamp)
                  ("parent_timestamp", block_2_result.parent_timestamp)
                  ("dynamic_data", mvo() 
                     ("block_num", block_2_result.block->block_num())
                     ("action_proofs", fc::variants())
                     ("action_mroot", block_2_result.action_mroot)
                  )}
               )
               ("merkle_branches", finality_proof::generate_proof_of_inclusion(cluster.get_finality_leaves(2), 2))
            )
         );
      

      // verify first heavy proof
      action_trace check_heavy_proof_1_trace = cluster.node0.push_action("ibc"_n, "checkproof"_n, "ibc"_n, heavy_proof_1)->action_traces[0];

      // now that we stored the proven root, we should be able to verify the same proof without
      // the finality data (aka light proof)
      action_trace check_light_proof_1_trace = cluster.node0.push_action("ibc"_n, "checkproof"_n, "ibc"_n, light_proof_1)->action_traces[0];

      // verify a second proof where the target block is different from the finality block.
      // This also saves a second finality root to the contract, marking the beginning of the cache
      // timer for the older finality root.
      action_trace check_heavy_proof_2_trace = cluster.node0.push_action("ibc"_n, "checkproof"_n, "ibc"_n, heavy_proof_2)->action_traces[0];

      // produce the block to avoid duplicate transaction error
      auto block_7_result = cluster.produce_block();

      std::vector<digest_type> action_leaves;

      action_leaves.push_back(block_7_result.onblock_trace.digest_savanna());
      action_leaves.push_back(check_heavy_proof_1_trace.digest_savanna());
      action_leaves.push_back(check_light_proof_1_trace.digest_savanna());
      action_leaves.push_back(check_heavy_proof_2_trace.digest_savanna());

      // since a few actions were included in the previous block, we can verify that they correctly hash into the action_mroot for that block
      auto pair_1_hash = finality_proof::hash_pair(action_leaves[0], action_leaves[1]);
      auto pair_2_hash = finality_proof::hash_pair(action_leaves[2], action_leaves[3]);

      auto computed_action_mroot = finality_proof::hash_pair(pair_1_hash, pair_2_hash);

      BOOST_CHECK(computed_action_mroot == block_7_result.action_mroot);

      // verify same heavy proof we verified before, this time with simple_block_data as target
      action_trace check_simple_heavy_proof_1_trace = cluster.node0.push_action("ibc"_n, "checkproof"_n, "ibc"_n, simple_heavy_proof_1)->action_traces[0];

      // we now test a finalizer policy change
      auto indices1 = cluster.fin_policy_indices_0;  // start from original set of indices
      indices1[0] = 1;                       // update key used for node0 in policy, which will result in a new policy

      // take note of policy digest prior to changes
      digest_type previous_policy_digest = cluster.active_finalizer_policy_digest;

      // At this stage, we can prepare to test the change of pending policy.

      // We first take a note of the pending policy. When we get a QC on block #9, the pending policy will update.
      digest_type pending_policy_digest = cluster.last_pending_finalizer_policy_digest;

      // change the finalizer policy by rotating the key of node0
      cluster.node0.finkeys.set_finalizer_policy(indices1);

      // produce a new block. This block contains a new proposed finalizer policy
      auto block_8_result = cluster.produce_block();

      // verify we have a QC
      BOOST_TEST(block_8_result.qc_data.qc.has_value());

      // verify the block header contains the proposed finalizer policy differences
      BOOST_TEST(finality_proof::has_finalizer_policy_diffs(block_8_result.block));

      // advance finality
      auto block_9_result = cluster.produce_block();

      // verify we have a QC
      BOOST_TEST(block_9_result.qc_data.qc.has_value());

      // pending policy is still the same
      BOOST_TEST(pending_policy_digest==cluster.last_pending_finalizer_policy_digest);

      // QC on #9 included in #10 makes #8 final, proposed policy is now pending
      auto block_10_result = cluster.produce_block();

      // verify we have a QC
      BOOST_TEST(block_10_result.qc_data.qc.has_value());

      // Verify that the last pending policy has been updated
      BOOST_TEST(pending_policy_digest!=cluster.last_pending_finalizer_policy_digest);

      // At this stage, we can prove the inclusion of actions into block #7.

      // first, we create action proofs to verify inclusion of some actions

      // onblock action proof
      mutable_variant_object onblock_action_proof = mvo()
         ("target_block_index", 0)
         ("final_block_index", 3)
         ("target", mvo()
            ("action", mvo()
               ("account", block_7_result.onblock_trace.act.account)
               ("name", block_7_result.onblock_trace.act.name)
               ("authorization", block_7_result.onblock_trace.act.authorization)
               ("data", block_7_result.onblock_trace.act.data)
               ("return_value", block_7_result.onblock_trace.return_value)
            )
            ("receiver", block_7_result.onblock_trace.receiver)
            ("recv_sequence", block_7_result.onblock_trace.receipt.value().recv_sequence)
            ("witness_hash", block_7_result.onblock_trace.savanna_witness_hash())
         )
         ("merkle_branches", finality_proof::generate_proof_of_inclusion(action_leaves, 0));


      // first action proof (check_heavy_proof_1)
      mutable_variant_object action_proof_1 = mvo()
         ("target_block_index", 1)
         ("final_block_index", 3)
         ("target", mvo()
            ("action", mvo()
               ("account", check_heavy_proof_1_trace.act.account)
               ("name", check_heavy_proof_1_trace.act.name)
               ("authorization", check_heavy_proof_1_trace.act.authorization)
               ("data", check_heavy_proof_1_trace.act.data)
               ("return_value", check_heavy_proof_1_trace.return_value)
            )
            ("receiver", check_heavy_proof_1_trace.receiver)
            ("recv_sequence", check_heavy_proof_1_trace.receipt.value().recv_sequence)
            ("witness_hash", check_heavy_proof_1_trace.savanna_witness_hash())
         )
         ("merkle_branches", finality_proof::generate_proof_of_inclusion(action_leaves, 1));

      // second action proof (check_light_proof_1)
      mutable_variant_object action_proof_2 = mvo()
         ("target_block_index", 2)
         ("final_block_index", 3)
         ("target", mvo()
            ("action", mvo()
               ("account", check_light_proof_1_trace.act.account)
               ("name", check_light_proof_1_trace.act.name)
               ("authorization", check_light_proof_1_trace.act.authorization)
               ("data", check_light_proof_1_trace.act.data)
               ("return_value", check_light_proof_1_trace.return_value)
            )
            ("receiver", check_light_proof_1_trace.receiver)
            ("recv_sequence", check_light_proof_1_trace.receipt.value().recv_sequence)
            ("witness_hash", check_light_proof_1_trace.savanna_witness_hash())
         )
         ("merkle_branches", finality_proof::generate_proof_of_inclusion(action_leaves, 2));

      // proof to verify the inclusion of onblock action via heavy proof
      mutable_variant_object action_heavy_proof = mvo()
         ("proof", mvo() 
            ("finality_proof", mvo() //proves finality of block #7
               ("qc_block", mvo()
                  ("major_version", 1)
                  ("minor_version", 0)
                  ("active_finalizer_policy_generation", 1)
                  ("witness_hash", block_8_result.level_2_commitments_digest)
                  ("finality_mroot", block_8_result.finality_root)
               )
               ("active_policy_qc", mvo()
                  ("signature", block_9_result.qc_data.qc.value().active_policy_sig.sig.to_string())
                  ("finalizers", active_finalizers_string(block_9_result)) 
               )
            )
            ("target_block_proof_of_inclusion", mvo() 
               ("target_block_index", 7)
               ("final_block_index", 7)
               ("target", fc::variants{"extended_block_data", mvo() //target block #7
                  ("finality_data", mvo() 
                     ("major_version", 1)
                     ("minor_version", 0)
                     ("active_finalizer_policy_generation", 1)
                     ("witness_hash", block_7_result.level_2_commitments_digest)
                     ("finality_mroot", block_7_result.finality_root)
                  )
                  ("timestamp", block_7_result.block->timestamp)
                  ("parent_timestamp", block_7_result.parent_timestamp)
                  ("dynamic_data", mvo() 
                     ("block_num", block_7_result.block->block_num())
                     ("action_proofs", fc::variants({onblock_action_proof}))
                  )}
               )
               ("merkle_branches", finality_proof::generate_proof_of_inclusion(cluster.get_finality_leaves(7), 7))
            )
         );

      // proof to verify the inclusion of the first and second actions via light proof
      mutable_variant_object action_light_proof = mvo()
         ("proof", mvo() 
            ("target_block_proof_of_inclusion", mvo() 
               ("target_block_index", 7)
               ("final_block_index", 7)
               ("target", fc::variants{"extended_block_data", mvo() 
                  ("finality_data", mvo() 
                     ("major_version", 1)
                     ("minor_version", 0)
                     ("active_finalizer_policy_generation", 1)
                     ("witness_hash", block_7_result.level_2_commitments_digest)
                     ("finality_mroot", block_7_result.finality_root)
                  )
                  ("timestamp", block_7_result.block->timestamp)
                  ("parent_timestamp", block_7_result.parent_timestamp)
                  ("dynamic_data", mvo() 
                     ("block_num", block_7_result.block->block_num())
                     ("action_proofs", fc::variants({action_proof_1, action_proof_2}))
                  )}
               )
               ("merkle_branches", finality_proof::generate_proof_of_inclusion(cluster.get_finality_leaves(7), 7))
            )
         );

      // action proof verification
      action_trace check_action_heavy_proof_trace = cluster.node0.push_action("ibc"_n, "checkproof"_n, "ibc"_n, action_heavy_proof)->action_traces[0];

      action_trace check_action_light_proof_trace = cluster.node0.push_action("ibc"_n, "checkproof"_n, "ibc"_n, action_light_proof)->action_traces[0];

      auto block_11_result = cluster.produce_block();  // last pending policy (proposed in #8) takes effect and becomes active on next block

      auto block_12_result = cluster.produce_block();

      // verify that the new finalizer policy is now in force
      BOOST_TEST(previous_policy_digest!=cluster.active_finalizer_policy_digest);

      auto block_13_result = cluster.produce_block(); 

      // we can verify that the blocks containing single policy QCs (#10 and #13) don't have a pending policy QC
      // and we can verify that policy transition blocks where another policy is pending (#11 and #12) also carry a QC from that policy

      BOOST_TEST(block_10_result.qc_data.qc.has_value());
      BOOST_TEST(!block_10_result.qc_data.qc.value().pending_policy_sig.has_value()); //verify this block contains a single QC on block #9

      BOOST_TEST(block_11_result.qc_data.qc.has_value());

      BOOST_TEST(block_11_result.qc_data.qc.value().pending_policy_sig.has_value()); //this block contains joint policies QCs on block #10

      BOOST_TEST(block_12_result.qc_data.qc.has_value());
      BOOST_TEST(block_12_result.qc_data.qc.value().pending_policy_sig.has_value()); //this block contains joint policies QCs on block #11

      BOOST_TEST(block_13_result.qc_data.qc.has_value());
      BOOST_TEST(!block_13_result.qc_data.qc.value().pending_policy_sig.has_value()); //verify this block contains a single QC on block #12

      auto block_14_result = cluster.produce_block();
      auto block_15_result = cluster.produce_block();
      auto block_16_result = cluster.produce_block();
      auto block_17_result = cluster.produce_block();

      BOOST_TEST(block_14_result.qc_data.qc.has_value());
      BOOST_TEST(block_15_result.qc_data.qc.has_value());
      BOOST_TEST(block_16_result.qc_data.qc.has_value());
      BOOST_TEST(block_17_result.qc_data.qc.has_value());

      mutable_variant_object heavy_proof_3 = mvo()
         ("proof", mvo() 
            ("finality_proof", mvo()
               ("qc_block", mvo()
                  ("major_version", 1)
                  ("minor_version", 0)
                  ("active_finalizer_policy_generation", 1)
                  ("pending_finalizer_policy_generation", 2)
                  ("witness_hash", block_10_result.level_2_commitments_digest)
                  ("finality_mroot", block_10_result.finality_root)
               )
               ("active_policy_qc", mvo()
                  ("signature", block_11_result.qc_data.qc.value().active_policy_sig.sig.to_string())
                  ("finalizers", active_finalizers_string(block_11_result)) 
               )
            )
            ("target_block_proof_of_inclusion", mvo() 
               ("target_block_index", 9)
               ("final_block_index", 9)
               ("target",  fc::variants{"extended_block_data", mvo() 
                  ("finality_data", mvo() 
                     ("major_version", 1)
                     ("minor_version", 0)
                     ("active_finalizer_policy_generation", 1)
                     ("witness_hash", block_9_result.level_2_commitments_digest)
                     ("finality_mroot", block_9_result.finality_root)
                  )
                  ("timestamp", block_9_result.block->timestamp)
                  ("parent_timestamp", block_9_result.parent_timestamp)
                  ("dynamic_data", mvo() 
                     ("block_num", block_9_result.block->block_num())
                     ("action_proofs", fc::variants())
                     ("action_mroot", block_9_result.action_mroot)
                  )}
               )
               ("merkle_branches", finality_proof::generate_proof_of_inclusion(cluster.get_finality_leaves(9), 9))
            )
         );

      bool last_action_failed = false;

      // verify that this attempt to prove fails, because QC for pending finalizer policy generation is not provided
      try { cluster.node0.push_action("ibc"_n, "checkproof"_n, "ibc"_n, heavy_proof_3); }
      catch(const eosio_assert_message_exception& e){ last_action_failed = true; }

      // checkproof action has failed, as expected.
      BOOST_CHECK(last_action_failed); 

      // heavy proof #4. 
      
      // Proving finality of block #10 using block #10 finality root. 
      
      // A QC on block #11 makes #10 final, which also sets the finalizer policy proposed in #8 as the last pending policy.

      // This also implies finalizers are comitting to this finalizer policy as part of the canonical history of any 
      // chain extending from block #10 (even if the policy never becomes active).
      
      // This allows us to prove this finalizer policy to the IBC contract, and use it to prove finality of subsequent blocks.

      mutable_variant_object heavy_proof_4 = mvo()
         ("proof", mvo() 
            ("finality_proof", mvo()
               ("qc_block", mvo()
                  ("major_version", 1)
                  ("minor_version", 0)
                  ("active_finalizer_policy_generation", 1)
                  ("last_pending_finalizer_policy_generation", 2)
                  ("witness_hash", block_11_result.level_2_commitments_digest)
                  ("finality_mroot", block_11_result.finality_root)
               )
               ("active_policy_qc", mvo()
                  ("signature", block_12_result.qc_data.qc.value().active_policy_sig.sig.to_string())
                  ("finalizers", active_finalizers_string(block_12_result)) 
               )
               ("pending_policy_qc", mvo()
                  ("signature", block_12_result.qc_data.qc.value().pending_policy_sig.value().sig.to_string())
                  ("finalizers", active_finalizers_string(block_12_result)) 
               )
            )
            ("target_block_proof_of_inclusion", mvo() 
               ("target_block_index", 10)
               ("final_block_index", 10)
               ("target",  fc::variants{"extended_block_data", mvo() 
                  ("finality_data", mvo() 
                     ("major_version", 1)
                     ("minor_version", 0)
                     ("active_finalizer_policy_generation", 1)
                     ("last_pending_finalizer_policy_generation", 2)
                     ("pending_finalizer_policy", cluster.last_pending_finalizer_policy)
                     ("witness_hash", block_10_result.level_3_commitments_digest)
                     ("last_pending_finalizer_policy_start_timestamp", block_10_result.last_pending_finalizer_policy_start_timestamp )
                     ("finality_mroot", block_10_result.finality_root)
                  )
                  ("timestamp", block_10_result.block->timestamp)
                  ("parent_timestamp", block_10_result.parent_timestamp)
                  ("dynamic_data", mvo() 
                     ("block_num", block_10_result.block->block_num())
                     ("action_proofs", fc::variants())
                     ("action_mroot", block_10_result.action_mroot)
                  )}
               )
               ("merkle_branches", finality_proof::generate_proof_of_inclusion(cluster.get_finality_leaves(10), 10))
            )
         );

      // heavy proof #5.

      // Proving finality of block #11 using block #11 finality root.

      // The QC provided in this proof (over block #12) is signed by the second generation of finalizers.
      
      // heavy_proof_4 must be proven before we can prove heavy_proof_5.

      mutable_variant_object heavy_proof_5 = mvo()
         ("proof", mvo() 
            ("finality_proof", mvo()
               ("qc_block", mvo()
                  ("major_version", 1)
                  ("minor_version", 0)
                  ("active_finalizer_policy_generation", 2)
                  ("witness_hash", block_12_result.level_2_commitments_digest)
                  ("finality_mroot", block_12_result.finality_root)
               )
               ("active_policy_qc", mvo()
                  ("signature", block_13_result.qc_data.qc.value().active_policy_sig.sig.to_string())
                  ("finalizers", active_finalizers_string(block_13_result)) 
               )
            )
            ("target_block_proof_of_inclusion", mvo() 
               ("target_block_index", 11)
               ("final_block_index", 11)
               ("target",  fc::variants{"extended_block_data", mvo() 
                  ("finality_data", mvo() 
                     ("major_version", 1)
                     ("minor_version", 0)
                     ("active_finalizer_policy_generation", 1)
                     ("last_pending_finalizer_policy_generation", 2)
                     ("witness_hash", block_11_result.level_2_commitments_digest)
                     ("finality_mroot", block_11_result.finality_root)
                  )
                  ("timestamp", block_11_result.block->timestamp)
                  ("parent_timestamp", block_11_result.parent_timestamp)
                  ("dynamic_data", mvo() 
                     ("block_num", block_11_result.block->block_num())
                     ("action_proofs", fc::variants())
                     ("action_mroot", block_11_result.action_mroot)
                  )}
               )
               ("merkle_branches", finality_proof::generate_proof_of_inclusion(cluster.get_finality_leaves(11), 11))
            )
         );

      last_action_failed = false;

      // since heavy_proof_5 requires finalizer policy generation #2, we cannot prove it yet.
      try { cluster.node0.push_action("ibc"_n, "checkproof"_n, "ibc"_n, heavy_proof_5); }
      catch(const eosio_assert_message_exception& e){ last_action_failed = true; }

      // checkproof action has failed, as expected.
      BOOST_CHECK(last_action_failed); 

      // we must first prove that block #10 became final, which makes the policy proposed in block #8 pending.
      // The QC provided to prove this also proves a commitment from finalizers to this policy, so the smart contract can accept it.
      action_trace check_heavy_proof_4_trace = cluster.node0.push_action("ibc"_n, "checkproof"_n, "ibc"_n, heavy_proof_4)->action_traces[0];

      BOOST_CHECK(true); 
      // now that we have successfully proven finalizer policy generation #2, the contract has it, and we can prove heavy_proof_5
      action_trace check_heavy_proof_5_trace = cluster.node0.push_action("ibc"_n, "checkproof"_n, "ibc"_n, heavy_proof_5)->action_traces[0];

      BOOST_CHECK(true); 
      // we now test light proof we should still be able to verify a proof of finality for block #2 without finality proof,
      // since the previous root is still cached
      cluster.node0.push_action("ibc"_n, "checkproof"_n, "ibc"_n, light_proof_1);
      
      BOOST_CHECK(true); 
      cluster.produce_blocks(10); //advance 5 seconds

      // the root is still cached when performing this action, so the action succeeds.
      // However, it also triggers garbage collection,removing the old proven root for block #2,
      // so subsequent calls with the same action data will fail
      cluster.node0.push_action("ibc"_n, "checkproof"_n, "ibc"_n, light_proof_1);

      BOOST_CHECK(true); 
      cluster.produce_block(); //advance 1 block to avoid duplicate transaction

      last_action_failed = false;

      // Since garbage collection was previously triggered for the merkle root of block #2 which this
      // proof attempts to link to, action will now fail
      try {cluster.node0.push_action("ibc"_n, "checkproof"_n, "ibc"_n, light_proof_1);}
      catch(const eosio_assert_message_exception& e){last_action_failed = true;}

      // verify action has failed, as expected
      BOOST_CHECK(last_action_failed); 

   } FC_LOG_AND_RETHROW() }

   BOOST_AUTO_TEST_CASE(bitset_tests) { try {

      savanna_tester chain;

      chain.create_account( "ibc"_n );
      chain.set_code( "ibc"_n, eosio::testing::test_contracts::ibc_wasm());
      chain.set_abi( "ibc"_n, eosio::testing::test_contracts::ibc_abi());

      std::string bitset_1 = binary_to_hex("0");
      std::string bitset_2 = binary_to_hex("011");
      std::string bitset_3 = binary_to_hex("00011101010");
      std::string bitset_4 = binary_to_hex("11011000100001");
      std::string bitset_5 = binary_to_hex("111111111111111111111");
      std::string bitset_6 = binary_to_hex("000000111111111111111");

      chain.push_action("ibc"_n, "testbitset"_n, "ibc"_n, mvo()
         ("bitset_string", "00")
         ("bitset_vector", bitset_1)
         ("finalizers_count", 1)
      );

      chain.push_action("ibc"_n, "testbitset"_n, "ibc"_n, mvo()
         ("bitset_string", "30") //bitset bytes are reversed, so we do the same to test
         ("bitset_vector", bitset_2)
         ("finalizers_count", 3)
      );

      chain.push_action("ibc"_n, "testbitset"_n, "ibc"_n, mvo()
         ("bitset_string", "ae00")
         ("bitset_vector", bitset_3)
         ("finalizers_count", 11)
      );

      chain.push_action("ibc"_n, "testbitset"_n, "ibc"_n, mvo()
         ("bitset_string", "1263")
         ("bitset_vector", bitset_4)
         ("finalizers_count", 14)
      );

      chain.push_action("ibc"_n, "testbitset"_n, "ibc"_n, mvo()
         ("bitset_string", "fffff1")
         ("bitset_vector", bitset_5)
         ("finalizers_count", 21)
      );

      chain.push_action("ibc"_n, "testbitset"_n, "ibc"_n, mvo()
         ("bitset_string", "fff700")
         ("bitset_vector", bitset_6)
         ("finalizers_count", 21)
      );

   } FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
