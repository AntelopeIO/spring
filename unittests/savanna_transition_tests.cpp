#include "savanna_cluster.hpp"

using namespace eosio::chain;
using namespace eosio::testing;

BOOST_AUTO_TEST_SUITE(savanna_transition_tests)

// ---------------------------------------------------------------------------------------------------
// Verify a straightforward transition, with all four nodes healthy and voting
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(straightforward_transition,
                        savanna_cluster::pre_transition_cluster_t) try {
   auto& A=_nodes[0];

   // set one finalizer per node
   // --------------------------
   for (size_t i = 0; i < _nodes.size(); ++i)
      _nodes[i].set_node_finalizers({&_fin_keys[i], 1});

   // set finalizer policy and transition to Savanna
   // ----------------------------------------------
   A.transition_to_savanna({&_fin_keys[0], num_nodes()});

   // A produces blocks, verify lib advances
   BOOST_REQUIRE_EQUAL(3u, A.lib_advances_by([&]() { A.produce_blocks(3); }));
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------------------
// Verify that Savanna transition works correctly even if the network splits a couple blocks after the
// genesis block for some blocks.
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(transition_with_split_network_before_critical_block,
                        savanna_cluster::pre_transition_cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2]; auto& D=_nodes[3];

   // set two producers, so that we have at least one block between the genesis and critical block.
   // with one producer the critical block comes right after the genesis block.
   // with two producers there are 17 blocks in between (genesis=7, critical=25, pt=28)
   // ---------------------------------------------------------------------------------------------
   std::vector<account_name> producers { "pa"_n, "pb"_n };
   A.create_accounts(producers);
   A.set_producers(producers);

   // set one finalizer per node
   // --------------------------
   for (size_t i = 0; i < _nodes.size(); ++i)
      _nodes[i].set_node_finalizers({&_fin_keys[i], 1});

   // set finalizer policy
   // --------------------
   A.set_active_finalizers({&_fin_keys[0], _nodes.size()});

   // `genesis_block` is the first block where set_finalizers() was executed.
   // It is the genesis block.
   // It will include the first header extension for the instant finality.
   // -----------------------------------------------------------------------
   auto genesis_block = A.produce_block();
   dlog("Genesis block number: ${b}", ("b",genesis_block->block_num()));

   A.produce_blocks(2);
   BOOST_REQUIRE_GT(genesis_block->block_num(), A.lib_num()); // make sure we are before the critical block

   // partition network and produce blocks
   // ----------------------------------------
   set_partition( {&C, &D} );
   A.produce_blocks(20);

   // verify that lib has stalled
   // ---------------------------
   BOOST_REQUIRE_EQUAL(0u, num_lib_advancing([&]() { A.produce_blocks(10);  }));

   // remove network split
   // --------------------
   set_partition({});
   propagate_heads();

   // A produces 1 block. check that we have reached the critical block.
   // -----------------------------------------------------------------
   auto b = A.produce_block();
   BOOST_REQUIRE_GE(A.lib_num(), genesis_block->block_num()); // lib has reached genesis block
   BOOST_REQUIRE(b->is_proper_svnn_block());

   // with partition gone, transition to Savanna will complete and lib will start advancing again
   // -------------------------------------------------------------------------------------------
   BOOST_REQUIRE_EQUAL(num_nodes(), num_lib_advancing([&]() { A.produce_blocks(4);  }));
   BOOST_REQUIRE_EQUAL(3u, A.lib_advances_by([&]() { A.produce_blocks(3); }));
   BOOST_REQUIRE_EQUAL(A.head().block_num(), A.lib_num() + 2); // check that lib is 2 behind head

} FC_LOG_AND_RETHROW()


// ---------------------------------------------------------------------------------------------------
// Verify that Savanna transition works correctly even if the network splits a couple blocks after the
// genesis block, and some nodes go down and restart from a snapshot
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(restart_from_snapshot_at_beginning_of_transition_while_preserving_fsi,
                        savanna_cluster::pre_transition_cluster_t) try {
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];
   std::array<savanna_cluster::node_t*, 3> failing_nodes { &B, &C, &D };

   // set two producers, so that we have at least one block between the genesis and critical block.
   // with one producer the critical block comes right after the genesis block.
   // with two producers there are 17 blocks in between (genesis=7, critical=25, pt=28)
   // ---------------------------------------------------------------------------------------------
   std::vector<account_name> producers { "pa"_n, "pb"_n };
   A.create_accounts(producers);
   A.set_producers(producers);

   // set one finalizer per node
   // --------------------------
   for (size_t i = 0; i < _nodes.size(); ++i)
      _nodes[i].set_node_finalizers({&_fin_keys[i], 1});

   // set finalizer policy
   // --------------------
   A.set_active_finalizers({&_fin_keys[0], _nodes.size()});

   // -------------------------
   // and transition to Savanna
   // -------------------------

   // `genesis_block` is the first block where set_finalizers() was executed.
   // It will include the first header extension for the instant finality.
   // -----------------------------------------------------------------------
   auto genesis_block = A.produce_block();
   dlog("Genesis block number: ${b}", ("b",genesis_block->block_num()));

   A.produce_blocks(2);
   BOOST_REQUIRE_GT(genesis_block->block_num(), A.lib_num()); // make sure we are before the critical block

   // partition network and produce blocks
   // ----------------------------------------
   set_partition( {&C, &D} );
   A.produce_blocks(2);

   auto snapshot_C = C.snapshot();
   A.produce_blocks(5);          // arbitrary number, should be less than 14 so we don't reach the critical block

   // we can't leave the blocks log as it doesn't contain the snapshot's head block.
   for (auto& N : failing_nodes) {
      N->close();
      N->remove_reversible_data_and_blocks_log();
      N->remove_state();
   }

   // remove network split and restart the nodes from the snapshot
   // ------------------------------------------------------------
   set_partition({});

   for (auto& N : failing_nodes) {
      N->open_from_snapshot(snapshot_C);
   }

   // A produces blocks until we reach the critical block (i.e. until lib advances past the genesis block)
   // ----------------------------------------------------------------------------------------------------
   signed_block_ptr critical_block = nullptr;
   while (A.lib_num() < genesis_block->block_num())
      critical_block = A.produce_block();
   BOOST_REQUIRE(!critical_block->is_proper_svnn_block());   // critical block is not a proper savanna block yet
   dlog("Critical block number: ${b}", ("b",critical_block->block_num()));

   // A produces 1 block, which will be the first proper savanna block
   // it will take 2 more blocks for lib to start advancing again according to Savanna consensus.
   // -------------------------------------------------------------------------------------------
   signed_block_ptr b = A.produce_block();
   BOOST_REQUIRE(b->is_proper_svnn_block());
   dlog("First proper block number: ${b}", ("b",b->block_num()));

   // with partition gone, transition to Savanna will complete and lib will start advancing again
   // -------------------------------------------------------------------------------------------
   BOOST_REQUIRE_EQUAL(0u, num_lib_advancing([&]() { A.produce_blocks(1); }));
   BOOST_REQUIRE_EQUAL(num_nodes(), num_lib_advancing([&]() { b = A.produce_blocks(1); }));
   dlog("Lib starts advancing again according to Savanna consensus at block number: ${b}", ("b",b->block_num()));

   // Check that lib keeps advancing when we produce blocks.
   // ------------------------------------------------------
   BOOST_REQUIRE_EQUAL(3u, A.lib_advances_by([&]() { A.produce_blocks(3); }));
   BOOST_REQUIRE_EQUAL(A.head().block_num(), A.lib_num() + num_chains_to_final); // check that lib is 2 behind head
} FC_LOG_AND_RETHROW()


// ---------------------------------------------------------------------------------------------------
// Verify that Savanna transition works correctly even if the network splits a right after the
// critical block, and some nodes go down and restart from a snapshot taken at that point.
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(restart_from_snapshot_at_end_of_transition_while_preserving_fsi,
                        savanna_cluster::pre_transition_cluster_t) try {
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];
   std::array<savanna_cluster::node_t*, 3> failing_nodes { &B, &C, &D };

   // set two producers, so that we have at least one block between the genesis and critical block.
   // with one producer the critical block comes right after the genesis block.
   // with two producers there are 17 blocks in between (genesis=7, critical=25, pt=28)
   // ---------------------------------------------------------------------------------------------
   std::vector<account_name> producers { "pa"_n, "pb"_n };
   A.create_accounts(producers);
   A.set_producers(producers);

   // set one finalizer per node
   // --------------------------
   for (size_t i = 0; i < _nodes.size(); ++i)
      _nodes[i].set_node_finalizers({&_fin_keys[i], 1});

   // set finalizer policy
   // --------------------
   A.set_active_finalizers({&_fin_keys[0], _nodes.size()});

   // -------------------------
   // and transition to Savanna
   // -------------------------

   // `genesis_block` is the first block where set_finalizers() was executed.
   // It will include the first header extension for the instant finality.
   // -----------------------------------------------------------------------
   auto genesis_block = A.produce_block();
   dlog("Genesis block number: ${b}", ("b",genesis_block->block_num()));

   A.produce_blocks(2);
   BOOST_REQUIRE_GT(genesis_block->block_num(), A.lib_num()); // make sure we are before the critical block

   // A produces blocks until we reach the critical block (i.e. until lib advances past the genesis block)
   // ----------------------------------------------------------------------------------------------------
   signed_block_ptr critical_block = nullptr;
   while (A.lib_num() < genesis_block->block_num())
      critical_block = A.produce_block();
   BOOST_REQUIRE(!critical_block->is_proper_svnn_block());   // critical block is not a proper savanna block yet
   dlog("Critical block number: ${b}", ("b",critical_block->block_num()));

   // partition network and produce a block, which will be the first proper savanna block
   // -----------------------------------------------------------------------------------
   set_partition( {&C, &D} );

   signed_block_ptr b = A.produce_block();
   BOOST_REQUIRE(b->is_proper_svnn_block());
   dlog("First proper block number: ${b}", ("b",b->block_num()));

   auto snapshot_C = C.snapshot();

   // we can't leave the blocks log as it doesn't contain the snapshot's head block.
   for (auto& N : failing_nodes) {
      N->close();
      N->remove_reversible_data_and_blocks_log();
      N->remove_state();
   }

   // remove network split and restart the nodes from the snapshot
   // ------------------------------------------------------------
   set_partition({});

   for (auto& N : failing_nodes) {
      N->open_from_snapshot(snapshot_C);
   }

   // with partition gone, transition to Savanna will complete and lib will start advancing again
   // -------------------------------------------------------------------------------------------
   BOOST_REQUIRE_EQUAL(0u, num_lib_advancing([&]() { A.produce_blocks(1); }));
   BOOST_REQUIRE_EQUAL(num_nodes(), num_lib_advancing([&]() { b = A.produce_blocks(1); }));
   dlog("Lib starts advancing again according to Savanna consensus at block number: ${b}", ("b",b->block_num()));

   // Check that lib keeps advancing when we produce blocks.
   // ------------------------------------------------------
   BOOST_REQUIRE_EQUAL(3u, A.lib_advances_by([&]() { A.produce_blocks(3); }));
   BOOST_REQUIRE_EQUAL(A.head().block_num(), A.lib_num() + num_chains_to_final); // check that lib is 2 behind head
} FC_LOG_AND_RETHROW()


// ---------------------------------------------------------------------------------------------------
// Verify that Savanna transition works correctly even if the network splits a couple blocks after the
// genesis block, and some nodes go down and restart from a snapshot with no fsi file.
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(restart_from_snapshot_at_beginning_of_transition_with_lost_fsi,
                        savanna_cluster::pre_transition_cluster_t) try {
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];
   std::array<savanna_cluster::node_t*, 3> failing_nodes { &B, &C, &D };

   // set two producers, so that we have at least one block between the genesis and critical block.
   // with one producer the critical block comes right after the genesis block.
   // with two producers there are 17 blocks in between (genesis=7, critical=25, pt=28)
   // ---------------------------------------------------------------------------------------------
   std::vector<account_name> producers { "pa"_n, "pb"_n };
   A.create_accounts(producers);
   A.set_producers(producers);

   // set one finalizer per node
   // --------------------------
   for (size_t i = 0; i < _nodes.size(); ++i)
      _nodes[i].set_node_finalizers({&_fin_keys[i], 1});

   // set finalizer policy
   // --------------------
   A.set_active_finalizers({&_fin_keys[0], _nodes.size()});

   // -------------------------
   // and transition to Savanna
   // -------------------------

   // `genesis_block` is the first block where set_finalizers() was executed.
   // It will include the first header extension for the instant finality.
   // -----------------------------------------------------------------------
   auto genesis_block = A.produce_block();
   dlog("Genesis block number: ${b}", ("b",genesis_block->block_num()));

   A.produce_blocks(2);
   BOOST_REQUIRE_GT(genesis_block->block_num(), A.lib_num()); // make sure we are before the critical block

   // partition network and produce blocks
   // ----------------------------------------
   set_partition( {&C, &D} );
   A.produce_blocks(2);

   auto snapshot_C = C.snapshot();
   A.produce_blocks(5);          // arbitrary number, should be less than 14 so we don't reach the critical block

   // we can't leave the blocks log as it doesn't contain the snapshot's head block.
   for (auto& N : failing_nodes) {
      N->close();
      N->remove_reversible_data_and_blocks_log();
      N->remove_state();
      N->remove_fsi();           // ** remove fsi - only difference with previous test `..._while_preserving_fsi`
   }

   // remove network split and restart the nodes from the snapshot
   // ------------------------------------------------------------
   set_partition({});

   for (auto& N : failing_nodes) {
      N->open_from_snapshot(snapshot_C);
   }

   // A produces blocks until we reach the critical block (i.e. until lib advances past the genesis block)
   // ----------------------------------------------------------------------------------------------------
   signed_block_ptr critical_block = nullptr;
   while (A.lib_num() < genesis_block->block_num())
      critical_block = A.produce_block();
   BOOST_REQUIRE(!critical_block->is_proper_svnn_block());   // critical block is not a proper savanna block yet
   dlog("Critical block number: ${b}", ("b",critical_block->block_num()));

   // A produces 1 block, which will be the first proper savanna block
   // it will take 2 more blocks for lib to start advancing again according to Savanna consensus.
   // -------------------------------------------------------------------------------------------
   signed_block_ptr b = A.produce_block();
   BOOST_REQUIRE(b->is_proper_svnn_block());
   dlog("First proper block number: ${b}", ("b",b->block_num()));

   // with partition gone, transition to Savanna will complete and lib will start advancing again
   // -------------------------------------------------------------------------------------------
   BOOST_REQUIRE_EQUAL(0u, num_lib_advancing([&]() { A.produce_blocks(1); }));
   BOOST_REQUIRE_EQUAL(num_nodes(), num_lib_advancing([&]() { b = A.produce_blocks(1); }));
   dlog("Lib starts advancing again according to Savanna consensus at block number: ${b}", ("b",b->block_num()));

   // Check that lib keeps advancing when we produce blocks.
   // ------------------------------------------------------
   BOOST_REQUIRE_EQUAL(3u, A.lib_advances_by([&]() { A.produce_blocks(3); }));
   BOOST_REQUIRE_EQUAL(A.head().block_num(), A.lib_num() + num_chains_to_final); // check that lib is 2 behind head
} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_SUITE_END()