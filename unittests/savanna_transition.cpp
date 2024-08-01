#include "savanna_cluster.hpp"

using namespace eosio::chain;
using namespace eosio::testing;

BOOST_AUTO_TEST_SUITE(savanna_transition)

// ---------------------------------------------------------------------------------------------------
//                               Single finalizer goes down
// ---------------------------------------------------------------------------------------------------

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
   BOOST_REQUIRE_EQUAL(3, A.lib_advances_by([&]() { A.produce_blocks(3); }));
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(transition_with_split_network_before_critical_block,
                        savanna_cluster::pre_transition_cluster_t) try {
   auto& A=_nodes[0];

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
   BOOST_REQUIRE_GT(genesis_block->block_num(), A.lib_block->block_num()); // make sure we are before the critical block

   // partition network and produce blocks
   // ----------------------------------------
   const std::vector<size_t> partition {2, 3};
   set_partition(partition);
   A.produce_blocks(20);

   // verify that lib has stalled
   // ---------------------------
   BOOST_REQUIRE_EQUAL(0, num_lib_advancing([&]() { A.produce_blocks(10);  }));

   // remove network split
   // --------------------
   set_partition({});
   propagate_heads();

   // A produces 1 block. check that we have reached the critical block.
   // -----------------------------------------------------------------
   auto b = A.produce_block();
   BOOST_REQUIRE_GE(A.lib_block->block_num(), genesis_block->block_num()); // lib has reached genesis block
   BOOST_REQUIRE(b->is_proper_svnn_block());

   // with partition gone, transition to Savanna will complete and lib will start advancing again
   // -------------------------------------------------------------------------------------------
   BOOST_REQUIRE_EQUAL(num_nodes(), num_lib_advancing([&]() { A.produce_blocks(4);  }));
   BOOST_REQUIRE_EQUAL(3, A.lib_advances_by([&]() { A.produce_blocks(3); }));
   BOOST_REQUIRE_EQUAL(A.head().block_num(), A.lib_block->block_num() + 2); // check that lib is 2 behind head

} FC_LOG_AND_RETHROW()


// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(restart_from_snapshot_at_beginning_of_transition_while_preserving_blocks,
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
   // It is the genesis block.
   // It will include the first header extension for the instant finality.
   // -----------------------------------------------------------------------
   auto genesis_block = A.produce_block();
   dlog("Genesis block number: ${b}", ("b",genesis_block->block_num()));

   A.produce_blocks(2);
   BOOST_REQUIRE_GT(genesis_block->block_num(), A.lib_block->block_num()); // make sure we are before the critical block

   // partition network and produce blocks
   // ----------------------------------------
   const std::vector<size_t> partition {2, 3};
   set_partition(partition);
   A.produce_blocks(2);

   auto snapshot_C = C.snapshot();
   A.produce_blocks(15);

   // we can't leave the blocks log as it doesn't contain the snapshot's head block.
   for (auto& N : failing_nodes) {
      N->close();
      N->remove_reversible_data_and_blocks_log();
      // N->remove_blocks_log(); // if we remove the blocks log and leave the fork_db, the test fails.
      N->remove_state();
   }

   // remove network split
   // --------------------
   set_partition({});

   for (auto& N : failing_nodes) {
      N->open_from_snapshot(snapshot_C);
      A.push_blocks_to(*N);
   }

   // A produces 1 block. check that we have reached the critical block.
   // -----------------------------------------------------------------
   auto b = A.produce_block();
   BOOST_REQUIRE_GE(A.lib_block->block_num(), genesis_block->block_num()); // lib has reached genesis block
   BOOST_REQUIRE(b->is_proper_svnn_block());

   // with partition gone, transition to Savanna will complete and lib will start advancing again
   // -------------------------------------------------------------------------------------------
   BOOST_REQUIRE_EQUAL(num_nodes(), num_lib_advancing([&]() { A.produce_blocks(30);  }));
   BOOST_REQUIRE_EQUAL(3, A.lib_advances_by([&]() { A.produce_blocks(3); }));
   BOOST_REQUIRE_EQUAL(A.head().block_num(), A.lib_block->block_num() + 2); // check that lib is 2 behind head
} FC_LOG_AND_RETHROW()



BOOST_AUTO_TEST_SUITE_END()