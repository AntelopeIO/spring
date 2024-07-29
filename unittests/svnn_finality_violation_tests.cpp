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

    BOOST_AUTO_TEST_CASE(cluster_vote_propagation_tests) { try {

        finality_proof::proof_test_cluster cluster_1; 
        finality_proof::proof_test_cluster cluster_2; 
        finality_proof::proof_test_cluster cluster_3; 

        cluster_1.vote_propagation = {1,1,1}; //all finalizers present
        cluster_2.vote_propagation = {1,1,0}; //one finalizer missing
        cluster_3.vote_propagation = {1,0,0}; //2 finalizers missing (insufficient for finality progress)

        auto genesis_1_block_result = cluster_1.produce_block();
        auto block_1_1_result = cluster_1.produce_block();
        auto block_2_1_result = cluster_1.produce_block();
        auto block_3_1_result = cluster_1.produce_block();
        auto block_4_1_result = cluster_1.produce_block();

        BOOST_TEST(block_4_1_result.qc_data.qc.has_value());

        auto genesis_2_block_result = cluster_2.produce_block();
        auto block_1_2_result = cluster_2.produce_block();
        auto block_2_2_result = cluster_2.produce_block();
        auto block_3_2_result = cluster_2.produce_block();
        auto block_4_2_result = cluster_2.produce_block();

        BOOST_TEST(block_4_2_result.qc_data.qc.has_value());

        auto genesis_3_block_result = cluster_3.produce_block();
        auto block_1_3_result = cluster_3.produce_block();
        auto block_2_3_result = cluster_3.produce_block();
        auto block_3_3_result = cluster_3.produce_block();
        auto block_4_3_result = cluster_3.produce_block();

        BOOST_TEST(!block_4_3_result.qc_data.qc.has_value());

    } FC_LOG_AND_RETHROW() }

    BOOST_AUTO_TEST_CASE(finality_violation_tests) { try {

        //prepare a few actions we will be using for this test
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
            ("quantity", "100.0000 EOS")
            ("memo", "");

        mutable_variant_object user1_transfer = mvo()
            ("from", "user1"_n)
            ("to", "user2"_n)
            ("quantity", "1.0000 EOS")
            ("memo", "");

        //setup a "fake" chain and a "real" chain
        finality_proof::proof_test_cluster fake_chain; 
        finality_proof::proof_test_cluster real_chain; 

        //byzantine finalizers 0 and 1 are colluding and partition the network so that honest finalizers 2 and 3 are separated
        fake_chain.vote_propagation = {1,1,0};
        real_chain.vote_propagation = {1,0,1};

        fake_chain.node0.create_accounts( { "user1"_n, "user2"_n, "violation"_n, "eosio.token"_n } );
        real_chain.node0.create_accounts( { "user1"_n, "user2"_n, "violation"_n, "eosio.token"_n } );

        fake_chain.node0.set_code( "eosio.token"_n, test_contracts::eosio_token_wasm() );
        fake_chain.node0.set_abi( "eosio.token"_n, test_contracts::eosio_token_abi() );
        fake_chain.node0.set_code( "violation"_n, test_contracts::finality_violation_wasm() );
        fake_chain.node0.set_abi( "violation"_n, test_contracts::finality_violation_abi() );

        real_chain.node0.set_code( "eosio.token"_n, test_contracts::eosio_token_wasm() );
        real_chain.node0.set_abi( "eosio.token"_n, test_contracts::eosio_token_abi() );
        real_chain.node0.set_code( "violation"_n, test_contracts::finality_violation_wasm() );
        real_chain.node0.set_abi( "violation"_n, test_contracts::finality_violation_abi() );

        fake_chain.node0.push_action("eosio.token"_n, "create"_n, "eosio.token"_n, create_action);
        fake_chain.node0.push_action("eosio.token"_n, "issue"_n, "eosio"_n, issue_action);
        fake_chain.node0.push_action("eosio.token"_n, "transfer"_n, "eosio"_n, initial_transfer);

        real_chain.node0.push_action("eosio.token"_n, "create"_n, "eosio.token"_n, create_action);
        real_chain.node0.push_action("eosio.token"_n, "issue"_n, "eosio"_n, issue_action);
        real_chain.node0.push_action("eosio.token"_n, "transfer"_n, "eosio"_n, initial_transfer);

        //produce a few blocks on the fake chain
        auto fake_chain_genesis_block_result = fake_chain.produce_block();
        auto fake_chain_block_1_result = fake_chain.produce_block();
        auto fake_chain_block_2_result = fake_chain.produce_block();
        auto fake_chain_block_3_result = fake_chain.produce_block();
        auto fake_chain_block_4_result = fake_chain.produce_block();

        BOOST_REQUIRE(fake_chain_block_4_result.qc_data.qc.has_value());

        //produce a few blocks on the real chain
        auto real_chain_genesis_block_result = real_chain.produce_block();
        auto real_chain_block_1_result = real_chain.produce_block();
        auto real_chain_block_2_result = real_chain.produce_block();
        auto real_chain_block_3_result = real_chain.produce_block();
        auto real_chain_block_4_result = real_chain.produce_block();

        BOOST_REQUIRE(real_chain_block_4_result.qc_data.qc.has_value());

        //verify the two chains are the same so far
        BOOST_TEST(fake_chain_block_4_result.finality_digest == real_chain_block_4_result.finality_digest);

        //create a fork by pushing a transaction on the fake chain
        fake_chain.node0.push_action("eosio.token"_n, "transfer"_n, "user1"_n, user1_transfer);

        auto fake_chain_block_5_result = real_chain.produce_block();
        auto real_chain_block_5_result = real_chain.produce_block();

        //verify the chains are forked
        BOOST_TEST(fake_chain_block_5_result.finality_digest != real_chain_block_5_result.finality_digest);

    } FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
