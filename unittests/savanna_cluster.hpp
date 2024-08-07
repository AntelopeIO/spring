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

   class cluster_t;

   enum class skip_self_t : uint8_t { no, yes };
   namespace fs = std::filesystem;

   // ----------------------------------------------------------------------------
   class fin_keys_t {
   public:
      explicit fin_keys_t(size_t num_keys, size_t finalizer_policy_size) {

         for (size_t i=0; i<num_keys; ++i) {
            account_name name { std::string("finalizer") + (char)('a' + i/26) + (char)('a' + i%26) };
            key_names.push_back(name);

            auto [privkey, pubkey, pop] = get_bls_key(name);
            pubkeys.push_back(pubkey);
            privkeys.push_back(privkey);
         }
      }

      const account_name& operator[](size_t idx) const { return key_names.at(idx); }

   private:
      vector<account_name>    key_names;
      vector<bls_public_key>  pubkeys;
      vector<bls_private_key> privkeys;
   };

   // ----------------------------------------------------------------------------
   class node_t : public tester {
   private:
      size_t                  node_idx;
      bool                    pushing_a_block {false };

      std::function<void(const block_signal_params&)> accepted_block_cb;
      std::function<void(const vote_signal_params&)>  voted_block_cb;
   public:
      std::vector<account_name> node_finalizers;

   public:
      node_t(size_t node_idx, cluster_t& cluster, setup_policy policy = setup_policy::none);

      virtual ~node_t();

      node_t(node_t&&) = default;

      void set_node_finalizers(std::span<const account_name> names) {
         node_finalizers = std::vector<account_name>{ names.begin(), names.end() };
         tester::set_node_finalizers(node_finalizers);
      }

      void transition_to_savanna(std::span<const account_name> finalizer_policy_names) {
         tester::set_active_finalizers(finalizer_policy_names);

         // `genesis_block` is the first block where set_finalizers() was executed.
         // It is the genesis block.
         // It will include the first header extension for the instant finality.
         // -----------------------------------------------------------------------
         auto genesis_block = produce_block();

         // wait till the genesis_block becomes irreversible.
         // The critical block is the block that makes the genesis_block irreversible
         // -------------------------------------------------------------------------
         signed_block_ptr critical_block = nullptr;  // last value of this var is the critical block
         auto genesis_block_num = genesis_block->block_num();
         while(genesis_block_num > lib_num())
            critical_block = produce_block();

         // Blocks after the critical block are proper IF blocks.
         // -----------------------------------------------------
         auto first_proper_block = produce_block();
         BOOST_REQUIRE(first_proper_block->is_proper_svnn_block());

         // wait till the first proper block becomes irreversible. Transition will be done then
         // -----------------------------------------------------------------------------------
         signed_block_ptr pt_block  = nullptr;  // last value of this var is the first post-transition block
         while(first_proper_block->block_num() > lib_num()) {
            pt_block = produce_block();
            BOOST_REQUIRE(pt_block->is_proper_svnn_block());
         }

         // lib must advance after num_chains_to_final blocks
         // -------------------------------------------------
         for (size_t i=0; i<num_chains_to_final; ++i)
            auto b = produce_block();

         BOOST_REQUIRE_EQUAL(lib_num(), pt_block->block_num());
      }

      // updates producers (producer updates will be propagated to connected nodes), and
      // wait until one of the new producers is pending.
      // return the index of the pending new producer (we assume no duplicates in producer list)
      // -----------------------------------------------------------------------------------
      size_t set_producers(const std::vector<account_name>& producers) {
         tester::set_producers(producers);
         account_name pending;
         size_t max_blocks_produced = 400;
         while (--max_blocks_produced) {
            signed_block_ptr sb = produce_block();
            pending = control->pending_block_producer();
            if (ranges::any_of(producers, [&](auto a) { return a == pending; }))
               break;
         }
         BOOST_REQUIRE_GT(max_blocks_produced, 0u);
         return ranges::find(producers, pending) - producers.begin();
      }

      uint32_t lib_num() const { return lib_number; }

      template<class F>
      uint32_t lib_advances_by(F&& f) {
         assert(is_open()); // node better be open if we want to check whether lib advances
         auto lib = lib_number;
         std::forward<F>(f)();
         return lib_number - lib;
      }

      void push_block(const signed_block_ptr& b) {
         assert(!pushing_a_block);
         pushing_a_block = true;
         auto reset_pending_on_exit = fc::make_scoped_exit([this]{ pushing_a_block = false; });
         tester::push_block(b);
      }

      template <class Node>
      void push_blocks_to(Node& to, uint32_t block_num_limit = std::numeric_limits<uint32_t>::max()) const {
         auto limit = std::min(fork_db_head().block_num(), block_num_limit);
         while (to.fork_db_head().block_num() < limit) {
            auto sb = control->fetch_block_by_number(to.fork_db_head().block_num() + 1);
            to.push_block(sb);
         }
      }

      bool is_head_missing_finalizer_votes() {
         if (!control->get_testing_allow_voting_flag())
            return false;
         return control->is_block_missing_finalizer_votes(head());
      }

      std::string snapshot() const {
         dlog("node ${i} - taking snapshot", ("i", node_idx));
         auto writer = buffered_snapshot_suite::get_writer();
         control->write_snapshot(writer);
         return buffered_snapshot_suite::finalize(writer);
      }

      void open_from_snapshot(const std::string& snapshot) {
         dlog("node ${i} - restoring from snapshot", ("i", node_idx));
         open(buffered_snapshot_suite::get_reader(snapshot));
      }

      std::vector<uint8_t> save_fsi() const {
         dlog("node ${i} - saving fsi", ("i", node_idx));
         auto finalizer_path = get_fsi_path();
         std::ifstream file(finalizer_path.generic_string(), std::ios::binary | std::ios::ate);
         std::streamsize size = file.tellg();
         assert(size > 0);
         file.seekg(0, std::ios::beg);

         std::vector<uint8_t> buffer(size);
         file.read((char *)buffer.data(), size);
         return buffer;
      }

      void overwrite_fsi(const std::vector<uint8_t>& fsi) const {
         dlog("node ${i} - overwriting fsi", ("i", node_idx));
         auto finalizer_path = get_fsi_path();
         std::ofstream file(finalizer_path.generic_string(), std::ios::binary);
         assert(!fsi.empty());
         file.write((const char *)fsi.data(), fsi.size());
      }

      void remove_fsi() {
         dlog("node ${i} - removing fsi", ("i", node_idx));
         remove_all(get_fsi_path());
      }

      void remove_state() {
         auto state_path = cfg.state_dir;
         dlog("node ${i} - removing state data from: ${state_path}", ("i", node_idx)("${state_path}", state_path));
         remove_all(state_path);
         fs::create_directories(state_path);
      }

      void remove_reversible_data() {
         remove_blocks(false);
      }

      void remove_reversible_data_and_blocks_log() {
         remove_blocks(true);
      }

      void remove_blocks_log() {
         auto path = cfg.blocks_dir;
         for (auto const& dir_entry : std::filesystem::directory_iterator{path}) {
            auto path = dir_entry.path();
            if (path.filename().generic_string() != "reversible") {
               dlog("node ${i} - removing : ${path}", ("i", node_idx)("${path}", path));
               remove_all(path);
            }
         }
      }

   private:
      // always removes reversible data (`blocks/reversible`)
      // optionally remove the blocks log as well by deleting the whole `blocks` directory
      // ---------------------------------------------------------------------------------
      void remove_blocks(bool rm_blocks_log) {
         auto reversible_path = cfg.blocks_dir / config::reversible_blocks_dir_name;
         auto& path = rm_blocks_log ? cfg.blocks_dir : reversible_path;
         dlog("node ${i} - removing : ${path}", ("i", node_idx)("${path}", path));
         remove_all(path);
         fs::create_directories(reversible_path);
      }

      fs::path get_fsi_path() const { return  cfg.finalizers_dir / config::safety_filename; }
   };

   // ---------------------------------------------------------------------------------------
   struct cluster_config {
      bool   transition_to_savanna = true;
      size_t num_nodes = 4;
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
   // It is possible to split the 'virtual' network using `cluster_t::set_partition()`.
   //  --------------------------------------------------------------------------------------
   class cluster_t {
   public:
      explicit cluster_t(cluster_config cfg = {})
         : _fin_keys(cfg.num_nodes * 2, cfg.num_nodes) // allow for some spare heys
         , _num_nodes(cfg.num_nodes)
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

         //  -----------------------------------------------------------------------------------
         // from now on, propagation of blocks and votes happens automatically (thanks to the
         // callbacks registered in `node_t` constructor).
         //  -----------------------------------------------------------------------------------

         if (cfg.transition_to_savanna) {
            // set one finalizer per node
            // --------------------------
            for (size_t i = 0; i < _nodes.size(); ++i)
               _nodes[i].set_node_finalizers({&_fin_keys[i], 1});

            // set finalizer policy and transition to Savanna
            // ----------------------------------------------
            _nodes[0].transition_to_savanna({&_fin_keys[0], _num_nodes});
         }
      }

      ~cluster_t() {
         _shutting_down = true;
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

      // returns the number of nodes where `lib` has advanced after executing `f`
      template<class F>
      size_t num_lib_advancing(F&& f) {
         std::vector<uint32_t> libs(_nodes.size());
         for (size_t i=0; i<_nodes.size(); ++i)
            libs[i] = _nodes[i].lib_num();

         std::forward<F>(f)();
         size_t res = 0;
         for (size_t i=0; i<_nodes.size(); ++i)
            res += (size_t)(_nodes[i].lib_num() > libs[i]);
         return res;
      }

      void verify_lib_advances() {
         BOOST_REQUIRE_EQUAL(num_nodes(), num_lib_advancing([this]() {  _nodes[0].produce_blocks(3); }));
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
      std::vector<node_t>  _nodes;
      fin_keys_t           _fin_keys;

      static constexpr fc::microseconds _block_interval_us =
         fc::milliseconds(eosio::chain::config::block_interval_ms);

   private:
      using peers_t = boost::unordered_flat_map<size_t, std::vector<size_t>>;

      peers_t    _peers;
      size_t     _num_nodes;
      bool       _shutting_down {false};

      friend node_t;

      void dispatch_vote_to_peers(size_t node_idx, skip_self_t skip_self, const vote_message_ptr& msg) {
         static uint32_t connection_id = 0;
         for_each_peer(node_idx, skip_self, [&](node_t& n) {
            n.control->process_vote_message(++connection_id, msg);
         });
      }

      void push_block_to_peers(size_t node_idx, skip_self_t skip_self, const signed_block_ptr& b) {
         for_each_peer(node_idx, skip_self, [&](node_t& n) {
            if (!n.fetch_block_by_id(b->calculate_id()))
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

   // ---------------------------------------------------------------------------------------
   class cluster_6_t : public cluster_t {
   public:
      cluster_6_t()
         : cluster_t(cluster_config{.transition_to_savanna = true, .num_nodes = 6}) {}
   };

   // ---------------------------------------------------------------------------------------
   class pre_transition_cluster_t : public cluster_t {
   public:
      pre_transition_cluster_t()
         : cluster_t(cluster_config{.transition_to_savanna = false}) {}
   };
}
