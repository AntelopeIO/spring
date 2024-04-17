#include <eosio/chain/abi_serializer.hpp>
#include <eosio/testing/tester.hpp>

#include <fc/variant_object.hpp>

#include <boost/test/unit_test.hpp>

#include <contracts.hpp>
#include <test_contracts.hpp>
#include "fork_test_utilities.hpp"

#include <eosio/chain/exceptions.hpp>

#include "svnn_ibc_test_cluster.hpp"

using namespace eosio::chain;
using namespace eosio::testing;

using mvo = mutable_variant_object;

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

   struct merkle_branch_t {
      bool direction;
      digest_type hash;
   };

   //generate a proof of inclusion for a node at index from a list of leaves
   std::vector<merkle_branch_t> generate_proof_of_inclusion(const std::vector<digest_type> leaves, const size_t index) {

      auto _leaves = leaves;
      auto _index = index;

      std::vector<merkle_branch_t> proof;

      while (_leaves.size()>1){
         std::vector<digest_type> new_level;
         for (size_t i = 0 ; i < _leaves.size() ; i+=2){
            digest_type left = _leaves[i];

            if (i + 1 < _leaves.size() && (i + 1 != _leaves.size() - 1 || _leaves.size() % 2 == 0)){
               // Normal case: both children exist and are not at the end or are even
               digest_type right = _leaves[i+1];

               new_level.push_back(fc::sha256::hash(std::pair<digest_type, digest_type>(left, right)));
               if (_index == i || _index == i + 1) {
                 proof.push_back(_index == i ? merkle_branch_t{false, right} : merkle_branch_t{true, left});
                 _index = i / 2; // Update index for next level

               }
            }
            else {
               // Odd number of leaves at this level, and we're at the end
               new_level.push_back(left); // Promote the left (which is also the right in this case)
               if (_index == i) _index = i / 2; // Update index for next level, no sibling to add

            }
         }
         _leaves = new_level;
      }
      return proof;
   }

   BOOST_AUTO_TEST_CASE(ibc_test) { try {

       // cluster is set up with the head about to produce IF Genesis      
      svnn_ibc_test_cluster cluster;

      // produce IF Genesis block
      auto genesis_block = cluster.produce_and_push_block();

      // ensure out of scope setup and wiring is consistent  
      BOOST_CHECK(genesis_block->block_num() == 6);

      // check if IF Genesis block contains an IF extension
      std::optional<eosio::chain::block_header_extension> genesis_if_ext = genesis_block->extract_header_extension(eosio::chain::instant_finality_extension::extension_id());
      BOOST_CHECK(genesis_if_ext.has_value());
      
      // and that it has the expected initial finalizer_policy
      std::optional<eosio::chain::finalizer_policy> active_finalizer_policy = std::get<eosio::chain::instant_finality_extension>(*genesis_if_ext).new_finalizer_policy;
      BOOST_CHECK(!!active_finalizer_policy);
      BOOST_CHECK(active_finalizer_policy->finalizers.size() == 3);
      BOOST_CHECK(active_finalizer_policy->generation == 1);

      // compute the digest of the finalizer policy
      auto active_finalizer_policy_digest = fc::sha256::hash(*active_finalizer_policy);
      
      auto genesis_block_fd = cluster.node0.node.control->head_finality_data();

      // verify we have finality data for the IF genesis block
      BOOST_CHECK(genesis_block_fd.has_value());

      // compute IF finality leaf
      auto genesis_base_digest = genesis_block_fd.value().base_digest;
      auto genesis_afp_base_digest =  fc::sha256::hash(std::pair<const digest_type&, const digest_type&>(active_finalizer_policy_digest, genesis_base_digest));
      
      auto genesis_block_finality_digest = fc::sha256::hash(eosio::chain::finality_digest_data_v1{
         .active_finalizer_policy_generation      = active_finalizer_policy->generation,
         .finality_tree_digest                    = digest_type(), //nothing to finalize yet
         .active_finalizer_policy_and_base_digest = genesis_afp_base_digest
      });

      //action_mroot computed using the post-IF activation merkle tree rules
      auto genesis_block_action_mroot = genesis_block_fd.value().action_mroot;
      
      //initial finality leaf
      auto genesis_block_leaf = fc::sha256::hash(valid_t::finality_leaf_node_t{
         .block_num = genesis_block->block_num(),
         .finality_digest = genesis_block_finality_digest,
         .action_mroot = genesis_block_action_mroot
      });

      // create the ibc account and deploy the ibc contract to it 
      cluster.node0.node.create_account( "ibc"_n );
      cluster.node0.node.set_code( "ibc"_n, eosio::testing::test_contracts::svnn_ibc_wasm());
      cluster.node0.node.set_abi( "ibc"_n, eosio::testing::test_contracts::svnn_ibc_abi());

      // represent the public keys as std::vector<char> to be passed to smart contract
      std::array<uint8_t, 96> pub_key_0 = cluster.node0.priv_key.get_public_key().affine_non_montgomery_le();
      std::array<uint8_t, 96> pub_key_1 = cluster.node1.priv_key.get_public_key().affine_non_montgomery_le();
      std::array<uint8_t, 96> pub_key_2 = cluster.node2.priv_key.get_public_key().affine_non_montgomery_le();
      std::vector<char> vc_pub_key0(reinterpret_cast<char*>(pub_key_0.data()), reinterpret_cast<char*>(pub_key_0.data() + pub_key_0.size()));
      std::vector<char> vc_pub_key1(reinterpret_cast<char*>(pub_key_1.data()), reinterpret_cast<char*>(pub_key_1.data() + pub_key_1.size()));
      std::vector<char> vc_pub_key2(reinterpret_cast<char*>(pub_key_2.data()), reinterpret_cast<char*>(pub_key_2.data() + pub_key_2.size()));

      // configure ibc contract with the finalizer policy
      cluster.node0.node.push_action( "ibc"_n, "setfpolicy"_n, "ibc"_n, mvo()
         ("from_block_num", 1)
         ("policy", mvo() 
            ("generation", 1)
            ("threshold", 2)
            ("last_block_num", 0)
            ("finalizers", fc::variants({
               mvo() 
                  ("description","node0")
                  ("weight", 1)
                  ("public_key", vc_pub_key0)
               ,
               mvo() 
                  ("description","node1")
                  ("weight", 1)
                  ("public_key", vc_pub_key1)
               ,
               mvo() 
                  ("description","node2")
                  ("weight", 1)
                  ("public_key", vc_pub_key2)
         
            }))
         )
      );

      // Transition block. Finalizers are not expected to vote on this block. 
      auto block_1 = cluster.produce_and_push_block();
      auto block_1_fd = cluster.node0.node.control->head_finality_data();
      auto block_1_action_mroot = block_1_fd.value().action_mroot;
      auto block_1_finality_digest = cluster.node0.node.control->get_strong_digest_by_id(block_1->calculate_id());
      auto block_1_leaf = fc::sha256::hash(valid_t::finality_leaf_node_t{
         .block_num = block_1->block_num(),
         .finality_digest = block_1_finality_digest,
         .action_mroot = block_1_action_mroot
      });
      
      // Proper IF Block. From now on, finalizers must vote. Moving forward, the header action_mroot field is reconverted to provide the finality_mroot. The action_mroot is instead provided via the finality data 
      auto block_2 = cluster.produce_and_push_block();
      cluster.process_node1_vote(); //enough to reach quorum threshold
      auto block_2_fd = cluster.node0.node.control->head_finality_data();
      auto block_2_action_mroot = block_2_fd.value().action_mroot;
      auto block_2_base_digest = block_2_fd.value().base_digest;
      auto block_2_finality_digest = cluster.node0.node.control->get_strong_digest_by_id(block_2->calculate_id());
      auto block_2_afp_base_digest =  fc::sha256::hash(std::pair<const digest_type&, const digest_type&>(active_finalizer_policy_digest, block_2_base_digest));
      auto block_2_leaf = fc::sha256::hash(valid_t::finality_leaf_node_t{
         .block_num = block_2->block_num(),
         .finality_digest = block_2_finality_digest,
         .action_mroot = block_2_action_mroot
      });
      auto block_2_finality_root = block_2->action_mroot;

      // block_3 contains a QC over block_2
      auto block_3 = cluster.produce_and_push_block();
      cluster.process_node1_vote();

      // block_4 contains a QC over block_3
      auto block_4 = cluster.produce_and_push_block();
      cluster.process_node1_vote();
      auto block_4_fd = cluster.node0.node.control->head_finality_data();
      auto block_4_base_digest = block_4_fd.value().base_digest;
      auto block_4_afp_base_digest =  fc::sha256::hash(std::pair<const digest_type&, const digest_type&>(active_finalizer_policy_digest, block_4_base_digest));

      auto block_4_finality_root = block_4->action_mroot; 

      //  block_5 contains a QC over block_4, which completes the 3-chain for block_2 and serves as a proof of finality for it
      auto block_5 = cluster.produce_and_push_block();
      cluster.process_node1_vote();

      // retrieve the QC over block_4 that is contained in block_5
      qc_data_t qc_b_5 = extract_qc_data(block_5);

      BOOST_TEST(qc_b_5.qc.has_value());
      
      // generate proof of inclusion for block_2 in the merkle tree
      auto proof = generate_proof_of_inclusion({genesis_block_leaf, block_1_leaf, block_2_leaf}, 2); 

      // represent the QC signature as std::vector<char>
      std::array<uint8_t, 192> a_sig = bls_signature(qc_b_5.qc.value().qc._sig.to_string()).affine_non_montgomery_le();
      std::vector<char> vc_sig(reinterpret_cast<char*>(a_sig.data()), reinterpret_cast<char*>(a_sig.data() + a_sig.size()));

      std::vector<uint32_t> raw_biset = {3};
      
      // verify proof
      cluster.node0.node.push_action("ibc"_n, "checkproof"_n, "ibc"_n, mvo()
         ("proof", mvo() 
            ("finality_proof", mvo() 
               ("qc_block", mvo()
                  ("major_version", 1)
                  ("minor_version", 0)
                  ("finalizer_policy_generation", 1)
                  ("witness_hash", block_4_afp_base_digest)
                  ("finality_mroot", block_4_finality_root)
               )
               ("qc", mvo()
                  ("signature", vc_sig)
                  ("finalizers", raw_biset) //node0 and node1 signed
               )
            )
            ("target_block_proof_of_inclusion", mvo() 
               ("target_node_index", 2)
               ("last_node_index", 2)
               ("target", fc::variants({"block_data", mvo() 
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
               }))
               ("merkle_branches", fc::variants({
                  mvo() 
                     ("direction", proof[0].direction) // can be hardcoded since this test enforces block numbers
                     ("hash", proof[0].hash)
               }))
            )
         )
      );

      //test passed

   } FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()