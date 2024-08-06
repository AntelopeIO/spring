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

using namespace finality_proof;

using mvo = mutable_variant_object;


mvo prepare_rule_1_proof(  const finalizer_policy active_finalizer_policy, 
                    const ibc_block_data_t fake_qc_block, 
                    const qc_t fake_qc, 
                    const ibc_block_data_t real_qc_block, 
                    const qc_t real_qc){

    return mvo()
        ("finalizer_policy", active_finalizer_policy)
        ("proof_1", mvo() 
           ("qc_block", mvo()
              ("major_version", 1)
              ("minor_version", 0)
              ("active_finalizer_policy_generation", 1)
              ("pending_finalizer_policy_generation", 1)
              ("last_pending_finalizer_policy_start_timestamp", fake_qc_block.last_pending_finalizer_policy_start_timestamp)
              ("last_pending_finalizer_policy", active_finalizer_policy)
              ("level_3_commitments", fake_qc_block.level_3_commitments)
              ("witness_hash", fake_qc_block.base_digest)
              ("finality_mroot", fake_qc_block.finality_root)
           )
           ("active_policy_qc", mvo()
              ("signature", fake_qc.active_policy_sig.sig.to_string())
              ("finalizers", finality_proof::finalizers_string(fake_qc.active_policy_sig.strong_votes.value())) 
           )
        )
        ("proof_2", mvo()
           ("qc_block", mvo()
              ("major_version", 1)
              ("minor_version", 0)
              ("active_finalizer_policy_generation", 1)
              ("pending_finalizer_policy_generation", 1)
              ("last_pending_finalizer_policy_start_timestamp", real_qc_block.last_pending_finalizer_policy_start_timestamp)
              ("last_pending_finalizer_policy", active_finalizer_policy)
              ("level_3_commitments", real_qc_block.level_3_commitments)
              ("witness_hash", real_qc_block.base_digest)
              ("finality_mroot", real_qc_block.finality_root)
           )
           ("active_policy_qc", mvo()
              ("signature", real_qc.active_policy_sig.sig.to_string())
              ("finalizers", finality_proof::finalizers_string(real_qc.active_policy_sig.strong_votes.value())) 
           )
        );

}

mvo prepare_rule_2_lt_proof(  const finalizer_policy active_finalizer_policy, 
                    const ibc_block_data_t fake_qc_block, 
                    const qc_t fake_qc, 
                    const ibc_block_data_t real_qc_block, 
                    const qc_t real_qc){
    return prepare_rule_1_proof(active_finalizer_policy, fake_qc_block, fake_qc, real_qc_block, real_qc);
}

mvo prepare_rule_2_gt_proof(  const finalizer_policy active_finalizer_policy, 
                    const ibc_block_data_t fake_qc_block, 
                    const qc_t fake_qc, 
                    const ibc_block_data_t real_qc_block, 
                    const qc_t real_qc, 
                    const ibc_block_data_t real_target_block, 
                    const uint32_t target_index,
                    const std::vector<digest_type> merkle_branches){

    return mvo()
        ("finalizer_policy", active_finalizer_policy)
        ("proof_1", mvo() 
           ("qc_block", mvo()
              ("major_version", 1)
              ("minor_version", 0)
              ("active_finalizer_policy_generation", 1)
              ("pending_finalizer_policy_generation", 1)
              ("last_pending_finalizer_policy_start_timestamp", fake_qc_block.last_pending_finalizer_policy_start_timestamp)
              ("last_pending_finalizer_policy", active_finalizer_policy)
              ("level_3_commitments", fake_qc_block.level_3_commitments)
              ("witness_hash", fake_qc_block.base_digest)
              ("finality_mroot", fake_qc_block.finality_root)
           )
           ("active_policy_qc", mvo()
              ("signature", fake_qc.active_policy_sig.sig.to_string())
              ("finalizers", finality_proof::finalizers_string(fake_qc.active_policy_sig.strong_votes.value())) 
           )
        )
        ("proof_2", mvo()
           ("qc_block", mvo()
              ("major_version", 1)
              ("minor_version", 0)
              ("active_finalizer_policy_generation", 1)
              ("pending_finalizer_policy_generation", 1)
              ("last_pending_finalizer_policy_start_timestamp", real_qc_block.last_pending_finalizer_policy_start_timestamp)
              ("last_pending_finalizer_policy", active_finalizer_policy)
              ("level_3_commitments", real_qc_block.level_3_commitments)
              ("witness_hash", real_qc_block.base_digest)
              ("finality_mroot", real_qc_block.finality_root)
           )
           ("active_policy_qc", mvo()
              ("signature", real_qc.active_policy_sig.sig.to_string())
              ("finalizers", finality_proof::finalizers_string(real_qc.active_policy_sig.strong_votes.value())) 
           )
        )
        ("target_block_proof_of_inclusion", mvo() 
           ("target_block_index", target_index)
           ("final_block_index", target_index)
           ("target", fc::variants{"extended_block_data", mvo() //target block #2
              ("major_version", 1)
              ("minor_version", 0)
              ("finality_digest", real_target_block.finality_digest)
              ("timestamp", real_target_block.block->timestamp)
              ("parent_timestamp", real_target_block.parent_timestamp)
              ("dynamic_data", mvo() 
                 ("block_num", real_target_block.block->block_num())
                 ("action_proofs", fc::variants())
                 ("action_mroot", real_target_block.action_mroot)
              )}
           )
           ("merkle_branches", merkle_branches)
        );

}


bool shouldPass(const finality_proof::proof_test_cluster& chain, const account_name rule, const mvo proof){

    bool last_action_failed = false;
    try {chain.node0.push_action("violation"_n, rule, "violation"_n, proof);}
    catch (const eosio_assert_message_exception& e){last_action_failed = true;}

    return !last_action_failed;

}

bool shouldFail(const finality_proof::proof_test_cluster& chain, const account_name rule, const mvo proof){

    bool last_action_failed = false;
    try {chain.node0.push_action("violation"_n, rule, "violation"_n, proof);}
    catch (const eosio_assert_message_exception& e){last_action_failed = true;}

    return last_action_failed;
    
}

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

        //setup a light client data cache

        //data retained for every policy change
        struct policy_sunset_data { 
            
            finalizer_policy policy;

            ibc_block_data_t last_qc_block;
            qc_data_t last_qc;

        };

        struct data_cache {

            //The data_cache stores data relevant to finality violation proofs.
            //It only operates in optimistic mode, which is sufficient for finality violation proofs testing purposes

            //store the reversible blocks
            std::vector<ibc_block_data_t> reversible_blocks;

            //store the last block over which we have a QC, as well as said QC
            ibc_block_data_t high_qc_block;
            qc_t high_qc;

            //store the last final block, as wall as the first QC over it. The last_final_qc and high_qc together constitute the 2-chains required for finality progress
            ibc_block_data_t last_final_block;
            qc_t last_final_qc;

            //store all policies sunset data
            std::vector<policy_sunset_data> policy_sunsets;

            //current active finalizer policy
            finalizer_policy active_finalizer_policy;

            //observe a stream of blocks as they are received, and store minimal data required to construct finality violation proofs in the future
            ibc_block_data_t scan_block(const ibc_block_data_t& block){

                if (reversible_blocks.size()>=2){
                    last_final_block = high_qc_block;
                    last_final_qc = high_qc;
                    reversible_blocks.erase(reversible_blocks.begin());
                }

                if (reversible_blocks.size()>=1){
                    high_qc_block = reversible_blocks[reversible_blocks.size()-1];
                    if (block.qc_data.qc.has_value()) high_qc = block.qc_data.qc.value();
                }

                reversible_blocks.push_back(block);

                active_finalizer_policy = block.active_finalizer_policy;

                return block;
            }

        };

        data_cache light_client_data;

        //setup a "fake" chain and a "real" chain
        finality_proof::proof_test_cluster fake_chain; 
        finality_proof::proof_test_cluster real_chain; 

        //initial finalizer policy A
        auto policy_indices = fake_chain.fin_policy_indices_0;  // start from original set of indices

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
        auto fake_chain_genesis_block_result = light_client_data.scan_block(fake_chain.produce_block()) ;

        auto fake_chain_block_1_result = light_client_data.scan_block(fake_chain.produce_block());
        auto fake_chain_block_2_result = light_client_data.scan_block(fake_chain.produce_block());
        auto fake_chain_block_3_result = light_client_data.scan_block(fake_chain.produce_block());
        auto fake_chain_block_4_result = light_client_data.scan_block(fake_chain.produce_block());

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

        //at this point, we can prepare some "invalid proofs" that the contract will reject
        mutable_variant_object invalid_rule_1_proof_1 = prepare_rule_1_proof(  light_client_data.active_finalizer_policy, 
                                                                    light_client_data.high_qc_block, 
                                                                    light_client_data.high_qc, 
                                                                    real_chain_block_3_result, 
                                                                    real_chain_block_4_result.qc_data.qc.value()); //same finality digest, not a violation proof

        BOOST_CHECK(shouldFail(real_chain, "rule1"_n, invalid_rule_1_proof_1));

        mutable_variant_object invalid_rule_1_proof_2 = prepare_rule_1_proof(  light_client_data.active_finalizer_policy, 
                                                                    light_client_data.high_qc_block, 
                                                                    light_client_data.high_qc, 
                                                                    real_chain_block_2_result, 
                                                                    real_chain_block_3_result.qc_data.qc.value()); //different timestamps, not a violation proof

        BOOST_CHECK(shouldFail(real_chain, "rule1"_n, invalid_rule_1_proof_2));

        //create a fork by pushing a transaction on the fake chain
        fake_chain.node0.push_action("eosio.token"_n, "transfer"_n, "user1"_n, user1_transfer);

        auto fake_chain_block_5_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_5_result = real_chain.produce_block();

        //verify the chains have diverged
        BOOST_TEST(fake_chain_block_5_result.finality_digest != real_chain_block_5_result.finality_digest);

        //qc over block #5. Policy A is now locked on #5
        auto fake_chain_block_6_result = light_client_data.scan_block(fake_chain.produce_block()); 
        auto real_chain_block_6_result = real_chain.produce_block();

        //qc over block #6 makes block #5 final. Since these blocks are different, this is a finality violation.
        auto fake_chain_block_7_result = light_client_data.scan_block(fake_chain.produce_block()); 
        auto real_chain_block_7_result = real_chain.produce_block();

        auto fake_chain_block_8_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_8_result = real_chain.produce_block();

        //prove finality violation (violation of rule #1 : Don't vote on different blocks with the same timestamp)

        //block #8 on the real chain contains a strong QC over a different block #7 than what the light client recorded from the fake chain

        //this is a rule #1 finality violation proof

        //it can be proven by verifying that a strong QC on a block of a given timestamp conflicts with another strong QC on a different block with the same timestamp
        mutable_variant_object valid_rule_1_proof = prepare_rule_1_proof(  light_client_data.active_finalizer_policy, 
                                                                    light_client_data.high_qc_block, 
                                                                    light_client_data.high_qc, 
                                                                    real_chain_block_7_result, 
                                                                    real_chain_block_8_result.qc_data.qc.value());

        BOOST_CHECK(shouldPass(real_chain, "rule1"_n, valid_rule_1_proof));

        //we temporarily disable a finalizer on the fake chain, which serves to set up a proof of violation of rule #2
        fake_chain.vote_propagation = {1,0,0};

        //produce a block on a fake chain without propagating votes to all nodes
        auto fake_chain_block_9_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_9_result = real_chain.produce_block();

        //restore vote propagation for fake chain. This leaves a one-block gap where no finality progress was achieved
        fake_chain.vote_propagation = {1,1,0};

        auto fake_chain_block_10_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_10_result = real_chain.produce_block();

        //Real chain has a QC on #9, but fake chain doesn't
        BOOST_TEST(!fake_chain_block_10_result.qc_data.qc.has_value());
        BOOST_TEST(real_chain_block_10_result.qc_data.qc.has_value());
        
        auto fake_chain_block_11_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_11_result = real_chain.produce_block();

        //Things are back to normal, and we have a QC on both chains
        BOOST_TEST(fake_chain_block_11_result.qc_data.qc.has_value());
        BOOST_TEST(real_chain_block_11_result.qc_data.qc.has_value());
        
        //Light client recorded the last QC on fake chain, which was delivered via block #11, and is over block #10. 
        //Block #10 claims a QC over block #8 (skipping #9). We provide fake block #10 and its QC.
        //We also provide the real block #9 and a QC over it delivered via block #10, which is a proof of violation of rule #2.
        mutable_variant_object valid_rule_2_proof_1 = prepare_rule_2_lt_proof(  light_client_data.active_finalizer_policy, 
                                                                    light_client_data.high_qc_block, 
                                                                    light_client_data.high_qc, 
                                                                    real_chain_block_9_result, 
                                                                    real_chain_block_10_result.qc_data.qc.value());

        BOOST_CHECK(shouldPass(real_chain, "rule2a"_n, valid_rule_2_proof_1));

        real_chain.vote_propagation = {1,0,0};

        auto fake_chain_block_12_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_12_result = real_chain.produce_block();

        BOOST_TEST(fake_chain_block_12_result.qc_data.qc.has_value());
        BOOST_TEST(real_chain_block_12_result.qc_data.qc.has_value());
        
        real_chain.vote_propagation = {1,0,1};

        auto fake_chain_block_13_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_13_result = real_chain.produce_block();

        BOOST_TEST(fake_chain_block_13_result.qc_data.qc.has_value());
        BOOST_TEST(!real_chain_block_13_result.qc_data.qc.has_value());

/*        mutable_variant_object valid_rule_2_proof_2 = prepare_rule_2_gt_proof(  light_client_data.active_finalizer_policy, 
                                                                    light_client_data.high_qc_block, 
                                                                    light_client_data.high_qc, 
                                                                    real_chain_block_9_result, 
                                                                    real_chain_block_10_result.qc_data.qc.value());*/


    } FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
