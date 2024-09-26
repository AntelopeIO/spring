#include "savanna_cluster.hpp"

using namespace eosio::chain;
using namespace eosio::testing;

static const uint32_t prod_rep = static_cast<uint32_t>(config::producer_repetitions);

BOOST_AUTO_TEST_SUITE(savanna_proposer_policy_tests)

// ---------------------------------------------------------------------------------------------------
//     Proposer Policy change - check expected delay when policy change initiated on
//                              the first block of a round
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(policy_change_first_block_delay_check, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0];

   while(A.head().timestamp().slot % prod_rep != prod_rep - 1)
      A.produce_block();

   const vector<account_name> producers { "pa"_n, "pb"_n };
   A.create_accounts(producers);
   A.tester::set_producers(producers);              // push the action to update the producer schedule
   auto sb = A.produce_block();                     // produce a block that will include the policy change transaction
   auto orig_producer = sb->producer;               // save producer before transition
   auto start_slot = sb->timestamp.slot;
   BOOST_REQUIRE_EQUAL(start_slot % prod_rep, 0u);  // validate that the policy change occurs on the first block of prod_rep

   A.wait_for_producer(producers);                  // produce blocks until the new schedule will be active on next block produced
   BOOST_REQUIRE_EQUAL(A.head().block()->producer,  // head block should still have been produced using
                       orig_producer);              // the original producer
   sb = A.produce_block();
   bool using_new_sched = std::ranges::find(producers, sb->producer) != producers.end();
   BOOST_REQUIRE(using_new_sched);                 // verify that we have just switched to new schedule
   BOOST_REQUIRE_NE(sb->producer, orig_producer);  // and that the producer has changed
   auto end_slot = sb->timestamp.slot;
   BOOST_REQUIRE_EQUAL(end_slot % prod_rep, 0u);   // validate that the policy change occurs on the first block of prod_rep

   // under Savanna, a new policy becomes active on the first block of a prod_rep block round after:
   // 1. finishing the current round
   // 2. a full round
   // ----------------------------------------------------------------------------------------
   uint32_t expected_gap = prod_rep - (start_slot % prod_rep);   // 1. finishing the current round
   expected_gap += prod_rep;                                     // 2. a full round
   BOOST_REQUIRE_EQUAL(end_slot, start_slot + expected_gap);
} FC_LOG_AND_RETHROW()


// ---------------------------------------------------------------------------------------------------
//     Proposer Policy change - check expected delay when policy change initiated on
//                              the middle block of a round
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(policy_change_sixth_block_delay_check, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0];
   const uint32_t middle = prod_rep / 2;
   static_assert(middle > 0);

   while(A.head().timestamp().slot % prod_rep != middle - 1)
      A.produce_block();

   const vector<account_name> producers { "pa"_n, "pb"_n };
   A.create_accounts(producers);
   A.tester::set_producers(producers);              // push the action to update the producer schedule
   auto sb = A.produce_block();                     // produce a block that will include the policy change transaction
   auto orig_producer = sb->producer;               // save producer before transition
   auto start_slot = sb->timestamp.slot;
   BOOST_REQUIRE_EQUAL(start_slot % prod_rep, middle); // validate that the policy change occurs on the middle block of prod_rep

   A.wait_for_producer(producers);                  // produce blocks until the new schedule will be active on next block produced
   BOOST_REQUIRE_EQUAL(A.head().block()->producer,  // head block should still have been produced using
                       orig_producer);              // the original producer
   sb = A.produce_block();
   bool using_new_sched = std::ranges::find(producers, sb->producer) != producers.end();
   BOOST_REQUIRE(using_new_sched);                 // verify that we have just switched to new schedule
   BOOST_REQUIRE_NE(sb->producer, orig_producer);  // and that the producer has changed
   auto end_slot = sb->timestamp.slot;
   BOOST_REQUIRE_EQUAL(end_slot % prod_rep, 0u);   // validate that the policy change occurs on the first block of prod_rep

   // under Savanna, a new policy becomes active on the first block of a prod_rep block round after:
   // 1. finishing the current round
   // 2. a full round
   // ----------------------------------------------------------------------------------------
   uint32_t expected_gap = prod_rep - (start_slot % prod_rep);   // 1. finishing the current round
   expected_gap += prod_rep;                                     // 2. a full round
   BOOST_REQUIRE_EQUAL(end_slot, start_slot + expected_gap);
} FC_LOG_AND_RETHROW()


// ---------------------------------------------------------------------------------------------------
//     Proposer Policy change - check expected delay when policy change initiated on
//                              the last block of a round
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(policy_change_last_block_delay_check, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0];
   const uint32_t last = prod_rep - 1;
   static_assert(last > 0);

   while(A.head().timestamp().slot % prod_rep != last - 1)
      A.produce_block();

   const vector<account_name> producers { "pa"_n, "pb"_n };
   A.create_accounts(producers);
   A.tester::set_producers(producers);              // push the action to update the producer schedule
   auto sb = A.produce_block();                     // produce a block that will include the policy change transaction
   auto orig_producer = sb->producer;               // save producer before transition
   auto start_slot = sb->timestamp.slot;
   BOOST_REQUIRE_EQUAL(start_slot % prod_rep, last); // validate that the policy change occurs on the last block of prod_rep

   A.wait_for_producer(producers);                  // produce blocks until the new schedule will be active on next block produced
   BOOST_REQUIRE_EQUAL(A.head().block()->producer,  // head block should still have been produced using
                       orig_producer);              // the original producer
   sb = A.produce_block();
   bool using_new_sched = std::ranges::find(producers, sb->producer) != producers.end();
   BOOST_REQUIRE(using_new_sched);                 // verify that we have just switched to new schedule
   BOOST_REQUIRE_NE(sb->producer, orig_producer);  // and that the producer has changed
   auto end_slot = sb->timestamp.slot;
   BOOST_REQUIRE_EQUAL(end_slot % prod_rep, 0u);   // validate that the policy change occurs on the first block of prod_rep

   // under Savanna, a new policy becomes active on the first block of a prod_rep block round after:
   // 1. finishing the current round
   // 2. a full round
   // ----------------------------------------------------------------------------------------
   uint32_t expected_gap = prod_rep - (start_slot % prod_rep);   // 1. finishing the current round
   expected_gap += prod_rep;                                     // 2. a full round
   BOOST_REQUIRE_EQUAL(end_slot, start_slot + expected_gap);
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------------------
//     Verify that a proposer policy does not become active when finality has stalled
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(no_proposer_policy_change_without_finality, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2]; auto& D=_nodes[3];

   // split network { A, B } and { C, D }
   // Regardless of how many blocks A produces, finality will not advance
   // by more than one (1 QC in flight)
   // -------------------------------------------------------------------
   set_partition( {&C, &D} );
   auto sb = A.produce_block();                        // take care of the in-flight QC

   const vector<account_name> producers { "pa"_n, "pb"_n };
   auto orig_version = A.control->active_producers().version;
   auto orig_lib_num = A.lib_number;
   auto orig_producer = sb->producer;

   A.create_accounts(producers);
   A.tester::set_producers(producers);                 // push the action to update the producer schedule

   // produce `2 * prod_rep` more blocks. If finality was advancing, the new proposer policy would be active,
   // but with a split network finality will stall, and the new proposer policy should *not* become active.
   // -----------------------------------------------------------------------------------------------------
   A.produce_blocks(2 * prod_rep);                     // make sure finality stalls long enough for new policy to be eligible
   BOOST_REQUIRE_EQUAL(A.lib_number, orig_lib_num);
   BOOST_REQUIRE_EQUAL(A.control->active_producers().version, orig_version);

   // remove network split. verify that proposer policy becomes active
   // ----------------------------------------------------------------
   set_partition({});
   propagate_heads();

   // Now that the network is not split anymore, finality will start advancing again on the third
   // block produced, and we expect the new proposer policy to become active on the next first block of a round
   // ---------------------------------------------------------------------------------------------------------
   sb = A.produce_blocks(2);                           // allow two blocks to be voted on.
   BOOST_REQUIRE_EQUAL(sb->producer, orig_producer);   // should still use orig producer

   // now switch should happen within the next `prod_rep` blocks
   for (uint32_t i=0; i<prod_rep; ++i) {
      sb = A.produce_block();
      if (sb->producer != orig_producer) {
         BOOST_REQUIRE_EQUAL(sb->timestamp.slot % prod_rep, 0u);
         break;
      }
   }
   BOOST_REQUIRE_NE(sb->producer, orig_producer);
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------------------
//     Verify that a proposer policy does not become active when finality has stalled
//      AND
//     if finality starts advancing again while there are only two blocks left to produce in the round,
//     the proposer schedule change will happen exactly on the first block of the next round (provided
//     finality stalled long enough)
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(no_proposer_policy_change_without_finality_2, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2]; auto& D=_nodes[3];

   // split network { A, B } and { C, D }
   // Regardless of how many blocks A produces, finality will not advance
   // by more than one (1 QC in flight)
   // -------------------------------------------------------------------
   set_partition( {&C, &D} );
   auto sb = A.produce_block();                        // take care of the in-flight QC

   const vector<account_name> producers { "pa"_n, "pb"_n };
   auto orig_version = A.control->active_producers().version;
   auto orig_lib_num = A.lib_number;
   auto orig_producer = sb->producer;

   A.create_accounts(producers);
   A.tester::set_producers(producers);                 // push the action to update the producer schedule

   // produce `2 * prod_rep` more blocks. If finality was advancing, the new proposer policy would be active,
   // but with a split network finality will stall, and the new proposer policy should *not* become active.
   // -----------------------------------------------------------------------------------------------------
   sb = A.produce_blocks(2 * prod_rep);               // make sure finality stalls long enough for new policy to be eligible
   while(sb->timestamp.slot % prod_rep != prod_rep - 3) // produce blocks until there are only two more left in the round
      sb = A.produce_block();

   BOOST_REQUIRE_EQUAL(A.lib_number, orig_lib_num);
   BOOST_REQUIRE_EQUAL(A.control->active_producers().version, orig_version);

   // remove network split. Verify that proposer policy becomes active
   // ----------------------------------------------------------------
   set_partition({});
   propagate_heads();

   // Now that the network is not split anymore, finality will start advancing again on the third
   // block produced, and we expect the new proposer policy to become active on the next first block of a round
   // ---------------------------------------------------------------------------------------------------------
   sb = A.produce_blocks(2);                           // allow two blocks to be voted on (the last two blocks of a round)
   BOOST_REQUIRE_EQUAL(sb->producer, orig_producer);   // should still use orig producer

   // now switch should happen in the next block, as it is the first block of a round
   // -------------------------------------------------------------------------------
   sb = A.produce_block();
   BOOST_REQUIRE_NE(sb->producer, orig_producer);      // verify switch has happened
   BOOST_REQUIRE_EQUAL(sb->timestamp.slot % prod_rep, 0u); // verify first block of a round
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------------------
//     Verify that a proposer policy becomes active when finality has advanced enough to make it pending
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(pending_proposer_policy_becomes_active_without_finality, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2]; auto& D=_nodes[3];
   static_assert(prod_rep >= 4);

   auto sb = A.produce_block();
   auto orig_producer = sb->producer;
   auto orig_version = A.control->active_producers().version;

   while ((A.head().timestamp().slot + 1) % prod_rep >= prod_rep - 3)
      A.produce_block();                  // make sure the next block is not one of the last three blocks of a round

   const vector<account_name> producers { "pa"_n, "pb"_n };

   A.create_accounts(producers);
   A.tester::set_producers(producers);    // push the action to update the producer schedule
   A.produce_block();                     // produce a block that will include the policy change transaction
   A.produce_blocks(prod_rep);            // produce `prod_rep` blocks which guarantees that the proposer policy is pending

   // split network { A, B } and { C, D }
   // Regardless of how many blocks A produces, finality will not advance
   // by more than one (1 QC in flight)
   // -------------------------------------------------------------------
   set_partition( {&C, &D} );

   sb = A.produce_block();                // produce one more block for lib final advance (in-flight QC)

   BOOST_REQUIRE_EQUAL(sb->producer, orig_producer);
   BOOST_REQUIRE_EQUAL(A.control->active_producers().version, orig_version);
   BOOST_REQUIRE(!!A.control->pending_producers());
   BOOST_REQUIRE_GT(A.control->pending_producers()->version, orig_version);
   auto orig_lib_num = A.lib_number;

   // produce `prod_rep` more blocks. Finality will not be advancing anymore, but still the new policy
   // will become active because it was already pending.
   // Indeed, the new policy would eventually become active as long as it was simply *proposed* prior to the
   // last final block when finality stalled, but this is not verified in this test.
   // ------------------------------------------------------------------------------------------------------
   sb = A.produce_blocks(prod_rep);
   BOOST_REQUIRE_EQUAL(A.lib_number, orig_lib_num);                      // check lib didn't advance
   BOOST_REQUIRE_GT(A.control->active_producers().version, orig_version);// check producer schedule version is greater
   BOOST_REQUIRE_NE(sb->producer, orig_producer);                        // and the last block was produced by a different producer

} FC_LOG_AND_RETHROW()



BOOST_AUTO_TEST_SUITE_END()