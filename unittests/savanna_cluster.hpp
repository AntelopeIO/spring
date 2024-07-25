#pragma once

#include <eosio/chain/finality/finalizer_authority.hpp>
#include <fc/crypto/bls_private_key.hpp>

#include <eosio/testing/tester.hpp>
#include <ranges>
#include <boost/unordered/unordered_flat_map.hpp>

#include "snapshot_suites.hpp"

namespace savanna_cluster {
   namespace ranges = std::ranges;

   using vote_message_ptr = eosio::chain::vote_message_ptr;
   using vote_result_t    = eosio::chain::vote_result_t;
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

   enum class skip_self_t : uint8_t { no, yes };
   namespace fs = std::filesystem;


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

      template<class F>
      void require_lib_advancing_by(uint32_t cnt, F &&f) {
         assert(is_open()); // cluster expects `_nodes[0]` to never be closed (shutdown)
         auto lib = lib_block->block_num();
         std::forward<F>(f)();
         BOOST_REQUIRE_EQUAL(lib_block->block_num(), lib + cnt);
      }

      void push_blocks_to(tester& to, uint32_t block_num_limit = std::numeric_limits<uint32_t>::max()) const {
         auto limit = std::min(fork_db_head().block_num(), block_num_limit);
         while (to.fork_db_head().block_num() < limit) {
            auto sb = control->fetch_block_by_number(to.fork_db_head().block_num() + 1);
            to.push_block(sb);
         }
      }

      bool is_head_missing_finalizer_votes() {
         return control->is_block_missing_finalizer_votes(head());
      }

      std::string snapshot() const {
         auto writer = buffered_snapshot_suite::get_writer();
         control->write_snapshot(writer);
         return buffered_snapshot_suite::finalize(writer);
      }

      void open_from_snapshot(const std::string& snapshot) {
         open(buffered_snapshot_suite::get_reader(snapshot));
      }

      std::vector<uint8_t> save_fsi() const {
         auto finalizer_path = get_fsi_path();
         std::ifstream file(finalizer_path.generic_string(), std::ios::binary | std::ios::ate);
         std::streamsize size = file.tellg();
         file.seekg(0, std::ios::beg);

         std::vector<uint8_t> buffer(size);
         file.read((char *)buffer.data(), size);
         return buffer;
      }

      void overwrite_fsi(const std::vector<uint8_t>& fsi) const {
         auto finalizer_path = get_fsi_path();
         std::ofstream file(finalizer_path.generic_string(), std::ofstream::binary);
         file.write((const char *)fsi.data(), fsi.size());
      }

      void remove_fsi() {
         remove_all(get_fsi_path());
      }

      void remove_state() {
         auto state_path = cfg.state_dir;
         remove_all(state_path);
         fs::create_directories(state_path);
      }

   private:
      fs::path get_fsi_path() const { return  cfg.finalizers_dir / config::safety_filename; }

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
   // (so quorum == (N * 2) / 3 + 1)
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
         assert(_num_nodes > 3); // cluster should have a minimum of 4 nodes (quorum = 3)

         _nodes.reserve(_num_nodes);
         _nodes.emplace_back(0, *this, setup_policy::full_except_do_not_transition_to_savanna);
         for (size_t i=1; i<_num_nodes; ++i)
            _nodes.emplace_back(i, *this);

         set_partition({}); // initialize to no partition

         // make sure we push _nodes[0] initialization (full_except_do_not_transition_to_savanna)
         // to the other nodes. Needed because the tester was initialized before `node_t`.
         // ------------------------------------------------------------------------------------
         for (size_t i = 0; i < _nodes.size(); ++i)
            _nodes[0].push_blocks_to(_nodes[i]);

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
      size_t set_producers(size_t node_idx, const std::vector<account_name>& producers, bool create_accounts = true) {
         node_t& n = _nodes[node_idx];
         n.set_producers(producers);
         account_name pending;
         while (1) {
            signed_block_ptr sb = n.produce_block();
            pending = n.control->pending_block_producer();
            if (ranges::any_of(producers, [&](auto a) { return a == pending; }))
               break;
         }
         return ranges::find(producers, pending) - producers.begin();
      }

      // `set_partitions` allows to configure logical network connections between nodes.
      // - an empty list will connect each node of the cluster to every other nodes.
      // - a non-empty list partitions the network into two or more partitions.
      //   each vector of node indices specifies a separate partition, and the unaccounted
      //   for nodes (`complement`) form another partition
      //   (within each partition, nodes are fully connected)
      // -----------------------------------------------------------------------------------------
      void set_partitions(std::initializer_list<std::vector<size_t>> l) {
         auto inside = [&](size_t node_idx) {
            return ranges::any_of(l, [node_idx](const auto& v) {
               return ranges::any_of(v, [node_idx](auto i) { return i == node_idx; }); });
         };

         std::vector<size_t> complement;
         for (size_t i = 0; i < _nodes.size(); ++i)
            if (!inside(i))
               complement.push_back(i);

         auto set_peers = [&](const std::vector<size_t>& v) { for (auto i : v)  _peers[i] = v; };

         _peers.clear();
         for (const auto& v : l)
            set_peers(v);
         set_peers(complement);
      }

      // this is a convenience function for the most common case where we want to partition
      // the nodes into two separate disconnected partitions.
      // Simply provide a set of indices for nodes that need to be logically disconnected
      // from the rest of the network.
      // ----------------------------------------------------------------------------------
      void set_partition(const std::vector<size_t>& indices) {
         set_partitions({indices});
      }

      // After creating forks on different nodes on a partitioned network, make sure that,
      // within any partition, all chain heads of any node are also pushed to all other nodes.
      //
      // Constraining the propagation within partitions may be useful for example if we had
      // forks on 3 partitions, and we switch to having two partitions.
      // -------------------------------------------------------------------------------------
      void propagate_heads() {
         struct head_track { digest_type id; size_t node_idx; };

         // propagate heads only within the same partition
         for (const auto& [_, peers] : _peers) {
            std::vector<head_track> heads;

            // store all different chain head found in cluster into `heads` vector
            for (auto i : peers) {
               if (_nodes[i].is_open()) {
                  auto head = _nodes[i].head();
                  if (!ranges::any_of(heads, [&](auto& h) { return h.id == head.id(); }))
                     heads.emplace_back(head.id(), i);
               }
            }

            for (auto i : peers) {
               auto& dest = _nodes[i];
               if (!dest.is_open())
                  continue;

               for (auto& h : heads) {
                  if (i == h.node_idx || dest.head().id() == h.id)
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
      }

      // returns the number of nodes on which `lib` advanced since we last checked
      // -------------------------------------------------------------------------
      size_t num_lib_advancing() {
         return ranges::count_if(_nodes, [](node_t& n) { return n.lib_advancing(); });
      }

      void reset_lib() { for (auto& n : _nodes) n.reset_lib();  }

      void verify_lib_advances() {
         assert(_nodes[0].is_open()); // cluster expects `_nodes[0]` to never be closed (shutdown)
         auto lib = _nodes[0].lib_block->block_num();
         size_t tries = 0;
         while (_nodes[0].lib_block->block_num() <= (lib + eosio::testing::num_chains_to_final) && ++tries < 10) {
            _nodes[0].produce_block();
         }
         BOOST_REQUIRE_GT(_nodes[0].lib_block->block_num(), lib + eosio::testing::num_chains_to_final);
      }

      void push_block(size_t dst_idx, const signed_block_ptr& sb) {
         push_block_to_peers(dst_idx, skip_self_t::no, sb);
      }

      // Push new blocks from src_idx node to all nodes in partition of dst_idx.
      // This is used when pushing one fork from a node to another node which has
      // another fork, so we can't use `dst.forkdb_head_num() + 1` for start_block_num
      // -----------------------------------------------------------------------------
      void push_blocks(size_t src_idx, size_t dst_idx, uint32_t start_block_num) {
         auto& src = _nodes[src_idx];
         assert(src.is_open() &&  _nodes[dst_idx].is_open());

         auto end_block_num   = src.fork_db_head().block_num();

         for (uint32_t i=start_block_num; i<=end_block_num; ++i) {
            auto sb = src.control->fetch_block_by_number(i);
            push_block(dst_idx, sb);
         }
      }

      // Push new blocks from src_idx node to a specific list of nodes.
      // this can be useful after we removed a network partition, and we want to push unseen blocks
      // to the nodes of a pre-existing partition, so that they vote on them.
      // ------------------------------------------------------------------------------------------
      void push_blocks(size_t src_idx, const std::vector<size_t> &indices,
                       uint32_t block_num_limit = std::numeric_limits<uint32_t>::max()) {
         auto& src = _nodes[src_idx];
         assert(src.is_open());

         for (auto i : indices)
            if (_nodes[i].is_open())
               src.push_blocks_to(_nodes[i], block_num_limit);
      }

      size_t num_nodes() const { return _num_nodes; }

   public:
      std::vector<node_t>             _nodes;

      static constexpr fc::microseconds _block_interval_us =
         fc::milliseconds(eosio::chain::config::block_interval_ms);

   private:
      using peers_t = boost::unordered_flat_map<size_t, std::vector<size_t>>;

      peers_t   _peers;
      size_t    _num_nodes;
      size_t    _keys_per_node;
      bool      _shutting_down {false};

      friend node_t;

      void dispatch_vote_to_peers(size_t node_idx, skip_self_t skip_self, const vote_message_ptr& msg) {
         static uint32_t connection_id = 0;
         for_each_peer(node_idx, skip_self, [&](node_t& n) {
            n.control->process_vote_message(++connection_id, msg);
         });
      }

      void push_block_to_peers(size_t node_idx, skip_self_t skip_self, const signed_block_ptr& b) {
         for_each_peer(node_idx, skip_self, [&](node_t& n) {
            n.push_block(b);
         });
      }

      template<class CB>
      void for_each_peer(size_t node_idx, skip_self_t skip_self, const CB& cb) {
         if (_shutting_down)
            return;
         assert(_peers.find(node_idx) != _peers.end());
         const auto& peers = _peers[node_idx];
         for (auto i : peers) {
            bool dont_skip = skip_self == skip_self_t::no || i != node_idx;
            if (dont_skip && _nodes[i].is_open())
               cb(_nodes[i]);
         }
      }

   };

   class cluster_6_t : cluster_t {
   public:
      cluster_6_t(size_t num_nodes = 6, size_t keys_per_node = 10)
         : cluster_t(num_nodes, keys_per_node) {}
   };
}
