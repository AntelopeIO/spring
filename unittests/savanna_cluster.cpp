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
         cluster.dispatch_vote(node_idx, std::get<2>(v));
   });

   set_produce_block_callback([&](const signed_block_ptr& b) { cluster.push_block(node_idx, b); });
}

void node_t::set_node_finalizers(size_t keys_per_node, size_t num_nodes) {
   finkeys.init_keys(keys_per_node * num_nodes, num_nodes);

   size_t first_node_key = node_idx * keys_per_node;
   cur_key               = first_node_key;
   finkeys.set_node_finalizers(first_node_key, keys_per_node);
}

}

// ------------------------
// Test the savanna_cluster
// ------------------------
using namespace eosio::chain;

BOOST_AUTO_TEST_SUITE(savanna_cluster_tests)

// test set_finalizer host function serialization and tester set_finalizers
BOOST_FIXTURE_TEST_CASE(simple_test, savanna_cluster::cluster_t) { try {
      reset_lib();
      auto lib = node3.lib_num();                           // store initial lib (after Savanna transtion)
      node0.produce_block();                                // blocks & votes are propagated to all connected peers.
      node0.produce_block();                                // and by default all nodes are interconnected
      BOOST_REQUIRE_EQUAL(num_lib_advancing(), num_nodes);  // check that lib advances on all nodes
      BOOST_REQUIRE_EQUAL(node3.lib_num(), lib + 2);        // check that each produced block advances lib by one

      auto prod = set_producers(0,  {"a"_n, "b"_n, "c"_n});
      auto sb = node3.produce_block();
      BOOST_REQUIRE_EQUAL(sb->producer, prod);

} FC_LOG_AND_RETHROW() }


BOOST_AUTO_TEST_SUITE_END()