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
      std::cout << "  active finalizer policy is : " << fc::sha256::hash(pf.qc_chain[0].active_finalizer_policy) << "\n";
      std::cout << "  qc over block : " << pf.qc_chain[2].block->block_num() << " (IF index : : " << pf.qc_chain[2].block->block_num() - chain.genesis_block_num << ")\n";
      std::cout << "  qc present in block : " << pf.qc_chain[3].block->block_num() << " (IF index : : " << pf.qc_chain[3].block->block_num() - chain.genesis_block_num << ")\n";

      if (pf.is_proof_of_finality_for_genesis_policy){
         std::cout   << "  block containing the QC that makes IF genesis final " << pf.qc_chain[3].block->block_num() 
                     << " (IF index : "  << pf.qc_chain[3].block->block_num() - chain.genesis_block_num << ")\n";
         std::cout   << "  No tombstone since this is the proof of IF Genesis finality\n";
      }
      else if (!pf.qc_chain[0].finality_data.tombstone_finalizer_policy_digest.empty()){
         std::cout   << "  block containing the QC which finalizes the previous policy tombstone moment : " << pf.qc_chain[3].block->block_num() 
                     << " (IF index : " << pf.qc_chain[3].block->block_num() - chain.genesis_block_num << ")\n";
         std::cout   << "  Tombstone for policy : " << pf.qc_chain[0].finality_data.tombstone_finalizer_policy_digest << "\n";
      }
      else {
         std::cout   << "  block containing the QC that proves the finality of the last known final block " << pf.qc_chain[3].block->block_num() 
                     << " (IF index : "  << pf.qc_chain[3].block->block_num() - chain.genesis_block_num << ")\n";
         std::cout   << "  No tombstone proof included in this block\n";
      }

      std::cout << "\n";

   }

   bool active_finalizer_policy_digest_comparer(const finality_proof::proof_of_finality& result, const digest_type& digest){
      return fc::sha256::hash(result.qc_chain[0].active_finalizer_policy) == digest;
   }

   bool tombstone_comparer(const finality_proof::proof_of_finality& first_result, const finality_proof::proof_of_finality& second_result){
      return first_result.qc_chain[0].finality_data.tombstone_finalizer_policy_digest == second_result.qc_chain[0].finality_data.tombstone_finalizer_policy_digest;
   }

   bool block_num_comparer(const finality_proof::proof_of_finality& first_result, const finality_proof::proof_of_finality& second_result){
      return first_result.qc_chain[0].block->block_num() >= second_result.qc_chain[0].block->block_num();
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
      std::cout << "    QC Signed by : " << fake_chain_finality_block_data.qc_signed_by_policy << "\n";
      std::cout << "    Tombstone Policy Digest : " << fake_chain_finality_block_data.finality_data.tombstone_finalizer_policy_digest << "\n";

      std::cout << "  Real Chain : " << real_chain_finality_block_data.last_proposed_finalizer_policy.generation << 
                                   " " << real_chain_finality_block_data.last_pending_finalizer_policy.generation << 
                                   " " << real_chain_finality_block_data.active_finalizer_policy.generation << "\n";
      std::cout << "    Prop -> Pend -> Act :" << fc::sha256::hash(real_chain_finality_block_data.last_proposed_finalizer_policy) << 
                                          "->" << fc::sha256::hash(real_chain_finality_block_data.last_pending_finalizer_policy) << 
                                          "->" << fc::sha256::hash(real_chain_finality_block_data.active_finalizer_policy) << "\n";
      std::cout << "    Finality Digest : " << real_chain_finality_block_data.finality_digest << "\n";
      std::cout << "    QC Signed by : " << real_chain_finality_block_data.qc_signed_by_policy << "\n";
      std::cout << "    Tombstone Policy Digest : " << real_chain_finality_block_data.finality_data.tombstone_finalizer_policy_digest << "\n";

   }

   struct finality_violation_blame {
      uint32_t generation;
      std::pair<char, digest_type> policy;
   };

   std::optional<finality_violation_blame> perform_test( const uint32_t fake_blocks_to_produce, 
                                                         const uint32_t real_blocks_to_produce, 
                                                         const std::map<uint32_t, char> fake_chain_policy_changes, 
                                                         const std::map<uint32_t, char> real_chain_policy_changes, 
                                                         const std::vector<uint32_t> fake_chain_transfers, 
                                                         const std::vector<uint32_t> real_chain_transfers){

      assert(fake_blocks_to_produce>0);
      assert(real_blocks_to_produce>0);

      // setup the fake chain. node3 doesn't receive votes on the fake chain
      finality_proof::proof_test_cluster<4> fake_chain;
      fake_chain.vote_propagation = {1, 1, 0};
      fake_chain.fully_discoverable = false;
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

      std::map<uint32_t, std::pair<char, digest_type>> real_chain_proposed_policies;

      std::map<char, std::array<size_t, 4>> policies_indices;

      uint32_t count = 0 ;

      //prepare policy indices for setfinalizer calls
      for (auto& pi : policies_identifiers){
         policies_indices[pi] = fake_chain.fin_policy_indices_0;
         policies_indices[pi][0] = count;
         policies_indices[pi][1] = 10 + count;
         count++;
      }

      std::vector<finality_proof::finality_block_data_t> fake_block_results;
      std::vector<finality_proof::finality_block_data_t> real_block_results;

      for (uint32_t i = 0 ; i < fake_blocks_to_produce; i++){

         auto f_pc_itr = fake_chain_policy_changes.find(i);
         auto f_t_itr = std::find(fake_chain_transfers.begin(), fake_chain_transfers.end(), i);
         if (f_pc_itr!=fake_chain_policy_changes.end()) fake_chain.node0.finkeys.set_finalizer_policy(policies_indices[f_pc_itr->second]);
         if (f_t_itr!=fake_chain_transfers.end()) fake_chain.node0.push_action("eosio.token"_n, "transfer"_n, "user1"_n, transfer_act);

         fake_block_results.push_back(fake_chain.produce_block());

         if (f_pc_itr!=fake_chain_policy_changes.end()){
            std::cout   << "Proposed finalizer policy generation : " 
                        << fake_chain.last_proposed_finalizer_policy.generation
                        << " -> " << f_pc_itr->second << " (" << fc::sha256::hash(fake_chain.last_proposed_finalizer_policy) 
                        << ") on fake chain\n";
 
         } 

      }

      for (uint32_t i = 0 ; i < real_blocks_to_produce; i++){

         auto r_pc_itr = real_chain_policy_changes.find(i);
         auto r_t_itr = std::find(real_chain_transfers.begin(), real_chain_transfers.end(), i);;
         if (r_pc_itr!=real_chain_policy_changes.end()) real_chain.node0.finkeys.set_finalizer_policy(policies_indices[r_pc_itr->second]);
         if (r_t_itr!=real_chain_transfers.end()) real_chain.node0.push_action("eosio.token"_n, "transfer"_n, "user1"_n, transfer_act);

         real_block_results.push_back(real_chain.produce_block());

         if (r_pc_itr!=real_chain_policy_changes.end()){
            std::cout   << "Proposed finalizer policy generation : " 
                        << real_chain.last_proposed_finalizer_policy.generation
                        << " -> " << r_pc_itr->second << " (" << fc::sha256::hash(real_chain.last_proposed_finalizer_policy) 
                        << ") on real chain\n";

            real_chain_proposed_policies[real_chain.last_proposed_finalizer_policy.generation] = std::make_pair(r_pc_itr->second, fc::sha256::hash(real_chain.last_proposed_finalizer_policy));

         }

         if (i == 0 ){
            //if block is IF genesis, verify we have the same initial state
            //BOOST_TEST(fake_chain.genesis_block_num == real_chain.genesis_block_num);
            //BOOST_TEST(fc::sha256::hash(fake_chain.active_finalizer_policy) == fc::sha256::hash(real_chain.active_finalizer_policy));

            std::cout << "Genesis finalizer policy generation : 1 -> A (" << fc::sha256::hash(fake_chain.active_finalizer_policy) 
                                                                  << ") on both chain\n";

            real_chain_proposed_policies[1] = std::make_pair('A', fc::sha256::hash(real_chain.active_finalizer_policy));

         }

      }

      std::vector<finality_proof::proof_of_finality> fake_chain_proofs_of_finality = fake_chain.get_light_client_proofs_of_finality();
      std::vector<finality_proof::proof_of_finality> real_chain_proofs_of_finality = real_chain.get_light_client_proofs_of_finality();

      std::cout << "\nfake chain -> get_light_client_proofs_of_finality() count : " << fake_chain_proofs_of_finality.size() << "\n\n";
      for (size_t i = 0 ; i < fake_chain_proofs_of_finality.size() ; i++){
         print_proof_of_finality(fake_chain_proofs_of_finality[i], fake_chain);
      }

      std::cout << "\nreal chain -> get_light_client_proofs_of_finality() count : " << real_chain_proofs_of_finality.size() << "\n\n";
      for (size_t i = 0 ; i < real_chain_proofs_of_finality.size() ; i++){
         print_proof_of_finality(real_chain_proofs_of_finality[i], real_chain);
      }
 
      auto f_fp_itr = fake_chain_proofs_of_finality.rbegin();

      auto r_common_policy = real_chain_proofs_of_finality.end();
      auto f_common_policy = fake_chain_proofs_of_finality.rend();

      auto r_next_policy = real_chain_proofs_of_finality.end();
      auto f_next_policy = fake_chain_proofs_of_finality.end();

      count = 0;

      std::vector<finality_proof::proof_of_finality>::iterator r_fp_itr;

      while (f_fp_itr!=fake_chain_proofs_of_finality.rend()){

         //std::cout << "Iteration " << count << "\n";

         if (f_fp_itr->qc_chain[0].finality_data.tombstone_finalizer_policy_digest.empty()){
            std::cout << "Looking for common active policy : " << fc::sha256::hash(f_fp_itr->qc_chain[0].active_finalizer_policy) << "\n";
            r_fp_itr = std::find_if(real_chain_proofs_of_finality.begin(), real_chain_proofs_of_finality.end(), std::bind(active_finalizer_policy_digest_comparer, std::placeholders::_1, fc::sha256::hash(f_fp_itr->qc_chain[0].active_finalizer_policy)));
         }
         else {
            std::cout << "Looking for common policy tombstone digest : " << f_fp_itr->qc_chain[0].finality_data.tombstone_finalizer_policy_digest << "\n";
            r_fp_itr = std::find_if(real_chain_proofs_of_finality.begin(), real_chain_proofs_of_finality.end(), std::bind(tombstone_comparer, std::placeholders::_1, *f_fp_itr));

         }

         if (r_fp_itr!=real_chain_proofs_of_finality.end()){

            if (r_fp_itr->is_proof_of_finality_for_genesis_policy){
               std::cout << "last common policy is IF genesis policy\n";
               r_common_policy = r_fp_itr;
               f_common_policy = f_fp_itr;
               r_next_policy = ++r_fp_itr;
               f_next_policy = f_fp_itr.base();
               break;
            }

            auto f_afp_itr = std::find_if(fake_chain_proofs_of_finality.begin(), fake_chain_proofs_of_finality.end(), std::bind(active_finalizer_policy_digest_comparer, std::placeholders::_1, fc::sha256::hash(f_fp_itr->qc_chain[0].active_finalizer_policy)));
            auto r_afp_itr = std::find_if(real_chain_proofs_of_finality.begin(), real_chain_proofs_of_finality.end(), std::bind(active_finalizer_policy_digest_comparer, std::placeholders::_1, fc::sha256::hash(r_fp_itr->qc_chain[0].active_finalizer_policy)));

            assert(f_afp_itr!=fake_chain_proofs_of_finality.end());
            assert(r_afp_itr!=real_chain_proofs_of_finality.end());

            //std::cout << "f_afp_itr->qc_chain[0].finality_digest : " << f_afp_itr->qc_chain[0].finality_digest << "\n";
            //std::cout << "r_afp_itr->qc_chain[0].finality_digest : " << r_afp_itr->qc_chain[0].finality_digest << "\n";
            
            if (f_afp_itr->qc_chain[0].finality_digest==r_afp_itr->qc_chain[0].finality_digest){
               std::cout << "last common policy found\n";
               r_common_policy = r_fp_itr;
               f_common_policy = f_fp_itr;
               r_next_policy = ++r_fp_itr;
               f_next_policy = f_fp_itr.base();
               break;
            }


         }

         f_fp_itr++;
         count++;

      }

      bool found_common = r_common_policy!=real_chain_proofs_of_finality.end();
      BOOST_REQUIRE(found_common); //should always be possible to find the last common policy if the chains share the same IF Genesis

      auto using_r_policy = r_next_policy == real_chain_proofs_of_finality.end() ? r_common_policy : r_next_policy;
      auto using_f_policy = f_next_policy == fake_chain_proofs_of_finality.end() ? --f_common_policy.base() : f_next_policy;

      std::cout << "found last common policy\n";
      std::cout << "last common policy is : " << fc::sha256::hash(r_common_policy->qc_chain[0].active_finalizer_policy) << "\n";
      std::cout << "\n";

      //if the digest of the last recorded proof of finality for the last common policy is the same for both chains, no finality violation occurred
      if(using_r_policy->qc_chain[0].finality_digest==using_f_policy->qc_chain[0].finality_digest ) {
         std::cout << "No finality violation detected\n";
         return std::nullopt;
      }
      else {

         //otherwise, we need to look at the block number of these proofs of finality to know if a finality violation occurred
         bool finality_violation_occurred = false;

         if (using_r_policy->qc_chain[0].block->block_num() <= using_f_policy->qc_chain[0].block->block_num() ){
            std::cout << "same or lower block number on the real chain\n";
            //if the block num for the last proof of finality of this policy on the real chain is less or equal to the block num on the fake chain, a finality violation has occurred 
            finality_violation_occurred = true;
         }
         else { 
            //if the real chain is longer than the fake chain, we look through its history for the proof of finality for the block at the same height than the last one we recorded on 
            //the fake chain

            std::cout << "higher block number on the real chain\n";
            std::vector<finality_proof::proof_of_finality> all_regular_proofs_of_finality = real_chain.all_regular_proofs_of_finality;

            auto itr = std::find_if(all_regular_proofs_of_finality.begin(), all_regular_proofs_of_finality.end(),  std::bind(block_num_comparer, std::placeholders::_1, *using_f_policy));

            BOOST_REQUIRE(itr!=all_regular_proofs_of_finality.end());

            if (itr->qc_chain[0].block->block_num() == using_f_policy->qc_chain[0].block->block_num()) 
               finality_violation_occurred = itr->qc_chain[0].finality_digest != using_f_policy->qc_chain[0].finality_digest;
            else {
               //must prove inclusion into final root

               //NOT IMPLEMENTED YET

               BOOST_REQUIRE(false);

            }

         }

         if (finality_violation_occurred) {

            std::cout << "*** Finality violation detected *** \n\n";
            std::cout << "Conflicting blocks signed by " << fc::sha256::hash(r_common_policy->qc_chain[0].active_finalizer_policy) << " were both made final : \n\n";
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

            
             return finality_violation_blame{ r_common_policy->qc_chain[0].active_finalizer_policy.generation, 
                                          real_chain_proposed_policies[r_common_policy->qc_chain[0].active_finalizer_policy.generation]} ;
         }
         else return std::nullopt;

      }

/*      for (uint32_t i = 1 ; i < blocks_to_produce; i++){
         print_blocks_comparison(fake_block_results[i-1], real_block_results[i-1], fake_chain.genesis_block_num);
      }*/

   }


   BOOST_AUTO_TEST_CASE(two_chains_tests) { try {

      //test same history on both fake and real chains
      std::optional<finality_violation_blame> result_1 = perform_test(12, 12,
         {},
         {},
         {},
         {});

      //verify this doesn't trigger finality violation
      BOOST_TEST(result_1.has_value() == false);

      //test a setfinalizer on the fake chain with sufficient blocks to capture a proof of finality on policy tombstone
      std::optional<finality_violation_blame> result_2 = perform_test(7, 7,
         {{3, 'B'}},
         {},
         {},
         {});

      //verify we correctly blame finalizer policy A at generation 1
      BOOST_TEST(result_2.has_value());
      BOOST_TEST(result_2.value().generation == 1);
      BOOST_TEST(result_2.value().policy.first == 'A'); 

      //test a setfinalizer on the fake chain without enough blocks to capture a proof of finality on policy tombstone
      std::optional<finality_violation_blame> result_3 = perform_test(6, 6,
         {{3, 'B'}},
         {},
         {},
         {});

      //verify that this is not enough to prove a finality violation
      //todo : discuss. this implies a gap of 1 block between what can be made final as per the protocol versus what can be proven using the tombstone method
      BOOST_TEST(result_3.has_value() == false);

      //test identical setfinalizer calls on both chains, but with a transfer action forking the fake chain
      std::optional<finality_violation_blame> result_5 = perform_test(30, 30,
         {{3, 'B'}, {16, 'C'}},
         {{3, 'B'}, {16, 'C'}},
         {24},
         {});

      //verify we correctly blame finalizer policy C at generation 3
      BOOST_TEST(result_5.has_value());
      BOOST_TEST(result_5.value().generation == 3);
      BOOST_TEST(result_5.value().policy.first == 'C'); 

      //test a different setfinalizer on the block before the tombstone moment for finalizer policy A
      std::optional<finality_violation_blame> result_6 = perform_test(14, 14,
         {{3, 'B'}, {9, 'C'}, {10, 'D'}, {11, 'E'}, {12, 'F'}, {13, 'G'}, {14, 'H'}},
         {{3, 'B'}, {9, 'J'}, {10, 'D'}, {11, 'E'}, {12, 'F'}, {13, 'G'}, {14, 'H'}},
         {},
         {});

      //verify we correctly blame finalizer policy A at generation 1
      BOOST_TEST(result_6.has_value());
      BOOST_TEST(result_6.value().generation == 1);
      BOOST_TEST(result_6.value().policy.first == 'A'); 

      //test a different setfinalizer on the tombstone block for finalizer policy A
      std::optional<finality_violation_blame> result_7 = perform_test(15, 15,
         {{3, 'B'}, {10, 'C'}, {11, 'D'}, {12, 'E'}, {13, 'F'}, {14, 'G'}, {15, 'H'}},
         {{3, 'B'}, {10, 'J'}, {11, 'D'}, {12, 'E'}, {13, 'F'}, {14, 'G'}, {15, 'H'}},
         {},
         {});

      //verify we correctly blame finalizer policy B at generation 2
      BOOST_TEST(result_7.has_value());
      BOOST_TEST(result_7.value().generation == 2);
      BOOST_TEST(result_7.value().policy.first == 'B');

      //test a complete divergence of both chains, caused by an initial finality violation by generation B
      std::optional<finality_violation_blame> result_8 = perform_test(35, 35,
         {{3, 'B'}, {11, 'C'}, {14, 'D'}, {17, 'E'}, {22, 'F'}, {26, 'G'}, {28, 'H'}},
         {{3, 'B'}, {13, 'J'}, {16, 'D'}, {18, 'E'}, {19, 'F'}, {21, 'G'}},
         {},
         {});

      //verify we correctly blame finalizer policy B at generation 2
      BOOST_TEST(result_8.has_value());
      BOOST_TEST(result_8.value().generation == 2);
      BOOST_TEST(result_8.value().policy.first == 'B');

      //test the fastest succession of policies possible
      std::optional<finality_violation_blame> result_9 = perform_test(15, 15,
         {{3, 'B'}, {4, 'C'}, {5, 'D'}, {6, 'E'}, {7, 'F'}, {8, 'G'}, {9, 'H'}, {10, 'I'}},
         {{3, 'B'}, {4, 'C'}, {5, 'D'}, {6, 'J'}, {7, 'F'}, {8, 'G'}, {9, 'H'}, {10, 'I'}},
         {},
         {});

      //verify we correctly blame finalizer policy A at generation 1
      BOOST_TEST(result_9.has_value());
      BOOST_TEST(result_9.value().generation == 1);
      BOOST_TEST(result_9.value().policy.first == 'A');

      //test the case where a real chain is longer than the fake chain
      std::optional<finality_violation_blame> result_10 = perform_test(15, 25,
         {},
         {},
         {},
         {});

      //verify this doesn't trigger finality violation
      BOOST_TEST(result_10.has_value() == false);

      //test the case where a fake chain is longer than the real chain
      std::optional<finality_violation_blame> result_11 = perform_test(25, 15,
         {},
         {},
         {},
         {});

      //verify we correctly blame finalizer policy A at generation 1
      BOOST_TEST(result_11.has_value());
      BOOST_TEST(result_11.value().generation == 1);
      BOOST_TEST(result_11.value().policy.first == 'A');

      //test a complete divergence of both chains, caused by an initial finality violation by generation B
      std::optional<finality_violation_blame> result_12 = perform_test(38, 34,
         {{3, 'B'}, {11, 'C'}, {14, 'D'}, {17, 'E'}, {22, 'F'}, {26, 'G'}, {28, 'H'}},
         {{3, 'B'}, {13, 'J'}, {16, 'D'}, {18, 'E'}, {19, 'F'}, {21, 'G'}},
         {},
         {});

      //verify we correctly blame finalizer policy B at generation 2
      BOOST_TEST(result_12.has_value());
      BOOST_TEST(result_12.value().generation == 2);
      BOOST_TEST(result_12.value().policy.first == 'B');

      //test the fastest succession of policies possible
      std::optional<finality_violation_blame> result_13 = perform_test(15, 18,
         {{3, 'B'}, {4, 'C'}, {5, 'D'}, {6, 'E'}, {7, 'F'}, {8, 'G'}, {9, 'H'}, {10, 'I'}},
         {{3, 'B'}, {4, 'C'}, {5, 'D'}, {6, 'J'}, {7, 'F'}, {8, 'G'}, {9, 'H'}, {10, 'I'}},
         {},
         {});

      //verify we correctly blame finalizer policy A at generation 1
      BOOST_TEST(result_13.has_value());
      BOOST_TEST(result_13.value().generation == 1);
      BOOST_TEST(result_13.value().policy.first == 'A');

   } FC_LOG_AND_RETHROW() }


BOOST_AUTO_TEST_SUITE_END()
