#include "savanna_cluster.hpp"

// ------------------------
// Test the savanna_cluster
// ------------------------
using namespace eosio::chain;

BOOST_AUTO_TEST_SUITE(savanna_cluster_tests)

// test set_finalizer host function serialization and tester set_finalizers
BOOST_FIXTURE_TEST_CASE(simple_test, savanna_cluster::cluster_t) { try {
      auto& C=_nodes[2]; auto& D=_nodes[3];

      auto node3_lib = _nodes[3].lib_num();                 // Store initial lib (after Savanna transtion)
      BOOST_REQUIRE_EQUAL(num_nodes(), num_lib_advancing([&]() { // Check that lib advances on all nodes
         _nodes[0].produce_block();                         // Blocks & votes are propagated to all connected peers.
         _nodes[0].produce_block();                         // and by default all nodes are interconnected
      }));
      BOOST_REQUIRE_EQUAL(_nodes[3].lib_num(), node3_lib + 2); // Check that each produced block advances lib by one

      const vector<account_name> producers {"a"_n, "b"_n, "c"_n};

      _nodes[0].create_accounts(producers);
      auto prod = _nodes[0].set_producers(producers);       // Set new producers and produce blocks until the
                                                            // switch is pending
      auto sb = _nodes[3].produce_block();                  // now the next block produced on any node
      BOOST_REQUIRE_EQUAL(sb->producer, producers[prod]);   // Should be produced by the producer returned by
                                                            // `set_producers`

      // because the network is not split, we expect all finalizers to have voted on the block
      // produced by _nodes[3]. Check that it is indeed the case.
      BOOST_REQUIRE_EQUAL(_nodes[0].is_head_missing_finalizer_votes(), false);

      // and memorize lib at this point
      // ------------------------------
      auto node0_lib = _nodes[0].lib_num();
      node3_lib      = _nodes[3].lib_num();
      BOOST_REQUIRE_EQUAL(node0_lib, node3_lib);

      set_partition( {&C, &D} );                            // Simulate 2 disconnected partitions:
                                                            // nodes {0, 1} and nodes {2, 3}

      // at this point, each node has a QC to include into the next block it produces which will advance lib.
      // So if we produce a block on _nodes[0], it will advance lib on _nodes[0], but not on _nodes[3]
      // because of the partition.
      _nodes[0].produce_block();
      BOOST_REQUIRE_EQUAL(_nodes[0].lib_num(), node0_lib+1);
      BOOST_REQUIRE_EQUAL(_nodes[3].lib_num(), node0_lib);

      _nodes[0].produce_blocks(4);                          // Produce 4 blocks. With the split network,
                                                            // lib shouldn't advance
      BOOST_REQUIRE_EQUAL(_nodes[0].lib_num(), node0_lib+1);
      BOOST_REQUIRE_EQUAL(_nodes[3].lib_num(), node0_lib);

      // all 4 blocks produced by _nodes[0] will have the same `latest_qc_claim_block_num`, which is node0_lib+2

      set_partition({});                                    // Reunite the two partitions
      propagate_heads();                                    // Push the blocks that _nodes[0] produced to the other
                                                            // nodes which will vote
      _nodes[0].produce_block();                            // Produce one block so the newly produced QC propagates.
                                                            // this is needed because we don't advance lib when
                                                            // receiving votes.

      BOOST_REQUIRE_EQUAL(_nodes[0].lib_num(), node0_lib+2);
      BOOST_REQUIRE_EQUAL(_nodes[3].lib_num(), node0_lib+2);

} FC_LOG_AND_RETHROW() }


BOOST_AUTO_TEST_SUITE_END()
