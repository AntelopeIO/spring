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

BOOST_AUTO_TEST_SUITE(svnn_finality_violation)

   //extract instant finality data from block header extension, as well as qc data from block extension
   qc_data_t extract_qc_data(const signed_block_ptr& b) {
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

   BOOST_AUTO_TEST_CASE(finality_violation_test) { try {

      /* 

         In this test, node0 and node1 are colluding to create a finality violation.
       
         node0 ande node1 partition the network in such a way that node2 and node3, who are both honest nodes, are disconnected from each other.

         node2 is tricked into joining a fake chain where node0 and node1 are finalizing, but from which node3 is absent.

         Meanwhile, node2 is absent from the real chain, but node0 and node1 are still finalizing on it, therefore double-signing between the fake and real chains.

         node3, unaware of the ongoing attack, is finalizing on the real chain normally.

         user1, a light client, unknowingly joins the fake chain, and submits an important transaction to it.

         user1 stores enough data to prove the final inclusion of this important transaction into the fake chain.

         user1 discovers the real chain later, and uses the stored data to construct a finality violation proof.

         user1 then submits it to the finality proof verification contract.

      */

      // setup the fake chain. node3 doesn't receive votes on the fake chain
      finality_proof::proof_test_cluster<4> fake_chain;
      fake_chain.vote_propagation = {1, 1, 0};
      std::string fake_bitset("07");

      fake_chain.node0.create_accounts( { "user1"_n, "user2"_n, "violation"_n, "eosio.token"_n } );

      fake_chain.node0.set_code( "eosio.token"_n, test_contracts::eosio_token_wasm() );
      fake_chain.node0.set_abi( "eosio.token"_n, test_contracts::eosio_token_abi() );

      fake_chain.node0.set_code( "violation"_n, test_contracts::finality_violation_wasm() );
      fake_chain.node0.set_abi( "violation"_n, test_contracts::finality_violation_abi() );

      // setup the real chain. node2 doesn't receive votes on the real chain
      finality_proof::proof_test_cluster<4> real_chain;
      real_chain.vote_propagation = {1, 0, 1};
      std::string real_bitset("0b");

      real_chain.node0.create_accounts( { "user1"_n, "user2"_n, "violation"_n, "eosio.token"_n } );

      real_chain.node0.set_code( "eosio.token"_n, test_contracts::eosio_token_wasm() );
      real_chain.node0.set_abi( "eosio.token"_n, test_contracts::eosio_token_abi() );

      real_chain.node0.set_code( "violation"_n, test_contracts::finality_violation_wasm() );
      real_chain.node0.set_abi( "violation"_n, test_contracts::finality_violation_abi() );

      // create and issue initial tokens on both chains
      mutable_variant_object create_action = mvo()
         ( "issuer", "eosio"_n)
         ( "maximum_supply", "100.0000 EOS");

      mutable_variant_object issue_action = mvo()
         ( "to", "eosio"_n)
         ( "quantity", "100.0000 EOS")
         ( "memo", "");

      mutable_variant_object initial_transfer = mvo()
         ("from", "eosio"_n)
         ("to", "user1"_n)
         ("quantity", "1.0000 EOS")
         ("memo", "");

      fake_chain.node0.push_action("eosio.token"_n, "create"_n, "eosio.token"_n, create_action);
      fake_chain.node0.push_action("eosio.token"_n, "issue"_n, "eosio"_n, issue_action);
      fake_chain.node0.push_action("eosio.token"_n, "transfer"_n, "eosio"_n, initial_transfer);

      real_chain.node0.push_action("eosio.token"_n, "create"_n, "eosio.token"_n, create_action);
      real_chain.node0.push_action("eosio.token"_n, "issue"_n, "eosio"_n, issue_action);
      real_chain.node0.push_action("eosio.token"_n, "transfer"_n, "eosio"_n, initial_transfer);

      // produce the IF genesis block on both chains
      auto fake_genesis_block_result = fake_chain.produce_block();
      auto real_genesis_block_result = real_chain.produce_block();

      // verify that the same finalizer policy is active on both chains
      BOOST_TEST(fake_chain.active_finalizer_policy_digest == real_chain.active_finalizer_policy_digest);

      // produce enough block to complete the IF transition, and a few more after that
      auto fake_block_1_result = fake_chain.produce_block();
      auto real_block_1_result = real_chain.produce_block();

      auto fake_block_2_result = fake_chain.produce_block();
      auto real_block_2_result = real_chain.produce_block();

      auto fake_block_3_result = fake_chain.produce_block();
      auto real_block_3_result = real_chain.produce_block();

      auto fake_block_4_result = fake_chain.produce_block();
      auto real_block_4_result = real_chain.produce_block();

      auto fake_block_5_result = fake_chain.produce_block();
      auto real_block_5_result = real_chain.produce_block();

      auto fake_block_6_result = fake_chain.produce_block();
      auto real_block_6_result = real_chain.produce_block();

      // verify that the two chains are the same so far
      BOOST_TEST(fake_genesis_block_result.finality_leaf==real_genesis_block_result.finality_leaf);
      BOOST_TEST(fake_block_1_result.finality_leaf==real_block_1_result.finality_leaf);
      BOOST_TEST(fake_block_2_result.finality_leaf==real_block_2_result.finality_leaf);
      BOOST_TEST(fake_block_3_result.finality_leaf==real_block_3_result.finality_leaf);
      BOOST_TEST(fake_block_4_result.finality_leaf==real_block_4_result.finality_leaf);
      BOOST_TEST(fake_block_5_result.finality_leaf==real_block_5_result.finality_leaf);
      BOOST_TEST(fake_block_6_result.finality_leaf==real_block_6_result.finality_leaf);

      qc_data_t fake_qc_b_4 = extract_qc_data(fake_block_4_result.block);
      qc_data_t real_qc_b_4 = extract_qc_data(real_block_4_result.block);

      qc_data_t fake_qc_b_5 = extract_qc_data(fake_block_5_result.block);
      qc_data_t real_qc_b_5 = extract_qc_data(real_block_5_result.block);

      qc_data_t fake_qc_b_6 = extract_qc_data(fake_block_6_result.block);
      qc_data_t real_qc_b_6 = extract_qc_data(real_block_6_result.block);

      // verify QCs on block #4 and onward for both chains
      BOOST_TEST(fake_qc_b_4.qc.has_value());
      BOOST_TEST(real_qc_b_4.qc.has_value());
      BOOST_TEST(fake_qc_b_5.qc.has_value());
      BOOST_TEST(real_qc_b_5.qc.has_value());
      BOOST_TEST(fake_qc_b_6.qc.has_value());
      BOOST_TEST(real_qc_b_6.qc.has_value());

      // verify that the signatures on QCs are different on both chains
      BOOST_TEST(fake_qc_b_4.qc.value().data.sig.to_string() != real_qc_b_4.qc.value().data.sig.to_string());
      BOOST_TEST(fake_qc_b_5.qc.value().data.sig.to_string() != real_qc_b_5.qc.value().data.sig.to_string());
      BOOST_TEST(fake_qc_b_6.qc.value().data.sig.to_string() != real_qc_b_6.qc.value().data.sig.to_string());
      
      // user1 pushes an important transaction to the fake chain.
      // user1 wants to preserve sufficient information about this transfer so they can construct a finality violation proof if/when they discover the real chain.
      mutable_variant_object important_transfer = mvo()
         ("from", "user1"_n)
         ("to", "user2"_n)
         ("quantity", "1.0000 EOS")
         ("memo", "");

      // user1 can record the trace of the transaction (as reported by the proposer that included the transaction into a block).
      // While this action trace is not necessary to prove a finality violation, it can be useful to prove damages.
      action_trace critical_transfer_trace = fake_chain.node0.push_action("eosio.token"_n, "transfer"_n, "user1"_n, important_transfer)->action_traces[0];

      // The double-signing of block_7 is the "original sin". The fork between the fake chain and the real chain happens here.
      // Since the fake block_7 is the one that includes the important transaction, user1 records it and its finality data (AKA the "important block").
      auto fake_block_7_result = fake_chain.produce_block();
      auto real_block_7_result = real_chain.produce_block();

      // verify that fake and real finality leaves for block_7 are different, which results in two different chains that are both valid in the eyes of a light client.
      BOOST_TEST(fake_block_7_result.finality_leaf!=real_block_7_result.finality_leaf);

      auto fake_block_8_result = fake_chain.produce_block();
      auto real_block_8_result = real_chain.produce_block();

      // A QC on this block makes the important block final. user1 records the fake block_9, as well as its associated finality data.
      auto fake_block_9_result = fake_chain.produce_block();
      auto real_block_9_result = real_chain.produce_block();   

      // The QC contained in block_10 is over block_9, which makes a block_7 final. user1 saves this QC, as well as the finalizer policy associated to it. 
      auto fake_block_10_result = fake_chain.produce_block();
      auto real_block_10_result = real_chain.produce_block();

      qc_data_t fake_qc_b_10 = extract_qc_data(fake_block_10_result.block);
      qc_data_t real_qc_b_10 = extract_qc_data(real_block_10_result.block);

      BOOST_TEST(fake_qc_b_10.qc.has_value());
      BOOST_TEST(real_qc_b_10.qc.has_value());

      // At this stage, user1 has enough information to :
      // 1) prove the inclusion of the important action into a block (the important block) AND
      // 2) prove the finality of the important block according to a given finalizer policy

      //proof of finality of the important block
      mutable_variant_object proof1 = mvo()
            ("finality_proof", mvo()
               ("qc_block", mvo()
                  ("major_version", 1)
                  ("minor_version", 0)
                  ("finalizer_policy_generation", 1)
                  ("witness_hash", fake_block_9_result.afp_base_digest)
                  ("finality_mroot", fake_block_9_result.finality_root)
               )
               ("qc", mvo()
                  ("signature", fake_qc_b_10.qc.value().data.sig.to_string())
                  ("finalizers", fake_bitset) 
               )
            )
            ("target_block_proof_of_inclusion", mvo() 
               ("target_node_index", 7)
               ("last_node_index", 7)
               ("target",  mvo() //target block #2
                  ("finality_data", mvo() 
                     ("major_version", 1)
                     ("minor_version", 0)
                     ("finalizer_policy_generation", 1)
                     ("witness_hash", fake_block_7_result.afp_base_digest)
                     ("finality_mroot", fake_block_7_result.finality_root)
                  )
                  ("dynamic_data", mvo() 
                     ("block_num", fake_block_7_result.block->block_num())
                     ("action_proofs", fc::variants())
                     ("action_mroot", fake_block_7_result.action_mroot)
                  )
               )
               ("merkle_branches", finality_proof::generate_proof_of_inclusion(fake_chain.get_finality_leaves(7), 7))
            );

      mutable_variant_object finalizer_policy = mvo()
         ("from_block_num", 1)
         ("policy", fake_chain.active_finalizer_policy);

      // verify that the same finalizer policy is still active on both chains
      BOOST_TEST(fake_chain.active_finalizer_policy_digest == real_chain.active_finalizer_policy_digest);

      // user1 now discovers the real chain, which does not include the important block or important transaction.
      
      // Since user1 recorded a proof of finality for the inclusion of the important block, and they now also have a proof of finality for a block conflicting with the important block
      // user1 can now construct a finality violation proof.

      // proof of finality for a block conflicting with the important block
      mutable_variant_object proof2 = mvo()
            ("finality_proof", mvo() //proves finality of block #2
               ("qc_block", mvo()
                  ("major_version", 1)
                  ("minor_version", 0)
                  ("finalizer_policy_generation", 1)
                  ("witness_hash", real_block_9_result.afp_base_digest)
                  ("finality_mroot", real_block_9_result.finality_root)
               )
               ("qc", mvo()
                  ("signature", real_qc_b_10.qc.value().data.sig.to_string())
                  ("finalizers", real_bitset) 
               )
            )
            ("target_block_proof_of_inclusion", mvo() 
               ("target_node_index", 7)
               ("last_node_index", 7)
               ("target",  mvo() //target block #2
                  ("finality_data", mvo() 
                     ("major_version", 1)
                     ("minor_version", 0)
                     ("finalizer_policy_generation", 1)
                     ("witness_hash", real_block_7_result.afp_base_digest)
                     ("finality_mroot", real_block_7_result.finality_root)
                  )
                  ("dynamic_data", mvo() 
                     ("block_num", real_block_7_result.block->block_num())
                     ("action_proofs", fc::variants())
                     ("action_mroot", real_block_7_result.action_mroot)
                  )
               )
               ("merkle_branches", finality_proof::generate_proof_of_inclusion(real_chain.get_finality_leaves(7), 7))
            );
      
      // assemble the finality violation proof
      mutable_variant_object finality_violation_proof = mvo()
         ("finalizer_policy", real_chain.active_finalizer_policy)
         ("proof1", proof1)
         ("proof2", proof2);

      // submit the finality violation proof to the smart contract
      auto result = real_chain.node0.push_action("violation"_n, "addviolation"_n, "user1"_n, finality_violation_proof);

      // if the proof was accepted, it means the finality violation has been verified as valid. Capture the return value
      std::vector<char> return_value = result->action_traces[0].return_value;

      // lazy parsing of the return value
      // skip one byte (variable-length int encoding), read 2 bytes (intersection as a string)
      std::string intersection(return_value.begin() + 1, return_value.begin() + 3);
      // skip one byte (variable-length int encoding), read 2 bytes (symmetric difference as a string)
      std::string symmetric_difference(return_value.begin() + 4, return_value.end());

      // verdict is reached, verify that node0 and node1 are guilty, while node2 and node3 are innocent
      BOOST_TEST(intersection == "03"); //node0 and node1 are guilty
      BOOST_TEST(symmetric_difference == "0c"); //node2 ande node3 are not guilty

   } FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
