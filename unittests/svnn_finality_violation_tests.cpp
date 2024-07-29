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

        finality_proof::proof_test_cluster cluster_1; 
        finality_proof::proof_test_cluster cluster_2; 

        cluster_1.vote_propagation = {1,1,0};
        cluster_2.vote_propagation = {1,0,1};

    } FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
