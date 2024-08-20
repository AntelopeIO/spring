#include "savanna_cluster.hpp"

using namespace eosio::chain;
using namespace eosio::testing;

BOOST_AUTO_TEST_SUITE(savanna_proposer_policy)

// ---------------------------------------------------------------------------------------------------
//     Proposer Policy change - check expected delay when policy change initiated on
//                              the first block of a round
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(policy_change_first_block_delay_check, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0];

   auto start_num = A.head().block_num();
   while(start_num % 12 != 0) {
      A.produce_block();
      start_num = A.head().block_num();
   }

   const vector<account_name> producers { "p0"_n, "p1"_n };
   A.create_accounts(producers);
   A.set_producers(producers);   // set new producers and produce blocks until the switch is pending
   auto end_num = A.head().block_num();

   // under Savanna, a new policy becomes active on the first block of a 12 block round after:
   // 1. finishing the current round
   // 2. a full round
   // ----------------------------------------------------------------------------------------
   uint32_t expected_gap = 12 - (start_num % 12);   // 1. finishing the current round
   expected_gap += 12;                              // 2. a full round
   BOOST_REQUIRE_EQUAL(end_num, start_num + expected_gap);
} FC_LOG_AND_RETHROW()


// ---------------------------------------------------------------------------------------------------
//     Proposer Policy change - check expected delay when policy change initiated on
//                              the 6th block of a round
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(policy_change_sixth_block_delay_check, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0];

   auto start_num = A.head().block_num();
   while(start_num % 12 != 6) {
      A.produce_block();
      start_num = A.head().block_num();
   }

   const vector<account_name> producers { "p0"_n, "p1"_n };
   A.create_accounts(producers);
   A.set_producers(producers);   // set new producers and produce blocks until the switch is pending
   auto end_num = A.head().block_num();

   // under Savanna, a new policy becomes active on the first block of a 12 block round after:
   // 1. finishing the current round
   // 2. a full round
   // ----------------------------------------------------------------------------------------
   uint32_t expected_gap = 12 - (start_num % 12);   // 1. finishing the current round
   expected_gap += 12;                              // 2. a full round
   BOOST_REQUIRE_EQUAL(end_num, start_num + expected_gap);
} FC_LOG_AND_RETHROW()


// ---------------------------------------------------------------------------------------------------
//     Proposer Policy change - check expected delay when policy change initiated on
//                              the last block of a round
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(policy_change_last_block_delay_check, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0];

   auto start_num = A.head().block_num();
   while(start_num % 12 != 11) {
      A.produce_block();
      start_num = A.head().block_num();
   }

   const vector<account_name> producers { "p0"_n, "p1"_n };
   A.create_accounts(producers);
   A.set_producers(producers);   // set new producers and produce blocks until the switch is pending
   auto end_num = A.head().block_num();

   // under Savanna, a new policy becomes active on the first block of a 12 block round after:
   // 1. finishing the current round
   // 2. a full round
   // ----------------------------------------------------------------------------------------
   uint32_t expected_gap = 12 - (start_num % 12);   // 1. finishing the current round
   expected_gap += 12;                              // 2. a full round
   BOOST_REQUIRE_EQUAL(end_num, start_num + expected_gap);
} FC_LOG_AND_RETHROW()


// ---------------------------------------------------------------------------------------------------
//     Verify that a proposer policy does not become active when finality has stalled
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(no_proposer_policy_change_without_finality, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0];

   // split network { A, B } and { C, D }
   // Regardless of how many blocks A produces, finality will not advance
   // by more than one (1 QC in flight)
   // -------------------------------------------------------------------
   const std::vector<size_t> partition {2, 3};
   set_partition(partition);
   A.produce_block();                     // take care of the in-flight QC

   const vector<account_name> producers { "p0"_n, "p1"_n };
   auto orig_version = A.control->active_producers().version;
   auto orig_lib_num = A.lib_number;

   A.create_accounts(producers);
   A.tester::set_producers(producers);    // push the action to update the producer schedule

   // produce 24 more blocks. If finality was advancing, the new proposer policy would be active,
   // but with a split network finality will stall, and the new proposer policy should *not* become active.
   // -----------------------------------------------------------------------------------------------------
   A.produce_blocks(24);
   BOOST_REQUIRE_EQUAL(A.lib_number, orig_lib_num);
   BOOST_REQUIRE_EQUAL(A.control->active_producers().version, orig_version);
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------------------
//     Verify that a proposer policy becomes active when finality has advanced enough to make it pending
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(pending_proposer_policy_becomes_active_without_finality, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0];

   const vector<account_name> producers { "p0"_n, "p1"_n };
   auto orig_version = A.control->active_producers().version;

   while (A.head().block_num() % 12 >= 10)
      A.produce_block();                  // make sure we are not on the last two blocks of a round

   A.create_accounts(producers);
   A.tester::set_producers(producers);    // push the action to update the producer schedule
   A.produce_blocks(13);                  // produce 13 blocks which guarantees that the proposer policy is pending

   BOOST_REQUIRE_EQUAL(A.control->active_producers().version, orig_version);
   BOOST_REQUIRE(!!A.control->pending_producers());
   BOOST_REQUIRE_GT(A.control->pending_producers()->version, orig_version);
   auto orig_lib_num = A.lib_number;

   // split network { A, B } and { C, D }
   // Regardless of how many blocks A produces, finality will not advance
   // by more than one (1 QC in flight)
   // -------------------------------------------------------------------
   const std::vector<size_t> partition {2, 3};
   set_partition(partition);

   // produce 24 more blocks. If finality was advancing, the new proposer policy would be active,
   // but with a split network finality will stall, and the new proposer policy should *not* become active.
   // -----------------------------------------------------------------------------------------------------
   A.produce_blocks(24);
   BOOST_REQUIRE_LE(A.lib_number, orig_lib_num + 1);
   BOOST_REQUIRE_GT(A.control->active_producers().version, orig_version);
} FC_LOG_AND_RETHROW()



BOOST_AUTO_TEST_SUITE_END()