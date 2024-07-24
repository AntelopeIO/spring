#include "finality_test_cluster.hpp"
#include <eosio/chain/block_header_state.hpp>

using namespace eosio::chain;
using namespace eosio::testing;

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
   block_header_state next_header_state;

   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 1u}); // lib
   prev.proposed_finalizer_policies.emplace_back(std::make_pair(5u, nullptr)); // proposed_finalizer_policy block_num 5
   prev.pending_finalizer_policy = std::make_pair(4u, nullptr); // pending_finalizer_policy block_num 4

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   // Make sure no changes
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies == prev.proposed_finalizer_policies);
   BOOST_REQUIRE(next_header_state.pending_finalizer_policy == prev.pending_finalizer_policy);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(empty_proposed_or_pending_test) try {
   // When a policy is empty, nothing changes

   block_header_state prev;
   block_header_state next_header_state;

   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 1u}); // lib
   prev.proposed_finalizer_policies.clear(); // empty proposed_finalizer_policies
   prev.pending_finalizer_policy = std::nullopt;  // empty pending_finalizer_policy

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   // Make sure no changes
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies == prev.proposed_finalizer_policies);
   BOOST_REQUIRE(next_header_state.pending_finalizer_policy == prev.pending_finalizer_policy);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(proposed_promotion_no_pending_1_test) try {
   // Pre:  the only proposed finalizer policy's block_num equals to LIB and no pending exists.
   // Post: the proposed finalizer policy becomes pending and `proposed_finalizer_policies` becomes empty.

   block_header_state prev;
   block_header_state next_header_state;

   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 4u}); // lib
   next_header_state.header.previous = id_from_num(6u); // block_num 7, which is previous + 1
   prev.proposed_finalizer_policies.emplace_back(std::make_pair(4u, nullptr)); // block_num 4
   prev.pending_finalizer_policy = std::nullopt; // no existing pending

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.empty());
   BOOST_REQUIRE(next_header_state.pending_finalizer_policy->first == 7u); // block num when becoming active
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(proposed_promotion_no_pending_2_test) try {
   // Pre:  the only proposed finalizer policy's block_num is less than LIB and no pending exists.
   // Post: the proposed finalizer policy becomes pending and `proposed_finalizer_policies` becomes empty.

   block_header_state prev;
   block_header_state next_header_state;

   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 5u}); // lib
   next_header_state.header.previous = id_from_num(7u); // block_num 8, which is previous + 1
   prev.proposed_finalizer_policies.emplace_back(std::make_pair(4u, nullptr)); // block_num 4
   prev.pending_finalizer_policy = std::nullopt;

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.empty());
   BOOST_REQUIRE(next_header_state.pending_finalizer_policy->first == 8u); // block num when becoming active
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(pending_promotion_no_proposed_test) try {
   // Pre:  the pending finalizer policy block_num is less than LIB and no proposed exists.
   // Post: the pending finalizer policy becomes active and `proposed_finalizer_policies` stays empty.

   block_header_state prev;
   block_header_state next_header_state;

   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 5u}); // lib
   std::vector<finalizer_authority> finalizers;
   finalizer_policy_ptr fin_policy = std::make_shared<finalizer_policy>(10u /*generation*/, 15u, finalizers);
   prev.pending_finalizer_policy = std::make_pair(4u, fin_policy); // generation is 10

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.empty());
   BOOST_REQUIRE(!next_header_state.pending_finalizer_policy.has_value()); // No pending
   BOOST_REQUIRE(next_header_state.active_finalizer_policy->generation == 10u); // pending (generation 10) becoming active
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(proposed_to_pending_to_active_test) try {
   // Pre:  both proposed and pending finalizer policies block_nums are less than LIB
   // Post: the proposed promoted to pending, the pending promoted to active.

   block_header_state prev;
   block_header_state next_header_state;

   next_header_state.header.previous = id_from_num(7u); // block_num 8, which is previous + 1
   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 5u}); // lib
   prev.proposed_finalizer_policies.emplace_back(std::make_pair(4u, nullptr)); // becoming pending

   std::vector<finalizer_authority> finalizers;
   finalizer_policy_ptr fin_policy = std::make_shared<finalizer_policy>(10u /*generation*/, 15u, finalizers);
   prev.pending_finalizer_policy = std::make_pair(4u, fin_policy); // becoming active

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.empty());
   BOOST_REQUIRE(next_header_state.pending_finalizer_policy->first == 8u);
   BOOST_REQUIRE(next_header_state.active_finalizer_policy->generation == 10u);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(proposed_promotion_pending_promotion_test) try {
   // Pre:  pending block num less than LIB; 2 proposed block nums less than LIB,
   //       1 proposed block num equal to LIB, 1 greater.
   // Post: 2 proposed garbage collected, 1 becoming pending, 1 kept as proposed;
   //       the pending becomes active.

   block_header_state prev;
   block_header_state next_header_state;

   next_header_state.header.previous = id_from_num(7u); // block_num, which is previous + 1
   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 5u}); // lib

   prev.proposed_finalizer_policies = {
     {3u, nullptr},  // garbage collected
     {4u, nullptr},  // garbage collected
     {5u, nullptr},  // becoming pending
     {6u, nullptr}   // kept as proposed
   };
   std::vector<finalizer_authority> finalizers;
   finalizer_policy_ptr fin_policy = std::make_shared<finalizer_policy>(10u /*generation*/, 15u, finalizers);
   prev.pending_finalizer_policy = std::make_pair(4u, fin_policy); // becoming active

   next_header_state.header.previous = id_from_num(7u); // block_num, which is previous + 1 
   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 5u}); // lib

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.size() == 1u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.front().first == 6u);
   BOOST_REQUIRE(next_header_state.pending_finalizer_policy->first == 8u);
   BOOST_REQUIRE(next_header_state.active_finalizer_policy->generation == 10u);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(proposed_promotion_no_pending_promotion_test) try {
   // Pre:  no existing pending;
   //       2 proposed block nums less than LIB, 1 equal, 1 greater.
   // Post: proposed less than LIB garbage collected, proposed equal to LIB becoming pending,
   //       proposed greater than LIB kept as proposed;

   block_header_state prev;
   block_header_state next_header_state;

   next_header_state.header.previous = id_from_num(7u); // block_num, which is previous + 1
   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 5u}); // lib

   prev.proposed_finalizer_policies = {
     {3u, nullptr},  // garbage collected
     {4u, nullptr},  // garbage collected
     {5u, nullptr},  // becoming pending
     {6u, nullptr}   // kept as proposed
   };

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.size() == 1u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.front().first == 6u);
   BOOST_REQUIRE(next_header_state.pending_finalizer_policy->first == 8u);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(no_proposed_promotion_no_pending_promotion_1_test) try {
   // Pre:  pending block number greater than LIB;
   //       2 proposed block nums less than LIB, 1 equal, 1 greater.
   // Post: Pending not due for promotion;
   //       As no pending slot is open, no proposed is promoted to pending;
   //       proposed less than LIB garbage collected, proposed equal and greater
   //       than LIB kept as proposed;

   block_header_state prev;
   block_header_state next_header_state;

   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 5u}); // lib
   prev.proposed_finalizer_policies = {
     {3u, nullptr},  // garbage collected
     {4u, nullptr},  // garbage collected
     {5u, nullptr},  // kept as proposed
     {6u, nullptr}   // kept as proposed
   };
   prev.pending_finalizer_policy = std::make_pair(7u, nullptr); // does NOT become active as block num (7) greater than lib (5)

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.size() == 2u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.front().first == 5u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.back().first == 6u);
   BOOST_REQUIRE(next_header_state.pending_finalizer_policy->first == 7u); // kept the same
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(no_proposed_promotion_no_pending_promotion_2_test) try {
   // Pre:  no pending exists; all proposed block nums greater than LIB
   // Post: pending slot stays open; proposed not due for promotion

   block_header_state prev;
   block_header_state next_header_state;

   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 5u}); // lib
   prev.proposed_finalizer_policies = {
     {6u, nullptr},  // greater than lib, kept as proposed
     {7u, nullptr},  // kept as proposed
     {8u, nullptr},  // kept as proposed
     {9u, nullptr}   // kept as proposed
   };

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.size() == 4u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies[0].first == 6u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies[1].first == 7u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies[2].first == 8u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies[3].first == 9u);
   BOOST_REQUIRE(!next_header_state.pending_finalizer_policy.has_value()); // No pending
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(no_proposed_policies_left_test) try {
   // Pre:  pending block number less than LIB;
   //       2 proposed block nums less than LIB, 1 equal to LIB
   // Post: pending promoted to active;
   //       proposed less than LIB garbage collected, proposed equal to LIB becoming pending,

   block_header_state prev;
   block_header_state next_header_state;

   next_header_state.header.previous = id_from_num(7u); // block_num, which is previous + 1
   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 5u}); // lib

   prev.proposed_finalizer_policies = {
     {3u, nullptr},  // garbage collected
     {4u, nullptr},  // garbage collected
     {5u, nullptr},  // becoming pending
   };
   std::vector<finalizer_authority> finalizers;
   finalizer_policy_ptr fin_policy = std::make_shared<finalizer_policy>(10u /*generation*/, 15u, finalizers);
   prev.pending_finalizer_policy = std::make_pair(4u, fin_policy); // becoming active


   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.size() == 0u);
   BOOST_REQUIRE(next_header_state.pending_finalizer_policy->first == 8u);
   BOOST_REQUIRE(next_header_state.active_finalizer_policy->generation == 10u);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(pending_promoted_proposed_not_promoted_test) try {
   // Pre:  pending block number less than LIB; all proposed block nums greater than LIB
   // Post: pending promoted to active;
   //       all proposed stay as proposed.

   block_header_state prev;
   block_header_state next_header_state;

   next_header_state.header.previous = id_from_num(7u); // block_num, which is previous + 1
   next_header_state.core.links.emplace_back(qc_link{.target_block_num = 5u}); // lib
   // Pending promotes to active, no proposed due for promotion: exiting proposed kept
   prev.proposed_finalizer_policies = {
     {6u, nullptr},  // kept as proposed
     {7u, nullptr},  // kept as proposed
     {8u, nullptr},  // kept as proposed
     {9u, nullptr}   // kept as proposed
   };

   std::vector<finalizer_authority> finalizers;
   finalizer_policy_ptr fin_policy = std::make_shared<finalizer_policy>(10u /*generation*/, 15u, finalizers);
   prev.pending_finalizer_policy = std::make_pair(4u, fin_policy); // becoming active

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies.size() == 4u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies[0].first == 6u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies[1].first == 7u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies[2].first == 8u);
   BOOST_REQUIRE(next_header_state.proposed_finalizer_policies[3].first == 9u);

   BOOST_REQUIRE(!next_header_state.pending_finalizer_policy.has_value());
   BOOST_REQUIRE(next_header_state.active_finalizer_policy->generation == 10u);
} FC_LOG_AND_RETHROW();

// finalizer_policies_change_edge_case_strong_qc_test and
// finalizer_policies_change_edge_case_weak_qc_test are for demonstrating
// the finalizer policy change edge cases have been resolved
// (https://github.com/AntelopeIO/spring/issues/292).
//
// Denote active finalizer policy as A, and two proposed finalizer policies as B and C.
// In the previous implementation:
//   * in the branch in which the strong QC claim was made,
//     B is immediately promoted to the active finalizer policy, and then B
//     votes for enough blocks to advance finality to the first block of that
//     fork past the fork point.
//   * At the same time, in the branch in which the weak QC claim was made,
//     B is never promoted to the active finalizer policy. Instead a couple of
//     blocks in which A is the finalizer policy are produced and voted on, which
//     then allows the block after that to promote C to the active finalizer
//     policy. Then C can vote on enough new blocks after that to advance finality
//     to a block in that branch which has C as an active finalizer policy.
//
// finalizer_policies_change_edge_case_strong_qc_test simulates the case of strong QC claim,
// and finalizer_policies_change_edge_case_strong_qc_test the case of weak QC claim.
// Both verifies the same sequence of finalizer policy promotions.
//
// Run under the previous implementation, finalizer_policies_change_edge_case_weak_qc_test
// fails as it skips B.
//
// Run under new implementation, finalizer_policies_change_edge_case_strong_qc_test
// and finalizer_policies_change_edge_case_weak_qc_test succeed,
// and both show the same sequence of finalizer policy promotions.
//
BOOST_FIXTURE_TEST_CASE(finalizer_policies_change_edge_case_strong_qc_test, finality_test_cluster<4>) try {
   // The test cluster consists of 4 nodes. node0 is both a producer and a finalizer.
   // node1, node2, and node3 are only finalizers.
   // The cluster has transitioned to Savanna after startup.

   // fin_policy_0 is the active finalizer policy
   BOOST_REQUIRE(fin_policy_0);

   // fin_policy_indices_0 is the set of indices used in active finalizer policy
   // to indicate which key of a node is used in the policy
   auto key_indices = fin_policy_indices_0;
   BOOST_REQUIRE(key_indices[0] == 0u);  // index 0 for node0 was used in active policy

   constexpr size_t node1_index = 1;
   constexpr size_t policy_a_generation = 1;
   constexpr size_t policy_b_generation = 2;
   constexpr size_t policy_c_generation = 3;

   // Propose Policy B by changing the index of the key used by node0 to 1
   key_indices[0] = 1;
   auto policy_b_pubkeys = node0.finkeys.set_finalizer_policy(key_indices).pubkeys;
   produce_and_push_block();

   // all `num_nodes - 1` non-producing nodes vote strong
   process_votes(node1_index, num_nodes - 1); // starting from node1_index
   node0.check_head_finalizer_policy(policy_a_generation, fin_policy_pubkeys_0); // active finalizer policy not changed yet

   // Propose Policy C by changing the index of the key used by node0 to 2
   key_indices[0] = 2;
   auto policy_c_pubkeys = node0.finkeys.set_finalizer_policy(key_indices).pubkeys;

   // We did produce_and_push_block() after proposing Policy B; need `2*num_chains_to_final - 1`
   // more rounds of strong QC to make two 2-chains for Policy B to be activated.
   for (size_t i=0; i<(2*num_chains_to_final - 1); ++i) {
      produce_and_push_block();
      process_votes(node1_index, num_nodes - 1); // all non-producing nodes vote strong
      node0.check_head_finalizer_policy(policy_a_generation, fin_policy_pubkeys_0); // original policy still active
   }

   // we just completed the two 2-chains, so the next block we produce will have
   // Policy B activated
   produce_and_push_block();
   node0.check_head_finalizer_policy(policy_b_generation, policy_b_pubkeys);
   node1.check_head_finalizer_policy(policy_b_generation, policy_b_pubkeys);

   // Under Policy B, LIB advances and Policy is promoted to active.
   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
   node0.check_head_finalizer_policy(policy_c_generation, policy_c_pubkeys);
   node1.check_head_finalizer_policy(policy_c_generation, policy_c_pubkeys);
} FC_LOG_AND_RETHROW();

BOOST_FIXTURE_TEST_CASE(finalizer_policies_change_edge_case_weak_qc_test, finality_test_cluster<4>) try {
   // The test cluster consists of 4 nodes. node0 is both a producer and a finalizer.
   // node1, node2, and node3 are only finalizers.
   // The cluster has transitioned to Savanna after startup.

   // fin_policy_0 is the active finalizer policy
   BOOST_REQUIRE(fin_policy_0);

   // fin_policy_indices_0 is the set of indices used in active finalizer policy
   // to indicate which key of a node is used in the policy
   auto key_indices = fin_policy_indices_0;
   BOOST_REQUIRE(key_indices[0] == 0u);

   constexpr size_t node1_index = 1;
   constexpr size_t node2_index = 2;
   constexpr size_t policy_a_generation = 1;
   constexpr size_t policy_b_generation = 2;
   constexpr size_t policy_c_generation = 3;

   // Propose Policy B by changing the index of the key used by node0 to 1
   key_indices[0] = 1;
   auto policy_b_pubkeys = node0.finkeys.set_finalizer_policy(key_indices).pubkeys;
   produce_and_push_block();

   // all `num_nodes - 1` non-producing nodes vote strong
   process_votes(node1_index, num_nodes - 1);  // starting from node1_index
   node0.check_head_finalizer_policy(policy_a_generation, fin_policy_pubkeys_0); // active finalizer policy not changed yet

   // Propose Policy C by changing the index of the key used by node0 to 2
   key_indices[0] = 2;
   auto policy_c_pubkeys = node0.finkeys.set_finalizer_policy(key_indices).pubkeys;

   // Require 2*num_chains_to_final rounds for a policy to become active;
   // reserve 2 rounds below. That's why produce (2*num_chains_to_final - 2) first.
   for (size_t i=0; i<(2*num_chains_to_final - 2); ++i) {
      produce_and_push_block();
      process_votes(node1_index, num_nodes - 1); // all non-producing nodes vote strong
      node0.check_head_finalizer_policy(policy_a_generation, fin_policy_pubkeys_0); // original policy still active
   }

   produce_and_push_block();
   // make a weak QC
   process_vote(node1_index); // node1 votes strong
   process_vote(node2_index, (size_t)-1 /*not used*/, vote_mode::weak); // node2 votes weak
   // active policy should still stay at Policy A as LIB has not advanced due to weak vote
   node0.check_head_finalizer_policy(policy_a_generation, fin_policy_pubkeys_0);

   // produce 1 round of strong QC block
   produce_and_push_block();
   process_votes(node1_index, num_nodes - 1); // all non-producing nodes vote strong
   node0.check_head_finalizer_policy(policy_a_generation, fin_policy_pubkeys_0); // original policy still active

   // Now a weak-strong chain is formed. LIB advances. Policy B becomes active.
   produce_and_push_block();
   node0.check_head_finalizer_policy(policy_b_generation, policy_b_pubkeys);
   node1.check_head_finalizer_policy(policy_b_generation, policy_b_pubkeys);

   // Under Policy B, LIB advances and Policy C is promoted to active
   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
   node0.check_head_finalizer_policy(policy_c_generation, policy_c_pubkeys);
   node1.check_head_finalizer_policy(policy_c_generation, policy_c_pubkeys);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
