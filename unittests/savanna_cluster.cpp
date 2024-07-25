#include "savanna_cluster.hpp"

namespace savanna_cluster {

node_t::node_t(size_t node_idx, cluster_t& cluster, setup_policy policy /* = setup_policy::none */)
   : tester(policy)
   , node_idx(node_idx)
   , finkeys(*this) {

   // since we are creating forks, finalizers may be locked on another fork and unable to vote.
   do_check_for_votes(false);

   [[maybe_unused]] auto voted_block_connection =
      control->voted_block().connect([&, node_idx](const eosio::chain::vote_signal_params& v) {
         // no mutex needed because controller is set in tester (via `disable_async_voting(true)`)
         // to vote (and emit the `voted_block` signal) synchronously.
         // --------------------------------------------------------------------------------------
         vote_result_t status = std::get<1>(v);
         if (status == vote_result_t::success)
            cluster.dispatch_vote_to_peers(node_idx, skip_self_t::yes, std::get<2>(v));
      });

   set_produce_block_callback([&, node_idx](const signed_block_ptr& b) { cluster.push_block_to_peers(node_idx, skip_self_t::yes, b); });
}

node_t::~node_t() {}

}