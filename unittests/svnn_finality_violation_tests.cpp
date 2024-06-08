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

   void print_policy(finalizer_policy policy){
      std::cout << "\n finalizer policy : " << policy.generation << " " << fc::sha256::hash(policy) << "\n";
      std::cout << "  threshold : " << policy.threshold << "\n\n";
      for (auto a : policy.finalizers){
         std::cout << a.public_key.to_string() << "\n";
      }

   }

   void print_proof_of_finality(finality_proof::proof_of_finality pf, finality_proof::proof_test_cluster<4>& chain){

      std::cout << "  proof of finality for block : " << pf.qc_chain[0].block->block_num() << " (IF index : " << pf.qc_chain[0].block->block_num() - chain.genesis_block_num << ")\n";
      std::cout << "  with finality_digest : " << pf.qc_chain[0].finality_digest << "\n";
      std::cout << "  signed by policy : " << pf.qc_chain[0].qc_policy << "\n";
      std::cout << "  qc over block : " << pf.qc_chain[2].block->block_num() << " (IF index : : " << pf.qc_chain[2].block->block_num() - chain.genesis_block_num << ")\n";
      std::cout << "  qc present in block : " << pf.qc_chain[3].block->block_num() << " (IF index : : " << pf.qc_chain[3].block->block_num() - chain.genesis_block_num << ")\n";

      if (pf.qc_chain[0].finality_data.tombstone_finalizer_policy_digest.empty()){
         std::cout   << "  block containing the last recorded QC " << pf.qc_chain[3].block->block_num() << " (IF index : " 
                     << pf.qc_chain[3].block->block_num() - chain.genesis_block_num << ")\n";
      }
      else {
         std::cout   << "  block containing the QC which finalizes the previous policy tombstone moment : " << pf.qc_chain[3].block->block_num() 
                     << " (IF index : " << pf.qc_chain[3].block->block_num() - chain.genesis_block_num << ")"
                     << " ,tombstone for policy : " << pf.qc_chain[0].finality_data.tombstone_finalizer_policy_digest << "\n";
      }

      if (pf.is_proof_of_finality_for_genesis_policy) std::cout << "  IF Genesis policy proof of finality\n";

      std::cout << "\n";

   }

   void print_blocks_comparison(const finality_proof::finality_block_data_t fake_chain_finality_block_data, const finality_proof::finality_block_data_t real_chain_finality_block_data, const uint32_t genesis_block_num){

      assert(fake_chain_finality_block_data.block->block_num() == real_chain_finality_block_data.block->block_num() );

      std::cout << "\n*** Block " << fake_chain_finality_block_data.block->block_num() << " (IF index : " 
                << fake_chain_finality_block_data.block->block_num() - genesis_block_num << ") ***" << "\n";

      std::cout << "  Fake Chain : " << fake_chain_finality_block_data.last_proposed_finalizer_policy.generation << 
                                   " " << fake_chain_finality_block_data.last_pending_finalizer_policy.generation << 
                                   " " << fake_chain_finality_block_data.active_finalizer_policy.generation << "\n";
      std::cout << "    Prop -> Pend -> Act :" << fc::sha256::hash(fake_chain_finality_block_data.last_proposed_finalizer_policy) << 
                                          "->" << fc::sha256::hash(fake_chain_finality_block_data.last_pending_finalizer_policy) << 
                                          "->" << fc::sha256::hash(fake_chain_finality_block_data.active_finalizer_policy) << "\n";
      std::cout << "    Finality Digest : " << fake_chain_finality_block_data.finality_digest << "\n";
      std::cout << "    QC Signed by : " << fake_chain_finality_block_data.qc_policy << "\n";
      std::cout << "    Tombstone Policy Digest : " << fake_chain_finality_block_data.finality_data.tombstone_finalizer_policy_digest << "\n";

      std::cout << "  Real Chain : " << real_chain_finality_block_data.last_proposed_finalizer_policy.generation << 
                                   " " << real_chain_finality_block_data.last_pending_finalizer_policy.generation << 
                                   " " << real_chain_finality_block_data.active_finalizer_policy.generation << "\n";
      std::cout << "    Prop -> Pend -> Act :" << fc::sha256::hash(real_chain_finality_block_data.last_proposed_finalizer_policy) << 
                                          "->" << fc::sha256::hash(real_chain_finality_block_data.last_pending_finalizer_policy) << 
                                          "->" << fc::sha256::hash(real_chain_finality_block_data.active_finalizer_policy) << "\n";
      std::cout << "    Finality Digest : " << real_chain_finality_block_data.finality_digest << "\n";
      std::cout << "    QC Signed by : " << real_chain_finality_block_data.qc_policy << "\n";
      std::cout << "    Tombstone Policy Digest : " << real_chain_finality_block_data.finality_data.tombstone_finalizer_policy_digest << "\n";

   }

   void perform_test(const uint32_t blocks_to_produce, 
      std::map<uint32_t, char> fake_chain_policy_changes, 
      std::map<uint32_t, char> real_chain_policy_changes, 
      std::vector<uint32_t> fake_chain_transfers, 
      std::vector<uint32_t> real_chain_transfers){

      assert(blocks_to_produce>0);

      // setup the fake chain. node3 doesn't receive votes on the fake chain
      finality_proof::proof_test_cluster<4> fake_chain;
      fake_chain.vote_propagation = {1, 1, 0};
      fake_chain.bitset = "07";

      fake_chain.node0.create_accounts( { "user1"_n, "user2"_n, "violation"_n, "eosio.token"_n } );

      fake_chain.node0.set_code( "eosio.token"_n, test_contracts::eosio_token_wasm() );
      fake_chain.node0.set_abi( "eosio.token"_n, test_contracts::eosio_token_abi() );

      fake_chain.node0.set_code( "violation"_n, test_contracts::finality_violation_wasm() );
      fake_chain.node0.set_abi( "violation"_n, test_contracts::finality_violation_abi() );

      // setup the real chain. node2 doesn't receive votes on the real chain
      finality_proof::proof_test_cluster<4> real_chain;
      real_chain.vote_propagation = {1, 0, 1};
      real_chain.bitset = "0b";

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
         ("quantity", "100.0000 EOS")
         ("memo", "");

      fake_chain.node0.push_action("eosio.token"_n, "create"_n, "eosio.token"_n, create_action);
      fake_chain.node0.push_action("eosio.token"_n, "issue"_n, "eosio"_n, issue_action);
      fake_chain.node0.push_action("eosio.token"_n, "transfer"_n, "eosio"_n, initial_transfer);

      real_chain.node0.push_action("eosio.token"_n, "create"_n, "eosio.token"_n, create_action);
      real_chain.node0.push_action("eosio.token"_n, "issue"_n, "eosio"_n, issue_action);
      real_chain.node0.push_action("eosio.token"_n, "transfer"_n, "eosio"_n, initial_transfer);

      // prepare a generic transfer action
      mutable_variant_object transfer_act = mvo()
         ("from", "user1"_n)
         ("to", "user2"_n)
         ("quantity", "1.0000 EOS")
         ("memo", "");

      //define policy identifiers
      std::vector<char> policies_identifiers = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J'};

      std::map<char, std::array<size_t, 4>> policies_indices;

      uint32_t count = 0 ;

      //prepare policy indices for setfinalizer calls
      for (auto& pi : policies_identifiers){
         policies_indices[pi] = fake_chain.fin_policy_indices_0;
         policies_indices[pi][0] = count;
         policies_indices[pi][1] = 10 + count;
         count++;
      }

      // produce the IF genesis block on both chains
      auto fake_genesis_block_result = fake_chain.produce_block();
      auto real_genesis_block_result = real_chain.produce_block();

      BOOST_TEST(fake_chain.genesis_block_num == real_chain.genesis_block_num);

      // verify that the same finalizer policy is active on both chains
      BOOST_TEST(fc::sha256::hash(fake_chain.active_finalizer_policy) == fc::sha256::hash(real_chain.active_finalizer_policy));

      std::cout << "Genesis finalizer policy digest : " << fc::sha256::hash(fake_chain.active_finalizer_policy) << "\n";

      std::vector<finality_proof::finality_block_data_t> fake_block_results;
      std::vector<finality_proof::finality_block_data_t> real_block_results;

      for (uint32_t i = 1 ; i < blocks_to_produce; i++){
         auto f_pc_itr = fake_chain_policy_changes.find(i);
         auto f_t_itr = std::find(fake_chain_transfers.begin(), fake_chain_transfers.end(), i);
         if (f_pc_itr!=fake_chain_policy_changes.end()) fake_chain.node0.finkeys.set_finalizer_policy(policies_indices[f_pc_itr->second]);
         if (f_t_itr!=fake_chain_transfers.end()) fake_chain.node0.push_action("eosio.token"_n, "transfer"_n, "user1"_n, transfer_act);

         fake_block_results.push_back(fake_chain.produce_block());

         auto r_pc_itr = real_chain_policy_changes.find(i);
         auto r_t_itr = std::find(real_chain_transfers.begin(), real_chain_transfers.end(), i);;
         if (r_pc_itr!=real_chain_policy_changes.end()) real_chain.node0.finkeys.set_finalizer_policy(policies_indices[r_pc_itr->second]);
         if (r_t_itr!=real_chain_transfers.end()) real_chain.node0.push_action("eosio.token"_n, "transfer"_n, "user1"_n, transfer_act);

         real_block_results.push_back(real_chain.produce_block());
      }

      std::cout << "\nfake chain -> proofs_of_finality count : " << fake_chain.proofs_of_finality.size() << "\n\n";
      for (size_t i = 0 ; i < fake_chain.proofs_of_finality.size() ; i++){
         print_proof_of_finality(fake_chain.proofs_of_finality[i], fake_chain);
      }

      std::cout << "\nreal chain -> proofs_of_finality count : " << real_chain.proofs_of_finality.size() << "\n\n";
      for (size_t i = 0 ; i < real_chain.proofs_of_finality.size() ; i++){
         print_proof_of_finality(real_chain.proofs_of_finality[i], real_chain);
      }
 
      auto f_itr = fake_chain.proofs_of_finality.rbegin();

      auto r_common_policy = real_chain.proofs_of_finality.end();
      auto f_common_policy = fake_chain.proofs_of_finality.rend();

      auto r_next_policy = real_chain.proofs_of_finality.end();
      auto f_next_policy = fake_chain.proofs_of_finality.end();

      // Going back through the fake chain's finalizer policies it has recorded, as well as going back through the history of the real chain, user1 
      // can trivially discover the last finalizer policy common to both chains.
      while (f_itr!=fake_chain.proofs_of_finality.rend()){
         auto r_itr = std::find_if(real_chain.proofs_of_finality.begin(), real_chain.proofs_of_finality.end(), 
            //find the last common policy (same hash + same finality_digest for the final_on_strong_qc block)
            [&](const auto& p){
               return   (!p.qc_chain[0].finality_data.tombstone_finalizer_policy_digest.empty() || p.is_proof_of_finality_for_genesis_policy)
                        && p.qc_chain[0].finality_data.tombstone_finalizer_policy_digest == f_itr->qc_chain[0].finality_data.tombstone_finalizer_policy_digest
                        && p.qc_chain[0].finality_digest == f_itr->qc_chain[0].finality_digest;
            });

         if (r_itr!=real_chain.proofs_of_finality.end()){
            // In this example, the last common finalizer policy is #1
            //BOOST_TEST(r_itr->qc_chain[0].qc_policy == fc::sha256::hash(fake_genesis_block_result.active_finalizer_policy));
            r_common_policy = r_itr;
            f_common_policy = f_itr;
            r_next_policy = ++r_itr;
            f_next_policy = f_itr.base();
            break;
         }
         f_itr++;
      }

      bool found_common = r_common_policy!=real_chain.proofs_of_finality.end();
      BOOST_REQUIRE(found_common);

      auto using_r_policy = r_next_policy == real_chain.proofs_of_finality.end() ? r_common_policy : r_next_policy;
      auto using_f_policy = f_next_policy == fake_chain.proofs_of_finality.end() ? --f_common_policy.base() : f_next_policy;

      std::cout << "found common policy\n";
      std::cout << "common policy is : " << r_common_policy->qc_chain[0].qc_policy << "\n";
      std::cout << "\n";

      if(using_r_policy->qc_chain[2].finality_digest==using_f_policy->qc_chain[2].finality_digest ) std::cout << "\nNo finality violation detected\n";
      else {
         std::cout << "Finality violation detected\n\n";
         std::cout << "Conflicting blocks signed by " << r_common_policy->qc_chain[0].qc_policy << " were both made final : \n\n";
         std::cout << "  fake chain : QC in -> block_num " << using_f_policy->qc_chain[3].block->block_num() 
                << " (IF index : " << using_f_policy->qc_chain[3].block->block_num() - fake_chain.genesis_block_num << ")"
                << " over block : " << using_f_policy->qc_chain[2].block->block_num()
                << " (IF index : " << using_f_policy->qc_chain[2].block->block_num() - fake_chain.genesis_block_num << ")"
                << " making target block final : " << using_f_policy->qc_chain[0].block->block_num()
                << " (IF index : " << using_f_policy->qc_chain[0].block->block_num() - fake_chain.genesis_block_num << ")"
                << "  -> target finality digest : " << using_f_policy->qc_chain[0].finality_digest << "\n";
         std::cout << "  real chain : QC in -> block_num " << using_r_policy->qc_chain[3].block->block_num() 
                << " (IF index : " << using_r_policy->qc_chain[3].block->block_num() - real_chain.genesis_block_num << ")"
                << " over block : " << using_r_policy->qc_chain[2].block->block_num() 
                << " (IF index : " << using_r_policy->qc_chain[2].block->block_num() - real_chain.genesis_block_num << ")"
                << " making target block final : " << using_r_policy->qc_chain[0].block->block_num() 
                << " (IF index : " << using_r_policy->qc_chain[0].block->block_num() - real_chain.genesis_block_num << ")"
                << "  -> target finality digest : " << using_r_policy->qc_chain[0].finality_digest << "\n";
      }

/*      for (uint32_t i = 0 ; i < blocks_to_produce; i++){
         print_blocks_comparison(fake_block_results[0], real_block_results[0], fake_chain.genesis_block_num);
      }
*/
   }


   BOOST_AUTO_TEST_CASE(two_chains_test) { try {

      perform_test(25, 
         {{11, 'B'},{12, 'C'},{13, 'D'},{14, 'E'},{15, 'F'},{16, 'G'}},
         {{11, 'B'}},
         {},
         {});

   } FC_LOG_AND_RETHROW() }


BOOST_AUTO_TEST_SUITE_END()
