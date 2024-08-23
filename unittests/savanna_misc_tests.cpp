#include "savanna_cluster.hpp"

using namespace eosio::chain;
using namespace eosio::testing;

BOOST_AUTO_TEST_SUITE(savanna_misc_tests)

// ------------------------------------------------------------------------------------
// Verify that we can restart a node from a snapshot without state or blocks (reversible
// or not)
// ------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(snapshot_startup_without_forkdb, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& B=_nodes[1];

   auto snapshot = B.snapshot();
   A.produce_blocks(3);

   B.close();
   B.remove_reversible_data_and_blocks_log(); // remove blocks log *and* fork database
   B.remove_state();
   B.open_from_snapshot(snapshot);

} FC_LOG_AND_RETHROW()

// ------------------------------------------------------------------------------------
// Verify that we cannot restart a node from a snapshot without state and blocks log,
// but with a fork database
// ------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(snapshot_startup_with_forkdb, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& B=_nodes[1];

   auto snapshot = B.snapshot();
   A.produce_blocks(3);

   B.close();
   B.remove_blocks_log(); // remove blocks log, but *not* fork database
   B.remove_state();
   BOOST_CHECK_EXCEPTION(B.open_from_snapshot(snapshot), fork_database_exception,
                         fc_exception_message_is("When starting from a snapshot with no block log, we shouldn't have a fork database either"));

} FC_LOG_AND_RETHROW()


// -----------------------------------------------------------------------------------------------------
// Test case demonstrating the weak masking issue (see https://github.com/AntelopeIO/spring/issues/534)
// Because the issue is fixed in spring https://github.com/AntelopeIO/spring/pull/537, test must pass
// on all versions past that commit.
// -----------------------------------------------------------------------------------------------------
/*
                                               S
                                  +------------------------------+
                                  V                              |
                  +-----+  S   +-----+      S     +-----+   no   +-----+   W  +-----+  S  +-----+
A produces   <----| b0  |<-----| b1  |<-----------|  b3 |<-------+ b4  |<-----| b5  |<----|  b6 |<-------
                  +-----+      +-----+            +-----+  claim +-----+      +-----+     +-----+
                     ^
                     |                    +-----+
D produces           +--------------------| b2  |
                                     S    +-----+

*/
BOOST_FIXTURE_TEST_CASE(weak_masking_issue, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];
   using vote_t = savanna_cluster::node_t::vote_t;
   //_debug_mode = true;

   auto b0 = A.produce_blocks(2);                     // receives strong votes from all finalizers
   print("b0", b0);

   // partition D out. D will be used to produce blocks on an alternative fork.
   // We will have 3 finalizers voting which is enough to reach QCs
   // -------------------------------------------------------------------------
   const std::vector<size_t> partition {3};
   set_partition(partition);

   auto b1 = A.produce_block();                       // receives strong votes from 3 finalizers (D partitioned out)
   print("b1", b1);

   auto b2 = D.produce_block(_block_interval_us * 2); // produce a `later` block on D
   print("b2", b2);

   BOOST_REQUIRE_GT(b2->timestamp.slot, b1->timestamp.slot);

   const std::vector<size_t> tmp_partition {0};       // we temporarily separate A (before pushing b2)
   set_partitions({tmp_partition, partition});        // because we don't want A to see the block produced by D (b2)
                                                      // otherwise it will switch forks and build its next block (b3)
                                                      // on top of it

   push_block(1, b2);                                 // push block to B and C, should receive weak votes
   BOOST_REQUIRE_EQUAL(B.last_vote, vote_t(b2, false));
   BOOST_REQUIRE_EQUAL(C.last_vote, vote_t(b2, false));
   BOOST_REQUIRE_EQUAL(A.last_vote, vote_t(b1, true));// A should not have seen b2, and therefore not voted on it

   BOOST_REQUIRE_EQUAL(qc_s(qc(b2)), qc_s(b0, true)); // b2 should include a strong qc on b0


   set_partition(partition);                          // restore our original partition {A, B, C} and {D}

   signed_block_ptr b3;
   {
      scoped_set_value tmp(B.propagate_votes, false);      // temporarily prevent B from broadcasting its votes)
                                                           // so A won't receive them and form a QC on b3

      b3 = A.produce_block(_block_interval_us * 2);        // A will see its own strong vote on b3, and C's weak vote
                                                           // (not a quorum)
                                                           // because B doesn't propagate and D is partitioned away
      print("b3", b3);
      BOOST_REQUIRE_EQUAL(A.last_vote, vote_t(b3, true));  // A didn't vote on b2 so it can vote strong
      BOOST_REQUIRE_EQUAL(B.last_vote, vote_t(b3, false)); // but B and C have to vote weak.
      BOOST_REQUIRE_EQUAL(C.last_vote, vote_t(b3, false)); // C did vote, but we turned vote propagation off so
                                                           // A will never see C's vote
      BOOST_REQUIRE_EQUAL(qc_s(qc(b3)), qc_s(b1, true));   // b3 should include a strong qc on b1
   }

   BOOST_REQUIRE_EQUAL(A.lib_number, b0->block_num());

                                                      // Now B broadcasts its votes again, so
   auto b4 = A.produce_block();                       // b4 should receive 3 weak votes from A, B and C
                                                      // and should include a strong QC claim on b1 (repeated)
                                                      // since we don't have enough votes to form a QC on b3
   print("b4", b4);
   BOOST_REQUIRE_EQUAL(A.last_vote, vote_t(b4, true));
   BOOST_REQUIRE_EQUAL(B.last_vote, vote_t(b4, false));
   BOOST_REQUIRE_EQUAL(C.last_vote, vote_t(b4, false));
   BOOST_REQUIRE_EQUAL(qc_claim(b3), qc_claim(b4));   // A didn't form a QC on b3, so b4 should repeat b3's claim
   BOOST_REQUIRE(!qc(b4));                            // b4 should not have a QC extension (no new QC formed on b3)

   BOOST_REQUIRE_EQUAL(A.lib_number, b0->block_num());

   auto b5 = A.produce_block();                       // a weak QC was formed on b4 and is included in b5
                                                      // b5 should receive 3 strong votes (because it has a
                                                      // weak QC on b4, which itself had a strong QC on b1.
                                                      // Upon receiving a strong QC on b5, b4 will be final
   print("b5", b5);
   BOOST_REQUIRE_EQUAL(A.last_vote, vote_t(b5, true));
   BOOST_REQUIRE_EQUAL(B.last_vote, vote_t(b5, true));
   BOOST_REQUIRE_EQUAL(qc_s(qc(b5)), qc_s(b4, false)); // b5 should include a weak qc on b4

   BOOST_REQUIRE_EQUAL(A.lib_number, b0->block_num());

   auto b6 = A.produce_block();                       // should include a strong QC on b5, b1 should be final
   print("b6", b6);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b6)), qc_s(b5, true)); // b6 should include a strong qc on b5

   BOOST_REQUIRE_EQUAL(A.last_vote, vote_t(b6, true));
   BOOST_REQUIRE_EQUAL(B.last_vote, vote_t(b6, true));

   BOOST_REQUIRE_EQUAL(A.lib_number, b4->block_num());

} FC_LOG_AND_RETHROW()


// -----------------------------------------------------------------------------------------------------
// see https://github.com/AntelopeIO/spring/issues/621 explaining the issue that this test demonstrates.
//
// The fix in https://github.com/AntelopeIO/spring/issues/534 for the weak masking issue respected a more
// conservative version of rule 2. This solved the safety concerns due to the weak masking issue, but it
// was unnecessarily restrictive with respect to liveness.
//
// As a consequence if this liveness issue, finalizers may be stuck voting weak if the QC is not formed
// quickly enough.
//
// This testcase fails prior to https://github.com/AntelopeIO/spring/issues/621 being fixed.
// -----------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(gh_534_liveness_issue, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];
   using vote_t = savanna_cluster::node_t::vote_t;
   _debug_mode = true;

   auto b0 = A.produce_block();                       // receives strong votes from all finalizers
   auto b1 = A.produce_block();                       // receives strong votes from all finalizers
   auto b2 = A.produce_block();                       // receives strong votes from all finalizers
   print("b1", b1);
   print("b2", b2);
   BOOST_REQUIRE_EQUAL(A.lib_number, b0->block_num());

   // partition D out. D will be used to produce blocks on an alternative fork.
   // We will have 3 finalizers voting which is enough to reach QCs
   // -------------------------------------------------------------------------
   const std::vector<size_t> partition {3};
   set_partition(partition);

   auto b3 = D.produce_block();                        // produce a block on D
   print("b3", b3);

   const std::vector<size_t> tmp_partition {0};        // we temporarily separate A (before pushing b3)
   set_partition(tmp_partition);                       // because we don't want A to see the block produced by D (b3)
                                                       // otherwise it will switch forks and build its next block (b4)
                                                       // on top of it

   push_block(1, b3);                                  // push block to A, B and C, should receive strong votes
   BOOST_REQUIRE_EQUAL(A.last_vote, vote_t(b2, true));
   BOOST_REQUIRE_EQUAL(B.last_vote, vote_t(b3, true));
   BOOST_REQUIRE_EQUAL(C.last_vote, vote_t(b3, true));
   BOOST_REQUIRE_EQUAL(D.last_vote, vote_t(b3, true));
   BOOST_REQUIRE_EQUAL(qc_s(qc(b3)), qc_s(b2, true));  // b3 should include a strong qc on b2
   BOOST_REQUIRE_EQUAL(B.lib_number, b1->block_num()); // don't use A.lib_number as A is partitioned by itself
                                                       // so it didn't see b3 and its enclosed QC.

   set_partition(partition);                           // restore our original partition {A, B, C} and {D}

   // from now on, to reproduce the scenario where votes are delayed, so the QC we receive don't
   // claim the parent block, but an ancestor, we need to artificially delay propagating the votes.
   // ---------------------------------------------------------------------------------------------

   // TODO - implement vote propagation delay and fix accordingly below checks.

   auto b4 = A.produce_block(_block_interval_us * 2);  // receives weak votes from {B, C}.
   print("b4", b4);
   BOOST_REQUIRE_EQUAL(A.last_vote, vote_t(b4, true)); // A votes strong because it didn't see (and vote on) B3
   BOOST_REQUIRE_EQUAL(B.last_vote, vote_t(b4, false));
   BOOST_REQUIRE_EQUAL(C.last_vote, vote_t(b4, false));
   BOOST_REQUIRE_EQUAL(qc_s(qc(b4)), qc_s(b2, true));  // b4 should include a strong qc on b2
   BOOST_REQUIRE_EQUAL(A.lib_number, b1->block_num());

   auto b5 = A.produce_block(_block_interval_us * 2);  // receives weak votes from {B, C}.
   print("b5", b5);
   BOOST_REQUIRE_EQUAL(A.last_vote, vote_t(b5, true)); // A votes strong because it didn't see (and vote on) B3
   BOOST_REQUIRE_EQUAL(B.last_vote, vote_t(b5, false));
   BOOST_REQUIRE_EQUAL(C.last_vote, vote_t(b5, false));
   BOOST_REQUIRE_EQUAL(qc_s(qc(b5)), qc_s(b2, true));  // b5 should include a strong qc on b2
   BOOST_REQUIRE_EQUAL(A.lib_number, b1->block_num());

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_SUITE_END()