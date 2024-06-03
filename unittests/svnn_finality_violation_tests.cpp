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

   BOOST_AUTO_TEST_CASE(two_chains_test) { try {

      // setup the fake chain. node3 doesn't receive votes on the fake chain
      finality_proof::proof_test_cluster<4> fake_chain;
      fake_chain.vote_propagation = {1, 1, 0};
      std::string fake_bitset("07"); //node0, node1, node2 are signing on the fake chain

      fake_chain.node0.create_accounts( { "user1"_n, "user2"_n, "violation"_n, "eosio.token"_n } );

      fake_chain.node0.set_code( "eosio.token"_n, test_contracts::eosio_token_wasm() );
      fake_chain.node0.set_abi( "eosio.token"_n, test_contracts::eosio_token_abi() );

      fake_chain.node0.set_code( "violation"_n, test_contracts::finality_violation_wasm() );
      fake_chain.node0.set_abi( "violation"_n, test_contracts::finality_violation_abi() );

      // setup the real chain. node2 doesn't receive votes on the real chain
      finality_proof::proof_test_cluster<4> real_chain;
      real_chain.vote_propagation = {1, 0, 1};
      std::string real_bitset("0b"); //node0, node1, node3 are signing on the real chain

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

      auto indices_policy_B = fake_chain.fin_policy_indices_0;  
      auto indices_policy_C = fake_chain.fin_policy_indices_0;  
      auto indices_policy_D = fake_chain.fin_policy_indices_0;  
      auto indices_policy_E = fake_chain.fin_policy_indices_0;  
      auto indices_policy_F = fake_chain.fin_policy_indices_0;  

      indices_policy_B[0] = 3;
      indices_policy_B[1] = 13;

      indices_policy_C[0] = 4;
      indices_policy_C[1] = 14;

      indices_policy_D[0] = 5;
      indices_policy_D[1] = 15;

      indices_policy_E[0] = 6;
      indices_policy_E[1] = 16;

      indices_policy_F[0] = 7;
      indices_policy_F[1] = 17;

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

      auto fake_block_7_result = fake_chain.produce_block();
      auto real_block_7_result = real_chain.produce_block();

      auto fake_block_8_result = fake_chain.produce_block();
      auto real_block_8_result = real_chain.produce_block();

      auto fake_block_9_result = fake_chain.produce_block();
      auto real_block_9_result = real_chain.produce_block();

      auto fake_block_10_result = fake_chain.produce_block();
      auto real_block_10_result = real_chain.produce_block();

      fake_chain.node0.finkeys.set_finalizer_policy(indices_policy_E);
      auto fake_block_11_result = fake_chain.produce_block();

      real_chain.node0.finkeys.set_finalizer_policy(indices_policy_B);
      auto real_block_11_result = real_chain.produce_block();

      auto fake_block_12_result = fake_chain.produce_block();
      auto real_block_12_result = real_chain.produce_block();

      auto fake_block_13_result = fake_chain.produce_block();
      auto real_block_13_result = real_chain.produce_block();

      fake_chain.node0.finkeys.set_finalizer_policy(indices_policy_C);
      auto fake_block_14_result = fake_chain.produce_block();

      real_chain.node0.finkeys.set_finalizer_policy(indices_policy_C);
      auto real_block_14_result = real_chain.produce_block();

      auto fake_block_15_result = fake_chain.produce_block();
      auto real_block_15_result = real_chain.produce_block();

      auto fake_block_16_result = fake_chain.produce_block();

      real_chain.node0.finkeys.set_finalizer_policy(indices_policy_D);
      auto real_block_16_result = real_chain.produce_block();

      fake_chain.node0.finkeys.set_finalizer_policy(indices_policy_F);
      auto fake_block_17_result = fake_chain.produce_block();
      auto real_block_17_result = real_chain.produce_block();

      auto fake_block_18_result = fake_chain.produce_block();
      auto real_block_18_result = real_chain.produce_block();

      auto fake_block_19_result = fake_chain.produce_block();
      auto real_block_19_result = real_chain.produce_block();

      auto fake_block_20_result = fake_chain.produce_block();
      auto real_block_20_result = real_chain.produce_block();

      std::cout << "\n*** Block 10 ***" << "\n";
      std::cout << "  Fake Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << fake_block_10_result.last_proposed_finalizer_policy_digest << 
                                          "->" << fake_block_10_result.last_pending_finalizer_policy_digest << 
                                          "->" << fake_block_10_result.active_finalizer_policy_digest << "\n";

      std::cout << "  Real Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << real_block_10_result.last_proposed_finalizer_policy_digest << 
                                          "->" << real_block_10_result.last_pending_finalizer_policy_digest << 
                                          "->" << real_block_10_result.active_finalizer_policy_digest << "\n";

      std::cout << "\n*** Block 11 ***" << "\n";
      std::cout << "  Fake Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << fake_block_11_result.last_proposed_finalizer_policy_digest << 
                                          "->" << fake_block_11_result.last_pending_finalizer_policy_digest << 
                                          "->" << fake_block_11_result.active_finalizer_policy_digest << "\n";

      std::cout << "  Real Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << real_block_11_result.last_proposed_finalizer_policy_digest << 
                                          "->" << real_block_11_result.last_pending_finalizer_policy_digest << 
                                          "->" << real_block_11_result.active_finalizer_policy_digest << "\n";

      std::cout << "\n*** Block 12 ***" << "\n";
      std::cout << "  Fake Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << fake_block_12_result.last_proposed_finalizer_policy_digest << 
                                          "->" << fake_block_12_result.last_pending_finalizer_policy_digest << 
                                          "->" << fake_block_12_result.active_finalizer_policy_digest << "\n";

      std::cout << "  Real Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << real_block_12_result.last_proposed_finalizer_policy_digest << 
                                          "->" << real_block_12_result.last_pending_finalizer_policy_digest << 
                                          "->" << real_block_12_result.active_finalizer_policy_digest << "\n";

      std::cout << "\n*** Block 13 ***" << "\n";
      std::cout << "  Fake Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << fake_block_13_result.last_proposed_finalizer_policy_digest << 
                                          "->" << fake_block_13_result.last_pending_finalizer_policy_digest << 
                                          "->" << fake_block_13_result.active_finalizer_policy_digest << "\n";

      std::cout << "  Real Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << real_block_13_result.last_proposed_finalizer_policy_digest << 
                                          "->" << real_block_13_result.last_pending_finalizer_policy_digest << 
                                          "->" << real_block_13_result.active_finalizer_policy_digest << "\n";

      std::cout << "\n*** Block 14 ***" << "\n";
      std::cout << "  Fake Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << fake_block_14_result.last_proposed_finalizer_policy_digest << 
                                          "->" << fake_block_14_result.last_pending_finalizer_policy_digest << 
                                          "->" << fake_block_14_result.active_finalizer_policy_digest << "\n";

      std::cout << "  Real Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << real_block_14_result.last_proposed_finalizer_policy_digest << 
                                          "->" << real_block_14_result.last_pending_finalizer_policy_digest << 
                                          "->" << real_block_14_result.active_finalizer_policy_digest << "\n";

      std::cout << "\n*** Block 15 ***" << "\n";
      std::cout << "  Fake Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << fake_block_15_result.last_proposed_finalizer_policy_digest << 
                                          "->" << fake_block_15_result.last_pending_finalizer_policy_digest << 
                                          "->" << fake_block_15_result.active_finalizer_policy_digest << "\n";

      std::cout << "  Real Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << real_block_15_result.last_proposed_finalizer_policy_digest << 
                                          "->" << real_block_15_result.last_pending_finalizer_policy_digest << 
                                          "->" << real_block_15_result.active_finalizer_policy_digest << "\n";

      std::cout << "\n*** Block 16 ***" << "\n";
      std::cout << "  Fake Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << fake_block_16_result.last_proposed_finalizer_policy_digest << 
                                          "->" << fake_block_16_result.last_pending_finalizer_policy_digest << 
                                          "->" << fake_block_16_result.active_finalizer_policy_digest << "\n";

      std::cout << "  Real Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << real_block_16_result.last_proposed_finalizer_policy_digest << 
                                          "->" << real_block_16_result.last_pending_finalizer_policy_digest << 
                                          "->" << real_block_16_result.active_finalizer_policy_digest << "\n";

      std::cout << "\n*** Block 17 ***" << "\n";
      std::cout << "  Fake Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << fake_block_17_result.last_proposed_finalizer_policy_digest << 
                                          "->" << fake_block_17_result.last_pending_finalizer_policy_digest << 
                                          "->" << fake_block_17_result.active_finalizer_policy_digest << "\n";

      std::cout << "  Real Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << real_block_17_result.last_proposed_finalizer_policy_digest << 
                                          "->" << real_block_17_result.last_pending_finalizer_policy_digest << 
                                          "->" << real_block_17_result.active_finalizer_policy_digest << "\n";

      std::cout << "\n*** Block 18 ***" << "\n";
      std::cout << "  Fake Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << fake_block_18_result.last_proposed_finalizer_policy_digest << 
                                          "->" << fake_block_18_result.last_pending_finalizer_policy_digest << 
                                          "->" << fake_block_18_result.active_finalizer_policy_digest << "\n";

      std::cout << "  Real Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << real_block_18_result.last_proposed_finalizer_policy_digest << 
                                          "->" << real_block_18_result.last_pending_finalizer_policy_digest << 
                                          "->" << real_block_18_result.active_finalizer_policy_digest << "\n";

      std::cout << "\n*** Block 19 ***" << "\n";
      std::cout << "  Fake Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << fake_block_19_result.last_proposed_finalizer_policy_digest << 
                                          "->" << fake_block_19_result.last_pending_finalizer_policy_digest << 
                                          "->" << fake_block_19_result.active_finalizer_policy_digest << "\n";

      std::cout << "  Real Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << real_block_19_result.last_proposed_finalizer_policy_digest << 
                                          "->" << real_block_19_result.last_pending_finalizer_policy_digest << 
                                          "->" << real_block_19_result.active_finalizer_policy_digest << "\n";

      std::cout << "\n*** Block 20 ***" << "\n";
      std::cout << "  Fake Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << fake_block_20_result.last_proposed_finalizer_policy_digest << 
                                          "->" << fake_block_20_result.last_pending_finalizer_policy_digest << 
                                          "->" << fake_block_20_result.active_finalizer_policy_digest << "\n";

      std::cout << "  Real Chain : " << "\n";
      std::cout << "    Prop -> Pend -> Act :" << real_block_20_result.last_proposed_finalizer_policy_digest << 
                                          "->" << real_block_20_result.last_pending_finalizer_policy_digest << 
                                          "->" << real_block_20_result.active_finalizer_policy_digest << "\n";



   } FC_LOG_AND_RETHROW() }

   BOOST_AUTO_TEST_CASE(finality_violation_test) { try {

      return;

      /* 

         In this test, node0 and node1 are colluding to create a finality violation.
       
         node0 ande node1 partition the network in such a way that node2 and node3, who are both honest nodes, are disconnected from each other.

         node2 is tricked into joining a fake chain where node0 and node1 are finalizing, but from which node3 is absent.

         Meanwhile, node2 is absent from the real chain, but node0 and node1 are still finalizing on it, therefore double-signing between the fake and real chains.

         node3, unaware of the ongoing attack, is finalizing on the real chain normally.

         user1, a light client, unknowingly joins the fake chain, and submits an important transaction to it.

         user1 stores enough data to prove the final inclusion of this important transaction into the fake chain.

         user1 discovers the real chain later, and uses the stored data to construct and submit a finality violation proof.

      */

      // setup the fake chain. node3 doesn't receive votes on the fake chain
      finality_proof::proof_test_cluster<4> fake_chain;
      fake_chain.vote_propagation = {1, 1, 0};
      std::string fake_bitset("07"); //node0, node1, node2 are signing on the fake chain

      fake_chain.node0.create_accounts( { "user1"_n, "user2"_n, "violation"_n, "eosio.token"_n } );

      fake_chain.node0.set_code( "eosio.token"_n, test_contracts::eosio_token_wasm() );
      fake_chain.node0.set_abi( "eosio.token"_n, test_contracts::eosio_token_abi() );

      fake_chain.node0.set_code( "violation"_n, test_contracts::finality_violation_wasm() );
      fake_chain.node0.set_abi( "violation"_n, test_contracts::finality_violation_abi() );

      // setup the real chain. node2 doesn't receive votes on the real chain
      finality_proof::proof_test_cluster<4> real_chain;
      real_chain.vote_propagation = {1, 0, 1};
      std::string real_bitset("0b"); //node0, node1, node3 are signing on the real chain

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
      action_trace important_transfer_trace = fake_chain.node0.push_action("eosio.token"_n, "transfer"_n, "user1"_n, important_transfer)->action_traces[0];

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
                  ("final_on_qc_block_num", 11)
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
               ("target", fc::variants{"extended_block_data", mvo()
                  ("finality_data", mvo() 
                     ("major_version", 1)
                     ("minor_version", 0)
                     ("finalizer_policy_generation", 1)
                     ("final_on_qc_block_num", 9)
                     ("witness_hash", fake_block_7_result.afp_base_digest)
                     ("finality_mroot", fake_block_7_result.finality_root)
                  )
                  ("dynamic_data", mvo() 
                     ("block_num", fake_block_7_result.block->block_num())
                     ("action_proofs", fc::variants())
                     ("action_mroot", fake_block_7_result.action_mroot)
                  )}
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
                  ("final_on_qc_block_num", 11)
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
               ("target",  fc::variants{"extended_block_data", mvo()
                  ("finality_data", mvo() 
                     ("major_version", 1)
                     ("minor_version", 0)
                     ("finalizer_policy_generation", 1)
                     ("final_on_qc_block_num", 9)
                     ("witness_hash", real_block_7_result.afp_base_digest)
                     ("finality_mroot", real_block_7_result.finality_root)
                  )
                  ("dynamic_data", mvo() 
                     ("block_num", real_block_7_result.block->block_num())
                     ("action_proofs", fc::variants())
                     ("action_mroot", real_block_7_result.action_mroot)
                  )}
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

   BOOST_AUTO_TEST_CASE(finality_violation_test_2) { try {

      return;

      /* 

         In this test, node0 and node1 are colluding to create a finality violation.
       
         node0 ande node1 partition the network in such a way that node2 and node3, who are both honest nodes, are disconnected from each other.

         node2 is tricked into joining a fake chain where node0 and node1 are finalizing, but from which node3 is absent.

         Meanwhile, node2 is absent from the real chain, but node0 and node1 are still finalizing on it, therefore double-signing between the fake and real chains.

         user1, a light client, is active on the chain. user1 records the active finalizer policy for the chain (both real and fake policy generation is #1 at this stage).

         The finalizer policy on the fake chain is updated, causing the initial fork between the two chains. user1, unknowingly, follows this chain.

         user1 records the finalizer policy change (fake policy generation becomes #2).

         Later, a finalizer policy change also occurs on the real change, without user1's knowledge (real policy generation becomes #2).

         Later, a second finalizer policy change occurs on the fake chain. user1 records this finalizer policy change as well (fake policy generation becomes #3).

         user1 then discovers the real chain. Looking at its own finalizer policies history, user1 discovers the last common policy is #1.

      */

      //internal state
      struct internal_state {

         //stores all finalizer policies (or enough data to reconstruct all finalizer policies) 
         std::vector<std::pair<finalizer_policy, mvo>> stored_policies;

         //always store the last proof of finality that advances the finality of the chain
         mvo last_proof_of_finality;

      };

      internal_state user1_state;
      internal_state real_chain_state;


      // setup the fake chain. node3 doesn't receive votes on the fake chain
      finality_proof::proof_test_cluster<4> fake_chain;
      fake_chain.vote_propagation = {1, 1, 0};
      std::string fake_bitset("07"); //node0, node1, node2 are signing on the fake chain

      fake_chain.node0.create_accounts( { "user1"_n, "user2"_n, "violation"_n, "eosio.token"_n } );

      fake_chain.node0.set_code( "eosio.token"_n, test_contracts::eosio_token_wasm() );
      fake_chain.node0.set_abi( "eosio.token"_n, test_contracts::eosio_token_abi() );

      fake_chain.node0.set_code( "violation"_n, test_contracts::finality_violation_wasm() );
      fake_chain.node0.set_abi( "violation"_n, test_contracts::finality_violation_abi() );

      // setup the real chain. node2 doesn't receive votes on the real chain
      finality_proof::proof_test_cluster<4> real_chain;
      real_chain.vote_propagation = {1, 0, 1};
      std::string real_bitset("0b"); //node0, node1, node3 are signing on the real chain

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

      // prepare finalizer policies indices ahead of time

      // verify initial indices are the same
      BOOST_TEST(fake_chain.fin_policy_indices_0 == real_chain.fin_policy_indices_0);

      // create indices for the upcoming policies we'll use in this test
      auto indices_fake_policy_2 = fake_chain.fin_policy_indices_0;  
      auto indices_fake_policy_3 = fake_chain.fin_policy_indices_0;  
      auto indices_real_policy_2 = fake_chain.fin_policy_indices_0;  

      indices_fake_policy_2[0] = 2;
      indices_fake_policy_2[1] = 12;

      indices_fake_policy_3[0] = 3;
      indices_fake_policy_3[1] = 13;

      indices_real_policy_2[0] = 4;
      indices_real_policy_2[1] = 14;

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

      //at this stage, we have enough data to create a proof of finality for the genesis block and the initial finalizer policy
      qc_data_t fake_qc_b_3 = extract_qc_data(fake_block_3_result.block);
      qc_data_t real_qc_b_3 = extract_qc_data(real_block_3_result.block);

      BOOST_TEST(fake_qc_b_3.qc.has_value());
      BOOST_TEST(real_qc_b_3.qc.has_value());

      BOOST_TEST(fake_qc_b_3.qc.value().data.sig.to_string() != real_qc_b_3.qc.value().data.sig.to_string());

      mvo proof_fake_gen_1 = finality_proof::get_finality_proof(fake_genesis_block_result, 
                                    fake_block_2_result, 
                                    fake_chain.active_finalizer_policy.generation, 
                                    fake_chain.active_finalizer_policy.generation, 
                                    0, 
                                    0, 
                                    fake_qc_b_3.qc.value().data.sig.to_string(), 
                                    fake_bitset,
                                    finality_proof::generate_proof_of_inclusion(fake_chain.get_finality_leaves(0), 0));

      // user1 stores the first policy
      user1_state.stored_policies.push_back(std::make_pair(fake_chain.last_pending_finalizer_policy, proof_fake_gen_1));

      mvo proof_real_gen_1 = finality_proof::get_finality_proof(real_genesis_block_result, 
                                    real_block_2_result, 
                                    real_chain.active_finalizer_policy.generation, 
                                    real_chain.active_finalizer_policy.generation, 
                                    0, 
                                    0, 
                                    real_qc_b_3.qc.value().data.sig.to_string(), 
                                    real_bitset,
                                    finality_proof::generate_proof_of_inclusion(real_chain.get_finality_leaves(0), 0));

      // the real chain state can also be updated
      real_chain_state.stored_policies.push_back(std::make_pair(real_chain.last_pending_finalizer_policy, proof_real_gen_1));

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

      qc_data_t fake_qc_b_5 = extract_qc_data(fake_block_5_result.block);
      qc_data_t real_qc_b_5 = extract_qc_data(real_block_5_result.block);

      qc_data_t fake_qc_b_6 = extract_qc_data(fake_block_6_result.block);
      qc_data_t real_qc_b_6 = extract_qc_data(real_block_6_result.block);

      // verify QCs on block #5 and onward for both chains
      BOOST_TEST(fake_qc_b_5.qc.has_value());
      BOOST_TEST(real_qc_b_5.qc.has_value());
      BOOST_TEST(fake_qc_b_6.qc.has_value());
      BOOST_TEST(real_qc_b_6.qc.has_value());

      // verify that the signatures on QCs are different on both chains
      BOOST_TEST(fake_qc_b_5.qc.value().data.sig.to_string() != real_qc_b_5.qc.value().data.sig.to_string());
      BOOST_TEST(fake_qc_b_6.qc.value().data.sig.to_string() != real_qc_b_6.qc.value().data.sig.to_string());
      
      // verify we still have the same chain on both sides at this stage
      BOOST_TEST(fake_chain.produce_blocks(6).finality_leaf == real_chain.produce_blocks(6).finality_leaf);
      
      // a new finalizer policy is proposed on the fake chain, setting the stage for the initial fork
      fake_chain.node0.finkeys.set_finalizer_policy(indices_fake_policy_2);

      auto fake_block_7_result = fake_chain.produce_block();
      auto real_block_7_result = real_chain.produce_block();

      // verify the chains are now forked
      BOOST_TEST(fake_block_7_result.finality_leaf != real_block_7_result.finality_leaf);

      auto fake_block_8_result = fake_chain.produce_block();
      auto fake_block_9_result = fake_chain.produce_block();

      auto real_block_8_result = real_chain.produce_block();
      auto real_block_9_result = real_chain.produce_block();

      // finalizer digest calculation is updated on the fake chain to reflect the pending finalizer policy.
      // However, current finalizer policy is still in effect for both chains.
      auto fake_block_10_result = fake_chain.produce_block(); // qc on fake block #3, making fake block #1 final
      auto real_block_10_result = real_chain.produce_block(); // qc on fake block #3, making real block #1 final

      qc_data_t fake_qc_b_10 = extract_qc_data(fake_block_10_result.block);
      qc_data_t real_qc_b_10 = extract_qc_data(real_block_10_result.block);

      BOOST_TEST(fake_qc_b_10.qc.has_value());
      BOOST_TEST(real_qc_b_10.qc.has_value());

      mvo proof_fake_gen_2 = finality_proof::get_finality_proof(fake_block_7_result, 
                                    fake_block_9_result, 
                                    fake_chain.active_finalizer_policy.generation, 
                                    fake_chain.active_finalizer_policy.generation, 
                                    7, 
                                    7, 
                                    fake_qc_b_10.qc.value().data.sig.to_string(), 
                                    fake_bitset,
                                    finality_proof::generate_proof_of_inclusion(fake_chain.get_finality_leaves(7), 7));

      // updated finalizer policy becomes active on the fake chain. user1 stores the 2nd policy
      user1_state.stored_policies.push_back(std::make_pair(fake_chain.last_pending_finalizer_policy, proof_fake_gen_2));

      // because this block was signed by node0 and node1 on both chains, a finality violation has now occured,
      // as a different version of block_1 has reached finality on both the fake and real chains.

      fake_chain.produce_blocks(2);
      real_chain.produce_blocks(2);

      // the finalizer policy on the real chain changes
      real_chain.node0.finkeys.set_finalizer_policy(indices_real_policy_2);

      auto fake_block_13_result = fake_chain.produce_block();
      auto real_block_13_result = real_chain.produce_block();

      auto fake_block_14_result = fake_chain.produce_block();
      auto fake_block_15_result = fake_chain.produce_block();

      auto real_block_14_result = real_chain.produce_block();
      auto real_block_15_result = real_chain.produce_block();

      auto fake_block_16_result = fake_chain.produce_block();
      auto real_block_16_result = real_chain.produce_block();

      qc_data_t fake_qc_b_16 = extract_qc_data(fake_block_16_result.block);
      qc_data_t real_qc_b_16 = extract_qc_data(real_block_16_result.block);

      BOOST_TEST(fake_qc_b_16.qc.has_value());
      BOOST_TEST(real_qc_b_16.qc.has_value());

      mvo proof_real_gen_2 = finality_proof::get_finality_proof(real_block_13_result, 
                                    real_block_15_result, 
                                    real_chain.active_finalizer_policy.generation, 
                                    real_chain.active_finalizer_policy.generation, 
                                    13, 
                                    13, 
                                    real_qc_b_16.qc.value().data.sig.to_string(), 
                                    real_bitset,
                                    finality_proof::generate_proof_of_inclusion(real_chain.get_finality_leaves(13), 13));

      //user1 is not yet aware of this policy
      real_chain_state.stored_policies.push_back(std::make_pair(real_chain.last_pending_finalizer_policy, proof_real_gen_2));

      fake_chain.produce_blocks(2);
      real_chain.produce_blocks(2);

      // one additional finalizer policy change on the fake chain
      fake_chain.node0.finkeys.set_finalizer_policy(indices_fake_policy_3);

      auto fake_block_17_result = fake_chain.produce_block();
      auto real_block_17_result = real_chain.produce_block();

      auto fake_block_18_result = fake_chain.produce_block();
      auto fake_block_19_result = fake_chain.produce_block();

      auto real_block_18_result = real_chain.produce_block();
      auto real_block_19_result = real_chain.produce_block();

      auto fake_block_20_result = fake_chain.produce_block();
      auto real_block_20_result = real_chain.produce_block();

      qc_data_t fake_qc_b_20 = extract_qc_data(fake_block_20_result.block);
      qc_data_t real_qc_b_20 = extract_qc_data(real_block_20_result.block);

      BOOST_TEST(fake_qc_b_20.qc.has_value());
      BOOST_TEST(real_qc_b_20.qc.has_value());

      fake_chain.produce_blocks(2);
      real_chain.produce_blocks(2);

      // user1 stores the 3rd policy
      mvo proof_fake_gen_3 = finality_proof::get_finality_proof(fake_block_17_result, 
                                    fake_block_19_result, 
                                    fake_chain.active_finalizer_policy.generation, 
                                    fake_chain.active_finalizer_policy.generation, 
                                    17, 
                                    17, 
                                    fake_qc_b_20.qc.value().data.sig.to_string(), 
                                    fake_bitset,
                                    finality_proof::generate_proof_of_inclusion(fake_chain.get_finality_leaves(17), 17));

      // updated finalizer policy becomes active on the fake chain. user1 stores the 2nd policy
      user1_state.stored_policies.push_back(std::make_pair(fake_chain.last_pending_finalizer_policy, proof_fake_gen_3));

      auto fake_block_21_result = fake_chain.produce_block();
      auto real_block_21_result = real_chain.produce_block();

      auto fake_block_22_result = fake_chain.produce_block();
      auto fake_block_23_result = fake_chain.produce_block();

      auto real_block_22_result = real_chain.produce_block();
      auto real_block_23_result = real_chain.produce_block();

      auto fake_block_24_result = fake_chain.produce_block();
      auto real_block_24_result = real_chain.produce_block();

      qc_data_t fake_qc_b_24 = extract_qc_data(fake_block_24_result.block);
      qc_data_t real_qc_b_24 = extract_qc_data(real_block_24_result.block);

      BOOST_TEST(fake_qc_b_24.qc.has_value());
      BOOST_TEST(real_qc_b_24.qc.has_value());

      // user1 always keeps the last block with a QC. For brevity, we only compute the proof on the final block of this test.
      // In production, a light client would generate a proof of finality for each block, and discard it when it has a proof of finality of a more recent block
      user1_state.last_proof_of_finality = finality_proof::get_finality_proof(fake_block_21_result, 
                                    real_block_23_result, 
                                    fake_chain.active_finalizer_policy.generation, 
                                    fake_chain.active_finalizer_policy.generation, 
                                    21, 
                                    21, 
                                    fake_qc_b_24.qc.value().data.sig.to_string(), 
                                    fake_bitset,
                                    finality_proof::generate_proof_of_inclusion(fake_chain.get_finality_leaves(21), 21));

      // todo : demonstrate that the real chain is conflict with my last proof of finality

      // user1 now discovers the real chain by receiving a proof of finality over a block that it cannot link to its existing state.
      // At this stage, user1 already knows a finality violation has occured, and must now be able to prove it, and be able to correctly assign the blame. 

      // We must assume at this stage that the fake chain has been completely erased, and there are no 
      // proofs of its existence, other than what user1 has recorded. The real chain however is fully discoverable, and all history from that chain can be
      // recovered.

      // The first step for user1 is to look at the real chain's history of finalizer policies, and compare with the history of the policies it has recorded
      // on the fake chain.

      auto f_itr = user1_state.stored_policies.rbegin();

      auto common_policy = real_chain_state.stored_policies.end();

      // user1 will also need to select the last proof signed by the common policy generation on the fake chain
      mvo last_known_gen_QC = user1_state.last_proof_of_finality;

      // Going back through the fake chain's finalizer policies it has recorded, as well as going back through the history of the real chain, user1 
      // can trivially discover the last finalizer policy common to both chains.
      while (f_itr!=user1_state.stored_policies.rend()){
         auto policy_digest = fc::sha256::hash(f_itr->first);
         auto r_itr = std::find_if(real_chain_state.stored_policies.begin(), real_chain_state.stored_policies.end(), 
            [&](const auto& p){return fc::sha256::hash(p.first) == policy_digest ;});
         if (r_itr!=real_chain_state.stored_policies.end()){
            // In this example, the last common finalizer policy is #1
            BOOST_TEST(r_itr->first.generation == 1);
            common_policy = r_itr;
            break;
         }
         last_known_gen_QC = f_itr->second; //if there was a policy change, the proof of finality for the setfinalizer action becomes the most relevant proof
         f_itr++;
      }

      bool found_common = common_policy!=real_chain_state.stored_policies.end();
      BOOST_TEST(found_common);

      // The initial finality violation must necessarily have occured while this last common policy was in force, as two conflicting chains cannot have a 
      // different active policy at the same height unless a finality violation has already occured before.

      // user1 is necessarily aware of a QC signed by the last common policy on the fake chain which makes a certain block final. 

      // ** Note : With the current implementation, user1 doesn't store information indicating which block is made final. The only piece of information they
      // record about this is the finality merkle root at that block height, which is useless without the surrounding chain data and history (which we assume
      // has been erased). However, if we expand the finality digest commitments to include the final 
      // on qc block number, we can compare

      // There must also exist one QC
      // which makes a different block at the same height become final on the real chain.
 
      // user1 can then find the QC on the real chain that made a conflicting block with the same number final.

      //


   } FC_LOG_AND_RETHROW() }


BOOST_AUTO_TEST_SUITE_END()
