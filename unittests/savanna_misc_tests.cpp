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
BOOST_FIXTURE_TEST_CASE(weak_masking_issue, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];
   using vote_t = savanna_cluster::node_t::vote_t;

   auto b0 = A.produce_blocks(2);                     // receive strong votes from all finalizers

   // partition D out. D will be used to produce blocks on an alternative fork.
   // We will have 3 finalizers voting which is enough to reach QCs
   // -------------------------------------------------------------------------
   const std::vector<size_t> partition {3};
   set_partition(partition);

   auto b1 = A.produce_block();                       // receives strong votes from 3 finalizers

   auto b2 = D.produce_block(_block_interval_us * 2); // produce a `later` block on D
   push_block(0, b2);                                 // push block to other partition, should receive weak votes
   BOOST_REQUIRE_EQUAL(A.last_vote, vote_t(b2, false));
   BOOST_REQUIRE_EQUAL(B.last_vote, vote_t(b2, false));

   {
      scoped_set_value tmp(B.propagate_votes, false); // temporarily prevent B from broadcasting its votes)
                                                      // so A won't receive them and form a QC on b3

      auto b3 = A.produce_block(_block_interval_us * 3); // b3 should receive 2 weak votes from A and C (not a quorum)
                                                      // because B doesn't propagate and D is partitioned away
      BOOST_REQUIRE_EQUAL(A.last_vote, vote_t(b3, false));
      BOOST_REQUIRE_EQUAL(B.last_vote, vote_t(b3, false));
   }

   std::cout << "lib=" <<  B.lib_number << '\n';

                                                      // Now B broadcasts its votes again, so
   auto b4 = A.produce_block();                       // b4 should receive 3 weak votes from A, B and C
                                                      // and should include a strong QC claim on b1 (repeated)
                                                      // since we don't have enough votes to form a QC on b3
   BOOST_REQUIRE_EQUAL(A.last_vote, vote_t(b4, false));
   BOOST_REQUIRE_EQUAL(B.last_vote, vote_t(b4, false));


   std::cout << "lib after b4=" <<  B.lib_number << '\n';
   auto b5 = A.produce_block();                       // a weak QC was formed on b4 and is be included in b5
                                                      // b5 should receive 3 strong votes (because it has a
                                                      // weak QC on b4, which itself had a strong QC on b1.
                                                      // Upon receiving a strong QC on b5, b1 will be final
   BOOST_REQUIRE_EQUAL(A.last_vote, vote_t(b5, true));
   BOOST_REQUIRE_EQUAL(B.last_vote, vote_t(b5, true));


   std::cout << "lib after b5=" <<  B.lib_number << '\n';
   auto b6 = A.produce_block();                       // should include strong QC on b5, b1 should be final

   BOOST_REQUIRE_EQUAL(A.last_vote, vote_t(b6, true));
   BOOST_REQUIRE_EQUAL(B.last_vote, vote_t(b6, true));

   BOOST_REQUIRE_EQUAL(B.lib_number, b1->block_num());

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
