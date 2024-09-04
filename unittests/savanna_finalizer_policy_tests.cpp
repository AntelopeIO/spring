#include "savanna_cluster.hpp"

using namespace eosio::chain;
using namespace eosio::testing;

BOOST_AUTO_TEST_SUITE(savanna_finalizer_policy)

// ---------------------------------------------------------------------------------------------------
//     Policy change - new key on one node - node shutdown and restarted while policy pending
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(policy_change, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2];

   C.close();
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2); }));

   // update finalizer_policy with a new key for B
   // --------------------------------------------
   base_tester::finalizer_policy_input input;
   for (size_t i=0; i<num_nodes(); ++i)
      input.finalizers.emplace_back(_fin_keys[i], 1);
   input.finalizers[1] = { _fin_keys[num_nodes()], 1 }; // overwrite finalizer key for B
   input.threshold =  (input.finalizers.size() * 2) / 3 + 1;
   A.set_finalizers(input);
   A.produce_block();                                   // so the block with `set_finalizers` is `head`

   // produce blocks on A, waiting for the new policy to become pending
   // -----------------------------------------------------------------
   BOOST_REQUIRE(!A.head_pending_finalizer_policy());     // we shouldn't have a pending policy
   size_t num_to_pending = 0;
   do {
      A.produce_block();
      ++num_to_pending;
   } while (!A.head_pending_finalizer_policy());
   BOOST_REQUIRE_EQUAL(num_to_pending, num_chains_to_final); // becomes pending when proposed block is final

   // now that the new policy is pending, we need B to vote on it for finality to advance, as C is down.
   // --------------------------------------------------------------------------------------------------
   B.close();
   // update `B.node_finalizers` with the new key so that B can vote both on the active and pending policy
   B.set_node_finalizers(std::vector<account_name>{ _fin_keys[1], _fin_keys[num_nodes()] }); // see node_t::node_t
   B.open();

   // quick sanity check
   // ------------------
   BOOST_REQUIRE(*A.head_active_finalizer_policy()  == *B.head_active_finalizer_policy());
   BOOST_REQUIRE(*A.head_pending_finalizer_policy() == *B.head_pending_finalizer_policy());
   BOOST_REQUIRE_EQUAL(A.head().id(), B.head().id());

   // produce blocks on A, waiting for transition to complete (until the updated policy is active on A's head)
   // --------------------------------------------------------------------------------------------------------
   auto finpol = A.head_active_finalizer_policy();
   auto current_gen  = finpol->generation;
   auto expected_gen = A.head_pending_finalizer_policy()->generation;
   BOOST_REQUIRE_GT(expected_gen, current_gen);

   size_t num_to_active = 0;
   do {
      A.produce_block();
      finpol = A.head_active_finalizer_policy();
      ++num_to_active;
   } while (finpol->generation != expected_gen);
   BOOST_REQUIRE_EQUAL(num_to_active, num_chains_to_final + 1); // becomes active when "pending" block is final

   // A produces blocks, verify lib advances
   // --------------------------------------
   BOOST_REQUIRE_EQUAL(3u, A.lib_advances_by([&]() { A.produce_blocks(3); }));
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------------------
//                           Policy change including weight and threshold
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(policy_change_including_weight_and_threshold, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];
   auto initial_gen = A.head_active_finalizer_policy()->generation;

   // shutdown C, verify that lib still advances (since threshold is 3)
   // -----------------------------------------------------------------
   C.close();
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2); }));

   // update finalizer_policy, so that C's weight is 3, B and D are removed, and the threshold is 4
   // ---------------------------------------------------------------------------------------------
   base_tester::finalizer_policy_input input;
   input.finalizers.emplace_back(_fin_keys[0], 1);
   input.finalizers.emplace_back(_fin_keys[2], 3);
   input.threshold = 4;
   A.set_finalizers(input);
   A.produce_block();                                   // so the block with `set_finalizers` is `head`

   // produce blocks on A, waiting for the new policy to become pending
   // -----------------------------------------------------------------
   BOOST_REQUIRE(!A.head_pending_finalizer_policy());     // we shouldn't have a pending policy
   size_t num_to_pending = 0;
   do {
      A.produce_block();
      ++num_to_pending;
   } while (!A.head_pending_finalizer_policy());
   BOOST_REQUIRE_EQUAL(num_to_pending, num_chains_to_final); // becomes pending when proposed block is final

   // verify that lib stops advancing (because C is down so we can't get a QC on the pending policy
   // which needs three C votes)
   // ---------------------------------------------------------------------------------------------
   BOOST_REQUIRE_EQUAL(0u, A.lib_advances_by([&]() { A.produce_blocks(2); }));

   // restart C.
   // ---------
   C.open();

   // produce blocks on A, waiting for transition to complete (until the updated policy is active on A's head)
   // --------------------------------------------------------------------------------------------------------
   size_t num_to_active = 0;
   do {
      A.produce_block();
      ++num_to_active;
   } while (A.head_active_finalizer_policy()->generation != initial_gen+1);
   BOOST_REQUIRE_EQUAL(num_to_active, num_chains_to_final); // becomes active when "pending" block is final

   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2); }));

   // shutdown B and D which are not used in new policy.
   // A produces 2 blocks, verify that *lib* advances by 2
   // ----------------------------------------------------
   B.close();
   D.close();
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2); }));

} FC_LOG_AND_RETHROW()


// ---------------------------------------------------------------------------------------------------
//                    Policy change, reducing threshold and replacing all keys
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(policy_change_reduce_threshold_replace_all_keys, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];
   auto initial_gen = A.head_active_finalizer_policy()->generation;

   // shutdown D, verify that lib still advances (since threshold is 3)
   // -----------------------------------------------------------------
   D.close();
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2); }));

   // update signing keys on each of { A, B }, so every node has 2 keys, the previous one + a new one
   // -----------------------------------------------------------------------------------------------
   A.close();
   B.close();
   A.set_node_finalizers(std::vector<account_name>{ _fin_keys[0], _fin_keys[num_nodes()] });
   B.set_node_finalizers(std::vector<account_name>{ _fin_keys[1], _fin_keys[num_nodes() + 1] });
   A.open();
   B.open();

   // verify that lib still advances even though D is down (since threshold is 3)
   // ---------------------------------------------------------------------------
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2); }));

   // update finalizer_policy to include only { A, B }'s new keys, and threshold is 2
   // -------------------------------------------------------------------------------
   base_tester::finalizer_policy_input input;
   input.finalizers.emplace_back(_fin_keys[num_nodes()], 1);
   input.finalizers.emplace_back(_fin_keys[num_nodes() + 1], 1);
   input.threshold = 2;
   A.set_finalizers(input);
   A.produce_block();                                   // so the block with `set_finalizers` is `head`

   // produce blocks on A, waiting for the new policy to become pending
   // -----------------------------------------------------------------
   BOOST_REQUIRE(!A.head_pending_finalizer_policy());     // we shouldn't have a pending policy
   size_t num_to_pending = 0;
   do {
      A.produce_block();
      ++num_to_pending;
   } while (!A.head_pending_finalizer_policy());
   BOOST_REQUIRE_EQUAL(num_to_pending, num_chains_to_final); // becomes pending when proposed block is final

   // produce blocks on A, waiting for the new policy to become final
   // ---------------------------------------------------------------
   size_t num_to_active = 0;
   do {
      A.produce_block();
      ++num_to_active;
   } while (A.head_active_finalizer_policy()->generation != initial_gen+1);
   BOOST_REQUIRE_EQUAL(num_to_active, num_chains_to_final); // becomes active when "pending" block is final

   // A produces 2 blocks, verify that *lib* advances by 2
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2); }));

   // shutdown C and D which are not used in new policy.
   // A produces 2 blocks, verify that *lib* advances by 2
   // ----------------------------------------------------
   C.close();
   D.close();
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2); }));

} FC_LOG_AND_RETHROW()


// ---------------------------------------------------------------------------------------------------
//                    Policy change: restart from snapshot
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(policy_change_restart_from_snapshot, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];
   auto initial_gen = A.head_active_finalizer_policy()->generation;

   // update signing keys on each of { A, B, C }, so every node has 2 keys, the previous one + a new one
   // --------------------------------------------------------------------------------------------------
   A.close();
   B.close();
   C.close();
   A.set_node_finalizers(std::vector<account_name>{ _fin_keys[0], _fin_keys[num_nodes()] });
   B.set_node_finalizers(std::vector<account_name>{ _fin_keys[1], _fin_keys[num_nodes() + 1] });
   C.set_node_finalizers(std::vector<account_name>{ _fin_keys[2], _fin_keys[num_nodes() + 2] });
   A.open();
   B.open();
   C.open();

   // update the  *finalizer_policy* to include only { A, B, C }'s new keys, C's weight is 2, and threshold is 3
   // ----------------------------------------------------------------------------------------------------------
   base_tester::finalizer_policy_input input;
   input.finalizers.emplace_back(_fin_keys[num_nodes()], 1);
   input.finalizers.emplace_back(_fin_keys[num_nodes() + 1], 1);
   input.finalizers.emplace_back(_fin_keys[num_nodes() + 2], 2);
   input.threshold = 3;
   A.set_finalizers(input);
   A.produce_block();                                   // so the block with `set_finalizers` is `head`

   // Take a snapshot of C. Produce 2 blocks on A so the snapshot block is stored in block log
   // ----------------------------------------------------------------------------------------
   auto snapshot_C = C.snapshot();
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2); }));

   // for each of { A, B, C, D }, shutdown, delete *state*, but not *blocks log*, *reversible data* or *fsi*
   // ------------------------------------------------------------------------------------------------------
   for (size_t i=0; i<4; ++i) {
      _nodes[i].close();
      _nodes[i].remove_state();
   }

   // for each of { A, B, D }, restart from the snapshot
   // --------------------------------------------------
   A.open_from_snapshot(snapshot_C);
   B.open_from_snapshot(snapshot_C);
   D.open_from_snapshot(snapshot_C);

   // A produces 4 blocks, verify that *lib* advances only by one and that the new policy is only pending
   // (because  C is down so no quorum on new policy)
   // ---------------------------------------------------------------------------------------------------
   BOOST_REQUIRE_EQUAL(1u, A.lib_advances_by([&]() { A.produce_blocks(4); }));
   BOOST_REQUIRE(!!A.head_pending_finalizer_policy());

   // restart C from the snapshot
   // ---------------------------
   C.open_from_snapshot(snapshot_C);

   // A produces 4 blocks, verify that the new policy is active and *lib* starts advancing again
   // ------------------------------------------------------------------------------------------
   BOOST_REQUIRE_LT(4u, A.lib_advances_by([&]() { A.produce_blocks(4); }));
   BOOST_REQUIRE_EQUAL(A.head_active_finalizer_policy()->generation, initial_gen+1);

   // shutdown B and D. A produces 3 blocks, verify that *lib* advances by 3
   // (because together A and C meet the 3 votes quorum for the new policy)
   // ----------------------------------------------------------------------
   B.close();
   D.close();
   BOOST_REQUIRE_EQUAL(3u, A.lib_advances_by([&]() { A.produce_blocks(3); }));

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
BOOST_FIXTURE_TEST_CASE(policy_change_restart_from_snapshot_only, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];
   auto initial_gen = A.head_active_finalizer_policy()->generation;

   // update signing keys on each of { A, B, C }, so every node has 2 keys, the previous one + a new one
   // --------------------------------------------------------------------------------------------------
   A.close();
   B.close();
   C.close();
   A.set_node_finalizers(std::vector<account_name>{ _fin_keys[0], _fin_keys[num_nodes()] });
   B.set_node_finalizers(std::vector<account_name>{ _fin_keys[1], _fin_keys[num_nodes() + 1] });
   C.set_node_finalizers(std::vector<account_name>{ _fin_keys[2], _fin_keys[num_nodes() + 2] });
   A.open();
   B.open();
   C.open();

   // update the  *finalizer_policy* to include only { A, B, C }'s new keys, C's weight is 2, and threshold is 3
   // ----------------------------------------------------------------------------------------------------------
   base_tester::finalizer_policy_input input;
   input.finalizers.emplace_back(_fin_keys[num_nodes()], 1);
   input.finalizers.emplace_back(_fin_keys[num_nodes() + 1], 1);
   input.finalizers.emplace_back(_fin_keys[num_nodes() + 2], 2);
   input.threshold = 3;
   A.set_finalizers(input);
   A.produce_block();                                   // so the block with `set_finalizers` is `head`
   C.close();                                           // C down, so no quorum

   A.produce_block();
   A.produce_block();                                   // advance so pending policy
   BOOST_TEST(!!A.head_pending_finalizer_policy());

   // Take a snapshot of A
   // ----------------------------------------------------------------------------------------
   auto snapshot_A = A.snapshot();

   // for each of { B, C, D }, shutdown, delete *state* and *blocks log*, *reversible data*, but not *fsi*
   // ------------------------------------------------------------------------------------------------------
   for (size_t i=1; i<4; ++i) {
      _nodes[i].close();
      _nodes[i].remove_state();
      _nodes[i].remove_reversible_data_and_blocks_log();
   }

   A.produce_block();
   A.produce_block();

   // for each of { B, C, D }, restart from the snapshot
   // --------------------------------------------------
   C.open_from_snapshot(snapshot_A);
   B.open_from_snapshot(snapshot_A);
   D.open_from_snapshot(snapshot_A);

   // A produces 1 blocks, verify that *lib* advances only by one and that the new policy is only pending
   // ---------------------------------------------------------------------------------------------------
   BOOST_REQUIRE_EQUAL(1u, A.lib_advances_by([&]() { A.produce_blocks(1); }));
   BOOST_REQUIRE(!!A.head_pending_finalizer_policy());

   // A produces 4 blocks, verify that the new policy is active and *lib* starts advancing again
   // ------------------------------------------------------------------------------------------
   BOOST_REQUIRE_LT(4u, A.lib_advances_by([&]() { A.produce_blocks(4); }));
   BOOST_REQUIRE_EQUAL(A.head_active_finalizer_policy()->generation, initial_gen+1);

   // shutdown B and D. A produces 3 blocks, verify that *lib* advances by 3
   // (because together A and C meet the 3 votes quorum for the new policy)
   // ----------------------------------------------------------------------
   B.close();
   D.close();
   BOOST_REQUIRE_EQUAL(3u, A.lib_advances_by([&]() { A.produce_blocks(3); }));

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()