#include <finality_test_cluster.hpp>

#include <eosio/chain/block_state.hpp>
#include <eosio/testing/tester.hpp>

#include <fc/exception/exception.hpp>
#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_utils.hpp>

#include <boost/test/unit_test.hpp>

using namespace eosio::chain;
using namespace fc::crypto::blslib;

BOOST_AUTO_TEST_SUITE(block_state_tests)

BOOST_AUTO_TEST_CASE(aggregate_vote_test) try {
   digest_type block_id(fc::sha256("0000000000000000000000000000001"));

   digest_type strong_digest(fc::sha256("0000000000000000000000000000002"));

   weak_digest_t weak_digest(create_weak_digest(fc::sha256("0000000000000000000000000000003")));

   const size_t num_finalizers = 3;

   // initialize a set of private keys
   std::vector<bls_private_key> private_key {
      bls_private_key("PVT_BLS_foNjZTu0k6qM5ftIrqC5G_sim1Rg7wq3cRUaJGvNtm2rM89K"),
      bls_private_key("PVT_BLS_FWK1sk_DJnoxNvUNhwvJAYJFcQAFtt_mCtdQCUPQ4jN1K7eT"),
      bls_private_key("PVT_BLS_tNAkC5MnI-fjHWSX7la1CPC2GIYgzW5TBfuKFPagmwVVsOeW"),
   };

   // construct finalizers
   std::vector<bls_public_key> public_key(num_finalizers);
   std::vector<finalizer_authority> finalizers(num_finalizers);
   for (size_t i = 0; i < num_finalizers; ++i) {
      public_key[i] = private_key[i].get_public_key();
      finalizers[i] = finalizer_authority{ "test", 1, public_key[i] };
   }

   {  // all finalizers can aggregate votes
      block_state_ptr bsp = std::make_shared<block_state>();
      bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( 10, 15, finalizers );
      bsp->strong_digest = strong_digest;
      bsp->weak_digest = weak_digest;
      bsp->open_qc = open_qc_t{ bsp->active_finalizer_policy, {} };

      for (size_t i = 0; i < num_finalizers; ++i) {
         bool strong = (i % 2 == 0); // alternate strong and weak
         auto sig = strong ? private_key[i].sign(strong_digest.to_uint8_span()) : private_key[i].sign(weak_digest);
         vote_message vote{ block_id, strong, public_key[i], sig };
         BOOST_REQUIRE(bsp->aggregate_vote(0, vote) == vote_status::success);
      }
   }

   {  // public and private keys mismatched
      block_state_ptr bsp = std::make_shared<block_state>();
      bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( 10, 15, finalizers );
      bsp->strong_digest = strong_digest;
      bsp->open_qc = open_qc_t{ bsp->active_finalizer_policy, {} };

      vote_message vote {block_id, true, public_key[0], private_key[1].sign(strong_digest.to_uint8_span()) };
      BOOST_REQUIRE(bsp->aggregate_vote(0, vote) != vote_status::success);
   }

   {  // duplicate votes 
      block_state_ptr bsp = std::make_shared<block_state>();
      bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( 10, 15, finalizers );
      bsp->strong_digest = strong_digest;
      bsp->open_qc = open_qc_t{ bsp->active_finalizer_policy, {} };

      vote_message vote {block_id, true, public_key[0], private_key[0].sign(strong_digest.to_uint8_span()) };
      BOOST_REQUIRE(bsp->aggregate_vote(0, vote) == vote_status::success);
      BOOST_REQUIRE(bsp->aggregate_vote(0, vote) != vote_status::success);
   }

   {  // public key does not exit in finalizer set
      block_state_ptr bsp = std::make_shared<block_state>();
      bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( 10, 15, finalizers );
      bsp->strong_digest = strong_digest;
      bsp->open_qc = open_qc_t{ bsp->active_finalizer_policy, {} };

      bls_private_key new_private_key{ "PVT_BLS_Wfs3KzfTI2P5F85PnoHXLnmYgSbp-XpebIdS6BUCHXOKmKXK" };
      bls_public_key new_public_key{ new_private_key.get_public_key() };

      vote_message vote {block_id, true, new_public_key, private_key[0].sign(strong_digest.to_uint8_span()) };
      BOOST_REQUIRE(bsp->aggregate_vote(0, vote) != vote_status::success);
   }
} FC_LOG_AND_RETHROW();

void do_quorum_test(const std::vector<uint64_t>& weights,
                    uint64_t threshold,
                    bool strong,
                    const std::vector<bool>& to_vote,
                    bool expected_quorum) {
   digest_type block_id(fc::sha256("0000000000000000000000000000001"));
   digest_type strong_digest(fc::sha256("0000000000000000000000000000002"));
   auto weak_digest(create_weak_digest(fc::sha256("0000000000000000000000000000003")));

   // initialize a set of private keys
   std::vector<bls_private_key> private_key {
      bls_private_key("PVT_BLS_foNjZTu0k6qM5ftIrqC5G_sim1Rg7wq3cRUaJGvNtm2rM89K"),
      bls_private_key("PVT_BLS_FWK1sk_DJnoxNvUNhwvJAYJFcQAFtt_mCtdQCUPQ4jN1K7eT"),
      bls_private_key("PVT_BLS_tNAkC5MnI-fjHWSX7la1CPC2GIYgzW5TBfuKFPagmwVVsOeW"),
   };
   const size_t num_finalizers = private_key.size();

   // construct finalizers
   std::vector<bls_public_key> public_key(num_finalizers);
   std::vector<finalizer_authority> finalizers(num_finalizers);
   for (size_t i = 0; i < num_finalizers; ++i) {
      public_key[i] = private_key[i].get_public_key();
      finalizers[i] = finalizer_authority{ "test", weights[i], public_key[i] };
   }

   block_state_ptr bsp = std::make_shared<block_state>();
   constexpr uint32_t generation = 1;
   bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( generation, threshold, finalizers );
   bsp->strong_digest = strong_digest;
   bsp->weak_digest = weak_digest;
   bsp->open_qc = open_qc_t{ bsp->active_finalizer_policy, {} };

   for (size_t i = 0; i < num_finalizers; ++i) {
      if( to_vote[i] ) {
         auto sig = strong ? private_key[i].sign(strong_digest.to_uint8_span()) : private_key[i].sign(weak_digest);
         vote_message vote{ block_id, strong, public_key[i], sig };
         BOOST_REQUIRE(bsp->aggregate_vote(0, vote) == vote_status::success);
      }
   }

   BOOST_REQUIRE_EQUAL(bsp->open_qc.active_policy_sig.is_quorum_met(), expected_quorum);
}

BOOST_AUTO_TEST_CASE(quorum_test) try {
   std::vector<uint64_t> weights{1, 3, 5};
   constexpr uint64_t threshold = 4;

   { // 1 strong vote, quorum not met
      constexpr bool strong = true;
      std::vector<bool> to_vote{true, false, false}; // finalizer 0 voting
      constexpr bool expected_quorum_met = false;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met );
   }

   { // 2 strong votes, quorum met
      constexpr bool strong = true;
      std::vector<bool> to_vote{true, true, false}; // finalizers 0 and 1 voting
      constexpr bool expected_quorum_met = true;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met );
   }

   { // 1 strong vote, quorum met
      constexpr bool strong = true;
      std::vector<bool> to_vote{false, false, true}; // finalizer 2 voting
      constexpr bool expected_quorum_met = true;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met );
   }

   { // 1 weak vote, quorum not met
      constexpr bool strong = false;
      std::vector<bool> to_vote{true, false, false}; // finalizer 0 voting
      constexpr bool expected_quorum_met = false;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met );
   }

   { // 2 weak votes, quorum met
      constexpr bool strong = false;
      std::vector<bool> to_vote{true, true, false}; // finalizers 0 and 1 voting
      constexpr bool expected_quorum_met = true;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met );
   }

   { // 1 weak vote, quorum met
      constexpr bool strong = false;
      std::vector<bool> to_vote{false, false, true}; // finalizer 2 voting
      constexpr bool expected_quorum_met = true;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met );
   }
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(verify_qc_test) try {
   // prepare digests
   digest_type strong_digest(fc::sha256("0000000000000000000000000000002"));
   auto weak_digest(create_weak_digest(fc::sha256("0000000000000000000000000000003")));

   // initialize a set of private keys
   std::vector<bls_private_key> private_key {
      bls_private_key("PVT_BLS_foNjZTu0k6qM5ftIrqC5G_sim1Rg7wq3cRUaJGvNtm2rM89K"),
      bls_private_key("PVT_BLS_FWK1sk_DJnoxNvUNhwvJAYJFcQAFtt_mCtdQCUPQ4jN1K7eT"),
      bls_private_key("PVT_BLS_tNAkC5MnI-fjHWSX7la1CPC2GIYgzW5TBfuKFPagmwVVsOeW"),
   };
   auto num_finalizers = private_key.size();
  
   // construct finalizers, with weight 1, 2, 3 respectively
   std::vector<bls_public_key> public_key(num_finalizers);
   std::vector<finalizer_authority> finalizers(num_finalizers);
   for (size_t i = 0; i < num_finalizers; ++i) {
      public_key[i] = private_key[i].get_public_key();
      uint64_t weight = i + 1;
      finalizers[i] = finalizer_authority{ "test", weight, public_key[i] };
   }
  
   // consturct a test bsp
   block_state_ptr bsp = std::make_shared<block_state>();
   constexpr uint32_t generation = 1;
   constexpr uint64_t threshold = 4; // 2/3 of total weights of 6
   bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( generation, threshold, finalizers );
   bsp->strong_digest = strong_digest;
   bsp->weak_digest = weak_digest;

   {  // valid strong QC
      vote_bitset strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      strong_votes[2] = 1;  // finalizer 2 voted with weight 3

      bls_signature sig_0 = private_key[0].sign(strong_digest.to_uint8_span());
      bls_signature sig_2 = private_key[2].sign(strong_digest.to_uint8_span());
      bls_aggregate_signature agg_sig;
      agg_sig.aggregate(sig_0);
      agg_sig.aggregate(sig_2);

      // create a qc_sig_t
      qc_sig_t qc_sig{strong_votes, {}, agg_sig};
      qc_t qc{bsp->block_num(), qc_sig, {}};

      BOOST_REQUIRE_NO_THROW( bsp->verify_qc(qc) );
   }

   {  // valid weak QC
      vote_bitset strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      bls_signature strong_sig = private_key[0].sign(strong_digest.to_uint8_span());

      vote_bitset weak_votes(num_finalizers);
      weak_votes[2] = 1;  // finalizer 2 voted with weight 3
      bls_signature weak_sig = private_key[2].sign(weak_digest);

      bls_aggregate_signature agg_sig;
      agg_sig.aggregate(strong_sig);
      agg_sig.aggregate(weak_sig);

      qc_sig_t qc_sig(strong_votes, weak_votes, agg_sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};
      BOOST_REQUIRE_NO_THROW( bsp->verify_qc(qc) );
   }

   {  // valid strong QC signed by all finalizers
      vote_bitset strong_votes(num_finalizers);
      std::vector<bls_signature> sigs(num_finalizers);
      bls_aggregate_signature agg_sig;

      for (auto i = 0u; i < num_finalizers; ++i) {
         strong_votes[i] = 1;
         sigs[i] = private_key[i].sign(strong_digest.to_uint8_span());
         agg_sig.aggregate(sigs[i]);
      }

      // create a qc_sig_t
      qc_sig_t qc_sig(strong_votes, {}, agg_sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};

      BOOST_REQUIRE_NO_THROW( bsp->verify_qc(qc) );
   }

   {  // valid weak QC signed by all finalizers
      vote_bitset weak_votes(num_finalizers);
      std::vector<bls_signature> sigs(num_finalizers);
      bls_aggregate_signature agg_sig;

      for (auto i = 0u; i < num_finalizers; ++i) {
         weak_votes[i] = 1;
         sigs[i] = private_key[i].sign(weak_digest);
         agg_sig.aggregate(sigs[i]);
      }

      // create a qc_sig_t
      qc_sig_t qc_sig({}, weak_votes, agg_sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};

      BOOST_REQUIRE_NO_THROW( bsp->verify_qc(qc) );
   }

   {  // strong QC quorem not met
      vote_bitset strong_votes(num_finalizers);
      strong_votes[2] = 1;  // finalizer 2 voted with weight 3 (threshold is 4)

      bls_aggregate_signature agg_sig;
      bls_signature sig_2 = private_key[2].sign(strong_digest.to_uint8_span());
      agg_sig.aggregate(sig_2);

      // create a qc_sig_t
      qc_sig_t qc_sig(strong_votes, {}, agg_sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc_claim, eosio::testing::fc_exception_message_starts_with("strong quorum is not met") );
   }

   {  // weak QC quorem not met
      vote_bitset weak_votes(num_finalizers);
      weak_votes[2] = 1;  // finalizer 2 voted with weight 3 (threshold is 4)

      bls_aggregate_signature agg_sig;
      bls_signature sig_2 = private_key[2].sign(weak_digest);
      agg_sig.aggregate(sig_2);

      // create a qc_sig_t
      qc_sig_t qc_sig({}, weak_votes, agg_sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc_claim, eosio::testing::fc_exception_message_starts_with("weak quorum is not met") );
   }

   {  // strong QC bitset size does not match number of finalizers in the policy

      // construct vote bitset with a size greater than num_finalizers
      vote_bitset strong_votes(num_finalizers + 1);

      // vote by finalizer 0
      strong_votes[0] = 1;

      // aggregate votes
      bls_aggregate_signature agg_sig;
      bls_signature sig = private_key[0].sign(strong_digest.to_uint8_span());
      agg_sig.aggregate(sig);

      // create a qc_sig_t
      qc_sig_t qc_sig(strong_votes, {}, agg_sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc_claim, eosio::testing::fc_exception_message_starts_with("vote bitset size is not the same as the number of finalizers") );
   }

   {  // weak QC bitset size does not match number of finalizers in the policy

      // construct vote bitset with a size less than num_finalizers
      vote_bitset weak_votes(num_finalizers - 1);

      // vote by finalizer 0
      weak_votes[0] = 1;

      // aggregate votes
      bls_aggregate_signature agg_sig;
      bls_signature sig = private_key[0].sign(weak_digest);
      agg_sig.aggregate(sig);

      // create a qc_sig_t
      qc_sig_t qc_sig({}, weak_votes, agg_sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc_claim, eosio::testing::fc_exception_message_starts_with("vote bitset size is not the same as the number of finalizers") );
   }

   {  // strong QC with a wrong signing private key
      vote_bitset strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      strong_votes[2] = 1;  // finalizer 2 voted with weight 3

      bls_signature sig_0 = private_key[0].sign(strong_digest.to_uint8_span());
      bls_signature sig_2 = private_key[1].sign(strong_digest.to_uint8_span()); // signed by finalizer 1 which is not set in strong_votes
      bls_aggregate_signature sig;
      sig.aggregate(sig_0);
      sig.aggregate(sig_2);

      // create a qc_sig_t
      qc_sig_t qc_sig(strong_votes, {}, sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc_claim, eosio::testing::fc_exception_message_is("qc signature validation failed") );
   }

   {  // strong QC with a wrong digest
      vote_bitset strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      strong_votes[2] = 1;  // finalizer 2 voted with weight 3

      bls_signature sig_0 = private_key[0].sign(weak_digest); // should have used strong digest
      bls_signature sig_2 = private_key[2].sign(strong_digest.to_uint8_span());
      bls_aggregate_signature sig;
      sig.aggregate(sig_0);
      sig.aggregate(sig_2);

      // create a qc_sig_t
      qc_sig_t qc_sig(strong_votes, {}, sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc_claim, eosio::testing::fc_exception_message_is("qc signature validation failed") );
   }

   {  // weak QC with a wrong signing private key
      vote_bitset strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      bls_signature strong_sig = private_key[0].sign(strong_digest.to_uint8_span());

      vote_bitset weak_votes(num_finalizers);
      weak_votes[2] = 1;  // finalizer 2 voted with weight 3
      bls_signature weak_sig = private_key[1].sign(weak_digest); // wrong key

      bls_aggregate_signature sig;
      sig.aggregate(strong_sig);
      sig.aggregate(weak_sig);

      qc_sig_t qc_sig(strong_votes, weak_votes, sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};
      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc_claim, eosio::testing::fc_exception_message_is("qc signature validation failed") );
   }

   {  // weak QC with a wrong digest
      vote_bitset strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      bls_signature strong_sig = private_key[0].sign(weak_digest); // wrong digest

      vote_bitset weak_votes(num_finalizers);
      weak_votes[2] = 1;  // finalizer 2 voted with weight 3
      bls_signature weak_sig = private_key[2].sign(weak_digest);

      bls_aggregate_signature sig;
      sig.aggregate(strong_sig);
      sig.aggregate(weak_sig);

      qc_sig_t qc_sig(strong_votes, weak_votes, sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};
      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc_claim, eosio::testing::fc_exception_message_is("qc signature validation failed") );
   }
} FC_LOG_AND_RETHROW();

BOOST_FIXTURE_TEST_CASE(get_finality_data_test, finality_test_cluster<4>) try {
   // The test cluster consists of only 4 node -- node0 is both a producer and a finalizer.
   // It has transitioned to Savanna after startup.

   // fin_policy_0 is the active finalizer policy
   BOOST_REQUIRE(fin_policy_0);

   // fin_policy_indices_0 is the set of indices used in active finalizer policy
   // to indicate which key of a node is used in the policy
   auto key_indices = fin_policy_indices_0;
   BOOST_REQUIRE(key_indices[0] == 0u);  // index 0 for node0 was used in active policy

   // Propose a finalizer policy by changing the index of the key used by node0 to 1
   key_indices[0] = 1;
   node0.finkeys.set_finalizer_policy(key_indices);

   finality_data_t finality_data;

   // It takes one 3-chain for LIB to advance and 1 LIB proposed finalizer to be promoted to pending.
   for (size_t i=0; i<3; ++i) {
      produce_and_push_block();
      process_votes(1, num_nodes - 1); // all non-producing nodes (starting from node1) vote

      // We should not see pending_finalizer_policy in finality_data
      finality_data = *node0.control->head_finality_data();
      BOOST_REQUIRE(!finality_data.pending_finalizer_policy.has_value());
   }

   // Produce one more block. The proposed finalizer policy is promoted to pending in this block.
   // We should see pending_finalizer_policy in finality_data
   produce_and_push_block();
   process_votes(1, num_nodes - 1); // all non-producing nodes (starting from node1) vote
   finality_data = *node0.control->head_finality_data();
   BOOST_REQUIRE(finality_data.pending_finalizer_policy.has_value());

   // Produce another block. We should not see pending_finalizer_policy as
   // no proposed finalizer policy is promoted to pending in this block
   produce_and_push_block();
   process_votes(1, num_nodes - 1); // all non-producing nodes (starting from node1) vote
   finality_data = *node0.control->head_finality_data();
   BOOST_REQUIRE(!finality_data.pending_finalizer_policy.has_value());
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
