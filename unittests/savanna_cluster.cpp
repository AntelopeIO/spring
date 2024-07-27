#include "savanna_cluster.hpp"

namespace savanna_cluster {

node_t::node_t(size_t node_idx, cluster_t& cluster, setup_policy policy /* = setup_policy::none */)
   : tester(policy)
   , node_idx(node_idx)
   , finkeys(*this) {

   // since we are creating forks, finalizers may be locked on another fork and unable to vote.
   do_check_for_votes(false);

   voted_block_cb = [&, node_idx](const eosio::chain::vote_signal_params& v) {
      // no mutex needed because controller is set in tester (via `disable_async_voting(true)`)
      // to vote (and emit the `voted_block` signal) synchronously.
      // --------------------------------------------------------------------------------------
      vote_result_t status = std::get<1>(v);
      if (status == vote_result_t::success)
         cluster.dispatch_vote_to_peers(node_idx, skip_self_t::yes, std::get<2>(v));
   };

   accepted_block_cb = [&, node_idx](const eosio::chain::block_signal_params& p) {
      auto& b = std::get<0>(p);
      if (!pushing_a_block) {
         // we want to propagate only blocks we produce, not the ones we receive from the network
         cluster.push_block_to_peers(node_idx, skip_self_t::yes, b);
      }
   };

   auto node_initialization_fn = [&]() {
      [[maybe_unused]] auto _a = control->voted_block().connect(voted_block_cb);
      [[maybe_unused]] auto _b = control->accepted_block().connect(accepted_block_cb);
      finkeys.set_node_finalizers();
   };

   node_initialization_fn();
   set_open_callback(node_initialization_fn);

   //set_produce_block_callback([&, node_idx](const signed_block_ptr& b) {
   //   cluster.push_block_to_peers(node_idx, skip_self_t::yes, b); });
}

node_t::~node_t() {}

}