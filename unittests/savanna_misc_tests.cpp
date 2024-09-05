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
   using namespace savanna_cluster;
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];

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
   BOOST_REQUIRE_EQUAL(B.last_vote(), weak_vote(b2));
   BOOST_REQUIRE_EQUAL(C.last_vote(), weak_vote(b2));
   BOOST_REQUIRE_EQUAL(A.last_vote(), strong_vote(b1));// A should not have seen b2, and therefore not voted on it

   BOOST_REQUIRE_EQUAL(qc_s(qc(b2)), strong_qc(b0));   // b2 should include a strong qc on b0


   set_partition(partition);                           // restore our original partition {A, B, C} and {D}

   signed_block_ptr b3;
   {
      fc::scoped_set_value tmp(B.propagate_votes(), false);// temporarily prevent B from broadcasting its votes)
                                                           // so A won't receive them and form a QC on b3

      b3 = A.produce_block(_block_interval_us * 2);        // A will see its own strong vote on b3, and C's weak vote
                                                           // (not a quorum)
                                                           // because B doesn't propagate and D is partitioned away
      print("b3", b3);
      BOOST_REQUIRE_EQUAL(A.last_vote(), strong_vote(b3)); // A didn't vote on b2 so it can vote strong
      BOOST_REQUIRE_EQUAL(B.last_vote(), weak_vote(b3));   // but B and C have to vote weak.
      BOOST_REQUIRE_EQUAL(C.last_vote(), weak_vote(b3));   // C did vote, but we turned vote propagation off so
                                                           // A will never see C's vote
      BOOST_REQUIRE_EQUAL(qc_s(qc(b3)), strong_qc(b1));    // b3 should include a strong qc on b1
   }

   BOOST_REQUIRE_EQUAL(A.lib_number, b0->block_num());

                                                       // Now B broadcasts its votes again, so
   auto b4 = A.produce_block();                        // b4 should receive 3 weak votes from A, B and C
                                                       // and should include a strong QC claim on b1 (repeated)
                                                       // since we don't have enough votes to form a QC on b3
   print("b4", b4);
   BOOST_REQUIRE_EQUAL(A.last_vote(), strong_vote(b4));
   BOOST_REQUIRE_EQUAL(B.last_vote(), weak_vote(b4));
   BOOST_REQUIRE_EQUAL(C.last_vote(), weak_vote(b4));
   BOOST_REQUIRE_EQUAL(qc_claim(b3), qc_claim(b4));   // A didn't form a QC on b3, so b4 should repeat b3's claim
   BOOST_REQUIRE(!qc(b4));                            // b4 should not have a QC extension (no new QC formed on b3)

   BOOST_REQUIRE_EQUAL(A.lib_number, b0->block_num());

   auto b5 = A.produce_block();                       // a weak QC was formed on b4 and is included in b5
                                                      // b5 should receive 3 strong votes (because it has a
                                                      // weak QC on b4, which itself had a strong QC on b1.
                                                      // Upon receiving a strong QC on b5, b4 will be final
   print("b5", b5);
   BOOST_REQUIRE_EQUAL(A.last_vote(), strong_vote(b5));
   BOOST_REQUIRE_EQUAL(B.last_vote(), strong_vote(b5));
   BOOST_REQUIRE_EQUAL(qc_s(qc(b5)), weak_qc(b4));    // b5 should include a weak qc on b4

   BOOST_REQUIRE_EQUAL(A.lib_number, b0->block_num());

   auto b6 = A.produce_block();                       // should include a strong QC on b5, b1 should be final
   print("b6", b6);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b6)), strong_qc(b5));  // b6 should include a strong qc on b5

   BOOST_REQUIRE_EQUAL(A.last_vote(), strong_vote(b6));
   BOOST_REQUIRE_EQUAL(B.last_vote(), strong_vote(b6));

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
/* -----------------------------------------------------------------------------------------------------
                                 testcase
                                 --------
Time:        t1      t2      t3      t4      t5      t6      t7      t8
Blocks:
     B0 <--- B1 <--- B2 <-|- B3
                          |
                          \--------- B4 <--- B5 <--- B6 <--- B7 <--- B8
QC claim:
           Strong  Strong  Strong  Strong  Strong   Weak    Weak   Strong
             B0      B1      B2      B2      B2      B4      B5      B6

Vote:      Strong  Strong  Strong   Weak    Weak   Strong  Strong  Strong



In the above example, things are moving along normally until time t4 when a microfork occurs.
Instead of building block B4 off of block B3, the producer builds block B4 off of block B2.
And then going forward, for some reason, it takes slightly longer for votes to propagate that a
QC on a block cannot be formed in time to be included in the very next block; instead the QC goes
in the block after.

The finalizer of interest is voting on all of the blocks as they come. For this example, it is
sufficient to only have one finalizer. The first time the finalizer is forced to vote weak is on
block B4. As the other blocks continue to build on that new branch, it votes on them appropriately
and the producer collects the vote and forms a QC as soon as it can, which always remains one block
late. The finalizer should begin voting strong again starting with block B6. However, prior to the
changes described in this issue, the finalizer would remain stuck voting weak indefinitely.

The expected state of the fsi record for the finalizer after each vote is provided below. It also
records what the new LIB should be after processing the block. In addition to checking that the blocks
have the claims as required above and the LIB as noted below, the test should also check that the fsi
record after each vote is as expected below.

Finalizer fsi after voting strong on block B2 (LIB B0):
last_vote: B2
lock:      B1
other_branch_latest_time: empty

Finalizer fsi after voting strong on block B3 (LIB B1):
last_vote: B3
lock:      B2
other_branch_latest_time: empty

Finalizer fsi after voting weak on block B4 (LIB B1):
last_vote: B4
lock:      B2
other_branch_latest_time: t3

Finalizer fsi after voting weak on block B5 (LIB B1):
last_vote: B5
lock:      B2
other_branch_latest_time: t3

Finalizer fsi after voting strong on block B6 (LIB B1):
last_vote: B6
lock:      B4
other_branch_latest_time: empty

Finalizer fsi after voting strong on block B7 (LIB B1):
last_vote: B7
lock:      B5
other_branch_latest_time: empty

Finalizer fsi after voting strong on block B8 (LIB B4):
last_vote: B8
lock:      B6
other_branch_latest_time: empty
--------------------------------------------------------------------------------------------------------- */
BOOST_FIXTURE_TEST_CASE(gh_534_liveness_issue, savanna_cluster::cluster_t) try {
   using namespace savanna_cluster;
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];

   //_debug_mode = true;
   auto b0 = A.produce_block();                         // receives strong votes from all finalizers
   auto b1 = A.produce_block();                         // receives strong votes from all finalizers
   auto b2 = A.produce_block();                         // receives strong votes from all finalizers
   print("b1", b1);
   print("b2", b2);
   BOOST_REQUIRE_EQUAL(A.lib_number, b0->block_num());

   // partition D out. D will be used to produce blocks on an alternative fork.
   // We will have 3 finalizers voting which is enough to reach QCs
   // -------------------------------------------------------------------------
   const std::vector<size_t> partition {3};
   set_partition(partition);

   auto b3 = D.produce_block();                          // produce a block on D
   print("b3", b3);

   const std::vector<size_t> tmp_partition {0};          // we temporarily separate A (before pushing b3)
   set_partition(tmp_partition);                         // because we don't want A to see the block produced by D (b3)
                                                         // otherwise it will switch forks and build its next block (b4)
                                                         // on top of it

   push_block(1, b3);                                    // push block to B and C, should receive strong votes
   BOOST_REQUIRE_EQUAL(A.last_vote(), strong_vote(b2));
   BOOST_REQUIRE_EQUAL(B.last_vote(), strong_vote(b3));
   BOOST_REQUIRE_EQUAL(C.last_vote(), strong_vote(b3));
   BOOST_REQUIRE_EQUAL(D.last_vote(), strong_vote(b3));
   BOOST_REQUIRE_EQUAL(qc_s(qc(b3)),  strong_qc(b2));    // b3 should include a strong qc on b2
   BOOST_REQUIRE_EQUAL(B.lib_number,  b1->block_num());  // don't use A.lib_number as A is partitioned by itself
                                                         // so it didn't see b3 and its enclosed QC.
   B.check_fsi({.last_vote = b3, .lock = b2, .other_branch_latest_time = {}});

   set_partition(partition);                             // restore our original partition {A, B, C} and {D}

   // from now on, to reproduce the scenario where votes are delayed, so the QC we receive don't
   // claim the parent block, but an ancestor, we need to artificially delay propagating the votes.
   // ---------------------------------------------------------------------------------------------

   fc::scoped_set_value tmp(B.vote_delay(), 1);          // delaying just B's votes should be enough to delay QCs

   auto b4 = A.produce_block(_block_interval_us * 2);    // b4 skips a slot. receives weak votes from {B, C}.
   print("b4", b4);
   BOOST_REQUIRE_EQUAL(A.last_vote(), strong_vote(b4));  // A votes strong because it didn't see (and vote on) B3
   BOOST_REQUIRE_EQUAL(B.last_vote(), weak_vote(b4));    // B's last vote even if it wasn't propagated
   BOOST_REQUIRE_EQUAL(C.last_vote(), weak_vote(b4));
   BOOST_REQUIRE_EQUAL(qc_s(qc(b4)),  strong_qc(b2));    // b4 should include a strong qc on b2
   BOOST_REQUIRE_EQUAL(A.lib_number,  b1->block_num());
   B.check_fsi(fsi_expect{.last_vote = b4, .lock = b2, .other_branch_latest_time = b3->timestamp });

   auto b5 = A.produce_block();                          // receives weak votes from {B, C}.
   print("b5", b5);
   BOOST_REQUIRE_EQUAL(A.last_vote(), strong_vote(b5));  // A votes strong because it didn't see (and vote on) B3
   BOOST_REQUIRE_EQUAL(B.last_vote(), weak_vote(b5));
   BOOST_REQUIRE_EQUAL(C.last_vote(), weak_vote(b5));
   BOOST_REQUIRE(!qc(b5));                               // Because B's vote was delayed, b5 should not have a QC
   BOOST_REQUIRE_EQUAL(A.lib_number,  b1->block_num());
   B.check_fsi(fsi_expect{.last_vote = b5, .lock = b2, .other_branch_latest_time = b3->timestamp });

   auto b6 = A.produce_block();                          // receives strong votes from {A, B, C}.
   print("b6", b6);
   BOOST_REQUIRE_EQUAL(A.last_vote(), strong_vote(b6));  // A votes strong because it didn't see (and vote on) B3
   BOOST_REQUIRE_EQUAL(B.last_vote(), strong_vote(b6));  // with issue #627 fix, should start voting strong again
   BOOST_REQUIRE_EQUAL(C.last_vote(), strong_vote(b6));  // with issue #627 fix, should start voting strong again
   BOOST_REQUIRE_EQUAL(qc_s(qc(b6)),  weak_qc(b4));      // Because B's vote was delayed, b6 has a weak QC on b4
   BOOST_REQUIRE_EQUAL(A.lib_number,  b1->block_num());
   B.check_fsi(fsi_expect{.last_vote = b6, .lock = b4, .other_branch_latest_time = {}});

   auto b7 = A.produce_block();                          // receives strong votes from {A, B, C}.
   print("b7", b7);
   BOOST_REQUIRE_EQUAL(A.last_vote(), strong_vote(b7));
   BOOST_REQUIRE_EQUAL(B.last_vote(), strong_vote(b7));
   BOOST_REQUIRE_EQUAL(C.last_vote(), strong_vote(b7));
   BOOST_REQUIRE_EQUAL(qc_s(qc(b7)),  weak_qc(b5));      // Because B's vote was delayed, b7 has a weak QC on b5
   BOOST_REQUIRE_EQUAL(A.lib_number,  b1->block_num());
   B.check_fsi(fsi_expect{.last_vote = b7, .lock = b5, .other_branch_latest_time = {}});

   auto b8 = A.produce_block();                          // receives strong votes from {A, B, C}.
   print("b8", b8);
   BOOST_REQUIRE_EQUAL(A.last_vote(), strong_vote(b8));
   BOOST_REQUIRE_EQUAL(B.last_vote(), strong_vote(b8));
   BOOST_REQUIRE_EQUAL(C.last_vote(), strong_vote(b8));
   BOOST_REQUIRE_EQUAL(qc_s(qc(b8)),  strong_qc(b6));    // Because of the strong votes on b6, b8 has a strong QC on b6
   BOOST_REQUIRE_EQUAL(A.lib_number,  b4->block_num());
   B.check_fsi(fsi_expect{.last_vote = b8, .lock = b6, .other_branch_latest_time = {}});

} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------------------
//                    Policy change: restart from snapshot with no blocklog or fork database
// B1 <- B2 <- B3 <- B4 <- B5 <- B6
//
// where:
// B2 claims a strong QC on B1.
// B3 claims a strong QC on B1.
// B4 claims a strong QC on B2. (B4 makes B1 final.)
// B5 claims a strong QC on B4. (B5 makes B2 final.)
// B6 claims a strong QC on B5. (B6 makes B4 final.)
//
// Let's say a node operator decided to take a snapshot on B3. After their node receives B6, B4 becomes final and the
// snapshot on B3 becomes available.
//
// Then the operator shuts down nodeos and decides to restart from the snapshot on B3.
//
// After starting up from the snapshot, their node receives block B4 from the P2P network. Since B4 advances the QC
// claim relative to its parent (from a strong QC claimed on B1 to a strong QC claimed on B2), it must include a QC
// attached to justify its claim. It does in fact contain the strong QC on block B2, but how does this node verify the
// QC? It started with B3 as the root block of its fork database, so block B2 does not exist in the fork database.
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(validate_qc_after_restart_from_snapshot, savanna_cluster::cluster_t) try {
   using namespace savanna_cluster;
   auto& A=_nodes[0];

   _debug_mode = true;
   auto b1 = A.produce_block();                         // receives strong votes from all finalizers

   const std::vector<size_t> partition {0};             // partition A so that B, C and D don't see b2 (yet)
   set_partition(partition);

   auto b2 = A.produce_block();                         // receives just 1 strong vote fron A
   BOOST_REQUIRE_EQUAL(qc_s(qc(b2)), strong_qc(b1));    // b4 claims a strong QC on b1

   auto b3 = A.produce_block();                         // b3 repeats b2 strong qc claim on b1 (because no qc on b2)
   BOOST_REQUIRE(!qc(b3));

   auto b3_snapshot = A.snapshot();

   set_partition({});                                   // remove partition so A will receive votes on b2 and b3

   push_block(0, b2);                                   // other nodes receive b2 and vote on it, so A forms a qc on b2
   auto b4 = A.produce_block();
   BOOST_REQUIRE_EQUAL(qc_s(qc(b4)), strong_qc(b2));    // b4 claims a strong QC on b2. (b4 makes b1 final.)
   BOOST_REQUIRE_EQUAL(A.lib_number, b1->block_num());

   push_block(0, b3);
   push_block(0, b4);                                   // push b4 again as it was unlinkable until the other
                                                        // nodes received b3

   auto b5 = A.produce_block();
   BOOST_REQUIRE_EQUAL(qc_s(qc(b5)), strong_qc(b4));    // b5 claims a strong QC on b4. (b5 makes b2 final.)
   BOOST_REQUIRE_EQUAL(A.lib_number, b2->block_num());

   auto b6 = A.produce_block();
   BOOST_REQUIRE_EQUAL(qc_s(qc(b6)), strong_qc(b5));    // b6 claims a strong QC on b5. (b6 makes b4 final.)
   BOOST_REQUIRE_EQUAL(A.lib_number, b4->block_num());

   // Then the operator shuts down nodeos and decides to restart from the snapshot on B3.
   A.close();
   A.remove_state();
   A.remove_reversible_data_and_blocks_log();

   A.open_from_snapshot(b3_snapshot);

   // After starting up from the snapshot, their node receives block b4 from the P2P network.
   // Since b4 advances the QC claim relative to its parent (from a strong QC claimed on b1
   // to a strong QC claimed on b2), it must include a QC attached to justify its claim.
   // It does in fact contain the strong QC on block b2, but how does this node verify the QC?
   // It started with b3 as the root block of its fork database, so block b2 does not exist in
   // the fork database.
   // -----------------------------------------------------------------------------------------
   A.push_block(b4);
   A.push_block(b5);
   A.push_block(b6);

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_SUITE_END()
