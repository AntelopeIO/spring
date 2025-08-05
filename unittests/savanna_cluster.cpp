#include "savanna_cluster.hpp"

namespace savanna_cluster {

node_t::node_t(size_t node_idx, cluster_t& cluster, setup_policy policy /* = setup_policy::none */)
   : tester(policy)
   , _node_idx(node_idx)
   , _last_vote({}, false)
   , _cluster(cluster)
{

   // since we are creating forks, finalizers may be locked on another fork and unable to vote.
   do_check_for_votes(false);

   _voted_block_cb = [&, node_idx](const eosio::chain::vote_signal_params& v) {
      // no mutex needed because controller is set in tester (via `disable_async_voting(true)`)
      // to vote (and emit the `voted_block` signal) synchronously.
      // --------------------------------------------------------------------------------------
      vote_result_t status = std::get<1>(v);

      if (status == vote_result_t::success) {
         vote_message_ptr vote_msg = std::get<2>(v);
         _last_vote = vote_t(vote_msg->block_id, vote_msg->strong);
         _votes[vote_msg->block_id] = vote_msg;

         if (_propagate_votes) {
            if (_vote_delay)
               _delayed_votes.push_back(std::move(vote_msg));
            while (_delayed_votes.size() > _vote_delay) {
               vote_message_ptr vote = _delayed_votes.front();
               _delayed_votes.erase(_delayed_votes.cbegin());
               _cluster.dispatch_vote_to_peers(node_idx, skip_self_t::yes, vote);
            }
            if (!_vote_delay)
               _cluster.dispatch_vote_to_peers(node_idx, skip_self_t::yes, vote_msg);
         }
      }
   };

   // called on `commit_block`, for both blocks received from `push_block` and produced blocks
   _accepted_block_cb = [&, node_idx](const eosio::chain::block_signal_params& p) {
      if (!_pushing_a_block) {
         // we want to propagate only blocks we produce, not the ones we receive from the network
         auto& b = std::get<0>(p);
         _cluster.push_block_to_peers(node_idx, skip_self_t::yes, b);
      }
   };

   auto node_initialization_fn = [&]() {
      [[maybe_unused]] auto _a = control->voted_block().connect(_voted_block_cb);
      [[maybe_unused]] auto _b = control->accepted_block().connect(_accepted_block_cb);
      tester::set_node_finalizers(_node_finalizers);
   };

   node_initialization_fn();                                    // initialize the node when it is first created
   set_open_callback([&, node_initialization_fn, node_idx]() {  // and do the initialization again every time `open()` is called + simulate blocks from peers
      node_initialization_fn();
      cluster.get_new_blocks_from_peers(node_idx);
   });
}

node_t::~node_t() {}

void node_t::propagate_delayed_votes_to(const node_t& n) {
   for (auto& vote : _delayed_votes)
      _cluster.dispatch_vote_to(n, vote);
}

void node_t::push_vote_to(const node_t& n, const block_id_type& block_id) {
   _cluster.dispatch_vote_to(n, get_vote(block_id));
}

}
