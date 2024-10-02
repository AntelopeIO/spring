#include "savanna_cluster.hpp"

using namespace eosio::chain;
using namespace eosio::testing;

BOOST_AUTO_TEST_SUITE(savanna_disaster_recovery_tests)

// ------------------------------------------------------------------------------------------
// Check that a node can go down cleanly, restart from its existing state, and start voting
// normally again.
// ------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(node_goes_down, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2];

   C.close();                                                                  // shutdown node C
   BOOST_REQUIRE_EQUAL(4u, A.lib_advances_by([&]() { A.produce_blocks(4);  })); // lib still advances with 3 finalizers
   C.open();                                                                   // restart node C
   BOOST_REQUIRE_EQUAL(4u, A.lib_advances_by([&]() { A.produce_blocks(4);  })); // all 4 finalizers should be back voting
   BOOST_REQUIRE(!C.is_head_missing_finalizer_votes());                        // let's make sure of that
} FC_LOG_AND_RETHROW()


// --------------------------------------------------------------------------------------------
// Check that a node can go down, lose its state and fsi, restart from a snapshot using an old
// saved fsi, and start voting normally again.
// --------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(recover_killed_node_with_old_fsi, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2];

   auto fsi = C.save_fsi();
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2);  }));
   auto snapshot = C.snapshot();
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2);  }));
   C.close();
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2);  })); // lib still advances with 3 finalizers
   C.remove_state();
   C.overwrite_fsi(fsi);
   C.open_from_snapshot(snapshot);
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2);  })); // all 4 finalizers should be back voting
   BOOST_REQUIRE(!C.is_head_missing_finalizer_votes());                        // let's make sure of that
} FC_LOG_AND_RETHROW()

// --------------------------------------------------------------------------------------------------
// Check that a node can go down, lose its state and fsi, restart from a snapshot without a fsi, and
// start voting normally again.
// --------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(recover_killed_node_with_deleted_fsi, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2];

   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2);  }));
   auto snapshot = C.snapshot();
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2);  }));
   C.close();
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2);  })); // lib still advances with 3 finalizers
   C.remove_state();
   C.remove_fsi();
   C.open_from_snapshot(snapshot);
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2);  })); // all 4 finalizers should be back voting
   BOOST_REQUIRE(!C.is_head_missing_finalizer_votes());                        // let's make sure of that
} FC_LOG_AND_RETHROW()

// -----------------------------------------------------------------------------------------------
// Check that a node can go down, lose its state (but not its fsi), restart from a snapshot, and
// start voting normally again.
// -----------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(recover_killed_node_while_retaining_fsi, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2];

   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2);  }));
   auto snapshot = C.snapshot();
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2);  }));
   C.close();
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2);  })); // lib still advances with 3 finalizers
   C.remove_state();
   C.open_from_snapshot(snapshot);
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2);  })); // all 4 finalizers should be back voting
   BOOST_REQUIRE(!C.is_head_missing_finalizer_votes());                        // let's make sure of that
} FC_LOG_AND_RETHROW()


// ---------------------------------------------------------------------------------------------------
//                               All but one finalizers go down
// ---------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
// Check that three out of four nodes can go down cleanly, restart from their existing states, and
// start voting normally again.
// ------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(nodes_go_down, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];
   std::array<savanna_cluster::node_t*, 3> failing_nodes { &B, &C, &D };

   for (auto& N : failing_nodes) N->close();
   BOOST_REQUIRE_EQUAL(1u, A.lib_advances_by([&]() { A.produce_blocks(4);  })); // lib stalls with 3 finalizers down, 1 QC in flight
   for (auto& N : failing_nodes) N->open();
   BOOST_REQUIRE_EQUAL(7u, A.lib_advances_by([&]() { A.produce_blocks(4);  })); // all 4 finalizers should be back voting
   for (auto& N : failing_nodes) BOOST_REQUIRE(!N->is_head_missing_finalizer_votes());
} FC_LOG_AND_RETHROW()


// --------------------------------------------------------------------------------------------------
// Check that three out of four nodes can go down, lose their state and fsi, restart from a snapshot
// using an old saved fsi, and start voting normally again.
// --------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(recover_killed_nodes_with_old_fsi, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];
   std::array<savanna_cluster::node_t*, 3> failing_nodes { &B, &C, &D };

   std::vector<std::vector<uint8_t>> fsis;
   std::vector<std::string> snapshots;

   for (auto& N : failing_nodes) fsis.push_back(N->save_fsi());
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2);  }));
   for (auto& N : failing_nodes) snapshots.push_back(N->snapshot());
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2);  }));
   for (auto& N : failing_nodes) N->close();
   BOOST_REQUIRE_EQUAL(1u, A.lib_advances_by([&]() { A.produce_blocks(2);  })); // lib stalls 3 finalizers down, 1 QC in flight
   size_t i = 0;
   for (auto& N : failing_nodes) {
      N->remove_state();
      N->overwrite_fsi(fsis[i]);
      N->open_from_snapshot(snapshots[i]);
      ++i;
   }
   BOOST_REQUIRE_EQUAL(3u, A.lib_advances_by([&]() { A.produce_blocks(2);  })); // all 4 finalizers should be back voting
   for (auto& N : failing_nodes) BOOST_REQUIRE(!N->is_head_missing_finalizer_votes());
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------------------
// Check that three out of four nodes can go down, lose their state and fsi, restart from a snapshot
// without a fsi, and start voting normally again.
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(recover_killed_nodes_with_deleted_fsi, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];
   std::array<savanna_cluster::node_t*, 3> failing_nodes { &B, &C, &D };

   std::vector<std::string> snapshots;

   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2);  }));
   for (auto& N : failing_nodes) snapshots.push_back(N->snapshot());
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2);  }));
   for (auto& N : failing_nodes) N->close();
   BOOST_REQUIRE_EQUAL(1u, A.lib_advances_by([&]() { A.produce_blocks(2);  })); // lib stalls 3 finalizers down, 1 QC in flight
   size_t i = 0;
   for (auto& N : failing_nodes) {
      N->remove_state();
      N->remove_fsi();
      N->open_from_snapshot(snapshots[i]);
      ++i;
   }
   BOOST_REQUIRE_EQUAL(3u, A.lib_advances_by([&]() { A.produce_blocks(2);  })); // all 4 finalizers should be back voting
   for (auto& N : failing_nodes) BOOST_REQUIRE(!N->is_head_missing_finalizer_votes());
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------------------
// Check that three out of four nodes can go down, lose their state (but not their fsi), restart from
// a snapshot, and start voting normally again.
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(recover_killed_nodes_while_retaining_fsi, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];
   std::array<savanna_cluster::node_t*, 3> failing_nodes { &B, &C, &D };

   std::vector<std::string> snapshots;

   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2);  }));
   for (auto& N : failing_nodes) snapshots.push_back(N->snapshot());
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2);  }));
   for (auto& N : failing_nodes) N->close();
   BOOST_REQUIRE_EQUAL(1u, A.lib_advances_by([&]() { A.produce_blocks(2);  })); // lib stalls 3 finalizers down, 1 QC in flight
   size_t i = 0;
   for (auto& N : failing_nodes) {
      N->remove_state();
      N->open_from_snapshot(snapshots[i]);
      ++i;
   }
   BOOST_REQUIRE_EQUAL(3u, A.lib_advances_by([&]() { A.produce_blocks(2);  })); // all 4 finalizers should be back voting
   for (auto& N : failing_nodes) BOOST_REQUIRE(!N->is_head_missing_finalizer_votes());
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------------------
//                      All nodes are shutdown with reversible blocks lost
// ---------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------
// Check that after the network of 4 nodes becomes split 2/2, and that one side produces two more
// blocks with finality stalling, all nodes can go down with their reversible blocks lost and restart
// from an older snapshot.
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(all_nodes_shutdown_with_reversible_blocks_lost, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];
   std::array<savanna_cluster::node_t*, 4> failing_nodes { &A, &B, &C, &D };

   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2);  }));

   // take snapshot
   // -------------
   std::string snapshot { C.snapshot() };

   // verify that all nodes have the same last irreversible block ID (lib_id) and head block ID (h_id)
   // ------------------------------------------------------------------------------------------------
   auto head_id = A.head().id();
   auto head_num = A.head().block_num();
   for (auto& N : failing_nodes) BOOST_REQUIRE_EQUAL(N->head().id(), head_id);

   // produce two blocks so that lib catches up to snapshot's head
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2);  }));

   auto lib_id = A.lib_id;
   auto lib_num = A.lib_number;
   BOOST_REQUIRE_EQUAL(lib_id, head_id);
   for (auto& N : failing_nodes) BOOST_REQUIRE_EQUAL(N->lib_id, lib_id);

   // split network { A, B } and { C, D }
   // A produces two more blocks, so A and B will vote strong but finality will not advance
   // -------------------------------------------------------------------------------------
   set_partition( {&C, &D} );
   BOOST_REQUIRE_EQUAL(1u, A.lib_advances_by([&]() { A.produce_blocks(2);  })); // lib stalls with network partitioned, 1 QC in flight

   // remove network split
   // --------------------
   set_partition({});

   // shutdown all four nodes, delete the state and the reversible data for all nodes, but do not
   // delete the fsi or blocks log restart all four nodes from previously saved snapshot. A and B
   // finalizers will be locked on lib_id's child which was lost.
   // -----------------------------------------------------------------------------------------------
   bool remove_blocks_log = false;
   for (auto& N : failing_nodes) {
      N->close();
      N->remove_state();
      remove_blocks_log ? N->remove_reversible_data_and_blocks_log() : N->remove_reversible_data();
   }

   // reopen after all nodes closed
   // -----------------------------
   for (auto& N : failing_nodes)
      N->open_from_snapshot(snapshot);

   propagate_heads(); // needed only if we don't remove the blocks log, otherwise lib advanced by 1 block
                      // which was stored in the blocks log, and when replayed after loading A and B's
                      // snapshots advanced head() by one

   BOOST_CHECK_EQUAL(A.lib_number, lib_num + (remove_blocks_log ? 0 : 1));
   // verify that lib does not advance and is stuck at lib_id (because validators A and B are locked on a
   // reversible block which has been lost, so they cannot vote any since the claim on the lib block
   // is just copied forward and will always be on a block with a timestamp < that the lock block in
   // the fsi)
   // ----------------------------------------------------------------------------------------------
   BOOST_REQUIRE_EQUAL(0u, A.lib_advances_by([&]() {
      for (size_t i=0; i<4; ++i) {
         A.produce_block();
         for (auto& N : failing_nodes) {
            BOOST_CHECK_EQUAL(N->head().block_num(), head_num + (i + 1) + (remove_blocks_log ? 0 : 1));

            if (N == &A || N == &B) {
               // A and B are locked on a lost block so they cannot vote anymore
               BOOST_CHECK(N->is_head_missing_finalizer_votes());
            } else {
               // C and D should be able to vote after a couple blocks.
               // monotony check can fail for a couple blocks because we voted on
               // two blocks after the snapshot and kept the fsi.
               // NOTE: if `remove_blocks_log == true` C and D may not be able to vote at all because
               // they are also locked on a lost block.
               if (i >= 2)
                  BOOST_CHECK(!N->is_head_missing_finalizer_votes());
            }
         }
      }
   }));

   // shutdown all four nodes again
   // delete every node's fsi
   // restart all four nodes
   // A produces 4 blocks, verify that every node is voting strong again on each new block and that lib advances
   // ----------------------------------------------------------------------------------------------------------
   for (auto& N : failing_nodes) {
      N->close();
      N->remove_fsi();
      N->open();
   }

   // 6 because we produced 8 blocks since the snapshot, and lib trails by two.
   BOOST_REQUIRE_EQUAL(6u, A.lib_advances_by([&]() {
      for (size_t i=0; i<4; ++i) {
         A.produce_block();
         for (auto& N : failing_nodes) {
            BOOST_CHECK(!N->is_head_missing_finalizer_votes());
         }
      }
   }));
} FC_LOG_AND_RETHROW()


// --------------------------------------------------------------------------------------------
// test to reproduce error from issue #709. When starting a node from a snapshot with a fork_db
// containing only the root block, we access `prev_finality_ext` which is empty because the
// header extension cache has not been initialized.
// --------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(restart_from_fork_db_with_only_root_block, savanna_cluster::cluster_t) try {
   auto& C=_nodes[2];

   BOOST_REQUIRE_EQUAL(2u, C.lib_advances_by([&]() { C.produce_blocks(2);  }));
   auto snapshot = C.snapshot();
   signed_block_ptr b1, b2;
   BOOST_REQUIRE_EQUAL(2u, C.lib_advances_by([&]() { b1 = C.produce_block();  b2 = C.produce_block(); }));

   // Partition C by itself, so it doesn't receive b1 and b2 when opebed
   set_partition( {&C} );

   C.close();
   C.remove_state();
   C.remove_reversible_data_and_blocks_log();

   C.open_from_snapshot(snapshot);   // at this point, fork_db's root is the snapshot block, and doesn't contain any other blocks
   C.close();                        // close node
   C.open();                         // and open(), so we get the root block_state from fork_db and not from the snapshot

   C.push_block(b1);                 // when creating the block_state for b1, `prev` will be the root block_state loaded from
                                     // fork_db, which doesn't have the header extension cache created (issue #709)
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()