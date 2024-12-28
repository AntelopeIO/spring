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

struct finality_block_data {

    uint32_t block_num{0};
    block_timestamp_type timestamp{};
    block_timestamp_type parent_timestamp{};
    digest_type finality_digest{};
    block_timestamp_type last_pending_finalizer_policy_start_timestamp{};
    level_3_commitments_t level_3_commitments{};
    digest_type base_digest{};
    digest_type finality_root{};
    qc_data_t qc_data{};

};

finality_block_data get_finality_block_data(const ibc_block_data_t& block_result){

    return finality_block_data {
        .block_num = block_result.block->block_num(),
        .timestamp = block_result.block->timestamp,
        .parent_timestamp = block_result.parent_timestamp,
        .finality_digest = block_result.finality_digest,
        .last_pending_finalizer_policy_start_timestamp = block_result.last_pending_finalizer_policy_start_timestamp,
        .level_3_commitments = block_result.level_3_commitments,
        .base_digest = block_result.base_digest,
        .finality_root = block_result.finality_root,
        .qc_data = block_result.qc_data
    };
}


void shouldPass(const finality_proof::proof_test_cluster& chain, const account_name& rule, const mvo& proof){

    action_trace trace =  chain.node0.push_action("violation"_n, rule, "violation"_n, proof)->action_traces[0];

    std::pair<std::string, std::string> blame = fc::raw::unpack<std::pair<std::string, std::string>>(trace.return_value);

    //finalizers 0 and 1 are guilty, while finalizer 2 and 3 are innocent, see bitset tests in svnn_ibc_tests
    BOOST_TEST(blame.first == "03"); //0011
    BOOST_TEST(blame.second == "0c"); //1100

}

bool shouldFail(const finality_proof::proof_test_cluster& chain, const account_name& rule, const mvo& proof){

    bool last_action_failed = false;
    try {
        chain.node0.push_action("violation"_n, rule, "violation"_n, proof);
    }
    catch (const eosio_assert_message_exception& e){
        last_action_failed = true;
    }

    return last_action_failed;
    
}

digest_type compute_block_ref_digest(const ibc_block_data_t& b){

    block_ref_digest_data data = {
       .block_num        = b.block->block_num(),
       .timestamp        = b.block->timestamp,
       .finality_digest  = b.finality_digest,
       .parent_timestamp = b.parent_timestamp
    };

    digest_type digest = fc::sha256::hash(data);

    return digest;

}

digest_type compute_block_ref_digest(const finality_block_data& b){

    block_ref_digest_data data = {
       .block_num        = b.block_num,
       .timestamp        = b.timestamp,
       .finality_digest  = b.finality_digest,
       .parent_timestamp = b.parent_timestamp
    };

    digest_type digest = fc::sha256::hash(data);

    return digest;

}

std::vector<digest_type> get_reversible_blocks_digests(const std::vector<finality_block_data>& blocks){

    std::vector<digest_type> block_ref_digests;

    for (int i = 0 ; i < blocks.size(); i++){

        finality_block_data b = blocks[i];

        digest_type digest = compute_block_ref_digest(b);

        block_ref_digests.push_back(digest);

    }

    return block_ref_digests;

}

digest_type calculate_reversible_blocks_merkle(const std::vector<finality_block_data>& blocks){

    std::vector<digest_type> block_ref_digests = get_reversible_blocks_digests(blocks);

    return calculate_merkle(block_ref_digests);

}

        
std::pair<finality_block_data, size_t> get_target_reversible_block(const std::vector<finality_block_data>& reversible_blocks, const uint32_t& block_num){

    auto itr = std::find_if(reversible_blocks.begin(), reversible_blocks.end(), [&](const finality_block_data& b){
        return b.block_num == block_num;
    });

    size_t index = std::distance(reversible_blocks.begin(), itr);
    
    return std::make_pair(*itr, index);

}

BOOST_AUTO_TEST_SUITE(svnn_finality_violation)

    mvo prepare_rule_1_proof(  const finalizer_policy& active_finalizer_policy, 
                        const finality_block_data& fake_qc_block, 
                        const qc_t& fake_qc, 
                        const finality_block_data& real_qc_block, 
                        const qc_t& real_qc){

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
                ("strong_votes", finality_proof::finalizers_string(fake_qc.active_policy_sig.strong_votes.value())) 
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
                ("strong_votes", finality_proof::finalizers_string(real_qc.active_policy_sig.strong_votes.value())) 
            )
            );

    }

    mvo prepare_rule_2_3_proof(  const finalizer_policy& active_finalizer_policy, 
                        const finality_block_data& high_qc_block, 
                        const qc_t& high_qc, 
                        const finality_block_data& low_qc_block, 
                        const qc_t& low_qc, 
                        const finality_block_data& target_reversible_block, 
                        const size_t target_reversible_block_index, 
                        const std::vector<finality_block_data>& reversible_blocks){

        std::vector<digest_type> merkle_branches = finality_proof::generate_proof_of_inclusion(get_reversible_blocks_digests(reversible_blocks), target_reversible_block_index);

        return mvo()
            ("finalizer_policy", active_finalizer_policy)
            ("high_proof", mvo() 
            ("qc_block", mvo()
                ("major_version", 1)
                ("minor_version", 0)
                ("active_finalizer_policy_generation", 1)
                ("pending_finalizer_policy_generation", 1)
                ("last_pending_finalizer_policy_start_timestamp", high_qc_block.last_pending_finalizer_policy_start_timestamp)
                ("last_pending_finalizer_policy", active_finalizer_policy)
                ("level_3_commitments", high_qc_block.level_3_commitments)
                ("witness_hash", high_qc_block.base_digest)
                ("finality_mroot", high_qc_block.finality_root)
            )
            ("active_policy_qc", mvo()
                ("signature", high_qc.active_policy_sig.sig.to_string())
                ("strong_votes", finality_proof::finalizers_string(high_qc.active_policy_sig.strong_votes.value())) 
            )
            )
            ("low_proof", mvo()
            ("qc_block", mvo()
                ("major_version", 1)
                ("minor_version", 0)
                ("active_finalizer_policy_generation", 1)
                ("pending_finalizer_policy_generation", 1)
                ("last_pending_finalizer_policy_start_timestamp", low_qc_block.last_pending_finalizer_policy_start_timestamp)
                ("last_pending_finalizer_policy", active_finalizer_policy)
                ("level_3_commitments", low_qc_block.level_3_commitments)
                ("witness_hash", low_qc_block.base_digest)
                ("finality_mroot", low_qc_block.finality_root)
            )
            ("active_policy_qc", mvo()
                ("signature", low_qc.active_policy_sig.sig.to_string())
                ("strong_votes", finality_proof::finalizers_string(low_qc.active_policy_sig.strong_votes.value())) 
            )
            )
            ("reversible_proof_of_inclusion", mvo()
                ("target_reversible_block_index", target_reversible_block_index)
                ("final_reversible_block_index", reversible_blocks.size() - 1)
                ("target", mvo()
                    ("block_num", target_reversible_block.block_num)
                    ("timestamp", target_reversible_block.timestamp)
                    ("finality_digest", target_reversible_block.finality_digest)
                    ("parent_timestamp", target_reversible_block.parent_timestamp)
                )
                ("merkle_branches", merkle_branches)
            );

    }

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
        struct data_cache {

            //The data_cache stores data relevant to finality violation proofs.
            //It only operates in optimistic mode, which is sufficient for finality violation proofs testing purposes

            //store the last block over which we have a QC, as well as said QC
            finality_block_data high_qc_block;
            qc_t high_qc;

            //store the last final block, as wall as the first QC over it. The last_final_qc and high_qc together constitute the 2-chains required for finality progress
            finality_block_data last_final_block;
            qc_t last_final_qc;

            //current active finalizer policy
            finalizer_policy active_finalizer_policy;

            //observe a stream of blocks as they are received, and store minimal data required to construct finality violation proofs in the future
            ibc_block_data_t scan_block(const ibc_block_data_t& block_result){

                finality_block_data block_data{
                    .block_num = block_result.block->block_num(),
                    .timestamp = block_result.block->timestamp,
                    .parent_timestamp = block_result.parent_timestamp,
                    .finality_digest = block_result.finality_digest,
                    .last_pending_finalizer_policy_start_timestamp = block_result.last_pending_finalizer_policy_start_timestamp,
                    .level_3_commitments = block_result.level_3_commitments,
                    .base_digest = block_result.base_digest,
                    .finality_root = block_result.finality_root,
                    .qc_data = block_result.qc_data
                };

                if (block_result.qc_data.qc.has_value()) {

                    auto high_qc_block_itr = std::lower_bound(
                        reversible_blocks.begin(), 
                        reversible_blocks.end(), 
                        block_result.qc_data.qc.value().to_qc_claim().block_num, 
                        [](const auto& r, const auto& block_num) {
                            return r.block_num < block_num;
                        }
                    );

                    if ( high_qc_block_itr != reversible_blocks.end()
                         && high_qc_block_itr->qc_data.qc.has_value()) {

                        auto last_final_block_itr = std::lower_bound(
                            reversible_blocks.begin(), 
                            reversible_blocks.end(), 
                            high_qc_block_itr->qc_data.qc.value().to_qc_claim().block_num, 
                            [](const auto& r, const auto& block_num) {
                                return r.block_num < block_num;
                            }
                        );
                        
                        if ( last_final_block_itr !=  reversible_blocks.end()
                             && block_result.qc_data.qc.value().to_qc_claim().is_strong_qc) {

                                //new strong QC
                                last_final_block = *last_final_block_itr;
                                last_final_qc = high_qc_block_itr->qc_data.qc.value();

                                reversible_blocks.erase(reversible_blocks.begin(), ++last_final_block_itr);

                        }

                        high_qc_block = *high_qc_block_itr;
                        high_qc = block_result.qc_data.qc.value();

                    }

                }

                reversible_blocks.push_back(block_data);

                active_finalizer_policy = block_result.active_finalizer_policy;

                return block_result;

            }

            std::vector<finality_block_data> get_reversible_blocks(){
                std::vector<finality_block_data> result(reversible_blocks.begin(), reversible_blocks.end() - 1);
                return result;
            }

            finality_block_data get_current_block(){
                return reversible_blocks.back();
            }

        private:

            //store the reversible blocks
            std::vector<finality_block_data> reversible_blocks;

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

        BOOST_TEST(light_client_data.get_reversible_blocks().size() == 1u);
        
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
                                                                    get_finality_block_data(real_chain_block_3_result), 
                                                                    get_finality_block_data(real_chain_block_4_result).qc_data.qc.value()); //same finality digest, not a violation proof

        BOOST_CHECK(shouldFail(real_chain, "rule1"_n, invalid_rule_1_proof_1));

        mutable_variant_object invalid_rule_1_proof_2 = prepare_rule_1_proof(  light_client_data.active_finalizer_policy, 
                                                                    light_client_data.high_qc_block, 
                                                                    light_client_data.high_qc, 
                                                                    get_finality_block_data(real_chain_block_2_result), 
                                                                    get_finality_block_data(real_chain_block_3_result).qc_data.qc.value()); //different timestamps, not a violation proof

        BOOST_CHECK(shouldFail(real_chain, "rule1"_n, invalid_rule_1_proof_2));

        //create a fork by pushing a transaction on the fake chain
        fake_chain.node0.push_action("eosio.token"_n, "transfer"_n, "user1"_n, user1_transfer);

        auto fake_chain_block_5_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_5_result = real_chain.produce_block();

        BOOST_TEST(light_client_data.get_reversible_blocks().size()  == 1u);
        
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
                                                                    get_finality_block_data(real_chain_block_7_result), 
                                                                    get_finality_block_data(real_chain_block_8_result).qc_data.qc.value());

        shouldPass(real_chain, "rule1"_n, valid_rule_1_proof);

        //we temporarily disable a finalizer on the fake chain, which allow us to set up a proof of violation of rule #2
        fake_chain.vote_propagation = {1,0,0};

        //produce a block on a fake chain without propagating votes to all nodes
        auto fake_chain_block_9_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_9_result = real_chain.produce_block();

        BOOST_TEST(light_client_data.get_reversible_blocks().size()  == 1u);

        //restore vote propagation for fake chain. This leaves a one-block gap where no finality progress was achieved
        fake_chain.vote_propagation = {1,1,0};

        auto fake_chain_block_10_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_10_result = real_chain.produce_block();

        //get the reversible blocks for block #10 (should be two digests, block ref data of #8 and #9)
        std::vector<finality_block_data> block_10_reversible_blocks = light_client_data.get_reversible_blocks();

        BOOST_TEST(light_client_data.get_reversible_blocks().size()  == 2u);
        BOOST_TEST(block_10_reversible_blocks.size() == 2u);

        BOOST_TEST(compute_block_ref_digest(block_10_reversible_blocks[0]) == compute_block_ref_digest(fake_chain_block_8_result));
        BOOST_TEST(compute_block_ref_digest(block_10_reversible_blocks[1]) == compute_block_ref_digest(fake_chain_block_9_result));

        //calculate the merkle root of the reversible blocks digests
        digest_type block_10_reversible_blocks_merkle_root = calculate_reversible_blocks_merkle(block_10_reversible_blocks);

        //verify the merkle root of the reversible blocks digests is the same as the one recorded by the light client
        BOOST_TEST(fake_chain_block_10_result.level_3_commitments.reversible_blocks_mroot == block_10_reversible_blocks_merkle_root);

        auto valid_proof_1_target_reversible_block = get_target_reversible_block(light_client_data.get_reversible_blocks(), real_chain_block_9_result.block->block_num());

        //Real chain has a QC on #9 carried by #10, but fake chain doesn't
        BOOST_TEST(!fake_chain_block_10_result.qc_data.qc.has_value());
        BOOST_TEST(real_chain_block_10_result.qc_data.qc.has_value());
        
        auto fake_chain_block_11_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_11_result = real_chain.produce_block();

        //Things are back to normal, and we have a QC on both chains
        BOOST_TEST(fake_chain_block_11_result.qc_data.qc.has_value());
        BOOST_TEST(real_chain_block_11_result.qc_data.qc.has_value());

        BOOST_TEST(light_client_data.get_reversible_blocks().size() == 3u);

        //Light client recorded the last QC (over block #10) on the fake chain, which was delivered via block #11. 
        //Block #10 claims a QC over block #8. We provide fake block #10 and the QC over it, as well as the digests of the reversible blocks the light client recorded (block #8 and block #9).
        //We also provide the real block #9 and a QC over it, delivered via block #10.
        //Since there is a time range conflict, and since the low proof block is not a descendant of the high proof block, this is a proof of violation of rule #2.
        mutable_variant_object valid_rule_2_proof_1 = prepare_rule_2_3_proof(  light_client_data.active_finalizer_policy, 
                                                                    light_client_data.get_reversible_blocks()[light_client_data.get_reversible_blocks().size()-1], //fake block #10
                                                                    light_client_data.get_current_block().qc_data.qc.value(), //QC over fake block #10
                                                                    get_finality_block_data(real_chain_block_9_result), 
                                                                    get_finality_block_data(real_chain_block_10_result).qc_data.qc.value(),
                                                                    valid_proof_1_target_reversible_block.first,
                                                                    valid_proof_1_target_reversible_block.second,
                                                                    block_10_reversible_blocks);
                                                                    

        //The contract correctly accepts the valid proof, as it is a proof of violation of rule #2
        shouldPass(real_chain, "rule2"_n, valid_rule_2_proof_1);

        //Now, to ensure the smart contract rejects invalid proofs, we can test providing a proof from the real chain where the high proof block is a descendant of the low proof block
        mutable_variant_object invalid_rule_2_proof_1 = prepare_rule_2_3_proof(  light_client_data.active_finalizer_policy, 
                                                                    get_finality_block_data(real_chain_block_10_result), //real block #10
                                                                    get_finality_block_data(real_chain_block_11_result).qc_data.qc.value(), //QC over real block #11
                                                                    get_finality_block_data(real_chain_block_9_result), 
                                                                    get_finality_block_data(real_chain_block_10_result).qc_data.qc.value(),
                                                                    get_finality_block_data(real_chain_block_9_result),
                                                                    0,
                                                                    {get_finality_block_data(real_chain_block_9_result)});

        //The contract rejects the invalid proof
        shouldFail(real_chain, "rule2"_n, invalid_rule_2_proof_1);

        //Now, to ensure the smart contract rejects invalid proofs, we can test providing a proof from the real chain where the high proof block is a descendant of the 
        //low proof block
        mutable_variant_object invalid_rule_2_proof_2 = prepare_rule_2_3_proof(  light_client_data.active_finalizer_policy, 
                                                                    get_finality_block_data(real_chain_block_10_result), //real block #10
                                                                    get_finality_block_data(real_chain_block_11_result).qc_data.qc.value(), //QC over real block #11
                                                                    get_finality_block_data(real_chain_block_9_result), 
                                                                    get_finality_block_data(real_chain_block_10_result).qc_data.qc.value(),
                                                                    valid_proof_1_target_reversible_block.first,
                                                                    valid_proof_1_target_reversible_block.second,
                                                                    block_10_reversible_blocks);

        //The contract rejects the invalid proof
        shouldFail(real_chain, "rule2"_n, invalid_rule_2_proof_2);

        auto fake_chain_block_12_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_12_result = real_chain.produce_block();

        BOOST_TEST(fake_chain_block_12_result.qc_data.qc.has_value());
        BOOST_TEST(real_chain_block_12_result.qc_data.qc.has_value());

        BOOST_TEST(light_client_data.get_reversible_blocks().size()  == 1u);
        
        //we can now test the other possibility for rule #2, where the high proof is actually from the real chain

        //produce a block on the real chain without propagating votes to all nodes
        real_chain.vote_propagation = {1,0,0};

        auto fake_chain_block_13_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_13_result = real_chain.produce_block();

        BOOST_TEST(light_client_data.get_reversible_blocks().size()  == 1u);

        //restore vote propagation on the real chain
        real_chain.vote_propagation = {1,0,1};

        auto fake_chain_block_14_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_14_result = real_chain.produce_block();

        BOOST_TEST(light_client_data.get_reversible_blocks().size()  == 1u);

        //compute reversible digests on block #12 and #13 on the real chain
        std::vector<finality_block_data> block_14_reversible_blocks = { get_finality_block_data(real_chain_block_12_result), 
                                                                        get_finality_block_data(real_chain_block_13_result)};

        //calculate the merkle root of the reversible blocks digests
        digest_type block_14_reversible_blocks_merkle_root = calculate_reversible_blocks_merkle(block_14_reversible_blocks);

        //verify the merkle root of the reversible blocks digests is the same as the one recorded by the light client
        BOOST_TEST(real_chain_block_14_result.level_3_commitments.reversible_blocks_mroot == block_14_reversible_blocks_merkle_root);

        //Fake chain has a QC on #13 carried by #14, but real chain doesn't
        BOOST_TEST(fake_chain_block_14_result.qc_data.qc.has_value());
        BOOST_TEST(!real_chain_block_14_result.qc_data.qc.has_value());

        //fake chain stops producing, but real chain continues
        auto real_chain_block_15_result = real_chain.produce_block();

        //Things are back to normal on the real chain, and we have a QC 
        BOOST_TEST(real_chain_block_15_result.qc_data.qc.has_value());
        
        auto valid_proof_2_target_reversible_block = get_target_reversible_block(block_14_reversible_blocks, block_14_reversible_blocks[block_14_reversible_blocks.size()-1].block_num);

        //We discovered a QC (over block #14) on the real chain, which was delivered via block #15. 
        //Last QC recorded on the fake chain was over block #13, and was delivered by block #14
        //We provide the real block #14 and its QC, the reversible digests at that time (block #12 and #13), as well as the fake block #13 and its QC.
        //Since there is a time range conflict, and the low proof block is not a descendant of the high proof block, this is a proof of violation of rule #2.
        mutable_variant_object valid_rule_2_proof_2 = prepare_rule_2_3_proof(  light_client_data.active_finalizer_policy, 
                                                                    get_finality_block_data(real_chain_block_14_result), 
                                                                    get_finality_block_data(real_chain_block_15_result).qc_data.qc.value(), 
                                                                    light_client_data.get_reversible_blocks()[light_client_data.get_reversible_blocks().size()-1], //fake block #13
                                                                    light_client_data.get_current_block().qc_data.qc.value(), //QC over fake block #13
                                                                    valid_proof_2_target_reversible_block.first,
                                                                    valid_proof_2_target_reversible_block.second,
                                                                    block_14_reversible_blocks);

        shouldPass(real_chain, "rule2"_n, valid_rule_2_proof_2);

        //Fake chain resume production, catches up with real chain
        auto fake_chain_block_15_result = light_client_data.scan_block(fake_chain.produce_block());
        
        //Caught up
        auto fake_chain_block_16_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_16_result = real_chain.produce_block();

        BOOST_TEST(fake_chain_block_16_result.qc_data.qc.has_value());
        BOOST_TEST(real_chain_block_16_result.qc_data.qc.has_value());

        //We once again disable vote propagation on the real chain
        real_chain.vote_propagation = {1,0,0};

        //Produce a few more blocks on both chains
        auto fake_chain_block_17_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_17_result = real_chain.produce_block();

        BOOST_TEST(fake_chain_block_17_result.qc_data.qc.has_value());
        BOOST_TEST(real_chain_block_17_result.qc_data.qc.has_value());

        auto fake_chain_block_18_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_18_result = real_chain.produce_block();

        BOOST_TEST(fake_chain_block_18_result.qc_data.qc.has_value());
        BOOST_TEST(!real_chain_block_18_result.qc_data.qc.has_value());

        //restore vote propagation on the real chain
        real_chain.vote_propagation = {1,0,1};

        auto fake_chain_block_19_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_19_result = real_chain.produce_block();

        //compute reversible digests on block #16, #17 and #18 on the real chain
        std::vector<finality_block_data> block_19_reversible_blocks = { get_finality_block_data(real_chain_block_16_result), get_finality_block_data(real_chain_block_17_result), get_finality_block_data(real_chain_block_18_result)};
  
        //calculate the merkle root of the reversible blocks digests
        digest_type block_19_reversible_blocks_merkle_root = calculate_reversible_blocks_merkle(block_19_reversible_blocks);

        //verify the merkle root of the reversible blocks digests is the same as the one recorded by the light client
        BOOST_TEST(real_chain_block_19_result.level_3_commitments.reversible_blocks_mroot == block_19_reversible_blocks_merkle_root);

        BOOST_TEST(fake_chain_block_19_result.qc_data.qc.has_value());
        BOOST_TEST(!real_chain_block_19_result.qc_data.qc.has_value());

        //At this point, we can verify that the time range of the last fake chain block on which we have a QC is fully contained within the time range of the last real chain block
        BOOST_TEST(fake_chain_block_18_result.level_3_commitments.latest_qc_claim_block_num == fake_chain_block_17_result.block->block_num());
        BOOST_TEST(real_chain_block_19_result.level_3_commitments.latest_qc_claim_block_num == real_chain_block_16_result.block->block_num());
        
        auto valid_proof_3_target_reversible_block = get_target_reversible_block(block_19_reversible_blocks, fake_chain_block_17_result.block->block_num());

        //stop producing on fake chain, produce one more block on the real chain to get a QC on previous block
        auto real_chain_block_20_result = real_chain.produce_block();

        BOOST_TEST(real_chain_block_20_result.qc_data.qc.has_value());

        //We can now produce a proof of finality violation demonstrating that finalizers were locked on #18 on the fake chain, while also voting on a conflicting block #19 
        //on the real chain which is not a descendant of #18, where the time range committed to by #18 is fully contained within the time range committed to by #19
        mutable_variant_object valid_rule_3_proof_1 = prepare_rule_2_3_proof(  light_client_data.active_finalizer_policy, 
                                                                    get_finality_block_data(real_chain_block_19_result), 
                                                                    get_finality_block_data(real_chain_block_20_result).qc_data.qc.value(), 
                                                                    light_client_data.get_reversible_blocks()[light_client_data.get_reversible_blocks().size()-1], //fake block #18
                                                                    light_client_data.get_current_block().qc_data.qc.value(), //QC over fake block #18
                                                                    valid_proof_3_target_reversible_block.first,
                                                                    valid_proof_3_target_reversible_block.second,
                                                                    block_19_reversible_blocks);

        shouldPass(real_chain, "rule3"_n, valid_rule_3_proof_1);
        
        //Now, to ensure the smart contract rejects invalid proofs, we can test providing a proof from the real chain where the high proof block is a descendant of the 
        //low proof block
        mutable_variant_object invalid_rule_3_proof_1 = prepare_rule_2_3_proof(  light_client_data.active_finalizer_policy, 
                                                                    get_finality_block_data(real_chain_block_19_result), 
                                                                    get_finality_block_data(real_chain_block_20_result).qc_data.qc.value(), 
                                                                    get_finality_block_data(real_chain_block_16_result), //real block #16
                                                                    get_finality_block_data(real_chain_block_17_result).qc_data.qc.value(), //QC over fake block #16
                                                                    get_finality_block_data(real_chain_block_16_result),
                                                                    0,
                                                                    {get_finality_block_data(real_chain_block_16_result)});

        //The contract rejects the invalid proof
        shouldFail(real_chain, "rule3"_n, invalid_rule_3_proof_1);
    
        //Resume production on fake chain
        auto fake_chain_block_20_result = light_client_data.scan_block(fake_chain.produce_block());
        BOOST_TEST(fake_chain_block_20_result.qc_data.qc.has_value());

        //Caught up
        auto fake_chain_block_21_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_21_result = real_chain.produce_block();

        BOOST_TEST(fake_chain_block_21_result.qc_data.qc.has_value());
        BOOST_TEST(real_chain_block_21_result.qc_data.qc.has_value());

        //We once again disable vote propagation on the fake chain
        fake_chain.vote_propagation = {1,0,0};

        //Produce a few more blocks on both chains
        auto fake_chain_block_22_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_22_result = real_chain.produce_block();

        BOOST_TEST(fake_chain_block_22_result.qc_data.qc.has_value());
        BOOST_TEST(real_chain_block_22_result.qc_data.qc.has_value());

        auto fake_chain_block_23_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_23_result = real_chain.produce_block();

        BOOST_TEST(!fake_chain_block_23_result.qc_data.qc.has_value());
        BOOST_TEST(real_chain_block_23_result.qc_data.qc.has_value());

        //restore vote propagation on the fake chain
        fake_chain.vote_propagation = {1,1,0};

        auto fake_chain_block_24_result = light_client_data.scan_block(fake_chain.produce_block());
        auto real_chain_block_24_result = real_chain.produce_block();

        BOOST_TEST(!fake_chain_block_24_result.qc_data.qc.has_value());
        BOOST_TEST(real_chain_block_24_result.qc_data.qc.has_value());

        //At this point, we can verify that the time range of the last real chain block on which we have a QC is fully encapsulated within the time range of the last fake chain block
        BOOST_TEST(fake_chain_block_24_result.level_3_commitments.latest_qc_claim_block_num == fake_chain_block_21_result.block->block_num());
        BOOST_TEST(real_chain_block_23_result.level_3_commitments.latest_qc_claim_block_num == real_chain_block_22_result.block->block_num());

        //get the reversible blocks for block #24 (should be three blocks: #21, #22 and #23)
        std::vector<finality_block_data> block_24_reversible_blocks = light_client_data.get_reversible_blocks();


        BOOST_TEST(block_24_reversible_blocks.size() == 3u);

        BOOST_TEST(compute_block_ref_digest(block_24_reversible_blocks[0]) == compute_block_ref_digest(fake_chain_block_21_result));
        BOOST_TEST(compute_block_ref_digest(block_24_reversible_blocks[1]) == compute_block_ref_digest(fake_chain_block_22_result));
        BOOST_TEST(compute_block_ref_digest(block_24_reversible_blocks[2]) == compute_block_ref_digest(fake_chain_block_23_result));

        //calculate the merkle root of the reversible blocks digests
        digest_type block_24_reversible_blocks_merkle_root = calculate_reversible_blocks_merkle(block_24_reversible_blocks);

        //verify the merkle root of the reversible blocks digests is the same as the one recorded by the light client
        BOOST_TEST(fake_chain_block_24_result.level_3_commitments.reversible_blocks_mroot == block_24_reversible_blocks_merkle_root);

        //stop producing on real chain, produce one more block on the fake chain to get a QC on previous block
        auto fake_chain_block_25_result = light_client_data.scan_block(fake_chain.produce_block());

        BOOST_TEST(fake_chain_block_25_result.qc_data.qc.has_value());

        auto valid_proof_4_target_reversible_block = get_target_reversible_block(block_24_reversible_blocks, fake_chain_block_22_result.block->block_num());

        //We can now produce a proof of finality violation demonstrating that finalizers were locked on #23 on the real chain, while also voting on a conflicting block #24 
        //on the fake chain which is not a descendant of #23, where the time range committed to by #23 is fully contained within the time range committed to by #24
        mutable_variant_object valid_rule_3_proof_2 = prepare_rule_2_3_proof(  light_client_data.active_finalizer_policy, 
                                                                    light_client_data.get_reversible_blocks()[light_client_data.get_reversible_blocks().size()-1], //fake block #23
                                                                    light_client_data.get_current_block().qc_data.qc.value(), //QC over fake block #23
                                                                    get_finality_block_data(real_chain_block_23_result), 
                                                                    get_finality_block_data(real_chain_block_24_result).qc_data.qc.value(),
                                                                    valid_proof_4_target_reversible_block.first,
                                                                    valid_proof_4_target_reversible_block.second,
                                                                    block_24_reversible_blocks);

        shouldPass(real_chain, "rule3"_n, valid_rule_3_proof_2);


    } FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
