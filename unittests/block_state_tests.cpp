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
   weak_digest_t weak_digest(create_weak_digest(strong_digest));

   const size_t num_finalizers = 3;

   // initialize a set of private keys for active finalizers
   std::vector<bls_private_key> active_private_keys {
      bls_private_key("PVT_BLS_foNjZTu0k6qM5ftIrqC5G_sim1Rg7wq3cRUaJGvNtm2rM89K"),
      bls_private_key("PVT_BLS_FWK1sk_DJnoxNvUNhwvJAYJFcQAFtt_mCtdQCUPQ4jN1K7eT"),
      bls_private_key("PVT_BLS_tNAkC5MnI-fjHWSX7la1CPC2GIYgzW5TBfuKFPagmwVVsOeW"),
   };

   std::vector<bls_private_key> pending_private_keys {
      bls_private_key("PVT_BLS_0d8dsux83r42Qg8CHgAqIuSsn9AV-QdCzx3tPj0K8yOJA_qb"),
      bls_private_key("PVT_BLS_74crPc__6BlpoQGvWjkHmUdzcDKh8QaiN_GtU4SD0QAi4BHY"),
      bls_private_key("PVT_BLS_foNjZTu0k6qM5ftIrqC5G_sim1Rg7wq3cRUaJGvNtm2rM89K"),
   };

   // construct active finalizers
   std::vector<bls_public_key> active_public_keys(num_finalizers);
   std::vector<finalizer_authority> active_finalizers(num_finalizers);
   for (size_t i = 0; i < num_finalizers; ++i) {
      active_public_keys[i] = active_private_keys[i].get_public_key();
      active_finalizers[i] = finalizer_authority{ "test", 1, active_public_keys[i] };
   }

   // construct pending finalizers
   std::vector<bls_public_key> pending_public_keys(num_finalizers);
   std::vector<finalizer_authority> pending_finalizers(num_finalizers);
   for (size_t i = 0; i < num_finalizers; ++i) {
      pending_public_keys[i] = pending_private_keys[i].get_public_key();
      pending_finalizers[i] = finalizer_authority{ "test", 1, pending_public_keys[i] };
   }

   {  // all finalizers can aggregate votes
      block_state_ptr bsp = std::make_shared<block_state>();
      bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( 10, 15, active_finalizers );
      bsp->strong_digest = strong_digest;
      bsp->weak_digest = weak_digest;
      bsp->aggregating_qc = aggregating_qc_t{ bsp->active_finalizer_policy, {} };

      for (size_t i = 0; i < num_finalizers; ++i) {
         bool strong = (i % 2 == 0); // alternate strong and weak
         auto sig = strong ? active_private_keys[i].sign(strong_digest.to_uint8_span()) : active_private_keys[i].sign(weak_digest);
         vote_message vote{ block_id, strong, active_public_keys[i], sig };
         BOOST_REQUIRE(bsp->aggregate_vote(0, vote).result == vote_result_t::success);
      }
   }

   {  // all finalizers can aggregate votes with pending
      block_state_ptr bsp = std::make_shared<block_state>();
      bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( 10, 15, active_finalizers );
      bsp->pending_finalizer_policy = { bsp->block_num(), std::make_shared<finalizer_policy>( 11, 15, pending_finalizers ) };
      bsp->strong_digest = strong_digest;
      bsp->weak_digest = weak_digest;
      bsp->aggregating_qc = aggregating_qc_t{ bsp->active_finalizer_policy, bsp->pending_finalizer_policy->second };

      for (size_t i = 0; i < num_finalizers; ++i) {
         bool strong = (i % 2 == 0); // alternate strong and weak
         auto sig = strong ? active_private_keys[i].sign(strong_digest.to_uint8_span()) : active_private_keys[i].sign(weak_digest);
         vote_message vote{ block_id, strong, active_public_keys[i], sig };
         BOOST_REQUIRE(bsp->aggregate_vote(0, vote).result == vote_result_t::success);
      }
      for (size_t i = 0; i < num_finalizers; ++i) {
         vote_result_t expected_vote_status = vote_result_t::success;
         if (std::ranges::find(active_public_keys, pending_public_keys[i]) != active_public_keys.end())
            expected_vote_status = vote_result_t::duplicate;
         bool strong = (i % 2 == 0); // alternate strong and weak
         auto sig = strong ? pending_private_keys[i].sign(strong_digest.to_uint8_span()) : pending_private_keys[i].sign(weak_digest);
         vote_message vote{ block_id, strong, pending_public_keys[i], sig };
         BOOST_REQUIRE(bsp->aggregate_vote(0, vote).result == expected_vote_status);
      }
   }

   {  // public and private keys mismatched
      block_state_ptr bsp = std::make_shared<block_state>();
      bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( 10, 15, active_finalizers );
      bsp->strong_digest = strong_digest;
      bsp->aggregating_qc = aggregating_qc_t{ bsp->active_finalizer_policy, {} };

      vote_message vote {block_id, true, active_public_keys[0], active_private_keys[1].sign(strong_digest.to_uint8_span()) };
      BOOST_REQUIRE(bsp->aggregate_vote(0, vote).result != vote_result_t::success);
   }

   {  // duplicate votes
      block_state_ptr bsp = std::make_shared<block_state>();
      bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( 10, 15, active_finalizers );
      bsp->strong_digest = strong_digest;
      bsp->aggregating_qc = aggregating_qc_t{ bsp->active_finalizer_policy, {} };

      vote_message vote {block_id, true, active_public_keys[0], active_private_keys[0].sign(strong_digest.to_uint8_span()) };
      BOOST_REQUIRE(bsp->aggregate_vote(0, vote).result == vote_result_t::success);
      BOOST_REQUIRE(bsp->aggregate_vote(0, vote).result == vote_result_t::duplicate);
   }

   {  // public key does not exist in active finalizer set
      block_state_ptr bsp = std::make_shared<block_state>();
      bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( 10, 15, active_finalizers );
      bsp->strong_digest = strong_digest;
      bsp->aggregating_qc = aggregating_qc_t{ bsp->active_finalizer_policy, {} };

      bls_private_key new_private_key{ "PVT_BLS_Wfs3KzfTI2P5F85PnoHXLnmYgSbp-XpebIdS6BUCHXOKmKXK" };
      bls_public_key new_public_key{ new_private_key.get_public_key() };

      vote_message vote {block_id, true, new_public_key, active_private_keys[0].sign(strong_digest.to_uint8_span()) };
      BOOST_REQUIRE(bsp->aggregate_vote(0, vote).result != vote_result_t::success);
   }

   {  // public key does not exist in active & pending finalizer sets
      block_state_ptr bsp = std::make_shared<block_state>();
      bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( 10, 15, active_finalizers );
      bsp->pending_finalizer_policy = { bsp->block_num(), std::make_shared<finalizer_policy>( 11, 15, pending_finalizers ) };
      bsp->strong_digest = strong_digest;
      bsp->aggregating_qc = aggregating_qc_t{ bsp->active_finalizer_policy, bsp->pending_finalizer_policy->second };

      bls_private_key new_private_key{ "PVT_BLS_Wfs3KzfTI2P5F85PnoHXLnmYgSbp-XpebIdS6BUCHXOKmKXK" };
      bls_public_key new_public_key{ new_private_key.get_public_key() };

      vote_message vote {block_id, true, new_public_key, active_private_keys[0].sign(strong_digest.to_uint8_span()) };
      BOOST_REQUIRE(bsp->aggregate_vote(0, vote).result == vote_result_t::unknown_public_key);
   }
} FC_LOG_AND_RETHROW();

void do_quorum_test(const std::vector<uint64_t>& weights,
                    uint64_t threshold,
                    bool strong,
                    const std::vector<bool>& to_vote,
                    bool expected_quorum,
                    bool include_pending) {
   digest_type block_id(fc::sha256("0000000000000000000000000000001"));
   digest_type strong_digest(fc::sha256("0000000000000000000000000000002"));
   auto weak_digest(create_weak_digest(strong_digest));

   // initialize a set of private keys
   std::vector<bls_private_key> active_private_keys {
      bls_private_key("PVT_BLS_foNjZTu0k6qM5ftIrqC5G_sim1Rg7wq3cRUaJGvNtm2rM89K"),
      bls_private_key("PVT_BLS_FWK1sk_DJnoxNvUNhwvJAYJFcQAFtt_mCtdQCUPQ4jN1K7eT"),
      bls_private_key("PVT_BLS_tNAkC5MnI-fjHWSX7la1CPC2GIYgzW5TBfuKFPagmwVVsOeW")
   };
   std::vector<bls_private_key> pending_private_keys {
      bls_private_key("PVT_BLS_0d8dsux83r42Qg8CHgAqIuSsn9AV-QdCzx3tPj0K8yOJA_qb"),
      bls_private_key("PVT_BLS_74crPc__6BlpoQGvWjkHmUdzcDKh8QaiN_GtU4SD0QAi4BHY"),
      bls_private_key("PVT_BLS_Wfs3KzfTI2P5F85PnoHXLnmYgSbp-XpebIdS6BUCHXOKmKXK")
   };

   const size_t num_finalizers = active_private_keys.size();

   // construct active finalizers
   std::vector<bls_public_key> active_public_keys(num_finalizers);
   std::vector<finalizer_authority> active_finalizers(num_finalizers);
   for (size_t i = 0; i < num_finalizers; ++i) {
      active_public_keys[i] = active_private_keys[i].get_public_key();
      active_finalizers[i] = finalizer_authority{ "active", weights[i], active_public_keys[i] };
   }

   // construct pending finalizers
   std::vector<bls_public_key> pending_public_keys(num_finalizers);
   std::vector<finalizer_authority> pending_finalizers(num_finalizers);
   for (size_t i = 0; include_pending && i < num_finalizers; ++i) {
      pending_public_keys[i] = pending_private_keys[i].get_public_key();
      pending_finalizers[i] = finalizer_authority{ "pending", weights[i], pending_public_keys[i] };
   }

   block_state_ptr bsp = std::make_shared<block_state>();
   constexpr uint32_t generation = 1;
   bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( generation, threshold, active_finalizers );
   if (include_pending) {
      bsp->pending_finalizer_policy = {bsp->block_num(), std::make_shared<finalizer_policy>( generation, threshold, pending_finalizers )};
   }
   bsp->strong_digest = strong_digest;
   bsp->weak_digest = weak_digest;
   bsp->aggregating_qc = aggregating_qc_t{ bsp->active_finalizer_policy, bsp->pending_finalizer_policy ? bsp->pending_finalizer_policy->second : finalizer_policy_ptr{} };

   for (size_t i = 0; i < num_finalizers; ++i) {
      if( to_vote.at(i) ) {
         auto sig = strong ? active_private_keys[i].sign(strong_digest.to_uint8_span()) : active_private_keys[i].sign(weak_digest);
         vote_message vote{ block_id, strong, active_public_keys[i], sig };
         BOOST_REQUIRE(bsp->aggregate_vote(0, vote).result == vote_result_t::success);
      }
   }
   for (size_t i = 0; i < num_finalizers; ++i) {
      auto vote_index = i + num_finalizers;
      if (vote_index >= to_vote.size())
         break;
      if( to_vote.at(vote_index) ) {
         auto sig = strong ? pending_private_keys[i].sign(strong_digest.to_uint8_span()) : pending_private_keys[i].sign(weak_digest);
         vote_message vote{ block_id, strong, pending_public_keys[i], sig };
         BOOST_REQUIRE(bsp->aggregate_vote(0, vote).result == vote_result_t::success);
      }
   }

   BOOST_REQUIRE_EQUAL(bsp->aggregating_qc.is_quorum_met(), expected_quorum);
}

BOOST_AUTO_TEST_CASE(quorum_test) try {
   std::vector<uint64_t> weights{1, 3, 5};
   constexpr uint64_t threshold = 4;

   { // 1 strong vote, quorum not met
      constexpr bool strong = true;
      std::vector<bool> to_vote{true, false, false}; // finalizer 0 voting
      constexpr bool expected_quorum_met = false;
      bool include_pending = false;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met, include_pending );
      include_pending = true;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met, include_pending );
   }

   { // 2 strong votes, quorum met
      constexpr bool strong = true;
      std::vector<bool> to_vote{true, true, false}; // finalizers 0 and 1 voting
      bool expected_quorum_met = true;
      bool include_pending = false;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met, include_pending );
      // not met with pending
      include_pending = true;
      expected_quorum_met = false;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met, include_pending );
      // vote for pending, quorum now met
      to_vote.push_back(true);
      to_vote.push_back(true);
      to_vote.push_back(false);
      expected_quorum_met = true;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met, include_pending );
   }

   { // 1 strong vote, quorum met
      constexpr bool strong = true;
      std::vector<bool> to_vote{false, false, true}; // finalizer 2 voting
      bool expected_quorum_met = true;
      bool include_pending = false;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met, include_pending);
      // not met with pending
      include_pending = true;
      expected_quorum_met = false;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met, include_pending );
      // vote for pending, quorum now met
      to_vote.push_back(false);
      to_vote.push_back(false);
      to_vote.push_back(true);
      expected_quorum_met = true;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met, include_pending );
   }

   { // 1 weak vote, quorum not met
      constexpr bool strong = false;
      std::vector<bool> to_vote{true, false, false}; // finalizer 0 voting
      constexpr bool expected_quorum_met = false;
      bool include_pending = false;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met, include_pending );
      include_pending = true;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met, include_pending );
   }

   { // 2 weak votes, quorum met
      constexpr bool strong = false;
      std::vector<bool> to_vote{true, true, false}; // finalizers 0 and 1 voting
      bool expected_quorum_met = true;
      bool include_pending = false;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met, include_pending );
      // not met with pending
      include_pending = true;
      expected_quorum_met = false;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met, include_pending );
      // vote for pending, quorum now met
      to_vote.push_back(true);
      to_vote.push_back(true);
      to_vote.push_back(false);
      expected_quorum_met = true;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met, include_pending );
   }

   { // 1 weak vote, quorum met
      constexpr bool strong = false;
      std::vector<bool> to_vote{false, false, true}; // finalizer 2 voting
      bool expected_quorum_met = true;
      bool include_pending = false;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met, include_pending );
      // not met with pending
      include_pending = true;
      expected_quorum_met = false;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met, include_pending );
      // vote for pending, quorum now met
      to_vote.push_back(false);
      to_vote.push_back(false);
      to_vote.push_back(true);
      expected_quorum_met = true;
      do_quorum_test( weights, threshold, strong, to_vote, expected_quorum_met, include_pending );
   }
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(verify_qc_test) try {
   // prepare digests
   digest_type strong_digest(fc::sha256("0000000000000000000000000000002"));
   auto weak_digest(create_weak_digest(strong_digest));

   // initialize a set of private keys
   std::vector<bls_private_key> active_private_keys {
      bls_private_key("PVT_BLS_foNjZTu0k6qM5ftIrqC5G_sim1Rg7wq3cRUaJGvNtm2rM89K"),
      bls_private_key("PVT_BLS_FWK1sk_DJnoxNvUNhwvJAYJFcQAFtt_mCtdQCUPQ4jN1K7eT"),
      bls_private_key("PVT_BLS_tNAkC5MnI-fjHWSX7la1CPC2GIYgzW5TBfuKFPagmwVVsOeW"),
   };
   auto num_finalizers = active_private_keys.size();

   // construct finalizers, with weight 1, 2, 3 respectively
   std::vector<bls_public_key> active_public_keys(num_finalizers);
   std::vector<finalizer_authority> active_finalizers(num_finalizers);
   for (size_t i = 0; i < num_finalizers; ++i) {
      active_public_keys[i] = active_private_keys[i].get_public_key();
      uint64_t weight = i + 1;
      active_finalizers[i] = finalizer_authority{ "test", weight, active_public_keys[i] };
   }

   // construct a test bsp
   block_state_ptr bsp = std::make_shared<block_state>();
   constexpr uint32_t generation = 1;
   constexpr uint64_t threshold = 4; // 2/3 of total weights of 6
   bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( generation, threshold, active_finalizers );
   bsp->strong_digest = strong_digest;
   bsp->weak_digest = weak_digest;

   {  // valid strong QC
      vote_bitset_t strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      strong_votes[2] = 1;  // finalizer 2 voted with weight 3

      bls_signature sig_0 = active_private_keys[0].sign(strong_digest.to_uint8_span());
      bls_signature sig_2 = active_private_keys[2].sign(strong_digest.to_uint8_span());
      bls_aggregate_signature agg_sig;
      agg_sig.aggregate(sig_0);
      agg_sig.aggregate(sig_2);

      // create a qc_sig_t
      qc_sig_t qc_sig{strong_votes, {}, agg_sig};
      qc_t qc{bsp->block_num(), qc_sig, {}};

      BOOST_REQUIRE_NO_THROW( bsp->verify_qc(qc) );
   }

   {  // valid weak QC
      vote_bitset_t strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      bls_signature strong_sig = active_private_keys[0].sign(strong_digest.to_uint8_span());

      vote_bitset_t weak_votes(num_finalizers);
      weak_votes[2] = 1;  // finalizer 2 voted with weight 3
      bls_signature weak_sig = active_private_keys[2].sign(weak_digest);

      bls_aggregate_signature agg_sig;
      agg_sig.aggregate(strong_sig);
      agg_sig.aggregate(weak_sig);

      qc_sig_t qc_sig(strong_votes, weak_votes, agg_sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};
      BOOST_REQUIRE_NO_THROW( bsp->verify_qc(qc) );
   }

   {  // valid strong QC signed by all finalizers
      vote_bitset_t strong_votes(num_finalizers);
      std::vector<bls_signature> sigs(num_finalizers);
      bls_aggregate_signature agg_sig;

      for (auto i = 0u; i < num_finalizers; ++i) {
         strong_votes[i] = 1;
         sigs[i] = active_private_keys[i].sign(strong_digest.to_uint8_span());
         agg_sig.aggregate(sigs[i]);
      }

      // create a qc_sig_t
      qc_sig_t qc_sig(strong_votes, {}, agg_sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};

      BOOST_REQUIRE_NO_THROW( bsp->verify_qc(qc) );
   }

   {  // valid weak QC signed by all finalizers
      vote_bitset_t weak_votes(num_finalizers);
      std::vector<bls_signature> sigs(num_finalizers);
      bls_aggregate_signature agg_sig;

      for (auto i = 0u; i < num_finalizers; ++i) {
         weak_votes[i] = 1;
         sigs[i] = active_private_keys[i].sign(weak_digest);
         agg_sig.aggregate(sigs[i]);
      }

      // create a qc_sig_t
      qc_sig_t qc_sig({}, weak_votes, agg_sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};

      BOOST_REQUIRE_NO_THROW( bsp->verify_qc(qc) );
   }

   {  // strong QC quorum not met
      vote_bitset_t strong_votes(num_finalizers);
      strong_votes[2] = 1;  // finalizer 2 voted with weight 3 (threshold is 4)

      bls_aggregate_signature agg_sig;
      bls_signature sig_2 = active_private_keys[2].sign(strong_digest.to_uint8_span());
      agg_sig.aggregate(sig_2);

      // create a qc_sig_t
      qc_sig_t qc_sig(strong_votes, {}, agg_sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc, eosio::testing::fc_exception_message_starts_with("strong quorum is not met") );
   }

   {  // weak QC quorum not met
      vote_bitset_t weak_votes(num_finalizers);
      weak_votes[2] = 1;  // finalizer 2 voted with weight 3 (threshold is 4)

      bls_aggregate_signature agg_sig;
      bls_signature sig_2 = active_private_keys[2].sign(weak_digest);
      agg_sig.aggregate(sig_2);

      // create a qc_sig_t
      qc_sig_t qc_sig({}, weak_votes, agg_sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc, eosio::testing::fc_exception_message_starts_with("weak quorum is not met") );
   }

   {  // strong QC bitset size does not match number of finalizers in the policy

      // construct vote bitset with a size greater than num_finalizers
      vote_bitset_t strong_votes(num_finalizers + 1);

      // vote by finalizer 0
      strong_votes[0] = 1;

      // aggregate votes
      bls_aggregate_signature agg_sig;
      bls_signature sig = active_private_keys[0].sign(strong_digest.to_uint8_span());
      agg_sig.aggregate(sig);

      // create a qc_sig_t
      qc_sig_t qc_sig(strong_votes, {}, agg_sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc, eosio::testing::fc_exception_message_starts_with("vote bitset size is not the same as the number of finalizers") );
   }

   {  // weak QC bitset size does not match number of finalizers in the policy

      // construct vote bitset with a size less than num_finalizers
      vote_bitset_t weak_votes(num_finalizers - 1);

      // vote by finalizer 0
      weak_votes[0] = 1;

      // aggregate votes
      bls_aggregate_signature agg_sig;
      bls_signature sig = active_private_keys[0].sign(weak_digest);
      agg_sig.aggregate(sig);

      // create a qc_sig_t
      qc_sig_t qc_sig({}, weak_votes, agg_sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc, eosio::testing::fc_exception_message_starts_with("vote bitset size is not the same as the number of finalizers") );
   }

   {  // strong QC with a wrong signing private key
      vote_bitset_t strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      strong_votes[2] = 1;  // finalizer 2 voted with weight 3

      bls_signature sig_0 = active_private_keys[0].sign(strong_digest.to_uint8_span());
      bls_signature sig_2 = active_private_keys[1].sign(strong_digest.to_uint8_span()); // signed by finalizer 1 which is not set in strong_votes
      bls_aggregate_signature sig;
      sig.aggregate(sig_0);
      sig.aggregate(sig_2);

      // create a qc_sig_t
      qc_sig_t qc_sig(strong_votes, {}, sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc_signature, eosio::testing::fc_exception_message_is("qc signature validation failed") );
   }

   {  // strong QC with a wrong digest
      vote_bitset_t strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      strong_votes[2] = 1;  // finalizer 2 voted with weight 3

      bls_signature sig_0 = active_private_keys[0].sign(weak_digest); // should have used strong digest
      bls_signature sig_2 = active_private_keys[2].sign(strong_digest.to_uint8_span());
      bls_aggregate_signature sig;
      sig.aggregate(sig_0);
      sig.aggregate(sig_2);

      // create a qc_sig_t
      qc_sig_t qc_sig(strong_votes, {}, sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc_signature, eosio::testing::fc_exception_message_is("qc signature validation failed") );
   }

   {  // weak QC with a wrong signing private key
      vote_bitset_t strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      bls_signature strong_sig = active_private_keys[0].sign(strong_digest.to_uint8_span());

      vote_bitset_t weak_votes(num_finalizers);
      weak_votes[2] = 1;  // finalizer 2 voted with weight 3
      bls_signature weak_sig = active_private_keys[1].sign(weak_digest); // wrong key

      bls_aggregate_signature sig;
      sig.aggregate(strong_sig);
      sig.aggregate(weak_sig);

      qc_sig_t qc_sig(strong_votes, weak_votes, sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};
      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc_signature, eosio::testing::fc_exception_message_is("qc signature validation failed") );
   }

   {  // weak QC with a wrong digest
      vote_bitset_t strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      bls_signature strong_sig = active_private_keys[0].sign(weak_digest); // wrong digest

      vote_bitset_t weak_votes(num_finalizers);
      weak_votes[2] = 1;  // finalizer 2 voted with weight 3
      bls_signature weak_sig = active_private_keys[2].sign(weak_digest);

      bls_aggregate_signature sig;
      sig.aggregate(strong_sig);
      sig.aggregate(weak_sig);

      qc_sig_t qc_sig(strong_votes, weak_votes, sig);
      qc_t qc{bsp->block_num(), qc_sig, {}};
      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc_signature, eosio::testing::fc_exception_message_is("qc signature validation failed") );
   }
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(verify_qc_test_with_pending) try {
   // prepare digests
   digest_type strong_digest(fc::sha256("0000000000000000000000000000002"));
   auto weak_digest(create_weak_digest(strong_digest));

   // initialize a set of private keys
   std::vector<bls_private_key> active_private_keys {
      bls_private_key("PVT_BLS_foNjZTu0k6qM5ftIrqC5G_sim1Rg7wq3cRUaJGvNtm2rM89K"),
      bls_private_key("PVT_BLS_FWK1sk_DJnoxNvUNhwvJAYJFcQAFtt_mCtdQCUPQ4jN1K7eT"),
      bls_private_key("PVT_BLS_tNAkC5MnI-fjHWSX7la1CPC2GIYgzW5TBfuKFPagmwVVsOeW"),
   };
   auto num_finalizers = active_private_keys.size();

   // construct active finalizers, with weight 1, 2, 3 respectively
   std::vector<bls_public_key> active_public_keys(num_finalizers);
   std::vector<finalizer_authority> active_finalizers(num_finalizers);
   for (size_t i = 0; i < num_finalizers; ++i) {
      active_public_keys[i] = active_private_keys[i].get_public_key();
      uint64_t weight = i + 1;
      active_finalizers[i] = finalizer_authority{ "test", weight, active_public_keys[i] };
   }

   std::vector<bls_private_key> pending_private_keys {
      bls_private_key("PVT_BLS_0d8dsux83r42Qg8CHgAqIuSsn9AV-QdCzx3tPj0K8yOJA_qb"),
      bls_private_key("PVT_BLS_74crPc__6BlpoQGvWjkHmUdzcDKh8QaiN_GtU4SD0QAi4BHY"),
      bls_private_key("PVT_BLS_Wfs3KzfTI2P5F85PnoHXLnmYgSbp-XpebIdS6BUCHXOKmKXK"),
   };

   // construct pending finalizers, with weight 1, 2, 3 respectively
   std::vector<bls_public_key> pending_public_keys(num_finalizers);
   std::vector<finalizer_authority> pending_finalizers(num_finalizers);
   for (size_t i = 0; i < num_finalizers; ++i) {
      pending_public_keys[i] = pending_private_keys[i].get_public_key();
      uint64_t weight = i + 1;
      pending_finalizers[i] = finalizer_authority{ "test", weight, pending_public_keys[i] };
   }

   // construct a test bsp
   block_state_ptr bsp = std::make_shared<block_state>();
   constexpr uint32_t generation = 1;
   constexpr uint64_t threshold = 4; // 2/3 of total weights of 6
   bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( generation, threshold, active_finalizers );
   bsp->pending_finalizer_policy = { bsp->block_num(), std::make_shared<finalizer_policy>( generation+1, threshold, pending_finalizers ) };
   bsp->strong_digest = strong_digest;
   bsp->weak_digest = weak_digest;

   {  // valid strong QC
      vote_bitset_t strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      strong_votes[2] = 1;  // finalizer 2 voted with weight 3

      bls_aggregate_signature active_agg_sig;
      active_agg_sig.aggregate(active_private_keys[0].sign(strong_digest.to_uint8_span()));
      active_agg_sig.aggregate(active_private_keys[2].sign(strong_digest.to_uint8_span()));

      bls_aggregate_signature pending_agg_sig;
      pending_agg_sig.aggregate(pending_private_keys[0].sign(strong_digest.to_uint8_span()));
      pending_agg_sig.aggregate(pending_private_keys[2].sign(strong_digest.to_uint8_span()));

      // create qc_sig_t
      qc_sig_t active_qc_sig{strong_votes, {}, active_agg_sig};
      qc_sig_t pending_qc_sig{strong_votes, {}, pending_agg_sig};
      qc_t qc{bsp->block_num(), active_qc_sig, pending_qc_sig};
      bsp->verify_qc(qc);
      BOOST_CHECK_NO_THROW( bsp->verify_qc(qc) );
   }

   {  // valid weak QC
      vote_bitset_t strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      bls_signature active_strong_sig = active_private_keys[0].sign(strong_digest.to_uint8_span());
      bls_signature pending_strong_sig = pending_private_keys[0].sign(strong_digest.to_uint8_span());

      vote_bitset_t weak_votes(num_finalizers);
      weak_votes[2] = 1;  // finalizer 2 voted with weight 3
      bls_signature active_weak_sig = active_private_keys[2].sign(weak_digest);
      bls_signature pending_weak_sig = pending_private_keys[2].sign(weak_digest);

      bls_aggregate_signature active_agg_sig;
      active_agg_sig.aggregate(active_strong_sig);
      active_agg_sig.aggregate(active_weak_sig);
      bls_aggregate_signature pending_agg_sig;
      pending_agg_sig.aggregate(pending_strong_sig);
      pending_agg_sig.aggregate(pending_weak_sig);

      qc_sig_t active_qc_sig(strong_votes, weak_votes, active_agg_sig);
      qc_sig_t pending_qc_sig(strong_votes, weak_votes, pending_agg_sig);
      qc_t qc{bsp->block_num(), active_qc_sig, pending_qc_sig};
      BOOST_CHECK_NO_THROW( bsp->verify_qc(qc) );
   }

   {  // valid strong QC signed by all finalizers
      vote_bitset_t strong_votes(num_finalizers);
      bls_aggregate_signature active_agg_sig;
      bls_aggregate_signature pending_agg_sig;

      for (auto i = 0u; i < num_finalizers; ++i) {
         strong_votes[i] = 1;
         active_agg_sig.aggregate(active_private_keys[i].sign(strong_digest.to_uint8_span()));
         pending_agg_sig.aggregate(pending_private_keys[i].sign(strong_digest.to_uint8_span()));
      }

      // create qc_sig_t
      qc_sig_t active_qc_sig(strong_votes, {}, active_agg_sig);
      qc_sig_t pending_qc_sig(strong_votes, {}, pending_agg_sig);
      qc_t qc{bsp->block_num(), active_qc_sig, pending_qc_sig};

      BOOST_CHECK_NO_THROW( bsp->verify_qc(qc) );
   }

   {  // valid weak QC signed by all finalizers
      vote_bitset_t weak_votes(num_finalizers);
      bls_aggregate_signature active_agg_sig;
      bls_aggregate_signature pending_agg_sig;

      for (auto i = 0u; i < num_finalizers; ++i) {
         weak_votes[i] = 1;
         active_agg_sig.aggregate(active_private_keys[i].sign(weak_digest));
         pending_agg_sig.aggregate(pending_private_keys[i].sign(weak_digest));
      }

      // create qc_sig_t
      qc_sig_t active_qc_sig({}, weak_votes, active_agg_sig);
      qc_sig_t pending_qc_sig({}, weak_votes, pending_agg_sig);
      qc_t qc{bsp->block_num(), active_qc_sig, pending_qc_sig};

      BOOST_CHECK_NO_THROW( bsp->verify_qc(qc) );
   }

   {  // strong QC quorum not met
      vote_bitset_t strong_votes(num_finalizers);
      strong_votes[2] = 1;  // finalizer 2 voted with weight 3 (threshold is 4)

      bls_aggregate_signature active_agg_sig;
      active_agg_sig.aggregate(active_private_keys[2].sign(strong_digest.to_uint8_span()));
      bls_aggregate_signature pending_agg_sig;
      pending_agg_sig.aggregate(pending_private_keys[2].sign(strong_digest.to_uint8_span()));

      // create qc_sig_t
      qc_sig_t active_qc_sig(strong_votes, {}, active_agg_sig);
      qc_sig_t pending_qc_sig(strong_votes, {}, pending_agg_sig);
      qc_t qc{bsp->block_num(), active_qc_sig, pending_qc_sig};

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc, eosio::testing::fc_exception_message_starts_with("strong quorum is not met") );
   }

   {  // weak QC quorum not met
      vote_bitset_t weak_votes(num_finalizers);
      weak_votes[2] = 1;  // finalizer 2 voted with weight 3 (threshold is 4)

      bls_aggregate_signature active_agg_sig;
      active_agg_sig.aggregate(active_private_keys[2].sign(weak_digest));
      bls_aggregate_signature pending_agg_sig;
      pending_agg_sig.aggregate(pending_private_keys[2].sign(weak_digest));

      // create qc_sig_t
      qc_sig_t active_qc_sig({}, weak_votes, active_agg_sig);
      qc_sig_t pending_qc_sig({}, weak_votes, pending_agg_sig);
      qc_t qc{bsp->block_num(), active_qc_sig, pending_qc_sig};

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc, eosio::testing::fc_exception_message_starts_with("weak quorum is not met") );
   }

   {  // strong QC bitset size does not match number of finalizers in the policy

      // construct vote bitset with a size greater than num_finalizers
      vote_bitset_t strong_votes(num_finalizers + 1);

      // vote by finalizer 0
      strong_votes[0] = 1;

      // aggregate votes
      bls_aggregate_signature active_agg_sig;
      active_agg_sig.aggregate(active_private_keys[0].sign(strong_digest.to_uint8_span()));
      bls_aggregate_signature pending_agg_sig;
      pending_agg_sig.aggregate(pending_private_keys[0].sign(strong_digest.to_uint8_span()));

      // create a qc_sig_t
      qc_sig_t active_qc_sig(strong_votes, {}, active_agg_sig);
      qc_sig_t pending_qc_sig(strong_votes, {}, pending_agg_sig);
      qc_t qc{bsp->block_num(), active_qc_sig, pending_qc_sig};

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc, eosio::testing::fc_exception_message_starts_with("vote bitset size is not the same as the number of finalizers") );
   }

   {  // weak QC bitset size does not match number of finalizers in the policy

      // construct vote bitset with a size less than num_finalizers
      vote_bitset_t weak_votes(num_finalizers - 1);

      // vote by finalizer 0
      weak_votes[0] = 1;

      // aggregate votes
      bls_aggregate_signature active_agg_sig;
      active_agg_sig.aggregate(active_private_keys[0].sign(weak_digest));
      bls_aggregate_signature pending_agg_sig;
      pending_agg_sig.aggregate(pending_private_keys[0].sign(weak_digest));

      // create a qc_sig_t
      qc_sig_t active_qc_sig({}, weak_votes, active_agg_sig);
      qc_sig_t pending_qc_sig({}, weak_votes, pending_agg_sig);
      qc_t qc{bsp->block_num(), active_qc_sig, pending_qc_sig};

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc, eosio::testing::fc_exception_message_starts_with("vote bitset size is not the same as the number of finalizers") );
   }

   {  // strong QC with a wrong signing private key
      vote_bitset_t strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      strong_votes[2] = 1;  // finalizer 2 voted with weight 3

      bls_aggregate_signature active_agg_sig;
      active_agg_sig.aggregate(active_private_keys[0].sign(strong_digest.to_uint8_span()));
      active_agg_sig.aggregate(active_private_keys[1].sign(strong_digest.to_uint8_span())); // signed by finalizer 1 which is not set in strong_votes
      bls_aggregate_signature pending_agg_sig;
      pending_agg_sig.aggregate(pending_private_keys[0].sign(strong_digest.to_uint8_span()));
      pending_agg_sig.aggregate(pending_private_keys[1].sign(strong_digest.to_uint8_span())); // signed by finalizer 1 which is not set in strong_votes

      // create qc_sig_t
      qc_sig_t active_qc_sig(strong_votes, {}, active_agg_sig);
      qc_sig_t pending_qc_sig(strong_votes, {}, pending_agg_sig);
      qc_t qc{bsp->block_num(), active_qc_sig, pending_qc_sig};

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc_signature, eosio::testing::fc_exception_message_is("qc signature validation failed") );
   }

   {  // strong QC with a wrong digest
      vote_bitset_t strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1
      strong_votes[2] = 1;  // finalizer 2 voted with weight 3

      bls_aggregate_signature active_agg_sig;
      active_agg_sig.aggregate(active_private_keys[0].sign(weak_digest)); // should have used strong digest
      active_agg_sig.aggregate(active_private_keys[2].sign(strong_digest.to_uint8_span()));
      bls_aggregate_signature pending_agg_sig;
      pending_agg_sig.aggregate(pending_private_keys[0].sign(weak_digest)); // should have used strong digest
      pending_agg_sig.aggregate(pending_private_keys[2].sign(strong_digest.to_uint8_span()));

      // create qc_sig_t
      qc_sig_t active_qc_sig(strong_votes, {}, active_agg_sig);
      qc_sig_t pending_qc_sig(strong_votes, {}, pending_agg_sig);
      qc_t qc{bsp->block_num(), active_qc_sig, pending_qc_sig};

      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc_signature, eosio::testing::fc_exception_message_is("qc signature validation failed") );
   }

   {  // weak QC with a wrong signing private key
      vote_bitset_t strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1

      vote_bitset_t weak_votes(num_finalizers);
      weak_votes[2] = 1;  // finalizer 2 voted with weight 3

      bls_aggregate_signature active_agg_sig;
      active_agg_sig.aggregate(active_private_keys[0].sign(strong_digest.to_uint8_span()));
      active_agg_sig.aggregate(active_private_keys[1].sign(weak_digest)); // wrong key
      bls_aggregate_signature pending_agg_sig;
      pending_agg_sig.aggregate(pending_private_keys[0].sign(strong_digest.to_uint8_span()));
      pending_agg_sig.aggregate(pending_private_keys[1].sign(weak_digest)); // wrong key

      qc_sig_t active_qc_sig(strong_votes, weak_votes, active_agg_sig);
      qc_sig_t pending_qc_sig(strong_votes, weak_votes, pending_agg_sig);
      qc_t qc{bsp->block_num(), active_qc_sig, pending_qc_sig};
      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc_signature, eosio::testing::fc_exception_message_is("qc signature validation failed") );
   }

   {  // weak QC with a wrong digest
      vote_bitset_t strong_votes(num_finalizers);
      strong_votes[0] = 1;  // finalizer 0 voted with weight 1

      vote_bitset_t weak_votes(num_finalizers);
      weak_votes[2] = 1;  // finalizer 2 voted with weight 3

      bls_aggregate_signature active_agg_sig;
      active_agg_sig.aggregate(active_private_keys[0].sign(weak_digest)); // wrong digest
      active_agg_sig.aggregate(active_private_keys[2].sign(weak_digest));
      bls_aggregate_signature pending_agg_sig;
      pending_agg_sig.aggregate(pending_private_keys[0].sign(weak_digest)); // wrong digest
      pending_agg_sig.aggregate(pending_private_keys[2].sign(weak_digest));

      qc_sig_t active_qc_sig(strong_votes, weak_votes, active_agg_sig);
      qc_sig_t pending_qc_sig(strong_votes, weak_votes, active_agg_sig);
      qc_t qc{bsp->block_num(), active_qc_sig, pending_qc_sig};
      BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc_signature, eosio::testing::fc_exception_message_is("qc signature validation failed") );
   }
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(verify_qc_dual_finalizers) try {
   // prepare digests
   digest_type strong_digest(fc::sha256("0000000000000000000000000000002"));
   auto weak_digest(create_weak_digest(strong_digest));

   // initialize a set of private keys
   std::vector<bls_private_key> active_private_keys {
      bls_private_key("PVT_BLS_tNAkC5MnI-fjHWSX7la1CPC2GIYgzW5TBfuKFPagmwVVsOeW"),
      bls_private_key("PVT_BLS_FWK1sk_DJnoxNvUNhwvJAYJFcQAFtt_mCtdQCUPQ4jN1K7eT"),
      bls_private_key("PVT_BLS_foNjZTu0k6qM5ftIrqC5G_sim1Rg7wq3cRUaJGvNtm2rM89K"),
   };
   auto num_finalizers = active_private_keys.size();

   // construct active finalizers, with weight 1, 2, 3 respectively
   std::vector<bls_public_key> active_public_keys(num_finalizers);
   std::vector<finalizer_authority> active_finalizers(num_finalizers);
   for (size_t i = 0; i < num_finalizers; ++i) {
      active_public_keys[i] = active_private_keys[i].get_public_key();
      uint64_t weight = (i < num_finalizers - 1) ? i + 1 : 9; // intentionally make the last one big to meet quorum in tests
      active_finalizers[i] = finalizer_authority{ "test", weight, active_public_keys[i] };
   }

   // create a dual finalizer by using the same key PVT_BLS_tNAkC5MnI-fjHWSX7la1CPC2GIYgzW5TBfuKFPagmwVVsOeW
   // This makes active finalizer 0 and pending finalizer 1 dual finalizers
   std::vector<bls_private_key> pending_private_keys {
      bls_private_key("PVT_BLS_0d8dsux83r42Qg8CHgAqIuSsn9AV-QdCzx3tPj0K8yOJA_qb"),
      bls_private_key("PVT_BLS_tNAkC5MnI-fjHWSX7la1CPC2GIYgzW5TBfuKFPagmwVVsOeW"), // dual finalizer
      bls_private_key("PVT_BLS_Wfs3KzfTI2P5F85PnoHXLnmYgSbp-XpebIdS6BUCHXOKmKXK"),
   };

   // construct pending finalizers, with weight 1, 2, 3 respectively
   std::vector<bls_public_key> pending_public_keys(num_finalizers);
   std::vector<finalizer_authority> pending_finalizers(num_finalizers);
   for (size_t i = 0; i < num_finalizers; ++i) {
      pending_public_keys[i] = pending_private_keys[i].get_public_key();
      uint64_t weight = (i < num_finalizers - 1) ? i + 1 : 9; // intentionally make the last one big to meet quorum in tests
      pending_finalizers[i] = finalizer_authority{ "test", weight, pending_public_keys[i] };
   }

   // construct a test bsp
   block_state_ptr bsp = std::make_shared<block_state>();
   constexpr uint32_t generation = 1;
   constexpr uint64_t threshold = 8; // 2/3 of total weights of 12
   bsp->active_finalizer_policy = std::make_shared<finalizer_policy>( generation, threshold, active_finalizers );
   bsp->pending_finalizer_policy = { bsp->block_num(), std::make_shared<finalizer_policy>( generation+1, threshold, pending_finalizers ) };
   bsp->strong_digest = strong_digest;
   bsp->weak_digest = weak_digest;

   auto vote_same_test = [&](bool expected_same,
                             std::optional<bool> vote_strong_on_active,
                             std::optional<bool> vote_weak_on_active,
                             std::optional<bool> vote_strong_on_pending,
                             std::optional<bool> vote_weak_on_pending)
   {
      // Active finalizer 0 and pending finalizer 1 are configured as
      // the dual finalizers.

      std::optional<vote_bitset_t> active_strong_votes;
      std::optional<vote_bitset_t> active_weak_votes;
      std::optional<vote_bitset_t> pending_strong_votes;
      std::optional<vote_bitset_t> pending_weak_votes;

      bls_aggregate_signature active_agg_sig;
      bls_aggregate_signature pending_agg_sig;

      vote_bitset_t init_votes(num_finalizers);
      init_votes[2] = 1; // for meeting quorum

      if (vote_strong_on_active) {
         active_strong_votes = init_votes;
         if (*vote_strong_on_active) {
            (*active_strong_votes)[0] = 1;  // dual finalizer votes
            active_agg_sig.aggregate(active_private_keys[0].sign(strong_digest.to_uint8_span()));
         }
         active_agg_sig.aggregate(active_private_keys[2].sign(strong_digest.to_uint8_span()));
      }

      if (vote_weak_on_active) {
         active_weak_votes = init_votes;
         if (*vote_weak_on_active) {
            (*active_weak_votes)[0] = 1;  // dual finalizer votes
            active_agg_sig.aggregate(active_private_keys[0].sign(weak_digest));
         }
         active_agg_sig.aggregate(active_private_keys[2].sign(weak_digest));
      }

      if (vote_strong_on_pending) {
         pending_strong_votes = init_votes;
         if (*vote_strong_on_pending) {
            (*pending_strong_votes)[1] = 1;  // dual finalizer votes with weight 1
            pending_agg_sig.aggregate(pending_private_keys[1].sign(strong_digest.to_uint8_span()));
         }
         pending_agg_sig.aggregate(pending_private_keys[2].sign(strong_digest.to_uint8_span()));
      }

      if (vote_weak_on_pending) {
         pending_weak_votes = init_votes;
         if (*vote_weak_on_pending) {
            (*pending_weak_votes)[1] = 1;  // dual finalizer votes with weight 1
            pending_agg_sig.aggregate(pending_private_keys[1].sign(weak_digest));
         }
         pending_agg_sig.aggregate(pending_private_keys[2].sign(weak_digest));
      }

      // create qc_sig_t
      qc_sig_t active_qc_sig{active_strong_votes, active_weak_votes, active_agg_sig};
      qc_sig_t pending_qc_sig{pending_strong_votes, pending_weak_votes, pending_agg_sig};
      qc_t qc{bsp->block_num(), active_qc_sig, pending_qc_sig};

      if (expected_same) {
         BOOST_CHECK_NO_THROW( bsp->verify_qc(qc) );
      } else {
         BOOST_CHECK_EXCEPTION( bsp->verify_qc(qc), invalid_qc, eosio::testing::fc_exception_message_contains("does not vote the same on active and pending policies") );
      }
   };

   // dual finalizers vote the same on strong
   vote_same_test(true /*expected same*/, true /*vote_strong_on_active*/, {}, true /*vote_strong_on_pending*/, {});
   vote_same_test(true /*expected same*/, false /*vote_strong_on_active*/, {}, false /*vote_strong_on_pending*/, {});

   // dual finalizers vote the same on weak
   vote_same_test(true /*expected same*/, {}, true /*vote_weak_on_active*/, {}, true /*vote_weak_on_pending*/);
   vote_same_test(true /*expected same*/, {}, false /*vote_weak_on_active*/, {}, false /*vote_weak_on_pending*/);

   // dual finalizers do not vote the same on strong
   vote_same_test(false /*expected same*/, true /*vote_strong_on_active*/, {}, false /*vote_strong_on_pending*/, {});
   vote_same_test(false /*expected same*/, false /*vote_strong_on_active*/, {}, true /*vote_strong_on_pending*/, {});

   // dual finalizers do not vote the same on weak
   vote_same_test(false /*expected same*/, {}, true /*vote_weak_on_active*/, {}, false /*vote_weak_on_pending*/);
   vote_same_test(false /*expected same*/, {}, false /*vote_weak_on_active*/, {}, true /*vote_weak_on_pending*/);

   // one dual finalizer votes on strong, the other votes on weak
   vote_same_test(false /*expected same*/, {}, true /*vote_weak_on_active*/, true /* vote_strong_on_pending*/, {});
   vote_same_test(false /*expected same*/, true /*vote_strong_on_active*/, {}, {}, true /*vote_weak_on_pending*/);

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

   // It takes one 2-chain for LIB to advance and 1 LIB proposed finalizer to be promoted to pending.
   for (size_t i=0; i<eosio::testing::num_chains_to_final; ++i) {
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
