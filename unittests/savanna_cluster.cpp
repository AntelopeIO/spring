#include "savanna_cluster.hpp"

namespace savanna_cluster {

node_t::node_t(size_t _node_idx, cluster_t& _cluster, setup_policy policy)
   : tester(policy)
   , node_idx(_node_idx)
   , cluster(_cluster)
   , finkeys(*this) {

   control->voted_block().connect([&](const eosio::chain::vote_signal_params& v) {
      std::lock_guard g(votes_mtx);
      vote_status     status = std::get<1>(v);
      if (status == vote_status::success)
         cluster.dispatch_vote_to_peers(node_idx, std::get<2>(v));
   });

   set_produce_block_callback([&](const signed_block_ptr& b) { cluster.push_block_to_peers(node_idx, b); });
}

}

// ------------------------
// Test the savanna_cluster
// ------------------------
using namespace eosio::chain;

BOOST_AUTO_TEST_SUITE(savanna_cluster_tests)

// test set_finalizer host function serialization and tester set_finalizers
BOOST_FIXTURE_TEST_CASE(simple_test, savanna_cluster::cluster_t) { try {
      auto vote_delay {std::chrono::milliseconds(100)};

      reset_lib();
      auto node3_lib = node3.lib_num();                     // store initial lib (after Savanna transtion)
      node0.produce_block();                                // blocks & votes are propagated to all connected peers.
      node0.produce_block();                                // and by default all nodes are interconnected
      BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);  // check that lib advances on all nodes
      BOOST_REQUIRE_EQUAL(node3.lib_num(), node3_lib + 2);  // check that each produced block advances lib by one

      auto prod = set_producers(0,  {"a"_n, "b"_n, "c"_n}); // set new producers and produce blocks until the switch is pending
      auto sb = node3.produce_block();                      // now the next block produced on any node
      BOOST_REQUIRE_EQUAL(sb->producer, prod);              // should be produced by the producer returned by `set_producers`

      std::this_thread::sleep_for(vote_delay);              // because vote_processor asynchronous

      // and memorize lib at this point
      auto node0_lib = node0.lib_num();
      node3_lib      = node3.lib_num();
      BOOST_REQUIRE_EQUAL(node0_lib, node3_lib);

      const std::vector<size_t> partition {2, 3};
      set_partition(partition);                             // simulate 2 disconnected partitions:  nodes {0, 1} and nodes {2, 3}

      // at this point, each node has a QC to include into the next block it produces which will advance lib.
      // So if we produce a block on node0, it will advance lib on node0, but not on node3 because of the partition.
      node0.produce_block();
      BOOST_REQUIRE_EQUAL(node0.lib_num(), node0_lib+1);
      BOOST_REQUIRE_EQUAL(node3.lib_num(), node0_lib);

      node0.produce_blocks(4);                              // produce 4 blocks. With the split network, lib shouldn't advance
      BOOST_REQUIRE_EQUAL(node0.lib_num(), node0_lib+1);
      BOOST_REQUIRE_EQUAL(node3.lib_num(), node0_lib);

      // all 4 blocks produced by node0 will have the same `final_on_strong_qc`, which is node0_lib+2

      set_partition({});                                    // reunite the two partitions
      push_blocks(node0, partition);                        // push the blocks that node0 produced to the other nodes which will vote
      std::this_thread::sleep_for(vote_delay);              // because vote_processor asynchronous

      node0.produce_block();                                // produce one block so the newly produced QC propagates.
                                                            // this is needed because we don't advance lib when receiving votes.

      BOOST_REQUIRE_EQUAL(node0.lib_num(), node0_lib+2);
      BOOST_REQUIRE_EQUAL(node3.lib_num(), node0_lib+2);

} FC_LOG_AND_RETHROW() }


BOOST_AUTO_TEST_SUITE_END()