#include <eosio/chain/block_header_state.hpp>
#include <eosio/testing/tester.hpp>
#include <boost/test/unit_test.hpp>

using namespace eosio::chain;

// Function to be tested. Implemented in block_header_state.cpp`
namespace eosio::chain {
   void evaluate_finalizer_policies_for_promotion(const block_header_state& prev,
                                                  block_header_state& next_header_state);
}

BOOST_AUTO_TEST_SUITE(block_header_state_tests)

// Build a block ID for a block_number
block_id_type id_from_num(block_num_type block_number) {
   block_id_type id;
   id._hash[0] &= 0xffffffff00000000;
   id._hash[0] += fc::endian_reverse_u32(block_number);
   return id;
}

BOOST_AUTO_TEST_CASE(future_proposed_pending_test) try {
   // When the block associated with a policy is not final (block_num is greater than LIB),
   // keep the policy as is.
   block_header_state prev;
   prev.proposed_finalizer_policies.emplace_back(std::make_pair(5u, nullptr)); // block_num 5
   prev.pending_finalizer_policy = std::make_pair(4u, nullptr); // block_num 4

   block_header_state next_header_state;
   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 1u}); // lib

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   // Make sure no changes
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies == prev.proposed_finalizer_policies);
   BOOST_REQUIRE(next_header_state.pending_finalizer_policy == prev.pending_finalizer_policy);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(empty_proposed_pending_test) try {
   // When a policy is empty, nothing changes
   block_header_state prev;
   prev.proposed_finalizer_policies.clear(); // make it empty
   prev.pending_finalizer_policy = std::nullopt;

   block_header_state next_header_state;
   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 1u}); // lib

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   // Make sure no changes
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies == prev.proposed_finalizer_policies);
   BOOST_REQUIRE(next_header_state.pending_finalizer_policy == prev.pending_finalizer_policy);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(proposed_promotion_no_pending_1_test) try {
   // A proposed policy is due to promote (block_num equal to LIB), no existing pending
   block_header_state prev;
   prev.proposed_finalizer_policies.emplace_back(std::make_pair(4u, nullptr)); // block_num 4
   prev.pending_finalizer_policy = std::nullopt;

   block_header_state next_header_state;
   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 4u}); // lib

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.empty());
   BOOST_REQUIRE(next_header_state.pending_finalizer_policy.has_value());
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(proposed_promotion_no_pending_2_test) try {
   // A proposed policy is due to promote (block_num less than LIB), no existing pending
   block_header_state prev;
   prev.proposed_finalizer_policies.emplace_back(std::make_pair(4u, nullptr)); // block_num 4
   prev.pending_finalizer_policy = std::nullopt;

   block_header_state next_header_state;
   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 5u}); // lib

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.empty());
   BOOST_REQUIRE(next_header_state.pending_finalizer_policy.has_value());
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(pending_promotion_no_proposed_test) try {
   // Pending promotes to active, no existing proposed
   block_header_state prev;

   std::vector<finalizer_authority> finalizers;
   finalizer_policy_ptr fin_policy = std::make_shared<finalizer_policy>(10u /*generation*/, 15u, finalizers);
   prev.pending_finalizer_policy = std::make_pair(4u, fin_policy); // becoming active

   block_header_state next_header_state;
   next_header_state.header.previous = id_from_num(7u); // block_num, which is previous + 1 
   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 5u}); // lib

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.empty());
   BOOST_REQUIRE(!next_header_state.pending_finalizer_policy.has_value()); // pending slot available
   BOOST_REQUIRE(next_header_state.active_finalizer_policy->generation == 10u);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(proposed_to_pending_to_active_test) try {
   // Pending promotes to active, and proposed to pending
   block_header_state prev;
   prev.proposed_finalizer_policies.emplace_back(std::make_pair(4u, nullptr)); // becoming pending

   std::vector<finalizer_authority> finalizers;
   finalizer_policy_ptr fin_policy = std::make_shared<finalizer_policy>(10u /*generation*/, 15u, finalizers);
   prev.pending_finalizer_policy = std::make_pair(4u, fin_policy); // becoming active

   block_header_state next_header_state;
   next_header_state.header.previous = id_from_num(7u); // block_num, which is previous + 1 
   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 5u}); // lib

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.empty());
   BOOST_REQUIRE(next_header_state.pending_finalizer_policy->first == 8u);
   BOOST_REQUIRE(next_header_state.active_finalizer_policy->generation == 10u);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(proposed_promotion_pending_promotion_test) try {
   // Pending promotes to active, highest proposed policy to pending, old proposed policies removed
   block_header_state prev;
   prev.proposed_finalizer_policies = {
     {3u, nullptr},  // garbage collected
     {4u, nullptr},  // garbage collected
     {5u, nullptr},  // becoming pending
     {6u, nullptr}   // kept as proposed
   };

   std::vector<finalizer_authority> finalizers;
   finalizer_policy_ptr fin_policy = std::make_shared<finalizer_policy>(10u /*generation*/, 15u, finalizers);
   prev.pending_finalizer_policy = std::make_pair(4u, fin_policy); // becoming active

   block_header_state next_header_state;
   next_header_state.header.previous = id_from_num(7u); // block_num, which is previous + 1 
   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 5u}); // lib

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.size() == 1u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.front().first == 6u);
   BOOST_REQUIRE(next_header_state.pending_finalizer_policy->first == 8u);
   BOOST_REQUIRE(next_header_state.active_finalizer_policy->generation == 10u);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(proposed_promotion_no_pending_promotion_test) try {
   // No existing pending, highest proposed policy to pending, old proposed policies removed
   block_header_state prev;
   prev.proposed_finalizer_policies = {
     {3u, nullptr},  // garbage collected
     {4u, nullptr},  // garbage collected
     {5u, nullptr},  // becoming pending
     {6u, nullptr}   // kept as proposed
   };

   std::vector<finalizer_authority> finalizers;

   block_header_state next_header_state;
   next_header_state.header.previous = id_from_num(7u); // block_num, which is previous + 1 
   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 5u}); // lib

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.size() == 1u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.front().first == 6u);
   BOOST_REQUIRE(next_header_state.pending_finalizer_policy->first == 8u);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(no_proposed_promotion_no_pending_promotion_1_test) try {
   // Pending not due for promotion, no proposed to be promoted, old proposed policies removed
   block_header_state prev;
   prev.proposed_finalizer_policies = {
     {3u, nullptr},  // garbage collected
     {4u, nullptr},  // garbage collected
     {5u, nullptr},  // kept as  pending
     {6u, nullptr}   // kept as proposed
   };

   std::vector<finalizer_authority> finalizers;
   prev.pending_finalizer_policy = std::make_pair(6u, nullptr); // does NOT become active as block num (6) greater than lib (5)

   block_header_state next_header_state;
   next_header_state.header.previous = id_from_num(7u); // block_num, which is previous + 1 
   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 5u}); // lib

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.size() == 2u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.front().first == 5u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.back().first == 6u);
   BOOST_REQUIRE(next_header_state.pending_finalizer_policy->first == 6u); // kept the same
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(no_proposed_promotion_no_pending_promotion_2_test) try {
   // No existing pending, no proposed policy due for promotion
   block_header_state prev;
   prev.proposed_finalizer_policies = {
     {6u, nullptr},  // greater than lib, kept as proposed
     {7u, nullptr},  // kept as proposed
     {8u, nullptr},  // kept as proposed
     {9u, nullptr}   // kept as proposed
   };

   std::vector<finalizer_authority> finalizers;

   block_header_state next_header_state;
   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 5u}); // lib

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.size() == 4u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies[0].first == 6u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies[1].first == 7u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies[2].first == 8u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies[3].first == 9u);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(no_proposed_policies_left_test) try {
   // Pending promotes to active, highest proposed policy to pending, old proposed policies removed
   block_header_state prev;
   prev.proposed_finalizer_policies = {
     {3u, nullptr},  // garbage collected
     {4u, nullptr},  // garbage collected
     {5u, nullptr},  // becoming pending
   };

   std::vector<finalizer_authority> finalizers;
   finalizer_policy_ptr fin_policy = std::make_shared<finalizer_policy>(10u /*generation*/, 15u, finalizers);
   prev.pending_finalizer_policy = std::make_pair(4u, fin_policy); // becoming active

   block_header_state next_header_state;
   next_header_state.header.previous = id_from_num(7u); // block_num, which is previous + 1 
   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 5u}); // lib

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.size() == 0u);
   BOOST_REQUIRE(next_header_state.pending_finalizer_policy->first == 8u);
   BOOST_REQUIRE(next_header_state.active_finalizer_policy->generation == 10u);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(pending_promoted_proposed_not_promoted_test) try {
   // Pending promotes to active, no proposed due for promotion: exiting proposed kept
   block_header_state prev;
   prev.proposed_finalizer_policies = {
     {6u, nullptr},  // kept as proposed
     {7u, nullptr},  // kept as proposed
     {8u, nullptr},  // kept as proposed
     {9u, nullptr}   // kept as proposed
   };

   std::vector<finalizer_authority> finalizers;
   finalizer_policy_ptr fin_policy = std::make_shared<finalizer_policy>(10u /*generation*/, 15u, finalizers);
   prev.pending_finalizer_policy = std::make_pair(4u, fin_policy); // becoming active

   block_header_state next_header_state;
   next_header_state.header.previous = id_from_num(7u); // block_num, which is previous + 1 
   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 5u}); // lib

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.size() == 4u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies[0].first == 6u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies[1].first == 7u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies[2].first == 8u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies[3].first == 9u);

   BOOST_REQUIRE(!next_header_state.pending_finalizer_policy.has_value());
   BOOST_REQUIRE(next_header_state.active_finalizer_policy->generation == 10u);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
