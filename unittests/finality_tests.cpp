#include "finality_test_cluster.hpp"

using namespace eosio::chain;
using namespace eosio::testing;

/*
 * register test suite `finality_tests`
 */
BOOST_AUTO_TEST_SUITE(finality_tests)

// test set_finalizer host function serialization and tester set_finalizers
BOOST_AUTO_TEST_CASE(initial_set_finalizer_test) { try {
   // Do not transition to Savanna at constrution. Transition explicitly later.
   legacy_validating_tester t;

   // Create finalizer keys
   constexpr size_t num_finalizers = 21;
   finalizer_keys fin_keys(t, num_finalizers, num_finalizers);

   // activate savanna
   fin_keys.set_node_finalizers(0, num_finalizers); // activate `num_finalizers` keys for this node,
                                                    // starting at key index 0.
   fin_keys.set_finalizer_policy(0);                // sets the finalizer_policy using consecutive keys,
                                                    // starting at key index 0.

   // this block contains the header extension for the instant finality, savanna activated when it is LIB
   auto block = t.produce_block();

   std::optional<block_header_extension> ext = block->extract_header_extension(finality_extension::extension_id());
   BOOST_TEST(!!ext);
   std::optional<finalizer_policy_diff> fin_policy_diff = std::get<finality_extension>(*ext).new_finalizer_policy_diff;
   BOOST_TEST(!!fin_policy_diff);
   BOOST_TEST(fin_policy_diff->finalizers_diff.insert_indexes.size() == num_finalizers);
   BOOST_TEST(fin_policy_diff->generation == 1);
   // same as reference-contracts/.../contracts/eosio.system/src/finalizer_key.cpp#L73
   BOOST_TEST(fin_policy_diff->threshold == (num_finalizers * 2) / 3 + 1);
   block_id_type if_genesis_block_id = block->calculate_id();

   for (block_num_type active_block_num = block->block_num(); active_block_num > t.lib_block->block_num(); t.produce_block()) {
      (void)active_block_num; // avoid warning
   };

   // lib_block is IF Genesis Block
   // block is IF Critical Block
   auto fb = t.fetch_block_by_id(t.lib_id);
   BOOST_REQUIRE(!!fb);
   BOOST_TEST(fb->calculate_id() == t.lib_id);
   ext = fb->extract_header_extension(finality_extension::extension_id());
   BOOST_REQUIRE(!!ext);
   BOOST_TEST(if_genesis_block_id == fb->calculate_id());

   auto lib_after_transition = t.lib_block->block_num();
   // block after IF Critical Block is IF Proper Block
   block = t.produce_block();

   // lib must advance after num_chains_to_final blocks
   t.produce_blocks(num_chains_to_final);
   BOOST_CHECK_GT(t.lib_block->block_num(), lib_after_transition);
} FC_LOG_AND_RETHROW() }

void test_finality_transition(const vector<account_name>& accounts,
                              const base_tester::finalizer_policy_input& input,
                              bool lib_advancing_expected) {
   // Do not transition to Savanna at constrution. Transition explicitly later.
   legacy_validating_tester t;

   t.produce_block();

   // Create finalizer accounts
   t.create_accounts(accounts);
   t.produce_block();

   // activate savanna
   t.set_finalizers(input);
   // this block contains the header extension for the instant finality, savanna activated when it is LIB
   auto block = t.produce_block();

   std::optional<block_header_extension> ext = block->extract_header_extension(finality_extension::extension_id());
   BOOST_TEST(!!ext);
   std::optional<finalizer_policy_diff> fin_policy_diff = std::get<finality_extension>(*ext).new_finalizer_policy_diff;
   BOOST_TEST(!!fin_policy_diff);
   BOOST_TEST(fin_policy_diff->finalizers_diff.insert_indexes.size() == accounts.size());
   BOOST_TEST(fin_policy_diff->generation == 1);
   block_id_type if_genesis_block_id = block->calculate_id();

   block_num_type active_block_num = block->block_num();
   while (active_block_num > t.lib_block->block_num()) {
      block = t.produce_block();
   }
   // lib_block is IF Genesis Block
   // block is IF Critical Block
   auto fb = t.fetch_block_by_id(t.lib_id);
   BOOST_REQUIRE(!!fb);
   BOOST_TEST(fb->calculate_id() == t.lib_id);
   ext = fb->extract_header_extension(finality_extension::extension_id());
   BOOST_REQUIRE(!!ext);
   BOOST_TEST(if_genesis_block_id == fb->calculate_id());

   auto lib_after_transition = t.lib_block->block_num();
   // block after IF Critical Block is IF Proper Block
   block = t.produce_block();

   t.produce_blocks(num_chains_to_final);
   if( lib_advancing_expected ) {
      BOOST_CHECK_GT(t.lib_block->block_num(), lib_after_transition);
   } else {
      BOOST_CHECK_EQUAL(t.lib_block->block_num(), lib_after_transition);
   }
}

BOOST_AUTO_TEST_CASE(threshold_equal_to_half_weight_sum_test) { try {
   vector<account_name> account_names = {
      "alice"_n, "bob"_n, "carol"_n
   };

   // threshold set to half of the weight sum of finalizers
   base_tester::finalizer_policy_input policy_input = {
      .finalizers       = { {.name = "alice"_n, .weight = 1},
                            {.name = "bob"_n,   .weight = 2},
                            {.name = "carol"_n, .weight = 3} },
      .threshold        = 3,
      .local_finalizers = {"alice"_n, "bob"_n}
   };

   // threshold must be greater than half of the sum of the weights
   BOOST_REQUIRE_THROW( test_finality_transition(account_names, policy_input, false), eosio_assert_message_exception );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(votes_equal_to_threshold_test) { try {
   vector<account_name> account_names = {
      "alice"_n, "bob"_n, "carol"_n
   };

   base_tester::finalizer_policy_input policy_input = {
      .finalizers       = { {.name = "alice"_n, .weight = 1},
                            {.name = "bob"_n,   .weight = 3},
                            {.name = "carol"_n, .weight = 5} },
      .threshold        = 5,
      .local_finalizers = {"carol"_n}
   };

   // Carol votes with weight 5 and threshold 5
   test_finality_transition(account_names, policy_input, true); // lib_advancing_expected
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(votes_greater_than_threshold_test) { try {
   vector<account_name> account_names = {
      "alice"_n, "bob"_n, "carol"_n
   };

   base_tester::finalizer_policy_input policy_input = {
      .finalizers       = { {.name = "alice"_n, .weight = 1},
                            {.name = "bob"_n,   .weight = 4},
                            {.name = "carol"_n, .weight = 2} },
      .threshold        = 4,
      .local_finalizers = {"alice"_n, "bob"_n}
   };

   // alice and bob vote with weight 5 and threshold 4
   test_finality_transition(account_names, policy_input, true); // lib_advancing_expected
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(votes_less_than_threshold_test) { try {
   vector<account_name> account_names = {
      "alice"_n, "bob"_n, "carol"_n
   };

   base_tester::finalizer_policy_input policy_input = {
      .finalizers       = { {.name = "alice"_n, .weight = 1},
                            {.name = "bob"_n,   .weight = 3},
                            {.name = "carol"_n, .weight = 10} },
      .threshold        = 8,
      .local_finalizers = {"alice"_n, "bob"_n}
   };

   // alice and bob vote with weight 4 but threshold 8. LIB cannot advance
   test_finality_transition(account_names, policy_input, false); // not expecting lib advancing
} FC_LOG_AND_RETHROW() }
// verify LIB advances with a quorum of finalizers voting.
// -------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(quorum_of_votes, finality_test_cluster<4>) { try {
   produce_and_push_block();
   for (auto i = 0; i < 3; ++i) {
      process_votes(1, num_needed_for_quorum);
      produce_and_push_block();

      // when a quorum of nodes vote, LIB should advance
      BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);
   }
} FC_LOG_AND_RETHROW() }

// verify LIB does not advances with finalizers not voting.
// --------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(no_votes, finality_test_cluster<4>) { try {
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);
   produce_and_push_block();
   for (auto i = 0; i < 3; ++i) {
      produce_and_push_block();
      // don't process votes

      // when only node0 votes, LIB shouldn't advance
      BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);
   }
} FC_LOG_AND_RETHROW() }


// verify LIB does not advances when one less than the quorum votes
// ----------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(quorum_minus_one, finality_test_cluster<4>) { try {
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);
   produce_and_push_block();
   for (auto i = 0; i < 3; ++i) {
      produce_and_push_block();
      process_votes(1, num_needed_for_quorum - 1);

      // when one less than required vote, LIB shouldn't advance
      BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);
   }
} FC_LOG_AND_RETHROW() }


// verify LIB advances with all finalizers voting
// ----------------------------------------------
BOOST_FIXTURE_TEST_CASE(all_votes, finality_test_cluster<4>) { try {
   produce_and_push_block();
   for (auto i = 0; i < 3; ++i) {
      process_votes(1, num_nodes - 1);
      produce_and_push_block();

      // when all nodes vote, LIB should advance
      BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);
   }
} FC_LOG_AND_RETHROW() }

// verify LIB advances when votes conflict (strong first and followed by weak)
// ---------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(conflicting_votes_strong_first, finality_test_cluster<4>) { try {
   produce_and_push_block();
   for (auto i = 0; i < 3; ++i) {
      auto next_idx = process_votes(1, num_needed_for_quorum);  // first a quorum of strong votes
      assert(next_idx < num_nodes);
      process_vote(next_idx, -1, vote_mode::weak); // and one weak vote
      produce_and_push_block();

      // when we have a quorum of strong votes, one weak vote should not prevent LIB from advancing
      BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);
   }
} FC_LOG_AND_RETHROW() }

// verify LIB advances when votes conflict (weak first and followed by strong)
// really not significant difference with previous test, just position of weak
// vote in bitset changes.
// ---------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(conflicting_votes_weak_first, finality_test_cluster<4>) { try {
   produce_and_push_block();
   for (auto i = 0; i < 3; ++i) {
      process_vote(1, -1, vote_mode::weak);    // a weak vote on node 1
      process_votes(2, num_needed_for_quorum);           // and a quorum of strong votes
      produce_and_push_block();

      // when we have a quorum of strong votes, one weak vote should not prevent LIB from advancing
      BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);
   }
} FC_LOG_AND_RETHROW() }

// Verify a delayed vote works
// ---------------------------
BOOST_FIXTURE_TEST_CASE(one_delayed_votes, finality_test_cluster<4>) { try {
   // hold the vote for the first block to simulate delay
   produce_and_push_block();
   produce_and_push_block();

   // now node1 to nodeN each have a 2 vote vector
   // vote block 0 (index 0) to make it have a strong QC,
   // prompting LIB advancing on node2
   process_votes(1, num_needed_for_quorum, 0);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);

   // block 1 (index 1) has the same QC claim as block 0. It cannot move LIB
   process_votes(1, num_needed_for_quorum, 1);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);

   // producing, pushing, and voting a new block makes LIB moving
   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// Verify 3 consecutive delayed votes work
// ---------------------------------------
BOOST_FIXTURE_TEST_CASE(three_delayed_votes, finality_test_cluster<4>) { try {
   // produce 4 blocks and hold the votes for the first 3 to simulate delayed votes
   // The 4 blocks have the same QC claim as no QCs are created because quorum was
   // not reached
   for (auto i = 0; i < 4; ++i)
      produce_and_push_block();

   // LIB did not advance
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);

   // vote block 0 (index 0) to make it have a strong QC,
   // prompting LIB advacing on nodes
   process_votes(1, num_needed_for_quorum, 0);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);

   // blocks 1 to 3 have the same QC claim as block 0. They cannot move LIB
   for (auto i=1; i < 4; ++i) {
      process_votes(1, num_needed_for_quorum, i);
      produce_and_push_block();
      BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);
   }

   // Now send votes for the last block that node0 produced (block 8). It will be
   // able to incorporate these votes into a new QC, which will be attached to
   // the next block it produces.
   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// What happens when votes are processed out of order
// --------------------------------------------------
BOOST_FIXTURE_TEST_CASE(out_of_order_votes, finality_test_cluster<4>) { try {
   // produce 3 blocks and hold the votes to simulate delayed votes
   // The 3 blocks have the same QC claim as no QCs are created because missing votes
   for (auto i = 0; i < 3; ++i)
      produce_and_push_block();

   // vote out of the order: the newest to oldest

   // vote block 2 (index 2) to make it have a strong QC,
   // prompting LIB advacing
   process_votes(1, num_needed_for_quorum, 2);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);

   // block 1 (index 1) has the same QC claim as block 2. It will not move LIB
   process_votes(1, num_needed_for_quorum, 1);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);

   // block 0 (index 0) has the same QC claim as block 2. It will not move LIB
   process_votes(1, num_needed_for_quorum, 0);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);

   // producing, pushing, and voting a new block makes LIB moving
   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// Verify a vote which was delayed by a large number of blocks does not cause any issues
// -------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(long_delayed_votes, finality_test_cluster<4>) { try {
   // Produce and push a block, vote on it after a long delay.
   constexpr uint32_t delayed_vote_index = 0;

   produce_and_push_block(); // this is the block we will vote on later
   produce_and_push_block();

   for (auto i = 2; i < 100; ++i) {
      process_votes(1, num_needed_for_quorum);
      produce_and_push_block();
      BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);
   }

   // Late vote does not cause any issues
   BOOST_REQUIRE_NO_THROW(process_votes(1, num_needed_for_quorum, delayed_vote_index));

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// Check that if we never vote on a block, it doesn't cause any problem
// --------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(lost_votes, finality_test_cluster<4>) { try {
   // Produce and push a block, never vote on it to simulate lost.
   // The block contains a strong QC extension for prior block
   auto b1 = produce_and_push_block();
   process_votes(1, num_needed_for_quorum);
   auto b2 = produce_and_push_block(); // this block contains a strong QC for the previous block
   const auto& ext = b2->template extract_extension<quorum_certificate_extension>();
   BOOST_REQUIRE_EQUAL(ext.qc.block_num, b1->block_num());

   // The strong QC extension for prior block makes LIB advance on nodes
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);

   // but don't propagate the votes on b2. Make sure they are lost
   clear_votes_and_reset_lib();

   produce_and_push_block();                // Produce another block
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u); // LIB doesn't advance

   process_votes(1, num_needed_for_quorum); // and propagate the votes for this new block to node0
   produce_and_push_block();

   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes); // vote causes lib to advance

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// One weak vote preventing a strong QC
// ------------------------------------
BOOST_FIXTURE_TEST_CASE(one_weak_vote, finality_test_cluster<4>) { try {
   produce_and_push_block();

   auto next_idx = process_votes(1, num_needed_for_quorum -1); // one less strong vote than needed for quorum
   process_vote(next_idx, -1, vote_mode::weak);   // and one weak vote
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u); // weak QC (1 shy of strong) => LIB does not advance

   process_votes(1, num_needed_for_quorum); // now this provides enough strong votes for quorum
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes); // strong QC => LIB does advance

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// A quorum-1 of weak votes and one strong vote
// --------------------------------------------
BOOST_FIXTURE_TEST_CASE(quorum_minus_one_weak_vote, finality_test_cluster<4>) { try {
   produce_and_push_block();

   process_votes(1, num_needed_for_quorum, -1, vote_mode::weak);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u); // weak QC => LIB does not advance

   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes); // strong QC => LIB does advance

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// A sequence of "weak - strong - weak - strong" QCs
// -------------------------------------------------
BOOST_FIXTURE_TEST_CASE(weak_strong_weak_strong, finality_test_cluster<4>) { try {
   produce_and_push_block();

   process_votes(1, num_needed_for_quorum, -1, vote_mode::weak);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);           // weak QC => LIB does not advance

   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);  // strong QC => LIB does advance

   process_votes(1, num_needed_for_quorum, -1, vote_mode::weak);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);          // weak QC => LIB does not advance

   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes); // strong QC => LIB does advance

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// A sequence of "weak - weak - strong - strong" QCs
// -------------------------------------------------
BOOST_FIXTURE_TEST_CASE(weak_weak_strong_strong, finality_test_cluster<4>) { try {
   produce_and_push_block();

   process_votes(1, num_needed_for_quorum, -1, vote_mode::weak);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);         // weak QC => LIB does not advance

   process_votes(1, num_needed_for_quorum, -1, vote_mode::weak);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);         // weak QC => LIB does not advance

   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes); // strong QC => LIB does advance

   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes); // strong QC => LIB does advance

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }


// Verify a combination of weak, delayed, lost votes still works
// -------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(weak_delayed_lost_vote, finality_test_cluster<4>) { try {
   produce_and_push_block();

   // quorum of weak votes
   process_votes(1, num_needed_for_quorum, -1, vote_mode::weak);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);

   // delay votes at index 1
   constexpr uint32_t delayed_index = 1; 
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);

   // quorum of strong votes
   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);

   // A lost vote
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);

   // The delayed vote arrives, does not advance lib
   process_votes(1, num_needed_for_quorum, delayed_index);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);

   // strong vote advances lib
   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// Verify a combination of delayed, weak, lost votes still work
// -------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(delayed_strong_weak_lost_vote, finality_test_cluster<4>) { try {
   produce_and_push_block();

   // delay votes at index 1
   constexpr uint32_t delayed_index = 0;
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);

   // quorum of strong votes
   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);

   // quorum of weak votes
   process_votes(1, num_needed_for_quorum, -1, vote_mode::weak);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);

   // quorum of strong votes
   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);

   // A lost vote
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);

   // The delayed vote arrives, does not advance lib
   process_votes(1, num_needed_for_quorum, delayed_index);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);

   // strong vote advances lib
   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// verify duplicate votes do not affect LIB advancing
// --------------------------------------------------
BOOST_FIXTURE_TEST_CASE(duplicate_votes, finality_test_cluster<4>) { try {
   produce_and_push_block();

   for (auto i = 0; i < 5; ++i) {
      process_votes(1, num_needed_for_quorum, i, vote_mode::strong);

      // vote again (with duplicate == true) to make it duplicate
      process_votes(1, num_needed_for_quorum, i, vote_mode::strong, true);
      produce_and_push_block();

      // verify duplicate votes do not affect LIB advancing
      BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);
   }

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// verify unknown_proposal votes are handled properly
// --------------------------------------------------
BOOST_FIXTURE_TEST_CASE(unknown_proposal_votes, finality_test_cluster<4>) { try {
   produce_and_push_block();

   // intentionally corrupt block_id in node1's vote (vote index 0)
   node1.corrupt_vote_block_id();

   // process the corrupted vote
   BOOST_REQUIRE_THROW(process_votes(1, 1), fc::exception); // throws as it times out waiting on vote (block id not found)
   process_votes(2, num_needed_for_quorum - 1);

   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);

   node1.restore_to_original_vote(0u);                      // restore node1's vote at index 0 to original vote
   process_votes(1, 1, 0, vote_mode::strong);              // send restored vote to node0
   produce_and_push_block();                               // produce a block so the new QC can propagate
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }


// verify unknown finalizer_key votes are handled properly
// -------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(unknown_finalizer_key_votes, finality_test_cluster<4>) { try {
   // node0 produces a block and pushes to node1
   produce_and_push_block();

   // intentionally corrupt finalizer_key in node1's vote
   node1.corrupt_vote_finalizer_key();

   // process the corrupted vote. LIB should not advance
   process_vote(1, 0);
   BOOST_REQUIRE(process_vote(1, 0) == eosio::chain::vote_result_t::unknown_public_key);

   // restore to original vote
   node1.restore_to_original_vote(0u);

   // process the original vote. LIB should advance
   process_vote(1, 0);

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// verify corrupted signature votes are handled properly
// -----------------------------------------------------
BOOST_FIXTURE_TEST_CASE(corrupted_signature_votes, finality_test_cluster<4>) { try {
   produce_and_push_block();

   // intentionally corrupt signature in node1's vote (vote index 0)
   node1.corrupt_vote_signature();

   // process the corrupted vote
   process_votes(1, 1);
   process_votes(2, num_needed_for_quorum - 1);

   produce_and_push_block();
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0u);           // because of the one corrupted vote, quorum is not reached

   node1.restore_to_original_vote(0u);                 // restore node1's vote at index 0 to original vote
   process_votes(1, 1, 0, vote_mode::strong);         // send restored vote to node0
   produce_and_push_block();                          // produce a block so the new QC can propagate
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);

   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// verify LIB advances after second set_finalizers
// -----------------------------------------------
BOOST_FIXTURE_TEST_CASE(second_set_finalizers, finality_test_cluster<4>) { try {
   produce_and_push_block();
   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();

   // when a quorum of nodes vote, LIB should advance
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);
   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());

   // run a second set_finalizers
   // ---------------------------
   assert(fin_policy_0);        // current finalizer policy from transition to Savanna

   auto indices1 = fin_policy_indices_0;  // start from original set of indices
   assert(indices1[0] == 0u);             // we used index 0 for node0 in original policy
   indices1[0] = 1;                       // update key used for node0 in policy
   auto pubkeys1 = node0.finkeys.set_finalizer_policy(indices1).pubkeys;

   // we need two 2-chains for the new finalizer policy to be activated
   for (size_t i=0; i<(2*num_chains_to_final); ++i) {
      produce_and_push_block();
      process_votes(1, num_nodes - 1);
      node0.check_head_finalizer_policy(1, fin_policy_pubkeys_0); // original policy still active
   }

   // we just completed the two 2-chains, so the next block we produce will have the new finalizer policy activated
   produce_and_push_block();
   node0.check_head_finalizer_policy(2u, pubkeys1);
   node1.check_head_finalizer_policy(2u, pubkeys1);

} FC_LOG_AND_RETHROW() }

// verify issue https://github.com/AntelopeIO/spring/issues/130 is fixed
// ---------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(finality_skip, finality_test_cluster<4>) { try {
   produce_and_push_block();
   process_votes(1, num_needed_for_quorum);
   produce_and_push_block();

   // when a quorum of nodes vote, LIB should advance
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);
   BOOST_REQUIRE(produce_blocks_and_verify_lib_advancing());

   auto add_set_finalizers = [&](size_t start_idx) {
      assert(fin_policy_0);                 // current finalizer policy from transition to Savanna
      auto indices = fin_policy_indices_0;  // start from original set of indices
      assert(indices[0] == 0u);             // we used index 0 for node0 in original policy
      indices[0] = start_idx;               // update key used for node0 in policy
      auto pubkeys = node0.finkeys.set_finalizer_policy(indices).pubkeys;
      produce_and_push_block();
      return pubkeys;
   };

   clear_votes_and_reset_lib();

   // produce num_chains_to_final blocks that will be made final after the three `add_set_finalizers` below
   // ------------------------------------------------------------------------------------
   for (size_t i=0; i<num_chains_to_final; ++i) {
      produce_and_push_block();
      process_votes(1, num_nodes - 1);
   }

   // run three set_finalizers in 2 blocks (assuming 2-chain finality) without voting
   // they will be in `proposed` state with different block numbers.
   // -------------------------------------------------------------
   auto pubkeys1 = add_set_finalizers(1); // will be generation == 2
   auto pubkeys2 = add_set_finalizers(2); // will be generation == 3

   // produce_and_push num_chains_to_final blocks. The last one will make finality skip over the three
   // `add_set_finalizers` blocks above, so that it becomes `pending` on the same block.
   // ---------------------------------------------------------------------------------------
   for (size_t i=0; i<num_chains_to_final; ++i) {
      produce_and_push_block();
      process_votes(1, num_nodes - 1);

      // make sure we don't have duplicate finalizer policies for the same block number
      // in either `proposed` or `pending` state
      // ------------------------------------------------------------------------------
      node0.check_head_finalizer_policy(1u, fin_policy_pubkeys_0);
   }

   // now *only* the second `set_finalizers` should be `pending`, the one with
   // `generation == 3`. The other policy must have been overwritten since it is
   // at the same block.
   //
   // we need another 2-chain to make that block final.
   // -------------------------------------------------------------------------------
   for (size_t i=0; i<num_chains_to_final; ++i) {
      produce_and_push_block();
      process_votes(1, num_nodes - 1);
      node0.check_head_finalizer_policy(1u, fin_policy_pubkeys_0);
   }

   // when we receive the votes of that last block finishing the 2-chain, the active
   // `finalizer_policy` finally changes.
   // ------------------------------------------------------------------------------
   produce_and_push_block();
   process_votes(1, num_nodes - 1);
   node0.check_head_finalizer_policy(3u, pubkeys2);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
