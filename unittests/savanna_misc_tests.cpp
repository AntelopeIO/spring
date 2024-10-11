#include <savanna_cluster.hpp>
#include <test-data.hpp>

#include <fc/io/fstream.hpp> // for read_file_contents

using namespace eosio::chain;
using namespace eosio::testing;

BOOST_AUTO_TEST_SUITE(savanna_misc_tests)

// ------------------------------------------------------------------------------------
// Verify that we can restart a node from a snapshot without state or blocks (reversible
// or not)
// ------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(snapshot_startup_without_fork_db, savanna_cluster::cluster_t) try {
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
BOOST_FIXTURE_TEST_CASE(snapshot_startup_with_fork_db, savanna_cluster::cluster_t) try {
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
   set_partition( {&D} );

   auto b1 = A.produce_block();                       // receives strong votes from 3 finalizers (D partitioned out)
   print("b1", b1);

   auto b2 = D.produce_block(_block_interval_us * 2); // produce a `later` block on D
   print("b2", b2);

   BOOST_REQUIRE_GT(b2->timestamp.slot, b1->timestamp.slot);

   set_partitions({ {&A}, {&D}});                     // because we don't want A to see the block produced by D (b2)
                                                      // otherwise it will switch forks and build its next block (b3)
                                                      // on top of it

   push_block(1, b2);                                 // push block to B and C, should receive weak votes
   BOOST_REQUIRE_EQUAL(B.last_vote(), weak_vote(b2));
   BOOST_REQUIRE_EQUAL(C.last_vote(), weak_vote(b2));
   BOOST_REQUIRE_EQUAL(A.last_vote(), strong_vote(b1));// A should not have seen b2, and therefore not voted on it

   BOOST_REQUIRE_EQUAL(qc_s(qc(b2)), strong_qc(b0));   // b2 should include a strong qc on b0


   set_partition( {&D} );                             // restore our original partition {A, B, C} and {D}

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
   set_partition( {&D} );

   auto b3 = D.produce_block();                          // produce a block on D
   print("b3", b3);

                                                         // we temporarily separate A (before pushing b3)
   set_partition( {&A} );                                // because we don't want A to see the block produced by D (b3)
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

   set_partition( {&D} );                               // restore our original partition {A, B, C} and {D}

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
//               validate qc after restart from snapshot with no blocklog or fork database
//               -------------------------------------------------------------------------
//
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

   // _debug_mode = true;
   auto b1 = A.produce_block();                         // receives strong votes from all finalizers
   print("b1", b1);

   set_partition( {&A} );                               // partition A so that B, C and D don't see b2 (yet)

   auto b2 = A.produce_block();                         // receives just 1 strong vote fron A
   print("b2", b2);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b2)), strong_qc(b1));    // b4 claims a strong QC on b1

   auto b3 = A.produce_block();                         // b3 repeats b2 strong qc claim on b1 (because no qc on b2)
   print("b3", b3);
   BOOST_REQUIRE(!qc(b3));

   auto b3_snapshot = A.snapshot();

   set_partition({});                                   // remove partition so A will receive votes on b2 and b3

   push_block(0, b2);                                   // other nodes receive b2 and vote on it, so A forms a qc on b2
   auto b4 = A.produce_block();
   print("b4", b4);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b4)), strong_qc(b2));    // b4 claims a strong QC on b2. (b4 makes b1 final.)
   BOOST_REQUIRE_EQUAL(A.lib_number, b1->block_num());

   push_block(0, b3);
   push_block(0, b4);                                   // push b4 again as it was unlinkable until the other
                                                        // nodes received b3

   auto b5 = A.produce_block();
   print("b5", b5);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b5)), strong_qc(b4));    // b5 claims a strong QC on b4. (b5 makes b2 final.)
   BOOST_REQUIRE_EQUAL(A.lib_number, b2->block_num());

   auto b6 = A.produce_block();
   print("b6", b6);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b6)), strong_qc(b5));    // b6 claims a strong QC on b5. (b6 makes b4 final.)
   BOOST_REQUIRE_EQUAL(A.lib_number, b4->block_num());

   // Then the operator shuts down nodeos and decides to restart from the snapshot on B3.
   A.close();
   A.remove_state();
   A.remove_reversible_data_and_blocks_log();

   set_partition( {&A} );                               // partition A so it doesn't receive blocks on `open()`
   A.open_from_snapshot(b3_snapshot);

   // After starting up from the snapshot, their node receives block b4 from the P2P network.
   // Since b4 advances the QC claim relative to its parent (from a strong QC claimed on b1
   // to a strong QC claimed on b2), it must include a QC attached to justify its claim.
   // It does in fact contain the strong QC on block b2, but how does this node verify the QC?
   // It started with b3 as the root block of its fork database, so block b2 does not exist in
   // the fork database.
   // -----------------------------------------------------------------------------------------
   A.push_block(b4);                                    // when pushing b4, if we try to access any block state
   A.push_block(b5);                                    // before b3, we will fail with a `verify_qc_claim`
   A.push_block(b6);                                    // exception, which is what will happens until issue
                                                        // #694 is addressed.
} FC_LOG_AND_RETHROW()


// ---------------------------------------------------------------------------------------------------
//               Missing finalizer policies needed to validate qc after
//               restart from snapshot with no blocklog or fork database
//               -------------------------------------------------------
//
//
// The node processes the following blockchain:
//
// <- B1 <- B2 <- B3 <- B4 <- B5 <- B6 <- B7 <- B8 <- B9
//
// where:
//
// B1 has active finalizer policy P1 and pending finalizer policy.
// B1 proposes finalizer policy P2.
//
// B2 claims a strong QC on B1.
// B2 has active finalizer policy P1 and no pending finalizer policy.
//
// B3 claims a strong QC on B2. (B3 makes B1 final.)
// B3 has active finalizer policy P1 and has pending finalizer policy P2.
//
// B4 claims a strong QC on B3. (B4 makes B2 final.)
// B4 has active finalizer policy P1 and has pending finalizer policy P2.
//
// B5 claims a strong QC on B3.
// B5 has active finalizer policy P1 and has pending finalizer policy P2.
//
// B6 claims a strong QC on B4. (B5 makes B3 final.)
// B6 has active finalizer policy P2 and no pending finalizer policy.
// (At this point, in the current implementation policy P2 is lost from the block_header_state
// of B6, which is the source of the problem.)
//
// B7 claims a strong QC on B5.
// B7 has active finalizer policy P2 and no pending finalizer policy.
//
// B8 claims a strong QC on B6. (B8 makes B4 final.)
// B8 has active finalizer policy P2 and no pending finalizer policy.
//
// B9 claims a strong QC on B8. (B9 makes B6 final.)
// B9 has active finalizer policy P2 and no pending finalizer policy.
//
// The node operator decided to take a snapshot on B6. After their node receives B9, B6 becomes
// final and the snapshot on B6 becomes available to the node operator as a valid snapshot.
//
// Then the operator shuts down nodeos and decides to restart from the snapshot on B6.
//
// After starting up from the snapshot, their node receives block B7 from the P2P network.
// Since B7 advances the QC claim relative to its parent (from a strong QC claimed on B4 to a
// strong QC claimed on B5), it must include a QC attached to justify its claim. It does in fact
// contain the strong QC on block B5, but how does this node verify the QC? It started with B6
// as the root block of its fork database, so block B5 does not exist in the fork database.
//
// Yes, the finality digest for B5 can be retrieved from the finality_core in the block_header_state
// for B6. But the block_header_state of B6 contains an active_finalizer_policy of policy P2 and it
// contains no pending_finalizer_policy. Not only does it not know the generation numbers for the
// active and pending (if present) finalizer policies of B5, even if it did know the generation
// numbers, it simply would no longer have policy P1 which it needs to validate the QC for block B5.
//
// The solution is to augment the state tracked in block_header_state.
//
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(validate_qc_requiring_finalizer_policies, savanna_cluster::cluster_t) try {
   using namespace savanna_cluster;
   auto& A=_nodes[0];

   // _debug_mode = true;

   // update finalizer_policy with a new key for B
   // --------------------------------------------
   base_tester::finalizer_policy_input input;
   for (size_t i=0; i<num_nodes(); ++i)
      input.finalizers.emplace_back(_fin_keys[i], 1);
   input.finalizers[1] = { _fin_keys[num_nodes()], 1 }; // overwrite finalizer key for B
   input.threshold =  (input.finalizers.size() * 2) / 3 + 1;
   A.set_finalizers(input);

   auto b1 = A.produce_block();                         // b1 has active finalizer policy p1 and pending finalizer policy.
   print("b1", b1);                                     // b1 proposes finalizer policy p2.
   auto p1 = A.head_active_finalizer_policy()->generation;

   auto b2 = A.produce_block();
   print("b2", b2);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b2)), strong_qc(b1));    // b2 claims a strong QC on b1

   auto b3 = A.produce_block();
   print("b3", b3);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b3)), strong_qc(b2));    // b3 claims a strong QC on b2
   BOOST_REQUIRE_EQUAL(A.lib_number, b1->block_num());  // b3 makes B1 final

   auto pending = A.head_pending_finalizer_policy();
   BOOST_REQUIRE(!!pending);                            // check that we have a pending finalizer policy
   auto p2 = pending->generation;                       // and its generation is higher than the active one
   BOOST_REQUIRE_EQUAL(p2, p1 + 1);                     // b3 has new pending finalizer policy p2

                                                        // partition A so that B, C and D don't see b4 (yet)
   set_partition( {&A} );                               // and don't vote on it

   auto b4 = A.produce_block();
   print("b4", b4);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b4)), strong_qc(b3));    // b4 claims a strong QC on b3
   BOOST_REQUIRE_EQUAL(A.lib_number, b2->block_num());  // b4 makes B2 final
   pending = A.head_pending_finalizer_policy();
   BOOST_REQUIRE_EQUAL(pending->generation, p2);        // b4 has new pending finalizer policy p2

   auto b5 = A.produce_block();
   print("b5", b5);
   BOOST_REQUIRE(!qc(b5));                              // b5 doesn't include a new qc (duplicates b4's strong claim on b3)
   BOOST_REQUIRE_EQUAL(A.lib_number, b2->block_num());  // finality unchanged stays at b2
   pending = A.head_pending_finalizer_policy();
   BOOST_REQUIRE_EQUAL(pending->generation, p2);        // b5 still has new pending finalizer policy p2
                                                        // since finality did not advance

   set_partition({});                                   // remove partition so A will receive votes on b4 and b5

   push_block(0, b4);                                   // other nodes receive b4 and vote on it, so A forms a qc on b4
   auto b6 = A.produce_block();
   print("b6", b6);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b6)), strong_qc(b4));    // b6 claims a strong QC on b4
   BOOST_REQUIRE_EQUAL(A.lib_number, b3->block_num());  // b6 makes b3 final.

   auto active = A.head_active_finalizer_policy();
   BOOST_REQUIRE_EQUAL(active->generation, p2);         // b6 has active finalizer policy p2
   BOOST_REQUIRE(!A.head_pending_finalizer_policy());   // and no pending finalizer policy.

   // At this point, in the Spring 1.0.0 implementation (which has the bug described in issue #694),
   // policy P2 is lost from the block_header_state of B6, which is the source of the problem

   auto b6_snapshot = A.snapshot();

   push_block(0, b5);

   auto b7 = A.produce_block();
   print("b7", b7);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b7)), strong_qc(b5));    // b7 claims a strong QC on b5
   BOOST_REQUIRE_EQUAL(A.lib_number, b3->block_num());  // lib is still b3

   active = A.head_active_finalizer_policy();
   BOOST_REQUIRE_EQUAL(active->generation, p2);         // b7 has active finalizer policy p2
   BOOST_REQUIRE(!A.head_pending_finalizer_policy());   // and no pending finalizer policy.

   push_block(0, b6);                                   // push b6 again as it was unlinkable until the other
                                                        // nodes received b5

   auto b8 = A.produce_block();
   print("b8", b8);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b8)), strong_qc(b6));    // b8 claims a strong QC on b6
   BOOST_REQUIRE_EQUAL(A.lib_number, b4->block_num());  // b8 makes B4 final

   active = A.head_active_finalizer_policy();
   BOOST_REQUIRE_EQUAL(active->generation, p2);         // b8 has active finalizer policy p2
   BOOST_REQUIRE(!A.head_pending_finalizer_policy());   // and no pending finalizer policy.

   push_block(0, b7);                                   // push b7 and b8 as they were unlinkable until the other
   push_block(0, b8);                                   // nodes received b6

   auto b9 = A.produce_block();
   print("b9", b9);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b9)), strong_qc(b8));    // b9 claims a strong QC on b8
   BOOST_REQUIRE_EQUAL(A.lib_number, b6->block_num());  // b9 makes B6 final

   active = A.head_active_finalizer_policy();
   BOOST_REQUIRE_EQUAL(active->generation, p2);         // b9 has active finalizer policy p2
   BOOST_REQUIRE(!A.head_pending_finalizer_policy());   // and no pending finalizer policy.

   // restart from b6 snapshot.
   // -------------------------
   A.close();
   A.remove_state();
   A.remove_reversible_data_and_blocks_log();

   set_partition({&A});                                 // partition A so it doesn't receive blocks on `open()`
   A.open_from_snapshot(b6_snapshot);

   A.push_block(b7);                                    // when pushing b7, if we try to access any block state
   A.push_block(b8);                                    // before b6, we will fail with a `verify_qc_claim`
   A.push_block(b9);                                    // exception, which is what will happens until issue
                                                        // #694 is addressed.

} FC_LOG_AND_RETHROW()

static void save_blockchain_data(const std::filesystem::path& ref_blockchain_path,
                                 const std::filesystem::path& blocks_path,
                                 const block_id_type&         id,
                                 const std::string&           snapshot) {
   auto source_log_file   = blocks_path / "blocks.log";
   auto source_index_file = blocks_path / "blocks.index";

   auto ref_log_file             = ref_blockchain_path / "blocks.log";
   auto ref_index_file           = ref_blockchain_path / "blocks.index";
   auto ref_id_file_name         = ref_blockchain_path / "id";
   auto ref_snapshot_file_name   = ref_blockchain_path / "snapshot";

   // save reference blocks log
   std::filesystem::copy_file(source_log_file, ref_log_file, std::filesystem::copy_options::overwrite_existing);
   std::filesystem::copy_file(source_index_file, ref_index_file, std::filesystem::copy_options::overwrite_existing);

   // save reference block_id
   fc::cfile ref_id_file;
   ref_id_file.set_file_path(ref_id_file_name);
   ref_id_file.open("wb");
   ref_id_file.write(id.data(), id.data_size());
   ref_id_file.close();

   // save reference snapshot
   fc::cfile snapshot_file;
   snapshot_file.set_file_path(ref_snapshot_file_name);
   snapshot_file.open("w");
   snapshot_file.write(snapshot.data(), snapshot.size());
   snapshot_file.close();
}

static block_id_type read_reference_id(const std::filesystem::path& ref_blockchain_path) {
   auto ref_id_file_path = ref_blockchain_path / "id";
   std::string content;
   fc::read_file_contents(ref_id_file_path, content);

   return block_id_type(content.data(), content.size());
}

static std::string read_reference_snapshot(const std::filesystem::path& ref_blockchain_path) {
   auto ref_snapshot_file_path = ref_blockchain_path / "snapshot";
   std::string content;
   fc::read_file_contents(ref_snapshot_file_path, content);

   return content;
}

// need to pass in temp_dir. otherwise it will be destroyed after replay_reference_blockchain returns
static std::unique_ptr<tester> replay_reference_blockchain(const std::filesystem::path& ref_blockchain_path,
                                                           const fc::temp_directory& temp_dir,
                                                           const block_log& blog) {
   // replay the reference blockchain and make sure LIB id in the replayed
   // chain matches reference LIB id
   // --------------------------------------------------------------------
   auto config = tester::default_config(temp_dir).first;

   auto genesis = eosio::chain::block_log::extract_genesis_state(ref_blockchain_path);
   BOOST_REQUIRE(genesis);

   std::filesystem::create_directories(config.blocks_dir);

   std::filesystem::copy(ref_blockchain_path / "blocks.log", config.blocks_dir / "blocks.log");
   std::filesystem::copy(ref_blockchain_path / "blocks.index", config.blocks_dir / "blocks.index");

   // do a full block invariants check
   config.force_all_checks = true;

   // replay the reference blockchain
   std::unique_ptr<tester> replay_chain = std::make_unique<tester>(config, *genesis);

   auto ref_lib_id = blog.head_id();
   BOOST_REQUIRE_EQUAL(*ref_lib_id, replay_chain->last_irreversible_block_id());

   return replay_chain;
}

static void sync_replayed_blockchain(const std::filesystem::path& ref_blockchain_path,
                                     std::unique_ptr<tester>&&    replay_chain,
                                     const block_log&             blog) {
   tester sync_chain;
   sync_chain.close();  // stop the chain

   // remove state and blocks log so we can restart from snapshot
   std::filesystem::remove_all(sync_chain.get_config().state_dir);
   std::filesystem::remove_all(sync_chain.get_config().blocks_dir);

   // restart from reference snapshot
   sync_chain.open(buffered_snapshot_suite::get_reader(read_reference_snapshot(ref_blockchain_path)));

   // sync with the replayed blockchain
   while( sync_chain.fork_db_head().block_num() < replay_chain->fork_db_head().block_num() ) {
      auto fb = replay_chain->fetch_block_by_number( sync_chain.fork_db_head().block_num()+1 );
      sync_chain.push_block( fb );
   }

   // In syncing, use the head for checking as it advances further than LIB
   auto head_block_num = sync_chain.head().block_num();
   signed_block_ptr ref_block = blog.read_block_by_num(head_block_num);

   BOOST_REQUIRE_EQUAL(ref_block->calculate_id(), sync_chain.head().id());
}

// ----------------------------------------------------------------------------------------------------
// For issue #694, we need to change the finality core of the `block_header_state`, but we want to
// ensure that this doesn't create a consensus incompatibility with Spring 1.0.0, so the blocks created
// with newer versions remain compatible (and linkable) with blocks by Spring 1.0.0.
//
// This test adds a utility that saves reference blockchain data and checks for
// regression in compatibility of syncing and replaying the reference blockchain data.
//
// To save reference blockchain data in `unittests/test-data/consensus_blockchain`,
// run
// `unittests/unit_test -t savanna_misc_tests/verify_block_compatibitity -- --save-blockchain`
// ----------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(verify_block_compatibitity, savanna_cluster::cluster_t) try {
   using namespace savanna_cluster;
   auto& A=_nodes[0];
   const auto& tester_account = "tester"_n;
   //_debug_mode = true;

   bool save_blockchain = tester::arguments_contains("--save-blockchain");

   std::string snapshot;
   if (save_blockchain) { // take a snapshot at the beginning
      snapshot = A.snapshot();
   }

   // update finalizer_policy with a new key for B
   // --------------------------------------------
   base_tester::finalizer_policy_input input;
   for (size_t i=0; i<num_nodes(); ++i)
      input.finalizers.emplace_back(_fin_keys[i], 1);
   input.finalizers[1] = { _fin_keys[num_nodes()], 1 }; // overwrite finalizer key for B
   input.threshold =  (input.finalizers.size() * 2) / 3 + 1;
   A.set_finalizers(input);

   auto b1 = A.produce_block();                         // b1 has active finalizer policy p1 and pending finalizer policy.
   print("b1", b1);                                     // b1 proposes finalizer policy p2.
   auto p1 = A.head_active_finalizer_policy()->generation;

   A.create_account("currency"_n);                      // do something so the block is not empty
   auto b2 = A.produce_block();
   print("b2", b2);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b2)), strong_qc(b1));    // b2 claims a strong QC on b1

   A.create_account(tester_account);                    // do something so the block is not empty
   auto b3 = A.produce_block();
   print("b3", b3);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b3)), strong_qc(b2));    // b3 claims a strong QC on b2
   BOOST_REQUIRE_EQUAL(A.lib_number, b1->block_num());  // b3 makes B1 final

   auto pending = A.head_pending_finalizer_policy();
   BOOST_REQUIRE(!!pending);                            // check that we have a pending finalizer policy
   auto p2 = pending->generation;                       // and its generation is higher than the active one
   BOOST_REQUIRE_EQUAL(p2, p1 + 1);                     // b3 has new pending finalizer policy p2

                                                        // partition A so that B, C and D don't see b4 (yet)
   set_partition({&A});                                 // and don't vote on it

   // push action so that the block is not empty
   A.push_action(config::system_account_name, updateauth::get_name(), tester_account,
                 fc::mutable_variant_object()("account", "tester")("permission", "first")("parent", "active")(
                    "auth", authority(A.get_public_key(tester_account, "first"))));

   auto b4 = A.produce_block();
   print("b4", b4);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b4)), strong_qc(b3));    // b4 claims a strong QC on b3
   BOOST_REQUIRE_EQUAL(A.lib_number, b2->block_num());  // b4 makes B2 final

   auto b5 = A.produce_block();
   print("b5", b5);
   BOOST_REQUIRE(!qc(b5));                              // b5 doesn't include a new qc (duplicates b4's strong claim on b3)
   BOOST_REQUIRE_EQUAL(A.lib_number, b2->block_num());  // finality unchanged stays at b2

   set_partition({});                                   // remove partition so A will receive votes on b4 and b5

   push_block(0, b4);                                   // other nodes receive b4 and vote on it, so A forms a qc on b4
   auto b6 = A.produce_block();
   print("b6", b6);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b6)), strong_qc(b4));    // b6 claims a strong QC on b4
   BOOST_REQUIRE_EQUAL(A.lib_number, b3->block_num());  // b6 makes b3 final.

   push_block(0, b5);

   auto b7 = A.produce_block();
   print("b7", b7);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b7)), strong_qc(b5));    // b7 claims a strong QC on b5
   BOOST_REQUIRE_EQUAL(A.lib_number, b3->block_num());  // lib is still b3

   push_block(0, b6);                                   // push b6 again as it was unlinkable until the other
                                                        // nodes received b5

   auto b8 = A.produce_block();
   print("b8", b8);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b8)), strong_qc(b6));    // b8 claims a strong QC on b6
   BOOST_REQUIRE_EQUAL(A.lib_number, b4->block_num());  // b8 makes B4 final

   push_block(0, b7);                                   // push b7 and b8 as they were unlinkable until the other
   push_block(0, b8);                                   // nodes received b6

   auto b9 = A.produce_block();
   print("b9", b9);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b9)), strong_qc(b8));    // b9 claims a strong QC on b8
   BOOST_REQUIRE_EQUAL(A.lib_number, b6->block_num());  // b9 makes B6 final

   std::filesystem::path test_data_path { UNITTEST_TEST_DATA_DIR };
   auto ref_blockchain_path = test_data_path / "consensus_blockchain";

   // check that the block id of b9 match what we got before.
   auto b9_id = b9->calculate_id();

   if (save_blockchain) {
      save_blockchain_data(ref_blockchain_path, A.get_config().blocks_dir, b9_id, snapshot);
      return;
   }

   // Do block id validation after we save blockchain data in case the id needs to be changed in future
   BOOST_REQUIRE_EQUAL(b9_id, read_reference_id(ref_blockchain_path));

   block_log blog(ref_blockchain_path);

   // replay the reference blockchain and make sure LIB id in the replayed
   // chain matches reference LIB id
   // --------------------------------------------------------------------
   fc::temp_directory temp_dir; // need to pass in temp_dir. otherwise it would be destroyed after replay_reference_blockchain returns
   std::unique_ptr<tester> replay_chain = replay_reference_blockchain(ref_blockchain_path, temp_dir, blog);

   // start another blockchain using reference snapshot, and sync with the blocks
   // from the replayed blockchain
   // ---------------------------------------------------------------------------
   sync_replayed_blockchain(ref_blockchain_path, std::move(replay_chain), blog);
} FC_LOG_AND_RETHROW()

/* -----------------------------------------------------------------------------------------------------
            Finality advancing past block claimed on alternate branch
            =========================================================

Time:        t1      t2      t3      t4      t5      t6      t7
Blocks:
    B0 <---  B1 <--- B2 <--- B3 <-|- B4 <--- B5
                                  |
                                  \----------------- B6 <--- B7
QC claim:
           Strong          Strong  Strong  Strong  Strong   No QC
             B0              B1      B3      B4      B2     achieved

Vote:                      Strong  Strong   Strong  Weak     -

                                                     ^
                                                     |
                                                 validating those weak votes on b2
                                                 would fail if nodes have received b4 and b5
                                                 which advanced lib to b3

    - Node D is isolated and has not seen B3, B4, and B5
    - it received B3 via push_block, (so it can make it its head and produce a child of B3), but has not
      received votes on b3 (only on b2), so b6 includes a strong QC on b2.
    - when b6 is pushed to A, B and C, finalizers of A, B, and C are unable to vote on it, because they
      are locked on B4,
          -> liveness check fails because: `B6' s core.latest_qc_block_timestamp() <  fsi.lock.timestamp`
             because `B2 timestamp < B4 timestamp`.
          -> safety check fails because `B6` does not extend `B4`.
--------------------------------------------------------------------------------------------------------*/
BOOST_FIXTURE_TEST_CASE(finalizers_locked_preventing_vote_on_alternate_branch, savanna_cluster::cluster_t) try {
   using namespace savanna_cluster;
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];

   // _debug_mode = true;

   auto b0 = A.produce_block();
   print("b0", b0);

   signed_block_ptr b1, b2;
   {
      // delay votes from B and C (can't delay on A as A produces), so b2 will not include a QC on B1
      fc::scoped_set_value tmp_B(B.vote_delay(), 1);
      fc::scoped_set_value tmp_C(C.vote_delay(), 1);

      b1 = A.produce_block();
      print("b1", b1);
      BOOST_REQUIRE_EQUAL(qc_s(qc(b1)), strong_qc(b0)); // b1 claims a strong QC on b0

      b2 = A.produce_block();
      print("b2", b2);
      BOOST_REQUIRE(!qc(b2));                           // b2 should not include a QC (votes on b1 delayed)
   }

   B.propagate_delayed_votes_to(D);                     // propagate votes on b2 to D, so it can form a QC on b2
   C.propagate_delayed_votes_to(D);                     // which will be included in b6

                                                        // partition D so that it doesn't see b3, b4 and b5
   set_partition({&D});                                 // and don't vote on it

   auto b3 = A.produce_block();
   print("b3", b3);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b3)), strong_qc(b1));    // b3 claims a strong QC on b1 (votes on b2 delayed)

   D.push_block(b3);                                    // we want D to see b3, but not receive the votes on
                                                        // b3, so that when it produces b6, b6 will have a
                                                        // qc claim on b2

   auto b4 = A.produce_block();
   print("b4", b4);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b4)), strong_qc(b3));    // b4 claims a strong QC on b3

   auto b5 = A.produce_block();
   print("b5", b5);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b5)), strong_qc(b4));    // b5 claims a strong QC on b4


   set_partition({});                                   // remove partition so all nodes can vote on b6 and above

   auto b6 = D.produce_block(_block_interval_us * 3);   // D (who has not seen b4 and b5) produces b6
                                                        // b6 has a higher timestamp than b5
   print("b6", b6);
   BOOST_REQUIRE_EQUAL(b6->previous, b3->calculate_id());
   BOOST_REQUIRE(!!qc(b6));                             // b6 should include a QC
   BOOST_REQUIRE_EQUAL(qc_s(qc(b6)), strong_qc(b2));    // b6 claims a strong QC on b2

   // ---------------------------------------------------------------------------------------------------
   // After voting on `b5` (which makes `b3` final), the finalizers who voted on `b5` are locked on `b4`,
   // and therefore cannot vote on `b6`:
   //
   // - liveness check fails because: `b6' s core.latest_qc_block_timestamp() <  fsi.lock.timestamp`
   //   because `b2 timestamp < b4 timestamp`.
   // - safety check fails because `b6` does not extend `b4`.
   //
   // As a result, we don't expect the next block (b7) to include a QC
   // ---------------------------------------------------------------------------------------------------

   auto b7 = D.produce_block();                         // D produces a block. It still has not seen b4 and b5.
   print("b7", b7);
   BOOST_REQUIRE(!qc(b7));                              // b7 should not include a QC

} FC_LOG_AND_RETHROW()


/* -----------------------------------------------------------------------------------------------------
            Finality advancing past block claimed on alternate branch
            =========================================================
Producer:    C       C       C       C       C       D       D       D       D
Timestamp:   t1      t2      t3      t4      t5      t6      t7      t8      t9
Blocks:
    b0 <---  b1 <--- b2 <--- b3 <-|- b4 <--- b5
                                  |
                                  \----------------- b6 <--- b7 <--- b8 <--- b9
QC claim:
           Strong  Strong  Strong  Strong  Strong  Strong  Strong   Weak   Strong
             b0      b0      b1      b3      b4      b1      b2      b7      b8

Votes:
  Node A:  Strong‡ Strong‡ Strong‡ Strong           Weak¹   Weak   Strong  Strong
  Node B:  Strong¹ Strong¹ Strong  Strong           Weak¹   Weak   Strong  Strong
  Node C:  Strong  Strong  Strong  Strong  Strong‡  Weak¹   Weak¹  Strong¹ Strong
  Node D:  Strong¹ Strong¹ Strong                  Strong  Strong  Strong  Strong

                                                             ^
                                                             |
                                             Validating the strong QC on b2 should
                                             not fail for nodes which receive b4 and
                                             b5 prior to b7 despite b5 advancing the
                                             fork DB root to b3.

Meaning of the superscripts and marks on the votes:
The vote on block b was delayed in reaching the node for the producer p scheduled
for the block at the next time slot t after block b by enough that a block produced on time by
producer p for time slot t could not possibly utilize the vote in any QC the block could claim.
Furthermore, the delay is such that the earliest time slot at which producer p could
produce a block that utilizes the delayed vote is the time slot (t + d) where ...
¹ ... d = 1.
‡ ... d is infinite meaning the vote may never be received by producer p.

steps mentioned in comments below refer to issue https://github.com/AntelopeIO/spring/issues/751

Diagram below shows the timeline for nodes A, B, C and D receiving blocks b1 through b9.
(x) marks the producer of the block.

step    network partition        A        B        C        D
---------------------------------------------------------------
                                 b1       b1       b1(x)    b1
(3)     A / B C D
(4)                                       b2       b2(x)    b2
(9)                                       b3       b3(x)    b3
(15)    A / B C / D
(18)                                      b4       b4(x)
(20)                                                        b6(x)
(22)    A D / B C
(23)                             b2
(25)                             b3
(26)    A B C / D
(28)                             b4
(30)    A B / C / D
(31)                                               b5(x)
(33)                                                        b7(x)
(35)    A B D / C
(36)                             b6       b6
(38)    A B C D
(39)                             b5       b5
(40)                                               b6
(41,43)                          b7       b7       b7
(44)                             b8       b8       b8       b8(x)
(51)                             b9       b9       b9       b9(x)

--------------------------------------------------------------------------------------------------------*/
BOOST_FIXTURE_TEST_CASE(finality_advancing_past_block_claimed_on_alternate_branch, savanna_cluster::cluster_t) try {
   using namespace savanna_cluster;
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];

   //_debug_mode = true;

   auto b0 = A.produce_block();
   print("b0", b0);

   signed_block_ptr b1, b2, b3, b4, b5, b6, b7, b8, b9;

   set_partition({ &A });

   {
      fc::scoped_set_value tmp_B(B.vote_delay(), 1);         // delay votes from B for 1 slot
      fc::scoped_set_value tmp_D(D.vote_delay(), 1);         // delay votes from D for 1 slot

      b1 = C.produce_block();
      print("b1", b1);
      BOOST_REQUIRE_EQUAL(qc_s(qc(b1)), strong_qc(b0));      // b1 claims a strong QC on b0

      b2 = C.produce_block();
      print("b2", b2);
      BOOST_REQUIRE(!qc(b2));                                // b2 should not include a QC (votes on b1 delayed)
      BOOST_REQUIRE_EQUAL(qc_claim(b2), qc_claim(b1));       // C didn't form a QC on b1, so b2 should repeat b1's claim

      // D doesn't receive B's vote on b2 yet because it is delayed, or A's vote because it is partitioned out
   }

   set_partitions({{ &A }, { &D }});                         // both A and D are isolated by themselves (step 15)

   b3 = C.produce_block();
   print("b3", b3);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b3)), strong_qc(b1));         // b3 claims a strong QC on b1 (B and D votes delayed by 1)

   C.push_blocks_to(D);                                      // we want D to receive b3 (so it can build b6 on it), but no votes
   D.push_vote_to(C, b3->calculate_id());                    // and we want C to get D's vote on b3 so it can form a QC
                                                             // this simulates D being isolated just after receiving b3 and voting
                                                             // on it, but before receiving B and C votes on b3.

   b4 = C.produce_block();
   print("b4", b4);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b4)), strong_qc(b3));         // b4 claims a strong QC on b3 (B and D votes not delayed anymore)

   b6 = D.produce_block(_block_interval_us * 2);             // Node D produces and broadcasts b6 one second early (due
   print("b6", b6);                                          // to clock differences).
   BOOST_REQUIRE_EQUAL(b6->previous, b3->calculate_id());    // b6 has b3 as its parent block
   BOOST_REQUIRE(!qc(b6));                                   // b6 does not include a new qc (lacking votes on b2 and b3)
   BOOST_REQUIRE_EQUAL(qc_claim(b6), qc_claim(b3));          // and repeats b3's strong QC claim on b1.

   C.push_blocks_to(A);                                      // simulates A and D temporarily reconnecting, D sending the blocks
   A.push_vote_to(D, b2->calculate_id());                    // produced by C, A voting on them and D receiving these votes

   set_partition({ &D });                                    // B and C re-establish connection with A (step 26,27)

   C.push_blocks_to(A);                                      // Now that A is reconnected to B and C, it can receive blocks and
   A.push_vote_to(C, b4->calculate_id());                    // vote on them

   set_partitions({ { &C }, { &D } });                       // Node C is isolated from the other nodes (step 30)
                                                             // so A, B and C get b5 after b6

   b5 = C.produce_block();
   print("b5", b5);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b5)), strong_qc(b4));         // b5 claims a strong QC on b4

   b7 = D.produce_block();                                   // Node D produces b7
   print("b7", b7);
   BOOST_REQUIRE_EQUAL(b7->previous, b6->calculate_id());    // b7 has b6 as its parent block
   BOOST_REQUIRE_EQUAL(qc_s(qc(b7)), strong_qc(b2));         // b7 claims a strong QC on b2

   set_partition( { &C } );                                  // step 35

   A.push_block(b6);                                         // don't use `push_blocks_to` because of fork
   B.push_block(b6);                                         // step 36

   set_partition( {} );                                      // step 38

   A.push_block(b5);                                         // A receives b5
   BOOST_REQUIRE_EQUAL(A.lib_number, b3->block_num());       // which advances lib to b3

   B.push_block(b5);                                         // B receives b5
   BOOST_REQUIRE_EQUAL(B.lib_number, b3->block_num());       // which advances lib to b3

   // Following requires issue #694 fix:
   // Nodes A and B have received b5, which has advanced finality to b3.
   // when we push b6 and b7 (produced by D) to these nodes, they will want to verify the QC included in b7 (strong QC on b2).
   // If, in order to verify this QC, they attempt to lookup b2 in fork_db, this will fail because lib (and hence fork_db's root)
   // has advanced to b3.
   // ---------------------------------------------------------------------------------------------------------------------------
   A.push_block(b7);                                         // prior to PR #719 (fixing issue #694), we'd have an exception here
   B.push_block(b7);                                         // prior to PR #719 (fixing issue #694), we'd have an exception here

   C.push_block(b6);
   C.push_block(b7);

   // with issue #694 fixed, A and B were able to successfully validate the received block b7
   // However, unless the separate issue #778 is fixed, A and B would still not vote on b7 (which is added to the fork database
   // but does not become the new best head since b5 has a later `latest_qc_block_timestamp`).
   // ---------------------------------------------------------------------------------------------------------------------------
   b8 = D.produce_block();                                   // Node D produces b8
   print("b8", b8);
   BOOST_REQUIRE_EQUAL(b8->previous, b7->calculate_id());    // b8 has b7 as its parent block
   BOOST_REQUIRE_EQUAL(qc_s(qc(b8)), weak_qc(b7));           // b8 claims a weak QC on b7 (A, B and C voted weak since locked on b4)
                                                             // prior to PR #788 (fixing issue #778), we'd have an test failure here

   b9 = D.produce_block();                                   // Node D produces b9
   print("b9", b9);
   BOOST_REQUIRE_EQUAL(qc_s(qc(b9)), strong_qc(b8));         // b9 claims a strong QC on b8 (all nodes were able to vote strong)

} FC_LOG_AND_RETHROW()

// ------------------------------------------------------------------------------------
// Test that replays blocks from fork_db at startup, and simulating a Ctrl-C
// interruption of that replay.
// (the cluster starts with 9 final blocks and 1 reversible block after the transition
// to Savanna)
// ------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(replay_fork_db_at_startup, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2]; auto& D=_nodes[3];

   // at this point we have 9 final blocks and 1 reversible block

   set_partition( { &C, &D } );                              // partition so blocks aren't finalized
   const size_t num_blocks = 20;

   std::vector<signed_block_ptr> blocks;
   blocks.reserve(num_blocks);
   for (size_t i=0; i<num_blocks; ++i)
      blocks.push_back(A.produce_block());

   const size_t num_fork_db_blocks = A.control->fork_db_size();;
   BOOST_REQUIRE_GT(num_fork_db_blocks, num_blocks);        // A should have 20+ unfinalized blocks in its fork_db (actually 21)

   controller::config copied_config = A.get_config();
   auto               genesis       = block_log::extract_genesis_state(A.get_config().blocks_dir);

   A.close();
   A.remove_state();

   A.open(make_protocol_feature_set(), genesis->compute_chain_id(), [genesis, &control=A.control]() {
      auto check_shutdown = [](){
         static size_t call_idx = 0;
         return ++call_idx >= 15;                         // simulate Ctrl-C being hit on 15th call, so fewer than
                                                          // 21 blocks from fork_db will be replayed.
      };

      control->startup([]() {}, check_shutdown, *genesis);
   } );

   A.close();
   A.open();                                              // open() the node again to make sure it restarts correctly
                                                          // after being interrupted.

   BOOST_REQUIRE_EQUAL(A.control->fork_db_size(), num_fork_db_blocks);

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
