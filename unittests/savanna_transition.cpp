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

   // -------------------------
   // and transition to Savanna
   // -------------------------

   // `genesis_block` is the first block where set_finalizers() was executed.
   // It is the genesis block.
   // It will include the first header extension for the instant finality.
   // -----------------------------------------------------------------------
   auto genesis_block = A.produce_block();
   dlog("Genesis block number: ${b}", ("b",genesis_block->block_num()));

   // wait till the genesis_block becomes irreversible.
   // The critical block is the block that makes the genesis_block irreversible
   // -------------------------------------------------------------------------
   signed_block_ptr critical_block = nullptr;  // last value of this var is the critical block
   auto genesis_block_num = genesis_block->block_num();
   while(genesis_block_num > A.lib_block->block_num())
      critical_block = A.produce_block();

   // partition network
   // ----------------------------------------
   const std::vector<size_t> partition {2, 3};
   set_partition(partition);

   // verify that lib doesn't advance even after producing ten more blocks
   // --------------------------------------------------------------
   BOOST_REQUIRE_EQUAL(0, num_lib_advancing([&]() { A.produce_blocks(10);  }));

   // remove network split
   // --------------------
   set_partition({});
   propagate_heads();

   // with partition gone, transition to Savanna will complete and lib will start advancing again
   // -------------------------------------------------------------------------------------------
   BOOST_REQUIRE_EQUAL(num_nodes(), num_lib_advancing([&]() { A.produce_blocks(10);  }));

} FC_LOG_AND_RETHROW()


// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(restart_from_snapshot_at_beginning_of_transition_while_preserving_blocks,
                        savanna_cluster::pre_transition_cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2];

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

   // wait till the genesis_block becomes irreversible.
   // The critical block is the block that makes the genesis_block irreversible
   // -------------------------------------------------------------------------
   signed_block_ptr critical_block = nullptr;  // last value of this var is the critical block
   auto genesis_block_num = genesis_block->block_num();
   while(genesis_block_num > A.lib_block->block_num())
      critical_block = A.produce_block();

   // partition network
   // ----------------------------------------
   const std::vector<size_t> partition {2, 3};
   set_partition(partition);

   // verify that lib doesn't advance even after producing ten more blocks
   // --------------------------------------------------------------
   BOOST_REQUIRE_EQUAL(0, num_lib_advancing([&]() { A.produce_blocks(10);  }));

   // take snapshot
   // -------------
   std::string snapshot { C.snapshot() };

   // remove network split
   // --------------------
   set_partition({});
   propagate_heads();

   BOOST_REQUIRE_EQUAL(num_nodes(), num_lib_advancing([&]() { A.produce_blocks(10);  }));
} FC_LOG_AND_RETHROW()



BOOST_AUTO_TEST_SUITE_END()