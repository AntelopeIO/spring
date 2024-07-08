#pragma once

#include <eosio/chain/finality/finalizer_authority.hpp>
#include <fc/crypto/bls_private_key.hpp>

#include <eosio/testing/tester.hpp>
#include <ranges>

namespace savanna_cluster {
   namespace ranges = std::ranges;

   using vote_message_ptr = eosio::chain::vote_message_ptr;
   using vote_status      = eosio::chain::vote_status;
   using signed_block_ptr = eosio::chain::signed_block_ptr;
   using account_name     = eosio::chain::account_name;
   using finalizer_policy = eosio::chain::finalizer_policy;
   using digest_type      = eosio::chain::digest_type;
   using block_header     = eosio::chain::block_header;
   using tester           = eosio::testing::tester;
   using setup_policy     = eosio::testing::setup_policy;
   using bls_public_key   = fc::crypto::blslib::bls_public_key;
   template<class tester>
   using finalizer_keys   = eosio::testing::finalizer_keys<tester>;

   class cluster_t;

   // ----------------------------------------------------------------------------
   class node_t : public tester {
      uint32_t                prev_lib_num{0};
      size_t                  node_idx;
      finalizer_keys<tester>  finkeys;
      size_t                  cur_key{0}; // index of key used in current policy

   public:
      node_t(size_t node_idx, cluster_t& cluster, setup_policy policy = setup_policy::none);

      virtual ~node_t();

      node_t(node_t&&) = default;

      void set_node_finalizers(size_t keys_per_node, size_t num_nodes) {
         finkeys.init_keys(keys_per_node * num_nodes, num_nodes);

         size_t first_node_key = node_idx * keys_per_node;
         cur_key               = first_node_key;
         finkeys.set_node_finalizers(first_node_key, keys_per_node);
      }

      std::pair<std::vector<bls_public_key>, eosio::chain::finalizer_policy>
      transition_to_savanna(std::span<const size_t> indices) {
         auto pubkeys = finkeys.set_finalizer_policy(indices).pubkeys;
         auto policy  = finkeys.transition_to_savanna();
         return { pubkeys, policy };
      }

      // returns true if LIB advances on this node since we last checked
      bool lib_advancing() {
         if (lib_num() > prev_lib_num) {
            prev_lib_num = lib_num();
            return true;
         }
         return false;
      }

      void reset_lib() { prev_lib_num = lib_num(); }

      uint32_t lib_num() const { return lib_block->block_num(); }

      uint32_t head_num() const { return control->fork_db_head_block_num(); }

      void push_blocks(node_t& to, uint32_t block_num_limit = std::numeric_limits<uint32_t>::max()) const {
         while (to.head_num() < std::min(head_num(), block_num_limit)) {
            auto sb = control->fetch_block_by_number(to.control->fork_db_head_block_num() + 1);
            to.push_block(sb);
         }
      }

      bool is_head_missing_finalizer_votes() {
         return control->is_block_missing_finalizer_votes(head());
      }
   };

   // ---------------------------------------------------------------------------------------
   // cluster_t
   // ---------
   //
   // Set up a test network which consists of N nodes, all of which have transitioned to
   // the Savanna consensus.
   //
   // They are all finalizers (Each node has one finalizer) and can all produce blocks.
   // quorum is computed using the same formula as in the system contracts
   // (so quorum == 3)
   //
   //
   // By default they are all connected, receive all produced blocks, vote on them,
   // and send their votes to all other nodes.
   //
   // It is possible to split the 'virtual' network using `cluster_t::split()`.
   //  --------------------------------------------------------------------------------------
   class cluster_t {
   public:
      cluster_t(size_t num_nodes = 4, size_t keys_per_node = 10)
         : _num_nodes(num_nodes)
         , _keys_per_node(keys_per_node)
      {
         assert(_num_nodes > 3); // cluster should have a minimum of 3 nodes (quorum = 2)

         _nodes.reserve(_num_nodes);
         _nodes.emplace_back(0, *this, setup_policy::full_except_do_not_transition_to_savanna);
         for (size_t i=1; i<_num_nodes; ++i)
            _nodes.emplace_back(i, *this);


         // make sure we push _nodes[0] initialization (full_except_do_not_transition_to_savanna)
         // to the other nodes. Needed because the tester was initialized before `node_t`.
         // ------------------------------------------------------------------------------------
         for (size_t i = 0; i < _nodes.size(); ++i)
            _nodes[0].push_blocks(_nodes[i]);

         // from now on, propagation of blocks and votes happens automatically (thanks to the
         // callbacks registered in `node_t` constructor).
         //
         // Set one finalizer per node (keys at indices { 0, 10, 20, 30}) and create initial
         // `finalizer_policy` using these indices.
         // -----------------------------------------------------------------------------------

         // set initial finalizer policy
         // ----------------------------
         std::vector<size_t> indices;

         for (size_t i = 0; i < _nodes.size(); ++i) {
            indices.push_back(i * _keys_per_node);
            _nodes[i].set_node_finalizers(_keys_per_node, _num_nodes);
         }

         // do the transition to Savanna on _nodes[0]. Blocks will be propagated to the other nodes.
         // ------------------------------------------------------------------------------------
         auto [fin_policy_pubkeys, fin_policy] = _nodes[0].transition_to_savanna(indices);

         // at this point, _nodes[0] has a QC to include in next block.
         // Produce that block and push it, but don't process votes so that
         // we don't start with an existing QC
         // ---------------------------------------------------------------
         _nodes[0].produce_block();

         // reset votes and saved lib, so that each test starts in a clean slate
         // --------------------------------------------------------------------
         reset_lib();
      }

      ~cluster_t() {
         _shutting_down = true;
      }

      // Create accounts and updates producers on node node_idx (producer updates will be
      // propagated to connected nodes), and wait until one of the new producers is pending.
      // return the index of the pending new producer (we assume no duplicates in producer list)
      // -----------------------------------------------------------------------------------
      size_t set_producers(size_t node_idx, const std::vector<account_name>& producers) {
         node_t& n = _nodes[node_idx];
         n.set_producers(producers);
         account_name pending;
         signed_block_ptr sb;
         while (1) {
            sb = n.produce_block();
            pending = n.control->pending_block_producer();
            if (ranges::any_of(producers, [&](auto a) { return a == pending; }))
               break;
         }
         return ranges::find(producers, pending) - producers.begin();
      }

      // provide a set of node indices which will be disconnected from other nodes of the network,
      // creating two separate networks.
      // within each of the two partitions, nodes are still fully connected
      // -----------------------------------------------------------------------------------------
      void set_partition(std::vector<size_t> indices) {
         _partition = std::move(indices);
      }

      void push_blocks(node_t& node, const std::vector<size_t> &indices,
                       uint32_t block_num_limit = std::numeric_limits<uint32_t>::max()) {
         for (auto i : indices)
            node.push_blocks(_nodes[i], block_num_limit);
      }

      // After creating forks on different nodes on a partitioned network,
      // make sure that all chain heads of any node are also pushed to all other nodes
      void propagate_heads() {
         struct head_track { digest_type id; size_t node_idx; };
         std::vector<head_track> heads;

         // store all different chain head found in cluster into `heads` vector
         for (size_t i = 0; i < _nodes.size(); ++i) {
            auto& n = _nodes[i];
            auto head = n.head();
            if (!ranges::any_of(heads, [&](auto& h) { return h.id == head.id(); }))
               heads.emplace_back(head.id(), i);
         }

         for (auto& dest : _nodes) {
            for (auto& h : heads) {
               if (h.id == dest.head().id())
                  continue;

               // propagate blocks from `h.node_idx` to `dest`.
               // We assume all nodes have at least a parent irreversible block in common
               auto& src = _nodes[h.node_idx];
               std::vector<signed_block_ptr> push_queue;
               digest_type id = h.id;
               while (!dest.control->fetch_block_by_id(id)) {
                  auto sb = src.control->fetch_block_by_id(id);
                  assert(sb);
                  push_queue.push_back(sb);
                  id = sb->previous;
               }

               for (auto& b : push_queue | ranges::views::reverse)
                  dest.push_block(b);
            }
         }

      }

      // returns the number of nodes on which `lib` advanced since we last checked
      // -------------------------------------------------------------------------
      size_t num_lib_advancing() {
         return ranges::count_if(_nodes, [](node_t& n) { return n.lib_advancing(); });
      }

      void reset_lib() { for (auto& n : _nodes) n.reset_lib();  }

      size_t num_nodes() const { return _num_nodes; }

   public:
      std::vector<node_t>             _nodes;

   private:
      std::vector<size_t>             _partition;
      size_t                          _num_nodes;
      size_t                          _keys_per_node;
      bool                            _shutting_down {false};

      friend node_t;

      void dispatch_vote_to_peers(size_t node_idx, const vote_message_ptr& msg) {
         static uint32_t connection_id = 0;
         for_each_peer(node_idx, [&](node_t& n) {
            n.control->process_vote_message(++connection_id, msg);
         });
      }

      void push_block_to_peers(size_t node_idx, const signed_block_ptr& b) {
         for_each_peer(node_idx, [&](node_t& n) {
            n.push_block(b);
         });
      }

      template<class CB>
      void for_each_peer(size_t node_idx, const CB& cb) {
         if (_shutting_down)
            return;

         if (_partition.empty()) {
            for (size_t i=0; i<_num_nodes; ++i)
               if (i != node_idx)
                  cb(_nodes[i]);
         } else {
            auto in_partition = [&](size_t node_idx) {
               return ranges::any_of(_partition, [&](auto i) { return i == node_idx; });
            };
            bool in = in_partition(node_idx);
            for (size_t i=0; i<_num_nodes; ++i)
               if (i != node_idx && in == in_partition(i))
                  cb(_nodes[i]);
         }
      }

   };
}