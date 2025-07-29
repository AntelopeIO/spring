#include <eosio/net_plugin/net_plugin.hpp>
#include <eosio/net_plugin/buffer_factory.hpp>
#include <eosio/net_plugin/gossip_bps_index.hpp>
#include <eosio/net_plugin/protocol.hpp>
#include <eosio/net_plugin/net_logger.hpp>
#include <eosio/net_plugin/net_utils.hpp>
#include <eosio/net_plugin/auto_bp_peering.hpp>
#include <eosio/chain/types.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/block.hpp>
#include <eosio/chain/plugin_interface.hpp>
#include <eosio/chain/thread_utils.hpp>
#include <eosio/producer_plugin/producer_plugin.hpp>
#include <eosio/chain/fork_database.hpp>

#include <fc/bitutil.hpp>
#include <fc/network/message_buffer.hpp>
#include <fc/io/json.hpp>
#include <fc/io/raw.hpp>
#include <fc/io/datastream.hpp>
#include <fc/variant_object.hpp>
#include <fc/crypto/rand.hpp>
#include <fc/exception/exception.hpp>
#include <fc/time.hpp>
#include <fc/mutex.hpp>
#include <fc/network/listener.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/multi_index/key.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#if __has_include(<sys/ioctl.h>)
#include <sys/ioctl.h>
#endif

#include <atomic>
#include <cmath>
#include <memory>
#include <new>

using namespace eosio::chain::plugin_interface;

using namespace std::chrono_literals;

namespace boost
{
   /// @brief Overload for boost::lexical_cast to convert vector of strings to string
   ///
   /// Used by boost::program_options to print the default value of an std::vector<std::string> option
   ///
   /// @param v the vector to convert
   /// @return the contents of the vector as a comma-separated string
   template<>
   inline std::string lexical_cast<std::string>(const std::vector<std::string>& v)
   {
      return boost::join(v, ",");
   }
}

namespace eosio {
   static auto _net_plugin = application::register_plugin<net_plugin>();

   using std::vector;

   using boost::asio::ip::tcp;
   using boost::asio::ip::address_v4;
   using boost::asio::ip::host_name;
   using boost::multi_index_container;
   using namespace boost::multi_index;

   using fc::time_point;
   using fc::time_point_sec;
   using eosio::chain::transaction_id_type;

   class connection;

   using connection_ptr = std::shared_ptr<connection>;
   using connection_wptr = std::weak_ptr<connection>;

   static constexpr int64_t block_interval_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(config::block_interval_ms)).count();

   using connection_id_t = uint32_t;
   using connection_id_set = boost::unordered_flat_set<connection_id_t>;
   struct node_transaction_state {
      transaction_id_type        id;
      time_point_sec             expires;           // time after which this may be purged.
      mutable connection_id_set  connection_ids;    // all connections trx or trx notice received or trx sent
      mutable bool               have_trx = false;  // trx received, not just trx notice, mutable because not indexed
   };

   typedef multi_index_container<
      node_transaction_state,
      indexed_by<
         ordered_unique<
            tag<by_id>,
            member<node_transaction_state, transaction_id_type, &node_transaction_state::id>
         >,
         ordered_non_unique<
            tag< struct by_expiry >,
            member< node_transaction_state, fc::time_point_sec, &node_transaction_state::expires > >
         >
      >
   node_transaction_index;

   struct peer_block_state {
      block_id_type      id;
      connection_id_t    connection_id = 0;

      block_num_type block_num() const { return block_header::num_from_id(id); }
   };

   struct by_connection_id;

   typedef multi_index_container<
      eosio::peer_block_state,
      indexed_by<
         ordered_unique< tag<by_connection_id>,
               composite_key< peer_block_state,
                     const_mem_fun<peer_block_state, block_num_type, &eosio::peer_block_state::block_num>,
                     member<peer_block_state, block_id_type, &eosio::peer_block_state::id>,
                     member<peer_block_state, connection_id_t, &eosio::peer_block_state::connection_id>
               >,
                         composite_key_compare< std::less<>, std::less<block_id_type>, std::less<> >
         >
      >
      > peer_block_state_index;

   class sync_manager {
   private:
      enum stages {
         lib_catchup,
         head_catchup,
         in_sync
      };

      alignas(hardware_destructive_interference_sz)
      fc::mutex      sync_mtx;
      uint32_t       sync_known_fork_db_root_num GUARDED_BY(sync_mtx) {0};  // highest known fork_db root num from currently connected peers
      uint32_t       sync_last_requested_num     GUARDED_BY(sync_mtx) {0};  // end block number of the last requested range, inclusive
      uint32_t       sync_next_expected_num      GUARDED_BY(sync_mtx) {0};  // the next block number we need from peer
      connection_ptr sync_source                 GUARDED_BY(sync_mtx);      // connection we are currently syncing from

      const uint32_t sync_fetch_span {0};
      const uint32_t sync_peer_limit {0};

      alignas(hardware_destructive_interference_sz)
      std::atomic<stages> sync_state{in_sync};
      std::atomic<int32_t> sync_timers_active{0};
      std::atomic<std::chrono::steady_clock::time_point> sync_active_time{};
      std::atomic<uint32_t> sync_ordinal{0};
      // indicate that we have received blocks to catch us up to head, delay sending out handshakes until we have
      // applied the blocks and our controller head is updated
      std::atomic<bool> send_handshakes_when_synced{false};

      // Instant finality makes it likely peers think their fork_db_root and head are
      // not in sync but in reality they are only within small difference.
      // To avoid unnecessary catchups, a margin of min_blocks_distance
      // between fork_db_root and head must be reached before catchup starts.
      const uint32_t min_blocks_distance{0};

   private:
      constexpr static auto stage_str( stages s );
      bool set_state( stages newstate );
      bool is_sync_required( uint32_t fork_db_head_block_num ) const REQUIRES(sync_mtx);
      bool is_sync_request_ahead_allowed(block_num_type blk_num) const REQUIRES(sync_mtx);
      void request_next_chunk( const connection_ptr& conn = connection_ptr() ) REQUIRES(sync_mtx);
      connection_ptr find_next_sync_node(); // call with locked mutex
      void start_sync( const connection_ptr& c, uint32_t target ); // locks mutex
      bool sync_recently_active() const;
      bool verify_catchup( const connection_ptr& c, uint32_t num, const block_id_type& id ); // locks mutex
   public:
      enum class closing_mode {
         immediately,  // closing connection immediately
         handshake     // sending handshake message
      };
      explicit sync_manager( uint32_t span, uint32_t sync_peer_limit, uint32_t min_blocks_distance );
      static void send_handshakes();
      static void send_block_nack_resets();
      bool syncing_from_peer() const { return sync_state == lib_catchup; }
      bool is_lib_catchup() const { return sync_state == lib_catchup; }
      void sync_reset_fork_db_root_num( const connection_ptr& conn, bool closing );
      void sync_timeout(const connection_ptr& c, const boost::system::error_code& ec);
      void sync_wait(const connection_ptr& c);
      void sync_reassign_fetch( const connection_ptr& c );
      void rejected_block( const connection_ptr& c, uint32_t blk_num, closing_mode mode );
      void sync_recv_block( const connection_ptr& c, const block_id_type& blk_id, uint32_t blk_num,
                            const fc::microseconds& blk_latency );
      void recv_handshake( const connection_ptr& c, const handshake_message& msg, uint32_t nblk_combined_latency );
      void sync_recv_notice( const connection_ptr& c, const notice_message& msg );
      void send_handshakes_if_synced(const fc::microseconds& blk_latency);
   };

   class dispatch_manager {
      alignas(hardware_destructive_interference_sz)
      mutable fc::mutex      blk_state_mtx;
      peer_block_state_index  blk_state GUARDED_BY(blk_state_mtx);

      alignas(hardware_destructive_interference_sz)
      mutable fc::mutex      local_txns_mtx;
      node_transaction_index  local_txns GUARDED_BY(local_txns_mtx);

   public:
      boost::asio::io_context::strand  strand;

      explicit dispatch_manager(boost::asio::io_context& io_context)
      : strand( io_context ) {}

      void bcast_transaction(const packed_transaction_ptr& trx);
      void bcast_transaction_notify(const packed_transaction_ptr& trx);
      void rejected_transaction(const packed_transaction_ptr& trx);
      void bcast_block( const signed_block_ptr& b, const block_id_type& id );

      void expire_blocks( uint32_t fork_db_root_num );
      void recv_notice(const connection_ptr& conn, const notice_message& msg, bool generated);

      bool add_peer_block( const block_id_type& blkid, connection_id_t connection_id );
      bool peer_has_block(const block_id_type& blkid, connection_id_t connection_id) const;
      bool have_block(const block_id_type& blkid) const;
      void rm_block(const block_id_type& blkid);

      // returns the number of tracked ids of connection, returns 0 if already have trx on any connection
      struct add_peer_txn_info {
         uint32_t trx_entries_size = 0;
         bool have_trx = false; // true if we already have received the trx
      };
      add_peer_txn_info add_peer_txn(const transaction_id_type& id, const time_point_sec& trx_expires, connection& c);
      size_t add_peer_txn_notice(const transaction_id_type& id, connection& c);
      connection_id_set peer_connections(const transaction_id_type& id) const;
      void expire_txns();

      void bcast_vote_msg( connection_id_t exclude_peer, const send_buffer_type& msg );
   };

   /**
    *  For a while, network version was a 16 bit value equal to the second set of 16 bits
    *  of the current build's git commit id. We are now replacing that with an integer protocol
    *  identifier. Based on historical analysis of all git commit identifiers, the larges gap
    *  between ajacent commit id values is shown below.
    *  these numbers were found with the following commands on the master branch:
    *
    *  git log | grep "^commit" | awk '{print substr($2,5,4)}' | sort -u > sorted.txt
    *  rm -f gap.txt; prev=0; for a in $(cat sorted.txt); do echo $prev $((0x$a - 0x$prev)) $a >> gap.txt; prev=$a; done; sort -k2 -n gap.txt | tail
    *
    *  DO NOT EDIT net_version_base OR net_version_range!
    */
   constexpr uint16_t net_version_base = 0x04b5;
   constexpr uint16_t net_version_range = 106;
   /**
    *  If there is a change to network protocol or behavior, increment net version to identify
    *  the need for compatibility hooks
    */
   enum class proto_version_t : uint16_t {
      base = 0,
      explicit_sync = 1,       // version at time of eosio 1.0
      block_id_notify = 2,     // reserved. feature was removed. next net_version should be 3
      pruned_types = 3,        // eosio 2.1: supports new signed_block & packed_transaction types
      heartbeat_interval = 4,        // eosio 2.1: supports configurable heartbeat interval
      dup_goaway_resolution = 5,     // eosio 2.1: support peer address based duplicate connection resolution
      dup_node_id_goaway = 6,        // eosio 2.1: support peer node_id based duplicate connection resolution
      leap_initial = 7,              // leap client, needed because none of the 2.1 versions are supported
      block_range = 8,               // include block range in notice_message
      savanna = 9,                   // savanna, adds vote_message
      block_nack = 10,               // adds block_nack_message & block_notice_message
      gossip_bp_peers = 11,          // adds gossip_bp_peers_message
      trx_notice = 12                // adds transaction_notice_message
   };

   constexpr proto_version_t net_version_max = proto_version_t::trx_notice;

   /**
    * default value initializers
    */
   constexpr auto     def_send_buffer_size_mb = 4;
   constexpr auto     def_send_buffer_size = 1024*1024*def_send_buffer_size_mb;
   constexpr auto     def_max_write_queue_size = def_send_buffer_size*10;
   constexpr uint32_t def_max_trx_in_progress_size = 100u*1024u*1024u; // 100 MB
   constexpr uint32_t def_max_trx_entries_per_conn_size = 100u*1024u*1024u; // 100 MB = ~100K TPS
   constexpr auto     def_max_consecutive_immediate_connection_close = 9; // back off if client keeps closing
   constexpr auto     def_max_clients = 25; // 0 for unlimited clients
   constexpr auto     def_max_nodes_per_host = 1;
   constexpr auto     def_conn_retry_wait = 30;
   constexpr auto     def_expire_timer_wait = std::chrono::seconds(3);
   constexpr auto     def_resp_expected_wait = std::chrono::seconds(5);
   constexpr auto     def_sync_fetch_span = 1000;
   constexpr auto     def_keepalive_interval = 10000;
   // transfer packed transaction is ~170 bytes, transaction notice is 41 bytes.
   // Since both notice and trx are sent when peer does not have a trx, set a minimum requirement for sending the notice.
   // 4096 chosen as an arbitrary threshold where an additional small notice adds little additional overhead.
   constexpr auto     def_trx_notice_min_size = 4096;
   constexpr auto     def_allowed_clock_skew = fc::seconds(15);

   class connections_manager {
   public:
      struct connection_detail {
         std::string host;
         connection_ptr c;
      };

      using connection_details_index = multi_index_container<
         connection_detail,
         indexed_by<
            ordered_non_unique<
               tag<struct by_host>,
               key<&connection_detail::host>
            >,
            ordered_unique<
               tag<struct by_connection>,
               key<&connection_detail::c>
            >
         >
      >;
      enum class timer_type { check, stats };
   private:
      alignas(hardware_destructive_interference_sz)
      mutable std::shared_mutex        connections_mtx;
      connection_details_index         connections;
      chain::flat_set<string>          supplied_peers;

      alignas(hardware_destructive_interference_sz)
      fc::mutex                             connector_check_timer_mtx;
      unique_ptr<boost::asio::steady_timer> connector_check_timer GUARDED_BY(connector_check_timer_mtx);
      fc::mutex                             connection_stats_timer_mtx;
      unique_ptr<boost::asio::steady_timer> connection_stats_timer GUARDED_BY(connection_stats_timer_mtx);

      /// thread safe, only modified on startup
      std::chrono::milliseconds                                heartbeat_timeout{def_keepalive_interval*2};
      fc::microseconds                                         max_cleanup_time;
      boost::asio::steady_timer::duration                      connector_period{0};
      uint32_t                                                 max_client_count{def_max_clients};
      std::function<void(net_plugin::p2p_connections_metrics)> update_p2p_connection_metrics;

   private: // must call with held mutex
      connection_ptr find_connection_i(const string& host) const;

      void connection_monitor(const std::weak_ptr<connection>& from_connection);
      void connection_statistics_monitor(const std::weak_ptr<connection>& from_connection);

   public:
      size_t number_connections() const;
      void add_supplied_peers(const vector<string>& peers );

      // not thread safe, only call on startup
      void init(std::chrono::milliseconds heartbeat_timeout_ms,
                fc::microseconds conn_max_cleanup_time,
                boost::asio::steady_timer::duration conn_period,
                uint32_t maximum_client_count);

      std::chrono::milliseconds get_heartbeat_timeout() const { return heartbeat_timeout; }

      uint32_t get_max_client_count() const { return max_client_count; }

      fc::microseconds get_connector_period() const;

      void register_update_p2p_connection_metrics(std::function<void(net_plugin::p2p_connections_metrics)>&& fun);

      void connect_supplied_peers(const string& p2p_address);

      void start_conn_timers();
      void start_conn_timer(boost::asio::steady_timer::duration du,
                            std::weak_ptr<connection> from_connection,
                            timer_type which);

      void add(connection_ptr c);
      string connect(const string& host, const string& p2p_address);
      string resolve_and_connect(const string& host, const string& p2p_address);
      string disconnect(const string& host);
      void disconnect_gossip_connection(const string& host);
      void close_all();

      std::optional<connection_status> status(const string& host) const;
      vector<connection_status> connection_statuses() const;

      template <typename Function>
      bool any_of_supplied_peers(Function&& f) const;

      template <typename Function>
      void for_each_connection(Function&& f) const;

      template <typename Function>
      void for_each_block_connection(Function&& f) const;

      template <typename UnaryPredicate>
      bool any_of_connections(UnaryPredicate&& p) const;

      template <typename UnaryPredicate>
      bool any_of_block_connections(UnaryPredicate&& p) const;

   }; // connections_manager

   class net_plugin_impl : public std::enable_shared_from_this<net_plugin_impl>,
                           public auto_bp_peering::bp_connection_manager<net_plugin_impl, connection> {
    public:
      uint16_t                                    thread_pool_size = 4;
      eosio::chain::named_thread_pool<struct net> thread_pool;

      std::atomic<connection_id_t>     current_connection_id{0};

      unique_ptr< sync_manager >       sync_master;
      dispatch_manager                 dispatcher {thread_pool.get_executor()};
      connections_manager              connections;

      /**
       * Thread safe, only updated in plugin initialize
       *  @{
       */
      vector<string>                        p2p_addresses;
      vector<string>                        p2p_server_addresses;
      const string&                         get_first_p2p_address() const;

      vector<chain::public_key_type>        allowed_peers; ///< peer keys allowed to connect
      std::map<chain::public_key_type,
               chain::private_key_type>     private_keys; ///< overlapping with producer keys, also authenticating non-producing nodes
      enum possible_connections : char {
         None = 0,
            Producers = 1 << 0,
            Specified = 1 << 1,
            Any = 1 << 2
            };
      possible_connections                  allowed_connections{None};

      boost::asio::steady_timer::duration   expire_timer_period{0};
      boost::asio::steady_timer::duration   resp_expected_period{0};
      std::chrono::milliseconds             keepalive_interval{std::chrono::milliseconds{def_keepalive_interval}};

      uint32_t                              max_nodes_per_host = 1;
      bool                                  p2p_accept_transactions = true;
      bool                                  p2p_disable_block_nack = false;
      bool                                  p2p_accept_votes = true;
      fc::microseconds                      p2p_dedup_cache_expire_time_us{};

      chain_id_type                         chain_id;
      fc::sha256                            node_id;
      string                                user_agent_name;

      chain_plugin*                         chain_plug = nullptr;
      producer_plugin*                      producer_plug = nullptr;
      bool                                  use_socket_read_watermark = false;
      /** @} */

      alignas(hardware_destructive_interference_sz)
      fc::mutex                             expire_timer_mtx;
      boost::asio::steady_timer             expire_timer GUARDED_BY(expire_timer_mtx) {thread_pool.get_executor()};

      alignas(hardware_destructive_interference_sz)
      fc::mutex                             keepalive_timer_mtx;
      boost::asio::steady_timer             keepalive_timer GUARDED_BY(keepalive_timer_mtx) {thread_pool.get_executor()};

      alignas(hardware_destructive_interference_sz)
      compat::channels::transaction_ack::channel_type::handle  incoming_transaction_ack_subscription;

      boost::asio::deadline_timer           accept_error_timer{thread_pool.get_executor()};

      alignas(hardware_destructive_interference_sz)
      std::atomic<fc::time_point>           head_block_time;

      alignas(hardware_destructive_interference_sz)
      std::atomic<fc::time_point>           last_block_received_time{};

      struct chain_info_t {
         block_id_type fork_db_root_id;
         uint32_t      fork_db_root_num = 0;
         block_id_type head_id;
         uint32_t      head_num = 0;
         block_id_type fork_db_head_id;
         uint32_t      fork_db_head_num = 0;
      };

      
      std::function<void()> increment_failed_p2p_connections;
      std::function<void()> increment_dropped_trxs;

   private:
      alignas(hardware_destructive_interference_sz)
      mutable fc::mutex             chain_info_mtx; // protects chain_info_t
      chain_info_t                  chain_info GUARDED_BY(chain_info_mtx);

   public:
      void update_chain_info();
      void update_chain_info(const block_id_type& fork_db_root_id);
      chain_info_t get_chain_info() const;
      uint32_t get_fork_db_root_num() const;
      uint32_t get_chain_head_num() const;
      uint32_t get_fork_db_head_num() const;

      void on_accepted_block_header( const signed_block_ptr& block, const block_id_type& id );
      void on_accepted_block( const signed_block_ptr& block, const block_id_type& id );
      void on_irreversible_block( const signed_block_ptr& block, const block_id_type& id );
      void broadcast_vote_message( connection_id_t connection_id, vote_result_t status,
                                   const vote_message_ptr& vote,
                                   const finalizer_authority_ptr& active_auth,
                                   const finalizer_authority_ptr& pending_auth);

      void transaction_ack(const std::pair<fc::exception_ptr, packed_transaction_ptr>&);

      void bcast_vote_message( uint32_t exclude_peer, const chain::vote_message_ptr& msg );

      void start_expire_timer();
      void start_monitors();

      void expire();
      /** \name Peer Timestamps
       *  Time message handling
       *  @{
       */
      /** \brief Peer heartbeat ticker.
       */
      void ticker();
      /** @} */
      /** \brief Determine if a peer is allowed to connect.
       *
       * Checks current connection mode and key authentication.
       *
       * \return False if the peer should not connect, true otherwise.
       */
      bool authenticate_peer(const handshake_message& msg) const;
      /** \brief Retrieve public key used to authenticate with peers.
       *
       * Finds a key to use for authentication.  If this node is a producer, use
       * the front of the producer key map.  If the node is not a producer but has
       * a configured private key, use it.  If the node is neither a producer nor has
       * a private key, returns an empty key.
       *
       * \note On a node with multiple private keys configured, the key with the first
       *       numerically smaller byte will always be used.
       */
      chain::public_key_type get_authentication_key() const;
      /** \brief Returns a signature of the digest using the corresponding private key of the signer.
       *
       * If there are no configured private keys, returns an empty signature.
       */
      chain::signature_type sign_compact(const chain::public_key_type& signer, const fc::sha256& digest) const;

      constexpr static proto_version_t to_protocol_version(uint16_t v);

      void plugin_initialize(const variables_map& options);
      void plugin_startup();
      void plugin_shutdown();

      // Conceptually interested if node is synced. Checking against in_sync is not recommended as a node can temporarily
      // switch to head_catchup on delayed blocks. Better to check not in lib_catchup.
      bool is_lib_catchup() const;

      void create_session(tcp::socket&& socket, string listen_address, size_t limit);

      std::string empty{};
   }; //net_plugin_impl


   template<class enum_type, class=typename std::enable_if<std::is_enum<enum_type>::value>::type>
   inline enum_type& operator|=(enum_type& lhs, const enum_type& rhs)
   {
      using T = std::underlying_type_t <enum_type>;
      return lhs = static_cast<enum_type>(static_cast<T>(lhs) | static_cast<T>(rhs));
   }

   static net_plugin_impl *my_impl;

   struct peer_sync_state {
      enum class sync_t {
         peer_sync,    // sync_request_message, syncing
         peer_catchup, // head catchup, syncing request_message:catch_up
         block_nack    // sync due to block nack (block_notice_message) request_message:normal
      };
      peer_sync_state(uint32_t start, uint32_t end, uint32_t last_acted, sync_t sync_type)
         :start_block( start ), end_block( end ), last( last_acted ), sync_type( sync_type )
      {}

      bool valid() const;

      uint32_t     start_block;
      uint32_t     end_block;
      uint32_t     last; ///< last sent or received
      sync_t       sync_type;
   };

   bool peer_sync_state::valid() const {
      bool valid = start_block > 0 && end_block >= start_block && last >= start_block-1 && last <= end_block;
      if (sync_type == sync_t::block_nack && valid) {
         // block nack should only be used for "current" blocks, limit size to something reasonable
         const auto size = end_block - start_block;
         valid = size < 100;
      }
      return valid;
   }

   // thread safe
   class queued_buffer : boost::noncopyable {
   public:
      void reset() {
         fc::lock_guard g( _mtx );
         _write_queue.clear();
         _sync_write_queue.clear();
         _write_queue_size = 0;
         _trx_write_queue.clear();
         _out_queue.clear();
      }

      void clear_write_queue() {
         fc::lock_guard g( _mtx );
         _write_queue.clear();
         _sync_write_queue.clear();
         _trx_write_queue.clear();
         _write_queue_size = 0;
      }

      void clear_out_queue(boost::system::error_code ec, std::size_t number_of_bytes_written) {
         fc::lock_guard g( _mtx );
         out_callback( ec, number_of_bytes_written );
         _out_queue.clear();
      }

      uint32_t write_queue_size() const {
         fc::lock_guard g( _mtx );
         return _write_queue_size;
      }

      // called from connection strand
      bool ready_to_send(connection_id_t connection_id) const {
         fc::unique_lock g( _mtx );
         // if out_queue is not empty then async_write is in progress
         const bool async_write_in_progress = !_out_queue.empty();
         const bool ready = !async_write_in_progress && _write_queue_size != 0;
         g.unlock();
         if (async_write_in_progress) {
            fc_dlog(p2p_conn_log, "Connection - ${id} not ready to send data, async write in progress", ("id", connection_id));
         }
         return ready;
      }

      enum class queue_t { block_sync, general };
      // @param callback must not callback into queued_buffer
      bool add_write_queue(msg_type_t net_msg,
                           queue_t queue,
                           const send_buffer_type& buff,
                           std::function<void(boost::system::error_code, std::size_t)> callback) {
         fc::lock_guard g( _mtx );
         if( net_msg == msg_type_t::packed_transaction || net_msg == msg_type_t::transaction_notice_message ) {
            _trx_write_queue.emplace_back( buff, std::move(callback) );
         } else if (queue == queue_t::block_sync) {
            _sync_write_queue.emplace_back( buff, std::move(callback) );
         } else {
            _write_queue.emplace_back( buff, std::move(callback) );
         }
         _write_queue_size += buff->size();
         if( _write_queue_size > 2 * def_max_write_queue_size ) {
            return false;
         }
         return true;
      }

      void fill_out_buffer( std::vector<boost::asio::const_buffer>& bufs ) {
         fc::lock_guard g( _mtx );
         if (!_sync_write_queue.empty()) { // always send msgs from sync_write_queue first
            fill_out_buffer( bufs, _sync_write_queue );
         } else if (!_write_queue.empty()) { // always send msgs from write_queue before trx queue
            fill_out_buffer( bufs, _write_queue );
         } else {
            fill_out_buffer( bufs, _trx_write_queue );
            assert(_trx_write_queue.empty() && _write_queue.empty() && _sync_write_queue.empty() && _write_queue_size == 0);
         }
      }

   private:
      struct queued_write;
      void fill_out_buffer( std::vector<boost::asio::const_buffer>& bufs,
                            deque<queued_write>& w_queue ) REQUIRES(_mtx) {
         while ( !w_queue.empty() ) {
            auto& m = w_queue.front();
            bufs.emplace_back( m.buff->data(), m.buff->size() );
            _write_queue_size -= m.buff->size();
            _out_queue.emplace_back( m );
            w_queue.pop_front();
         }
      }

      void out_callback( boost::system::error_code ec, std::size_t number_of_bytes_written ) REQUIRES(_mtx) {
         for( auto& m : _out_queue ) {
            m.callback( ec, number_of_bytes_written );
         }
      }

   private:
      struct queued_write {
         send_buffer_type buff;
         std::function<void( boost::system::error_code, std::size_t )> callback;
      };

      alignas(hardware_destructive_interference_sz)
      mutable fc::mutex   _mtx;
      uint32_t            _write_queue_size GUARDED_BY(_mtx) {0}; // size of _write_queue + _sync_write_queue + _trx_write_queue
      deque<queued_write> _write_queue      GUARDED_BY(_mtx); // queued messages, all messages except sync & trxs
      deque<queued_write> _sync_write_queue GUARDED_BY(_mtx); // sync_write_queue blocks will be sent first
      deque<queued_write> _trx_write_queue  GUARDED_BY(_mtx); // queued trx messages, trx_write_queue will be sent last
      deque<queued_write> _out_queue        GUARDED_BY(_mtx); // currently being async_write

   }; // queued_buffer


   /// monitors the status of blocks as to whether a block is accepted (sync'd) or
   /// rejected. It groups consecutive rejected blocks in a (configurable) time
   /// window (rbw) and maintains a metric of the number of consecutive rejected block
   /// time windows (rbws).
   class block_status_monitor {
   private:
      std::atomic<bool>             in_accepted_state_ {true};  ///< indicates of accepted(true) or rejected(false) state
      fc::microseconds              window_size_{2*1000};       ///< rbw time interval (2ms)
      fc::time_point                window_start_;              ///< The start of the recent rbw (0 implies not started)
      std::atomic<uint32_t>         events_{0};                 ///< The number of consecutive rbws
      const uint32_t max_consecutive_rejected_windows_{13};

   public:
      /// ctor
      ///
      /// @param[in] window_size          The time, in microseconds, of the rejected block window
      /// @param[in] max_rejected_windows The max consecutive number of rejected block windows
      /// @note   Copy ctor is not allowed
      explicit block_status_monitor(fc::microseconds window_size = fc::microseconds(2*1000),
            uint32_t max_rejected_windows = 13) :
         window_size_(window_size) {}
      block_status_monitor( const block_status_monitor& ) = delete;
      block_status_monitor( block_status_monitor&& ) = delete;
      ~block_status_monitor() = default;
      /// thread safe, reset to initial state
      void reset();
      /// thread safe, called when a block is accepted
      void accepted() { reset(); }
      /// called when a block is rejected
      void rejected();
      /// returns number of consecutive rbws
      auto events() const { return events_.load(); }
      /// indicates if the max number of consecutive rbws has been reached or exceeded
      bool max_events_violated() const { return events_ >= max_consecutive_rejected_windows_; }
      /// assignment not allowed
      block_status_monitor& operator=( const block_status_monitor& ) = delete;
      block_status_monitor& operator=( block_status_monitor&& ) = delete;
   }; // block_status_monitor


   class connection : public std::enable_shared_from_this<connection> {
   public:
      enum class connection_state { connecting, connected, closing, closed  };

      explicit connection( const string& endpoint, const string& listen_address );
      /// @brief ctor
      /// @param socket created by boost::asio in fc::listener
      /// @param address identifier of listen socket which accepted this new connection
      explicit connection( tcp::socket&& socket, const string& listen_address, size_t block_sync_rate_limit );
      ~connection() = default;

      connection( const connection& ) = delete;
      connection( connection&& ) = delete;
      connection& operator=( const connection& ) = delete;
      connection& operator=( connection&& ) = delete;

      bool start_session();

      bool socket_is_open() const { return socket_open.load(); } // thread safe, atomic
      connection_state state() const { return conn_state.load(); } // thread safe atomic
      void set_state(connection_state s);
      static std::string state_str(connection_state s);
      const string& peer_address() const { return peer_addr; } // thread safe, const

      void set_connection_type( const string& peer_addr );
      void set_peer_connection_type( const string& peer_addr );
      bool is_transactions_only_connection()const { return connection_type == transactions_only; } // thread safe, atomic
      bool is_blocks_only_connection()const { return connection_type == blocks_only; }
      bool is_transactions_connection() const { return connection_type != blocks_only; } // thread safe, atomic
      bool is_blocks_connection() const { return connection_type != transactions_only; } // thread safe, atomic
      uint32_t get_peer_start_block_num() const { return peer_start_block_num.load(); }
      uint32_t get_peer_fork_db_head_block_num() const { return peer_fork_db_head_block_num.load(); }
      uint32_t get_last_received_block_num() const { return last_received_block_num.load(); }
      uint32_t get_unique_blocks_rcvd_count() const { return unique_blocks_rcvd_count.load(); }
      size_t get_bytes_received() const { return bytes_received.load(); }
      std::chrono::nanoseconds get_last_bytes_received() const { return last_bytes_received.load(); }
      size_t get_bytes_sent() const { return bytes_sent.load(); }
      std::chrono::nanoseconds get_last_bytes_sent() const { return last_bytes_sent.load(); }
      size_t get_block_sync_bytes_received() const { return block_sync_bytes_received.load(); }
      size_t get_block_sync_bytes_sent() const { return block_sync_total_bytes_sent.load(); }
      bool get_block_sync_throttling() const { return block_sync_throttling.load(); }
      boost::asio::ip::port_type get_remote_endpoint_port() const { return remote_endpoint_port.load(); }
      void set_heartbeat_timeout(std::chrono::milliseconds msec) {
         hb_timeout = msec;
      }

      uint64_t get_peer_ping_time_ns() const { return peer_ping_time_ns; }

   private:
      static const string unknown;

      std::atomic<uint64_t> peer_ping_time_ns = std::numeric_limits<uint64_t>::max();

      std::optional<peer_sync_state> peer_requested;  // this peer is requesting info from us

      alignas(hardware_destructive_interference_sz)
      std::atomic<bool> socket_open{false};

      std::atomic<connection_state> conn_state{connection_state::connecting};

      const string            peer_addr;
      enum connection_types : char {
         both,
         transactions_only,
         blocks_only
      };

      size_t                          block_sync_rate_limit{0};  // bytes/second, default unlimited

      std::atomic<connection_types>   connection_type{both};
      std::atomic<uint32_t>           peer_start_block_num{0};
      std::atomic<uint32_t>           peer_fork_db_head_block_num{0};
      std::atomic<uint32_t>           last_received_block_num{0};
      std::atomic<fc::time_point>     last_received_block_time{};
      std::atomic<uint32_t>           unique_blocks_rcvd_count{0};
      std::atomic<size_t>             bytes_received{0};
      std::atomic<std::chrono::nanoseconds>   last_bytes_received{0ns};
      std::atomic<size_t>             bytes_sent{0};
      std::atomic<size_t>             block_sync_bytes_received{0};
      std::atomic<size_t>             block_sync_total_bytes_sent{0};
      std::chrono::nanoseconds        block_sync_send_start{0ns};     // start of enqueue blocks
      size_t                          block_sync_frame_bytes_sent{0}; // bytes sent in this set of enqueue blocks
      std::atomic<bool>               block_sync_throttling{false};
      std::atomic<std::chrono::nanoseconds>   last_bytes_sent{0ns};
      std::atomic<boost::asio::ip::port_type> remote_endpoint_port{0};

   public:
      boost::asio::strand<tcp::socket::executor_type> strand;
      std::shared_ptr<tcp::socket>     socket; // only accessed through strand after construction

      fc::message_buffer<1024*1024>    pending_message_buffer;
      std::size_t                      outstanding_read_bytes{0}; // accessed only from strand threads

      queued_buffer           buffer_queue;

      fc::sha256              conn_node_id;
      string                  short_conn_node_id;
      string                  listen_address; // address sent to peer in handshake
      string                  log_p2p_address;
      string                  log_remote_endpoint_ip;
      string                  log_remote_endpoint_port;
      string                  local_endpoint_ip;
      string                  local_endpoint_port;
      string                  short_agent_name;
      // kept in sync with last_handshake_recv.fork_db_root_num, only accessed from connection strand
      uint32_t                peer_fork_db_root_num = 0;

      std::atomic<uint32_t>   sync_ordinal{0};
      // when syncing from a peer, the last block expected of the current range
      uint32_t                sync_last_requested_block{0};

      alignas(hardware_destructive_interference_sz)
      std::atomic<uint32_t>   trx_in_progress_size{0};

      // approximate size of trx entries in the local txn cache local_txns, accessed by connection strand
      uint32_t                   trx_entries_size{0};
      fc::time_point             trx_entries_reset = fc::time_point::now();
      // does not account for the overhead of the multindex entry, but this is just an approximation
      static constexpr uint32_t  trx_full_entry_size = sizeof(node_transaction_state);
      static constexpr uint32_t  trx_conn_entry_size = sizeof(connection_id_t);

      fc::time_point          last_dropped_trx_msg_time;
      const connection_id_t   connection_id;
      int16_t                 sent_handshake_count = 0;

      alignas(hardware_destructive_interference_sz)
      std::atomic<bool>       peer_syncing_from_us{false};

      std::atomic<proto_version_t>   protocol_version = proto_version_t::base;
      proto_version_t                net_version = net_version_max;
      std::atomic<uint16_t>   consecutive_immediate_connection_close = 0;
      // bp_config = p2p-auto-bp-peer, bp_gossip = validated gossip connection,
      // bp_gossip_validating = only used when connection received before peer keys available
      enum class bp_connection_type { non_bp, bp_config, bp_gossip, bp_gossip_validating };
      std::atomic<bp_connection_type> bp_connection = bp_connection_type::non_bp;
      block_status_monitor    block_status_monitor_;
      std::atomic<time_point> last_vote_received;

      alignas(hardware_destructive_interference_sz)
      fc::mutex                        sync_response_expected_timer_mtx;
      boost::asio::steady_timer        sync_response_expected_timer GUARDED_BY(sync_response_expected_timer_mtx);

      alignas(hardware_destructive_interference_sz)
      std::atomic<go_away_reason>      no_retry{go_away_reason::no_reason};

      alignas(hardware_destructive_interference_sz)
      mutable fc::mutex                conn_mtx; //< mtx for last_handshake_recv .. remote_endpoint_ip
      handshake_message                last_handshake_recv    GUARDED_BY(conn_mtx);
      handshake_message                last_handshake_sent    GUARDED_BY(conn_mtx);
      block_id_type                    conn_fork_db_head      GUARDED_BY(conn_mtx);
      uint32_t                         conn_fork_db_head_num  GUARDED_BY(conn_mtx) {0};
      fc::time_point                   last_close             GUARDED_BY(conn_mtx);
      std::string                      p2p_address            GUARDED_BY(conn_mtx);
      std::string                      unique_conn_node_id    GUARDED_BY(conn_mtx);
      std::string                      remote_endpoint_ip     GUARDED_BY(conn_mtx);
      boost::asio::ip::address_v6::bytes_type remote_endpoint_ip_array GUARDED_BY(conn_mtx);

      std::atomic<std::chrono::nanoseconds>  connection_start_time;

      // block nack support
      static constexpr uint16_t consecutive_block_nacks_threshold{2}; // stop sending blocks when reached
      block_num_type  consecutive_blocks_nacks{0};
      block_id_type   last_block_nack;
      block_id_type   last_block_nack_request_message_id GUARDED_BY(conn_mtx);

      connection_status get_status()const;

      /** \name Peer Timestamps
       *  Time message handling
       *  @{
       */
      // See NTP protocol. https://datatracker.ietf.org/doc/rfc5905/
      std::chrono::nanoseconds               org{0}; //!< origin timestamp. Time at the client when the request departed for the server.
      // std::chrono::nanoseconds (not used) rec{0}; //!< receive timestamp. Time at the server when the request arrived from the client.
      std::chrono::nanoseconds               xmt{0}; //!< transmit timestamp, Time at the server when the response left for the client.
      // std::chrono::nanoseconds (not used) dst{0}; //!< destination timestamp, Time at the client when the reply arrived from the server.
      /** @} */
      // timestamp for the lastest message
      std::chrono::steady_clock::time_point       latest_msg_time{std::chrono::steady_clock::time_point::min()};
      std::chrono::milliseconds                   hb_timeout{std::chrono::milliseconds{def_keepalive_interval}};
      std::chrono::steady_clock::time_point       latest_blk_time{std::chrono::steady_clock::time_point::min()};

      bool connected() const;
      bool closed() const; // socket is not open or is closed or closing, thread safe
      bool current() const;
      bool should_sync_from(uint32_t sync_next_expected_num, uint32_t sync_known_fork_db_root_num, uint32_t sync_fetch_span) const;

      /// @param reconnect true if we should try and reconnect immediately after close
      /// @param shutdown true only if plugin is shutting down
      void close( bool reconnect = false, bool shutdown = false );
   private:
      void _close( bool reconnect, bool shutdown ); // for easy capture

      bool process_next_block_message(uint32_t message_length);
      bool process_next_trx_message(uint32_t message_length);
      bool process_next_trx_notice_message(uint32_t message_length);
      bool process_next_vote_message(uint32_t message_length);
      void update_endpoints(const tcp::endpoint& endpoint = tcp::endpoint());

      void send_gossip_bp_peers_initial_message();
      void send_gossip_bp_peers_message();
   public:
      static void send_gossip_bp_peers_message_to_bp_peers();

      bool populate_handshake( handshake_message& hello ) const;

      bool resolve_and_connect();
      void connect( const tcp::resolver::results_type& endpoints );
      void start_read_message();

      /** \brief Process the next message from the pending message buffer
       *
       * Process the next message from the pending_message_buffer.
       * message_length is the already determined length of the data
       * part of the message that will handle the message.
       * Returns true is successful. Returns false if an error was
       * encountered unpacking or processing the message.
       */
      bool process_next_message(uint32_t message_length);

      void send_block_nack(const block_id_type& block_id);
      void send_handshake();

      /** \name Peer Timestamps
       *  Time message handling
       */
      /**  \brief Check heartbeat time and send Time_message
       */
      void check_heartbeat( std::chrono::steady_clock::time_point current_time );
      /**  \brief Populate and queue time_message
       */
      void send_time();
      /** \brief Populate and queue time_message immediately using incoming time_message
       */
      void send_time(const time_message& msg);
      /** \brief Read system time and convert to a 64 bit integer.
       *
       * There are six calls to this routine in the program.  One
       * when a packet arrives from the network, one when a packet
       * is placed on the send queue, one during start session, one
       * when a sync block is queued and one each when data is
       * counted as received or sent.
       * Calls the kernel time of day routine and converts to 
       * a (at least) 64 bit integer.
       */
      static std::chrono::nanoseconds get_time() {
         return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch());
      }
      /** @} */

      void blk_send_branch( const block_id_type& msg_head_id );
      void blk_send_branch_from_nack_request( const block_id_type& msg_head_id, const block_id_type& req_id );
      void blk_send_branch(uint32_t msg_head_num, uint32_t fork_db_root_num, uint32_t head_num, peer_sync_state::sync_t sync_type);

      void enqueue( const net_message& msg );
      size_t enqueue_block( const std::vector<char>& sb, uint32_t block_num, queued_buffer::queue_t queue );
      void enqueue_buffer( msg_type_t net_msg,
                           std::optional<block_num_type> block_num,
                           queued_buffer::queue_t queue,
                           const send_buffer_type& send_buffer,
                           go_away_reason close_after_send);
      void cancel_sync();
      void flush_queues();
      bool enqueue_sync_block();
      void request_sync_blocks(uint32_t start, uint32_t end);

      void cancel_sync_wait();
      void sync_wait();

      void queue_write(msg_type_t net_msg,
                       std::optional<block_num_type> block_num,
                       queued_buffer::queue_t queue,
                       const send_buffer_type& buff,
                       std::function<void(boost::system::error_code, std::size_t)> callback);
      void do_queue_write(std::optional<block_num_type> block_num);
      void log_send_buffer_stats() const;

      bool is_valid( const handshake_message& msg ) const;

      void handle_message( const handshake_message& msg );
      void handle_message( const chain_size_message& msg );
      void handle_message( const go_away_message& msg );
      /** \name Peer Timestamps
       *  Time message handling
       *  @{
       */
      /** \brief Process time_message
       *
       * Calculate offset, delay and dispersion.  Note carefully the
       * implied processing.  The first-order difference is done
       * directly in 64-bit arithmetic, then the result is converted
       * to floating double.  All further processing is in
       * floating-double arithmetic with rounding done by the hardware.
       * This is necessary in order to avoid overflow and preserve precision.
       */
      void handle_message( const time_message& msg );
      /** @} */
      void handle_message( const notice_message& msg );
      void handle_message( const request_message& msg );
      void handle_message( const sync_request_message& msg );
      void handle_message( const signed_block& msg ) = delete; // signed_block_ptr overload used instead
      void handle_message( const block_id_type& id, signed_block_ptr ptr );
      void handle_message( const packed_transaction& msg ) = delete; // packed_transaction_ptr overload used instead
      void handle_message( const packed_transaction_ptr& trx );
      void handle_message( const vote_message_ptr& msg );
      void handle_message( const vote_message& msg ) = delete; // vote_message_ptr overload used instead
      void handle_message( const block_nack_message& msg);
      void handle_message( const block_notice_message& msg);
      void handle_message( gossip_bp_peers_message& msg);
      void handle_message( const gossip_bp_peers_message& msg) = delete;
      void handle_message( const transaction_notice_message& msg);

      // returns calculated number of blocks combined latency
      uint32_t calc_block_latency();

      fc::variant_object get_logger_variant() const {
         fc::mutable_variant_object mvo;
         mvo( "_peer", peer_addr.empty() ? log_p2p_address : peer_addr )
            ( "_name", log_p2p_address)
            ( "_cid", connection_id )
            ( "_id", conn_node_id )
            ( "_sid", short_conn_node_id )
            ( "_ip", log_remote_endpoint_ip )
            ( "_port", log_remote_endpoint_port )
            ( "_lip", local_endpoint_ip )
            ( "_lport", local_endpoint_port )
            ( "_agent", short_agent_name )
            ( "_nver", static_cast<uint16_t>(protocol_version.load()) );
         return mvo;
      }

      bool incoming() const { return peer_address().empty(); } // thread safe because of peer_address
      bool incoming_and_handshake_received() const {
         if (!incoming()) return false;
         fc::lock_guard g_conn( conn_mtx );
         return !last_handshake_recv.p2p_address.empty();
      }
   }; // class connection

   const string connection::unknown = "<unknown>";

   // called from connection strand
   struct msg_handler : public fc::visitor<void> {
      connection_ptr c;
      explicit msg_handler( connection_ptr conn) : c(std::move(conn)) {}

      template<typename T>
      void operator()( const T& ) const {
         EOS_ASSERT( false, plugin_config_exception, "Not implemented, call handle_message directly instead" );
      }

      void operator()( const handshake_message& msg ) const {
         // continue call to handle_message on connection strand
         peer_dlog( p2p_msg_log, c, "handle handshake_message" );
         c->handle_message( msg );
      }

      void operator()( const chain_size_message& msg ) const {
         // continue call to handle_message on connection strand
         peer_dlog( p2p_msg_log, c, "handle chain_size_message" );
         c->handle_message( msg );
      }

      void operator()( const go_away_message& msg ) const {
         // continue call to handle_message on connection strand
         peer_dlog( p2p_msg_log, c, "handle go_away_message" );
         c->handle_message( msg );
      }

      void operator()( const time_message& msg ) const {
         // continue call to handle_message on connection strand
         peer_dlog( p2p_msg_log, c, "handle time_message" );
         c->handle_message( msg );
      }

      void operator()( const notice_message& msg ) const {
         // continue call to handle_message on connection strand
         peer_dlog( p2p_msg_log, c, "handle notice_message" );
         c->handle_message( msg );
      }

      void operator()( const request_message& msg ) const {
         // continue call to handle_message on connection strand
         peer_dlog( p2p_msg_log, c, "handle request_message" );
         c->handle_message( msg );
      }

      void operator()( const sync_request_message& msg ) const {
         // continue call to handle_message on connection strand
         peer_dlog( p2p_msg_log, c, "handle sync_request_message" );
         c->handle_message( msg );
      }

      void operator()( const block_nack_message& msg ) const {
         // continue call to handle_message on connection strand
         peer_dlog( p2p_blk_log, c, "handle block_nack_message #${bn}:${id}", ("bn", block_header::num_from_id(msg.id))("id", msg.id) );
         c->handle_message( msg );
      }

      void operator()( const block_notice_message& msg ) const {
         // continue call to handle_message on connection strand
         peer_dlog( p2p_blk_log, c, "handle block_notice_message #${bn}:${id}", ("bn", block_header::num_from_id(msg.id))("id", msg.id) );
         c->handle_message( msg );
      }

      void operator()( gossip_bp_peers_message& msg ) const {
         // continue call to handle_message on connection strand
         peer_dlog( p2p_msg_log, c, "handle gossip_bp_peers_message ${m}", ("m", msg) );
         c->handle_message( msg );
      }
   };

   template<typename Function>
   bool connections_manager::any_of_supplied_peers( Function&& f ) const {
      std::shared_lock g( connections_mtx );
      return std::any_of(supplied_peers.begin(), supplied_peers.end(), std::forward<Function>(f));
   }

   template<typename Function>
   void connections_manager::for_each_connection( Function&& f ) const {
      std::shared_lock g( connections_mtx );
      auto& index = connections.get<by_host>();
      for( const connection_detail& cd : index ) {
         f(cd.c);
      }
   }

   template<typename Function>
   void connections_manager::for_each_block_connection( Function&& f ) const {
      std::shared_lock g( connections_mtx );
      auto& index = connections.get<by_host>();
      for( const connection_detail& cd : index ) {
         if (cd.c->is_blocks_connection()) {
            f(cd.c);
         }
      }
   }

   template <typename UnaryPredicate>
   bool connections_manager::any_of_connections(UnaryPredicate&& p) const {
      std::shared_lock g(connections_mtx);
      auto& index = connections.get<by_host>();
      for( const connection_detail& cd : index ) {
         if (p(cd.c))
            return true;
      }
      return false;
   }

   template <typename UnaryPredicate>
   bool connections_manager::any_of_block_connections(UnaryPredicate&& p) const {
      std::shared_lock g( connections_mtx );
      auto& index = connections.get<by_host>();
      for( const connection_detail& cd : index ) {
         if( cd.c->is_blocks_connection() ) {
            if (p(cd.c))
              return true;
         }
      }
      return false;
   }

   //---------------------------------------------------------------------------

   struct on_fork_t {
         bool on_fork = true;
         bool unknown = true;
      };
   on_fork_t block_on_fork(const block_id_type& id) { // thread safe
      auto id_num = block_header::num_from_id(id);
      bool on_fork = false;
      bool unknown_block = true;
      try {
         const controller& cc = my_impl->chain_plug->chain();
         std::optional<block_id_type> my_id = cc.fork_block_id_for_num( id_num ); // thread-safe
         unknown_block = !my_id;
         on_fork = my_id != id;
      } catch( ... ) {
      }
      return { on_fork, unknown_block };
   }

   connection::connection( const string& endpoint, const string& this_address )
      : peer_addr( endpoint ),
        strand( boost::asio::make_strand(my_impl->thread_pool.get_executor()) ),
        socket( new tcp::socket( strand ) ),
        log_p2p_address( endpoint ),
        connection_id( ++my_impl->current_connection_id ),
        sync_response_expected_timer( my_impl->thread_pool.get_executor() ),
        last_handshake_recv(),
        last_handshake_sent(),
        p2p_address( endpoint )
   {
      auto [host, port, type] = net_utils::split_host_port_type(this_address);
      listen_address = host + ":" + port; // do not include type in listen_address to avoid peer setting type on connection
      set_connection_type( peer_address() );
      my_impl->mark_configured_bp_connection(this);
      fc_ilog( p2p_conn_log, "created connection - ${c} to ${n}", ("c", connection_id)("n", endpoint) );
   }

   connection::connection(tcp::socket&& s, const string& listen_address, size_t block_sync_rate_limit)
      : peer_addr(),
        block_sync_rate_limit(block_sync_rate_limit),
        strand( s.get_executor() ),
        socket( new tcp::socket( std::move(s) ) ),
        listen_address( listen_address ),
        connection_id( ++my_impl->current_connection_id ),
        sync_response_expected_timer( my_impl->thread_pool.get_executor() ),
        last_handshake_recv(),
        last_handshake_sent()
   {
      fc_dlog( p2p_conn_log, "new connection - ${c} object created for peer ${address}:${port} from listener ${addr}",
               ("c", connection_id)("address", log_remote_endpoint_ip)("port", log_remote_endpoint_port)("addr", listen_address) );
   }

   void connection::update_endpoints(const tcp::endpoint& endpoint) {
      boost::system::error_code ec;
      boost::system::error_code ec2;
      auto rep = endpoint == tcp::endpoint() ? socket->remote_endpoint(ec) : endpoint;
      auto lep = socket->local_endpoint(ec2);
      remote_endpoint_port = ec ? 0 : rep.port();
      log_remote_endpoint_ip = ec ? unknown : rep.address().to_string();
      log_remote_endpoint_port = ec ? unknown : std::to_string(rep.port());
      local_endpoint_ip = ec2 ? unknown : lep.address().to_string();
      local_endpoint_port = ec2 ? unknown : std::to_string(lep.port());
      fc::lock_guard g_conn( conn_mtx );
      remote_endpoint_ip = log_remote_endpoint_ip;
      if(!ec) {
         if(rep.address().is_v4()) {
            remote_endpoint_ip_array = make_address_v6(boost::asio::ip::v4_mapped, rep.address().to_v4()).to_bytes();
         }
         else {
            remote_endpoint_ip_array = rep.address().to_v6().to_bytes();
         }
      }
      else {
         fc_dlog( p2p_conn_log, "unable to retrieve remote endpoint for local ${address}:${port}", ("address", local_endpoint_ip)("port", local_endpoint_port));
         remote_endpoint_ip_array = boost::asio::ip::address_v6().to_bytes();
      }
   }

   // called from connection strand
   void connection::set_connection_type( const std::string& peer_add ) {
      auto [host, port, type] = net_utils::split_host_port_type(peer_add);
      if (host.empty()) {
         fc_dlog( p2p_conn_log, "Invalid address: ${a}", ("a", peer_add));
      } else if( type.empty() ) {
         fc_dlog( p2p_conn_log, "Setting connection - ${c} type for: ${peer} to both transactions and blocks", ("c", connection_id)("peer", peer_add) );
         connection_type = both;
      } else if( type == "trx" ) {
         fc_dlog( p2p_conn_log, "Setting connection - ${c} type for: ${peer} to transactions only", ("c", connection_id)("peer", peer_add) );
         connection_type = transactions_only;
      } else if( type == "blk" ) {
         fc_dlog( p2p_conn_log, "Setting connection - ${c} type for: ${peer} to blocks only", ("c", connection_id)("peer", peer_add) );
         connection_type = blocks_only;
      } else {
         fc_wlog( p2p_conn_log, "Unknown connection - ${c} type: ${t}, for ${peer}", ("c", connection_id)("t", type)("peer", peer_add) );
      }
   }

   // called from connection strand
   void connection::set_peer_connection_type( const std::string& peer_add ) {
      // peer p2p-listen-endpoint received via handshake may indicate they do not want trx or blocks
      auto [host, port, type] = net_utils::split_host_port_type(peer_add);
      if (host.empty()) {
         fc_dlog( p2p_conn_log, "Invalid peer address: ${a}", ("a", peer_add));
      } else if( type.empty() ) {
         // peer asked for both, continue with p2p-peer-address type
      } else if( type == "trx" ) {
         if (connection_type == both) { // only switch to trx if p2p-peer-address didn't specify a connection type
            fc_dlog( p2p_conn_log, "Setting peer connection - ${c} type for: ${peer} to transactions only", ("c", connection_id)("peer", peer_add) );
            connection_type = transactions_only;
         }
      } else if( type == "blk" ) {
         if (connection_type == both) { // only switch to blocks if p2p-peer-address didn't specify a connection type
            fc_dlog( p2p_conn_log, "Setting peer connection - ${c} type for: ${peer} to blocks only", ("c", connection_id)("peer", peer_add) );
            connection_type = blocks_only;
         }
      } else {
         fc_dlog( p2p_conn_log, "Unknown peer connection - ${c} type: ${t}, for ${peer}", ("c", connection_id)("t", type)("peer", peer_add) );
      }
   }

   std::string connection::state_str(connection_state s) {
      switch (s) {
      case connection_state::connecting:
         return "connecting";
      case connection_state::connected:
         return "connected";
      case connection_state::closing:
         return "closing";
      case connection_state::closed:
         return "closed";
      }
      return "unknown";
   }

   void connection::set_state(connection_state s) {
      auto curr = state();
      if (curr == s)
         return;
      if (s == connection_state::connected && curr != connection_state::connecting)
         return;
      fc_dlog(p2p_conn_log, "old connection - ${c} state ${os} becoming ${ns}", ("c", connection_id)("os", state_str(curr))("ns", state_str(s)));

      conn_state = s;
   }

   connection_status connection::get_status()const {
      connection_status stat;
      stat.connecting = state() == connection_state::connecting;
      stat.syncing = peer_syncing_from_us;
      stat.is_bp_peer = bp_connection != bp_connection_type::non_bp;
      stat.is_bp_gossip_peer = bp_connection == bp_connection_type::bp_gossip;
      stat.is_socket_open = socket_is_open();
      stat.is_blocks_only = is_blocks_only_connection();
      stat.is_transactions_only = is_transactions_only_connection();
      stat.last_vote_received = last_vote_received;
      fc::lock_guard g( conn_mtx );
      stat.peer = peer_addr;
      stat.remote_ip = log_remote_endpoint_ip;
      stat.remote_port = log_remote_endpoint_port;
      stat.last_handshake = last_handshake_recv;
      return stat;
   }

   // called from connection strand
   bool connection::start_session() {
      verify_strand_in_this_thread( strand, __func__, __LINE__ );

      update_endpoints();
      boost::system::error_code ec;
      socket->set_option( boost::asio::ip::tcp::no_delay{true}, ec );
      if (!ec) socket->set_option(boost::asio::socket_base::send_buffer_size{1024*1024}, ec);
      if (!ec) socket->set_option(boost::asio::socket_base::receive_buffer_size{1024*1024}, ec);
      if( ec ) {
         peer_wlog( p2p_conn_log, this, "connection failed (set_option): ${e}", ( "e", ec.message() ) );
         close();
         return false;
      } else {
         peer_dlog( p2p_conn_log, this, "connected" );
         socket_open = true;
         connection_start_time = get_time();
         start_read_message();
         return true;
      }
   }

   // thread safe, all atomics
   bool connection::connected() const {
      return socket_is_open() && state() == connection_state::connected;
   }

   bool connection::closed() const {
      return !socket_is_open()
             || state() == connection_state::closing
             || state() == connection_state::closed;
   }

   // thread safe, all atomics
   bool connection::current() const {
      return (connected() && !peer_syncing_from_us);
   }

   // thread safe
   bool connection::should_sync_from(uint32_t sync_next_expected_num, uint32_t sync_known_fork_db_root_num, uint32_t sync_fetch_span) const {
      fc_dlog(p2p_conn_log, "id: ${id} blocks conn: ${t} current: ${c} socket_open: ${so} syncing from us: ${s} state: ${con} peer_start_block: ${sb} peer_fhead: ${h} ping: ${p}us no_retry: ${g}",
              ("id", connection_id)("t", is_blocks_connection())
              ("c", current())("so", socket_is_open())("s", peer_syncing_from_us.load())("con", state_str(state()))
              ("sb", peer_start_block_num.load())("h", peer_fork_db_head_block_num.load())("p", get_peer_ping_time_ns()/1000)("g", reason_str(no_retry)));
      if (is_blocks_connection() && current()) {
         if (no_retry == go_away_reason::no_reason) {
            if (peer_start_block_num <= sync_next_expected_num) { // has blocks we want
               auto needed_end = std::min(sync_next_expected_num + sync_fetch_span, sync_known_fork_db_root_num);
               if (peer_fork_db_head_block_num >= needed_end) { // has blocks
                  return true;
               }
            }
         }
      }
      return false;
   }

   void connection::flush_queues() {
      buffer_queue.clear_write_queue();
   }

   void connection::close( bool reconnect, bool shutdown ) {
      set_state(connection_state::closing);
      boost::asio::post(strand, [self = shared_from_this(), reconnect, shutdown]() {
         self->_close( reconnect, shutdown );
      });
   }

   // called from connection strand
   void connection::_close( bool reconnect, bool shutdown ) {
      if (socket_open)
         peer_ilog(p2p_conn_log, this, "closing");
      else
         peer_dlog(p2p_conn_log, this, "close called on already closed socket");
      socket_open = false;
      boost::system::error_code ec;
      socket->shutdown( tcp::socket::shutdown_both, ec );
      socket->close( ec );
      socket.reset( new tcp::socket( my_impl->thread_pool.get_executor() ) );
      flush_queues();
      peer_syncing_from_us = false;
      block_status_monitor_.reset();
      ++consecutive_immediate_connection_close;
      {
         fc::lock_guard g_conn( conn_mtx );
         last_handshake_recv = handshake_message();
         last_handshake_sent = handshake_message();
         last_close = fc::time_point::now();
         conn_node_id = fc::sha256();
         last_block_nack_request_message_id = block_id_type{};
      }
      peer_fork_db_root_num = 0;
      peer_ping_time_ns = std::numeric_limits<decltype(peer_ping_time_ns)::value_type>::max();
      peer_requested.reset();
      sent_handshake_count = 0;
      if( !shutdown) my_impl->sync_master->sync_reset_fork_db_root_num( shared_from_this(), true );
      cancel_sync_wait();
      sync_last_requested_block = 0;
      org = std::chrono::nanoseconds{0};
      latest_msg_time = std::chrono::steady_clock::time_point::min();
      latest_blk_time = std::chrono::steady_clock::time_point::min();
      set_state(connection_state::closed);
      block_sync_send_start = 0ns;
      block_sync_frame_bytes_sent = 0;
      block_sync_throttling = false;
      last_vote_received = time_point{};
      consecutive_blocks_nacks = 0;
      last_block_nack = block_id_type{};
      bp_connection = bp_connection_type::non_bp;

      // if recently received a block from the connection then reset all connection block nacks
      if (last_received_block_time.load() >= my_impl->last_block_received_time.load() - fc::seconds(3)) {
         sync_manager::send_block_nack_resets();
      }

      if( reconnect && !shutdown && !incoming() ) {
         my_impl->connections.start_conn_timer( std::chrono::milliseconds( 100 ),
                                                connection_wptr(),
                                                connections_manager::timer_type::check );
      }
   }

   // called from connection strand
   void connection::blk_send_branch( const block_id_type& msg_head_id ) {
      uint32_t head_num = my_impl->get_chain_head_num();

      peer_dlog(p2p_blk_log, this, "head_num = ${h}",("h",head_num));
      if(head_num == 0) {
         notice_message note;
         note.known_blocks.mode = normal;
         note.known_blocks.pending = 0;
         enqueue(note);
         return;
      }

      if( p2p_blk_log.is_enabled( fc::log_level::debug ) ) {
         fc::unique_lock g_conn( conn_mtx );
         if( last_handshake_recv.generation >= 1 ) {
            peer_dlog( p2p_blk_log, this, "maybe truncating branch at = ${h}:${id}",
                       ("h", block_header::num_from_id(last_handshake_recv.fork_db_head_id))("id", last_handshake_recv.fork_db_head_id) );
         }
      }
      const auto fork_db_root_num = peer_fork_db_root_num;
      if( fork_db_root_num == 0 ) return; // if fork_db_root_id is null (we have not received handshake or reset)

      auto msg_head_num = block_header::num_from_id(msg_head_id);
      if (msg_head_num == 0) {
         blk_send_branch( msg_head_num, fork_db_root_num, head_num, peer_sync_state::sync_t::peer_catchup );
         return;
      }

      auto [on_fork, unknown_block] = block_on_fork(msg_head_id);
      if( unknown_block ) {
         peer_ilog( p2p_blk_log, this, "Peer asked for unknown block ${mn}, sending: benign_other go away", ("mn", msg_head_num) );
         no_retry = go_away_reason::benign_other;
         enqueue( go_away_message{ go_away_reason::benign_other } );
      } else {
         // if peer on fork, start at their last fork_db_root_num, otherwise we can start at msg_head+1
         if (on_fork)
            msg_head_num = 0;
         blk_send_branch( msg_head_num, fork_db_root_num, head_num, peer_sync_state::sync_t::peer_catchup );
      }
   }

   // called from connection strand
   void connection::blk_send_branch_from_nack_request( const block_id_type& msg_head_id, const block_id_type& req_id ) {
      auto [on_fork, unknown_block] = block_on_fork(msg_head_id);
      uint32_t head_num = my_impl->get_chain_head_num();
      // peer head might be unknown if our LIB has moved past it, so if unknown then just send the requested block
      if (on_fork) { // send from lib if we know they are on a fork
         // a more complicated better approach would be to find where the fork branches and send from there, for now use lib
         uint32_t fork_db_root_num = my_impl->get_fork_db_root_num();
         // --fork_db_root_num since blk_send_branch adds one to the request, and we want to start at fork_db_root_num
         blk_send_branch( --fork_db_root_num, 0, head_num, peer_sync_state::sync_t::block_nack);
      } else {
         auto msg_req_num = block_header::num_from_id(req_id);
         // --msg_req_num since blk_send_branch adds one to the request, and we need to start at msg_req_num
         blk_send_branch( --msg_req_num, 0, head_num, peer_sync_state::sync_t::block_nack );
      }
   }

   // called from connection strand
   void connection::blk_send_branch( uint32_t msg_head_num, uint32_t fork_db_root_num, uint32_t head_num, peer_sync_state::sync_t sync_type) {
      if( !peer_requested ) {
         auto last = msg_head_num != 0 ? msg_head_num : fork_db_root_num;
         peer_requested = peer_sync_state( last+1, head_num, last, sync_type );
      } else {
         auto last = msg_head_num != 0 ? msg_head_num : std::min( peer_requested->last, fork_db_root_num );
         uint32_t end = std::max( peer_requested->end_block, head_num );
         if (peer_requested->start_block <= last+1 && peer_requested->end_block >= end)
            return; // nothing to do, send in progress
         peer_requested = peer_sync_state( last+1, end, last, sync_type );
      }
      if( peer_requested->valid() ) {
         peer_ilog( p2p_blk_log, this, "enqueue ${t} ${s} - ${e}",
                    ("t", sync_type)("s", peer_requested->start_block)("e", peer_requested->end_block) );
         enqueue_sync_block();
      } else {
         peer_ilog( p2p_blk_log, this, "nothing to enqueue" );
         peer_requested.reset();
      }
   }

   void connection::send_handshake() {
      if (closed())
         return;
      boost::asio::post(strand, [c = shared_from_this()]() {
         fc::unique_lock g_conn( c->conn_mtx );
         if( c->populate_handshake( c->last_handshake_sent ) ) {
            static_assert( std::is_same_v<decltype( c->sent_handshake_count ), int16_t>, "INT16_MAX based on int16_t" );
            if( c->sent_handshake_count == INT16_MAX ) c->sent_handshake_count = 1; // do not wrap
            c->last_handshake_sent.generation = ++c->sent_handshake_count;
            auto last_handshake = c->last_handshake_sent;
            g_conn.unlock();
            peer_dlog( p2p_msg_log, c, "Sending handshake generation ${g}, froot ${r}, fhead ${h}, id ${id}",
                       ("g", last_handshake.generation)
                       ("r", last_handshake.fork_db_root_num)
                       ("h", last_handshake.fork_db_head_num)("id", last_handshake.fork_db_head_id.str().substr(8,16)) );
            c->enqueue( last_handshake );
         }
      });
   }

   // called from connection strand
   void connection::check_heartbeat( std::chrono::steady_clock::time_point current_time ) {
      if( latest_msg_time > std::chrono::steady_clock::time_point::min() ) {
         if( current_time > latest_msg_time + hb_timeout ) {
            no_retry = go_away_reason::benign_other;
            if( !incoming() ) {
               peer_wlog(p2p_conn_log, this, "heartbeat timed out for peer address");
               close(true);
            } else {
               peer_wlog(p2p_conn_log, this, "heartbeat timed out");
               close(false);
            }
            return;
         }
         if (!my_impl->sync_master->syncing_from_peer()) {
            const std::chrono::milliseconds timeout = std::max(hb_timeout/2, 2*std::chrono::milliseconds(config::block_interval_ms));
            if (std::chrono::steady_clock::now() > latest_blk_time + timeout) {
               peer_wlog(p2p_conn_log, this, "half heartbeat timed out, sending handshake");
               send_handshake();
               return;
            }
         }

      }

      org = std::chrono::nanoseconds{0};
      send_time();
   }

   // called from connection strand
   void connection::send_time() {
      if (org == std::chrono::nanoseconds{0}) { // do not send if there is already a time loop in progress
         org = get_time();
         // xpkt.org == 0 means we are initiating a ping. Actual origin time is in xpkt.xmt.
         time_message xpkt{
            .org = 0,
            .rec = 0,
            .xmt = org.count(),
            .dst = 0 };
         peer_dlog(p2p_msg_log, this, "send init time_message: ${t}", ("t", xpkt));
         enqueue(xpkt);
      }
   }

   // called from connection strand
   void connection::send_time(const time_message& msg) {
      time_message xpkt{
         .org = msg.xmt,
         .rec = msg.dst,
         .xmt = get_time().count(),
         .dst = 0 };
      peer_dlog( p2p_msg_log, this, "send time_message: ${t}, org: ${o}", ("t", xpkt)("o", org.count()) );
      enqueue(xpkt);
   }

   // called from connection strand
   void connection::log_send_buffer_stats() const {
      if (!p2p_conn_log.is_enabled(fc::log_level::debug))
         return;

      boost::system::error_code ec;
      boost::asio::socket_base::send_buffer_size max_send_buffer_size{};
      socket->get_option(max_send_buffer_size, ec);
      if (ec) {
         peer_dlog(p2p_conn_log, this, "Error getting bytes in send buffer: ${e}", ("e", ec.what()));
         return;
      }

#if __has_include(<sys/ioctl.h>)
      auto sockfd = socket->native_handle();
      int bytes_in_send_buffer;
      if (ioctl(sockfd, TIOCOUTQ, &bytes_in_send_buffer) == -1) {
         peer_dlog(p2p_conn_log, this, "Error getting bytes in send buffer: ${e}", ("e", strerror(errno)));
      } else {
         // bytes_in_send_buffer now holds the number of bytes currently in the send buffer
         // to get available space, subtract this from the send_buffer_size (SO_SNDBUF) value
         auto available_send_buffer_space = max_send_buffer_size.value() - bytes_in_send_buffer;
         peer_dlog(p2p_conn_log, this, "send buffer: ${b} bytes, max send buffer: ${m} bytes, available: ${a} bytes",
                   ("b", bytes_in_send_buffer)("m", max_send_buffer_size.value())("a", available_send_buffer_space));
      }
#else
      peer_dlog(p2p_conn_log, this, "max send buffer: ${m} bytes", ("m", max_send_buffer_size.value()));
#endif
   }

   // called from connection strand
   void connection::queue_write(msg_type_t net_msg,
                                std::optional<block_num_type> block_num,
                                queued_buffer::queue_t queue,
                                const send_buffer_type& buff,
                                std::function<void(boost::system::error_code, std::size_t)> callback) {
      if( !buffer_queue.add_write_queue( net_msg, queue, buff, std::move(callback) )) {
         peer_wlog( p2p_conn_log, this, "write_queue full ${s} bytes, giving up on connection", ("s", buffer_queue.write_queue_size()) );
         close();
         return;
      }
      do_queue_write(block_num);
   }

   // called from connection strand
   void connection::do_queue_write(std::optional<block_num_type> block_num) {
      if( !buffer_queue.ready_to_send(connection_id) ) {
         if (block_num) {
            peer_dlog(p2p_conn_log, this, "connection currently sending, queueing block ${n}", ("n", *block_num) );
         }
         return;
      }
      if (closed()) {
         peer_dlog(p2p_conn_log, this, "connection closed, not sending queued write");
         return;
      }

      std::vector<boost::asio::const_buffer> bufs;
      buffer_queue.fill_out_buffer( bufs );

      log_send_buffer_stats();

      boost::asio::async_write( *socket, bufs,
         boost::asio::bind_executor( strand, [c=shared_from_this(), socket=socket]( boost::system::error_code ec, std::size_t w ) {
         try {
            peer_dlog(p2p_conn_log, c, "async write complete");
            // May have closed connection and cleared buffer_queue
            if (!c->socket->is_open() && c->socket_is_open()) { // if socket_open then close not called
               peer_ilog(p2p_conn_log, c, "async write socket closed before callback");
               c->buffer_queue.clear_out_queue(ec, w);
               c->close();
               return;
            }
            if (socket != c->socket ) { // different socket, c must have created a new socket, make sure previous is closed
               peer_ilog( p2p_conn_log, c, "async write socket changed before callback");
               c->buffer_queue.clear_out_queue(ec, w);
               boost::system::error_code ignore_ec;
               socket->shutdown( tcp::socket::shutdown_both, ignore_ec );
               socket->close( ignore_ec );
               return;
            }

            if( ec ) {
               if( ec.value() != boost::asio::error::eof ) {
                  peer_wlog( p2p_conn_log, c, "Error sending to peer: ${i}", ( "i", ec.message() ) );
               } else {
                  peer_wlog( p2p_conn_log, c, "connection closure detected on write" );
               }
               c->close();
               return;
            }
            c->bytes_sent += w;
            c->last_bytes_sent = c->get_time();

            c->buffer_queue.clear_out_queue(ec, w);

            c->enqueue_sync_block();
            c->do_queue_write(std::nullopt);
         } catch ( const std::bad_alloc& ) {
           throw;
         } catch ( const boost::interprocess::bad_alloc& ) {
           throw;
         } catch( const fc::exception& ex ) {
            peer_elog( p2p_conn_log, c, "fc::exception in do_queue_write: ${s}", ("s", ex.to_string()) );
         } catch( const std::exception& ex ) {
            peer_elog( p2p_conn_log, c, "std::exception in do_queue_write: ${s}", ("s", ex.what()) );
         } catch( ... ) {
            peer_elog( p2p_conn_log, c, "Unknown exception in do_queue_write" );
         }
      }));
   }

   // called from connection strand
   void connection::cancel_sync() {
      peer_dlog( p2p_blk_log, this, "cancel sync_wait, write queue size ${o} bytes", ("o", buffer_queue.write_queue_size()) );
      cancel_sync_wait();
      sync_last_requested_block = 0;
      flush_queues();
      peer_ilog(p2p_blk_log, this, "sending empty request but not calling sync wait");
      enqueue( ( sync_request_message ) {0,0} );
   }

   // called from connection strand
   bool connection::enqueue_sync_block() {
      if( !peer_requested ) {
         return false;
      } else {
         peer_dlog( p2p_blk_log, this, "enqueue sync block ${num}", ("num", peer_requested->last + 1) );
      }
      uint32_t num = peer_requested->last + 1;

      controller& cc = my_impl->chain_plug->chain();
      std::vector<char> sb;
      try {
         sb = cc.fetch_serialized_block_by_number( num ); // thread-safe
      } FC_LOG_AND_DROP();
      if( !sb.empty() ) {
         // Skip transmitting block this loop if threshold exceeded
         if (block_sync_send_start == 0ns) { // start of enqueue blocks
            block_sync_send_start = get_time();
            block_sync_frame_bytes_sent = 0;
         }
         if( block_sync_rate_limit > 0 && block_sync_frame_bytes_sent > 0 && peer_syncing_from_us ) {
            auto now = get_time();
            auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - block_sync_send_start);
            double current_rate_sec = (double(block_sync_frame_bytes_sent) / elapsed_us.count()) * 100000; // convert from bytes/us => bytes/sec
            peer_dlog(p2p_blk_log, this, "start enqueue block time ${st}, now ${t}, elapsed ${e}, rate ${r}, limit ${l}",
                      ("st", block_sync_send_start.count())("t", now.count())("e", elapsed_us.count())("r", current_rate_sec)("l", block_sync_rate_limit));
            if( current_rate_sec >= block_sync_rate_limit ) {
               block_sync_throttling = true;
               peer_dlog( p2p_blk_log, this, "throttling block sync to peer ${host}:${port}", ("host", log_remote_endpoint_ip)("port", log_remote_endpoint_port));
               std::shared_ptr<boost::asio::steady_timer> throttle_timer = std::make_shared<boost::asio::steady_timer>(my_impl->thread_pool.get_executor());
               throttle_timer->expires_from_now(std::chrono::milliseconds(100));
               throttle_timer->async_wait(boost::asio::bind_executor(strand, [c=shared_from_this(), throttle_timer](const boost::system::error_code& ec) {
                  if (!ec)
                    c->enqueue_sync_block();
               }));
               return false;
            }
         }
         block_sync_throttling = false;
         auto sent = enqueue_block( sb, num, queued_buffer::queue_t::block_sync );
         block_sync_total_bytes_sent += sent;
         block_sync_frame_bytes_sent += sent;
         ++peer_requested->last;
         if(num == peer_requested->end_block) {
            peer_requested.reset();
            block_sync_send_start = 0ns;
            block_sync_frame_bytes_sent = 0;
            peer_dlog( p2p_blk_log, this, "completing enqueue_sync_block ${num}", ("num", num) );
         }
      } else if (peer_requested->sync_type == peer_sync_state::sync_t::peer_catchup || peer_requested->sync_type == peer_sync_state::sync_t::block_nack) {
         // Do not have the block, likely because in the middle of a fork-switch. A fork-switch will send out
         // block_notice_message for the new blocks. Ignore, similar to the ignore in blk_send_branch().
         peer_ilog( p2p_blk_log, this, "enqueue block sync, unable to fetch block ${num}, resetting peer request", ("num", num) );
         peer_requested.reset(); // unable to provide requested blocks
         block_sync_send_start = 0ns;
         block_sync_frame_bytes_sent = 0;
      } else {
         peer_ilog( p2p_blk_log, this, "enqueue peer sync, unable to fetch block ${num}, sending benign_other go away", ("num", num) );
         peer_requested.reset(); // unable to provide requested blocks
         block_sync_send_start = 0ns;
         block_sync_frame_bytes_sent = 0;
         no_retry = go_away_reason::benign_other;
         enqueue( go_away_message{ go_away_reason::benign_other } );
      }
      return true;
   }

   //------------------------------------------------------------------------

   // called from connection strand
   void connection::enqueue( const net_message& m ) {
      verify_strand_in_this_thread( strand, __func__, __LINE__ );
      go_away_reason close_after_send = go_away_reason::no_reason;
      if (std::holds_alternative<go_away_message>(m)) {
         close_after_send = std::get<go_away_message>(m).reason;
      }

      buffer_factory buff_factory;
      const auto& send_buffer = buff_factory.get_send_buffer( m );
      enqueue_buffer( to_msg_type_t(m.index()), std::nullopt, queued_buffer::queue_t::general, send_buffer, close_after_send );
   }

   // called from connection strand
   size_t connection::enqueue_block( const std::vector<char>& b, uint32_t block_num, queued_buffer::queue_t queue ) {
      peer_dlog( p2p_blk_log, this, "enqueue block ${num}", ("num", block_num) );
      verify_strand_in_this_thread( strand, __func__, __LINE__ );

      block_buffer_factory buff_factory;
      const auto& sb = buff_factory.get_send_buffer( b );
      latest_blk_time = std::chrono::steady_clock::now();
      enqueue_buffer( msg_type_t::signed_block, block_num, queue, sb, go_away_reason::no_reason);
      return sb->size();
   }

   // called from connection strand
   void connection::enqueue_buffer( msg_type_t net_msg,
                                    std::optional<block_num_type> block_num, // only valid for net_msg == signed_block variant which
                                    queued_buffer::queue_t queue,
                                    const send_buffer_type& send_buffer,
                                    go_away_reason close_after_send)
   {
      connection_ptr self = shared_from_this();
      queue_write(net_msg, block_num, queue, send_buffer,
            [conn{std::move(self)}, close_after_send, net_msg, block_num](boost::system::error_code ec, std::size_t s) {
                        if (ec) {
                           if (ec != boost::asio::error::operation_aborted && ec != boost::asio::error::connection_reset && conn->socket_is_open()) {
                              fc_elog(p2p_conn_log, "Connection - ${cid} - send failed with: ${e}", ("cid", conn->connection_id)("e", ec.message()));
                           }
                           return;
                        }
                        if (net_msg == msg_type_t::signed_block && block_num)
                           fc_dlog(p2p_conn_log, "Connection - ${cid} - done sending block ${bn}", ("cid", conn->connection_id)("bn", *block_num));
                        if (close_after_send != go_away_reason::no_reason) {
                           fc_ilog( p2p_conn_log, "sent a go away message: ${r}, closing connection ${cid}",
                                    ("r", reason_str(close_after_send))("cid", conn->connection_id) );
                           conn->close();
                           return;
                        }
                  });
   }

   // thread safe
   void connection::cancel_sync_wait() {
      fc::lock_guard g( sync_response_expected_timer_mtx );
      sync_response_expected_timer.cancel();
   }

   // thread safe
   void connection::sync_wait() {
      connection_ptr c(shared_from_this());
      fc::lock_guard g( sync_response_expected_timer_mtx );
      sync_response_expected_timer.expires_from_now( my_impl->resp_expected_period );
      my_impl->sync_master->sync_wait(c);
      sync_response_expected_timer.async_wait(
            boost::asio::bind_executor( c->strand, [c]( boost::system::error_code ec ) {
               my_impl->sync_master->sync_timeout(c, ec);
            } ) );
   }

   // called from connection strand
   void connection::request_sync_blocks(uint32_t start, uint32_t end) {
      sync_last_requested_block = end;
      sync_request_message srm = {start,end};
      enqueue( net_message(srm) );
      peer_dlog(p2p_blk_log, this, "calling sync_wait, sync_request_message ${s} - ${e}", ("s", start)("e", end));
      sync_wait();
   }

   //-----------------------------------------------------------
   void block_status_monitor::reset() {
      in_accepted_state_ = true;
      events_ = 0;
   }

   void block_status_monitor::rejected() {
      const auto now = fc::time_point::now();

      // in rejected state
      if(!in_accepted_state_) {
         const auto elapsed = now - window_start_;
         if( elapsed < window_size_ ) {
            return;
         }
         ++events_;
         window_start_ = now;
         return;
      }

      // switching to rejected state
      in_accepted_state_ = false;
      window_start_ = now;
      events_ = 0;
   }
   //-----------------------------------------------------------

    sync_manager::sync_manager( uint32_t span, uint32_t sync_peer_limit, uint32_t min_blocks_distance )
      :sync_known_fork_db_root_num( 0 )
      ,sync_last_requested_num( 0 )
      ,sync_next_expected_num( 1 )
      ,sync_source()
      ,sync_fetch_span( span )
      ,sync_peer_limit( sync_peer_limit )
      ,sync_state(in_sync)
      ,min_blocks_distance(min_blocks_distance)
   {
   }

   constexpr auto sync_manager::stage_str(stages s) {
    switch (s) {
    case in_sync : return "in sync";
    case lib_catchup: return "lib catchup";
    case head_catchup : return "head catchup";
    default : return "unkown";
    }
  }

   bool sync_manager::set_state(stages newstate) {
      if( sync_state == newstate ) {
         return false;
      }
      fc_ilog( p2p_blk_log, "old state ${os} becoming ${ns}", ("os", stage_str( sync_state ))( "ns", stage_str( newstate ) ) );
      sync_state = newstate;
      return true;
   }

   // called from c's connection strand
   void sync_manager::sync_reset_fork_db_root_num(const connection_ptr& c, bool closing) {
      fc::unique_lock g( sync_mtx );
      if( sync_state == in_sync ) {
         sync_source.reset();
      }
      if( !c ) return;
      if( !closing ) {
         if( c->peer_fork_db_root_num > sync_known_fork_db_root_num ) {
            sync_known_fork_db_root_num = c->peer_fork_db_root_num;
         }
      } else {
         // Closing connection, therefore its view of fork_db_root can no longer be considered as we will no longer be connected.
         // Determine current fork_db_root of remaining peers as our sync_known_fork_db_root_num.
         uint32_t highest_fork_db_root_num = 0;
         my_impl->connections.for_each_block_connection( [&highest_fork_db_root_num]( const auto& cc ) {
            fc::lock_guard g_conn( cc->conn_mtx );
            if( cc->current() && cc->last_handshake_recv.fork_db_root_num > highest_fork_db_root_num ) {
               highest_fork_db_root_num = cc->last_handshake_recv.fork_db_root_num;
            }
         } );
         sync_known_fork_db_root_num = highest_fork_db_root_num;

         // if closing the connection we are currently syncing from then request from a diff peer
         if( c == sync_source ) {
            // if starting to sync need to always start from fork_db_root as we might be on our own fork
            uint32_t fork_db_root_num = my_impl->get_fork_db_root_num();
            sync_last_requested_num = 0;
            sync_next_expected_num = std::max( fork_db_root_num + 1, sync_next_expected_num );
            sync_source.reset();
            request_next_chunk();
         }
      }
   }

   connection_ptr sync_manager::find_next_sync_node() REQUIRES(sync_mtx) {
      fc_dlog(p2p_blk_log, "Number connections ${s}, sync_next_expected_num: ${e}, sync_known_fork_db_root_num: ${l}",
              ("s", my_impl->connections.number_connections())("e", sync_next_expected_num)("l", sync_known_fork_db_root_num));
      deque<connection_ptr> conns;
      my_impl->connections.for_each_block_connection([sync_next_expected_num = sync_next_expected_num,
                                                      sync_known_froot_num = sync_known_fork_db_root_num,
                                                      sync_fetch_span = sync_fetch_span,
                                                      &conns](const auto& c) {
         if (c->should_sync_from(sync_next_expected_num, sync_known_froot_num, sync_fetch_span)) {
            conns.push_back(c);
         }
      });
      if (conns.size() > sync_peer_limit) {
         std::partial_sort(conns.begin(), conns.begin() + sync_peer_limit, conns.end(), [](const connection_ptr& lhs, const connection_ptr& rhs) {
            return lhs->get_peer_ping_time_ns() < rhs->get_peer_ping_time_ns();
         });
         conns.resize(sync_peer_limit);
      }

      fc_dlog(p2p_blk_log, "Valid sync peers ${s}, sync_ordinal ${so}", ("s", conns.size())("so", sync_ordinal.load()));

      if (conns.empty()) {
         return {};
      }
      if (conns.size() == 1) { // only one available
         ++sync_ordinal;
         fc_dlog(p2p_blk_log, "sync from ${c}", ("c", conns.front()->connection_id));
         conns.front()->sync_ordinal = sync_ordinal.load();
         return conns.front();
      }

      // keep track of which node was synced from last; round-robin among the current (sync_peer_limit) lowest latency peers
      ++sync_ordinal;
      // example: sync_ordinal is 6 after inc above then there may be connections with 3,4,5 (5 being the last synced from)
      // Choose from the lowest sync_ordinal of the sync_peer_limit of lowest latency, note 0 means not synced from yet
      size_t the_one = 0;
      uint32_t lowest_ordinal = std::numeric_limits<uint32_t>::max();
      for (size_t i = 0; i < conns.size() && lowest_ordinal != 0; ++i) {
         uint32_t sync_ord = conns[i]->sync_ordinal;
         fc_dlog(p2p_blk_log, "compare sync ords, conn: ${lcid}, ord: ${l} < ${r}, ping: ${p}us",
                 ("lcid", conns[i]->connection_id)("l", sync_ord)("r", lowest_ordinal)("p", conns[i]->get_peer_ping_time_ns()/1000));
         if (sync_ord < lowest_ordinal) {
            the_one = i;
            lowest_ordinal = sync_ord;
         }
      }
      fc_dlog(p2p_blk_log, "sync from ${c}", ("c", conns[the_one]->connection_id));
      conns[the_one]->sync_ordinal = sync_ordinal.load();
      return conns[the_one];
   }

   // call with g_sync locked, called from conn's connection strand
   void sync_manager::request_next_chunk( const connection_ptr& conn ) REQUIRES(sync_mtx) {
      auto chain_info = my_impl->get_chain_info();

      fc_dlog( p2p_blk_log, "sync_last_requested_num: ${r}, sync_next_expected_num: ${e}, sync_known_fork_db_root_num: ${k}, sync-fetch-span: ${s}, fhead: ${h}, froot: ${fr}",
               ("r", sync_last_requested_num)("e", sync_next_expected_num)("k", sync_known_fork_db_root_num)("s", sync_fetch_span)("h", chain_info.fork_db_head_num)("fr", chain_info.fork_db_root_num) );

      if (conn) {
         // p2p_high_latency_test.py test depends on this exact log statement.
         peer_ilog(p2p_blk_log, conn, "Catching up with chain, our last req is ${cc}, theirs is ${t}, next expected ${n}, fhead ${h}",
                   ("cc", sync_last_requested_num)("t", sync_known_fork_db_root_num)("n", sync_next_expected_num)("h", chain_info.fork_db_head_num));
      }

      /* ----------
       * next chunk provider selection criteria
       * a provider is supplied and able to be used, use it.
       * otherwise select the next available from the list, round-robin style.
       */
      connection_ptr new_sync_source = (conn && conn->current()) ? conn : find_next_sync_node();

      auto reset_on_failure = [&]() REQUIRES(sync_mtx) {
         sync_source.reset();
         sync_known_fork_db_root_num = chain_info.fork_db_root_num;
         sync_last_requested_num = 0;
         sync_next_expected_num = std::max( sync_known_fork_db_root_num + 1, sync_next_expected_num );
         // not in sync, but need to be out of lib_catchup for start_sync to work
         set_state( in_sync );
         send_handshakes();
      };

      // verify there is an available source
      if( !new_sync_source ) {
         fc_wlog( p2p_blk_log, "Unable to continue syncing at this time");
         reset_on_failure();
         return;
      }

      bool request_sent = false;
      if( sync_last_requested_num != sync_known_fork_db_root_num ) {
         uint32_t start = sync_next_expected_num;
         uint32_t end = start + sync_fetch_span - 1;
         if( end > sync_known_fork_db_root_num )
            end = sync_known_fork_db_root_num;
         if( end > 0 && end >= start ) {
            sync_last_requested_num = end;
            sync_source = new_sync_source;
            request_sent = true;
            sync_active_time = std::chrono::steady_clock::now();
            boost::asio::post(new_sync_source->strand, [new_sync_source, start, end, fork_db_head_num=chain_info.fork_db_head_num, fork_db_root_num=chain_info.fork_db_root_num]() {
               peer_ilog( p2p_blk_log, new_sync_source, "requesting range ${s} to ${e}, fhead ${h}, froot ${r}", ("s", start)("e", end)("h", fork_db_head_num)("r", fork_db_root_num) );
               new_sync_source->request_sync_blocks( start, end );
            } );
         }
      }
      if( !request_sent ) {
         fc_wlog(p2p_blk_log, "Unable to request range, sending handshakes to everyone");
         reset_on_failure();
      }
   }

   // static, thread safe
   void sync_manager::send_handshakes() {
      my_impl->connections.for_each_connection( []( const connection_ptr& ci ) {
         if( ci->connected() ) {
            ci->send_handshake();
         }
      } );
   }

   // static, thread safe
   void sync_manager::send_block_nack_resets() {
      my_impl->connections.for_each_block_connection( []( const connection_ptr& cp ) {
         if (cp->current()) {
            boost::asio::post(cp->strand, [cp]() {
               cp->send_block_nack({});
            });
         }
      } );
   }

   bool sync_manager::is_sync_required( uint32_t fork_db_head_block_num ) const REQUIRES(sync_mtx) {
      fc_dlog( p2p_blk_log, "last req = ${req}, last recv = ${recv} known = ${known} our fhead = ${h}",
               ("req", sync_last_requested_num)( "recv", sync_next_expected_num-1 )( "known", sync_known_fork_db_root_num )
               ("h", fork_db_head_block_num ) );

      return( sync_last_requested_num < sync_known_fork_db_root_num ||
              sync_next_expected_num < sync_last_requested_num );
   }

   // called from c's connection strand
   bool sync_manager::is_sync_request_ahead_allowed(block_num_type blk_num) const REQUIRES(sync_mtx) {
      if (blk_num >= sync_last_requested_num && sync_last_requested_num < sync_known_fork_db_root_num) {
         // do not allow to get too far ahead (sync_fetch_span) of chain head
         // use chain head instead of fork head so we do not get too far ahead of applied blocks
         uint32_t head_num = my_impl->get_chain_head_num();
         block_num_type num_blocks_not_applied = blk_num > head_num ? blk_num - head_num : 0;
         if (num_blocks_not_applied < sync_fetch_span) {
            fc_dlog(p2p_blk_log, "sync ahead allowed past sync-fetch-span ${sp}, block ${bn} head ${h}, fork_db size ${s}",
                    ("bn", blk_num)("sp", sync_fetch_span)("h", head_num)("s", my_impl->chain_plug->chain().fork_db_size()));
            return true;
         }

         controller& cc = my_impl->chain_plug->chain();
         if (cc.get_read_mode() == db_read_mode::IRREVERSIBLE) {
            auto fork_db_head = cc.fork_db_head();
            auto calculated_lib = fork_db_head.irreversible_blocknum();
            auto num_blocks_that_can_be_applied = calculated_lib > head_num ? calculated_lib - head_num : 0;
            // add blocks that can potentially be applied as they are not in the fork_db yet
            num_blocks_that_can_be_applied += blk_num > fork_db_head.block_num() ? blk_num - fork_db_head.block_num() : 0;
            if (num_blocks_that_can_be_applied < sync_fetch_span) {
               if (head_num )
                  fc_ilog(p2p_blk_log, "sync ahead allowed past sync-fetch-span ${sp}, block ${bn} for paused lib ${l}, head ${h}, fork_db size ${s}",
                          ("bn", blk_num)("sp", sync_fetch_span)("l", calculated_lib)("h", head_num)("s", cc.fork_db_size()));
               return true;
            }
         }

         fc_dlog(p2p_blk_log, "sync ahead not allowed. block ${bn}, head ${h}, fhead ${fh}, fhead->lib ${fl}, sync-fetch-span ${sp}, fork_db size ${s}",
                 ("bn", blk_num)("h", head_num)("fh", cc.fork_db_head().block_num())("fl", cc.fork_db_head().irreversible_blocknum())
                 ("sp", sync_fetch_span)("s", cc.fork_db_size()));
      }

      fc_dlog(p2p_blk_log, "sync ahead not allowed. block ${bn}, sync_last_requested_num ${lrn}, sync-fetch-span ${s}",
              ("bn", blk_num)("lrn", sync_last_requested_num)("s", sync_fetch_span));
      return false;
   }

   // called from c's connection strand
   void sync_manager::start_sync(const connection_ptr& c, uint32_t target) {
      fc::unique_lock g_sync( sync_mtx );
      if( target > sync_known_fork_db_root_num) {
         sync_known_fork_db_root_num = target;
      }

      auto chain_info = my_impl->get_chain_info();
      if( !is_sync_required( chain_info.fork_db_head_num ) || target <= chain_info.fork_db_root_num ) {
         peer_dlog( p2p_blk_log, c, "We are already caught up, my irr = ${b}, fhead = ${h}, target = ${t}",
                  ("b", chain_info.fork_db_root_num)( "h", chain_info.fork_db_head_num )( "t", target ) );
         c->send_handshake(); // let peer know it is not syncing from us
         return;
      }

      stages current_sync_state = sync_state;
      if( current_sync_state != lib_catchup || !sync_recently_active()) {
         peer_dlog(p2p_blk_log, c, "requesting next chuck, set to lib_catchup and request_next_chunk, sync_state ${s}, sync_next_expected_num ${nen}",
                   ("s", stage_str(current_sync_state))("nen", sync_next_expected_num));
         set_state( lib_catchup );
         sync_last_requested_num = 0;
         sync_next_expected_num = chain_info.fork_db_root_num + 1;
         request_next_chunk( c );
      } else if (sync_last_requested_num > 0 && is_sync_request_ahead_allowed(sync_next_expected_num-1)) {
         request_next_chunk();
      } else {
         peer_dlog(p2p_blk_log, c, "already syncing, start sync ignored");
      }

   }

   // thread safe
   bool sync_manager::sync_recently_active() const {
      auto time_since_active = std::chrono::steady_clock::now() - sync_active_time.load();
      bool active = time_since_active < my_impl->resp_expected_period;
      if (!active) {
         fc_dlog(p2p_blk_log, "sync not recently active, time since last sync block ${t}ms",
                 ("t", std::chrono::duration_cast<std::chrono::milliseconds>(time_since_active).count()));
      }
      return active;
   }

   // called from connection strand
   void sync_manager::sync_wait(const connection_ptr& c) {
      ++sync_timers_active;
      peer_dlog(p2p_blk_log, c, "sync wait, active_timers ${t}", ("t", sync_timers_active.load()));
   }

   // called from connection strand
   void sync_manager::sync_timeout(const connection_ptr& c, const boost::system::error_code& ec) {
      if( !ec ) {
         peer_dlog(p2p_blk_log, c, "sync timed out");
         sync_reassign_fetch( c );
         c->close(true);
      } else if( ec != boost::asio::error::operation_aborted ) { // don't log on operation_aborted, called on destroy
         peer_elog( p2p_blk_log, c, "setting timer for sync request got error ${ec}", ("ec", ec.message()) );
      }
      --sync_timers_active;
      peer_dlog(p2p_blk_log, c, "sync active_timers ${t}", ("t", sync_timers_active.load()));
   }

   // called from connection strand
   void sync_manager::sync_reassign_fetch(const connection_ptr& c) {
      fc::unique_lock g( sync_mtx );
      if( c == sync_source ) {
         peer_ilog(p2p_blk_log, c, "reassign_fetch, our last req is ${cc}, next expected is ${ne}",
                   ("cc", sync_last_requested_num)("ne", sync_next_expected_num));
         c->cancel_sync();
         auto fork_db_root_num = my_impl->get_fork_db_root_num();
         sync_last_requested_num = 0;
         sync_next_expected_num = std::max(sync_next_expected_num, fork_db_root_num + 1);
         sync_source.reset();
         request_next_chunk();
      }
   }

   inline block_id_type make_block_id( uint32_t block_num ) {
      chain::block_id_type block_id;
      block_id._hash[0] = fc::endian_reverse_u32(block_num);
      return block_id;
   }

   // called from c's connection strand
   void sync_manager::recv_handshake( const connection_ptr& c, const handshake_message& msg, uint32_t nblk_combined_latency ) {

      if (!c->is_blocks_connection())
         return;

      auto chain_info = my_impl->get_chain_info();

      sync_reset_fork_db_root_num(c, false);

      //--------------------------------
      // sync need checks; (froot == fork database root)
      //
      // 0. my head block id == peer head id means we are all caught up block wise
      // 1. my head block num < peer froot then start sync locally by sending handshake
      // 2. my froot > peer head num + nblk_combined_latency then send last_irr_catch_up notice if not the first generation
      //
      // 3  my head block num + nblk_combined_latency < peer head block num then update sync state and send a catchup request
      // 4  my head block num >= peer block num + nblk_combined_latency send a notice catchup if this is not the first generation
      //    4.1 if peer appears to be on a different fork ( our_id_for( msg.head_num ) != msg.head_id )
      //        then request peer's blocks
      //
      //-----------------------------

      if (chain_info.fork_db_head_id == msg.fork_db_head_id) {
         peer_dlog( p2p_blk_log, c, "handshake msg.froot ${fr}, msg.fhead ${fh}, msg.id ${id}.. sync 0, fhead ${h}, froot ${r}",
                    ("fr", msg.fork_db_root_num)("fh", msg.fork_db_head_num)("id", msg.fork_db_head_id.str().substr(8,16))
                    ("h", chain_info.fork_db_head_num)("r", chain_info.fork_db_root_num) );
         c->peer_syncing_from_us = false;
         return;
      }
      if (chain_info.fork_db_head_num < msg.fork_db_root_num) {
         peer_dlog( p2p_blk_log, c, "handshake msg.froot ${fr}, msg.fhead ${fh}, msg.id ${id}.. sync 1, fhead ${h}, froot ${r}",
                    ("fr", msg.fork_db_root_num)("fh", msg.fork_db_head_num)("id", msg.fork_db_head_id.str().substr(8,16))
                    ("h", chain_info.fork_db_head_num)("r", chain_info.fork_db_root_num) );
         c->peer_syncing_from_us = false;
         if (c->sent_handshake_count > 0) {
            c->send_handshake();
         }
         return;
      }
      if (chain_info.fork_db_root_num > msg.fork_db_head_num + nblk_combined_latency + min_blocks_distance) {
         peer_dlog( p2p_blk_log, c, "handshake msg.froot ${fr}, msg.fhead ${fh}, msg.id ${id}.. sync 2, fhead ${h}, froot ${r}",
                    ("fr", msg.fork_db_root_num)("fh", msg.fork_db_head_num)("id", msg.fork_db_head_id.str().substr(8,16))
                    ("h", chain_info.fork_db_head_num)("r", chain_info.fork_db_root_num) );
         if (msg.generation > 1 || c->protocol_version > proto_version_t::base) {
            controller& cc = my_impl->chain_plug->chain();
            notice_message note;
            note.known_trx.pending = chain_info.fork_db_root_num;
            note.known_trx.mode = last_irr_catch_up;
            note.known_blocks.mode = last_irr_catch_up;
            note.known_blocks.pending = chain_info.fork_db_head_num;
            note.known_blocks.ids.push_back(chain_info.fork_db_head_id);
            if (c->protocol_version >= proto_version_t::block_range) {
               // begin, more efficient to encode a block num instead of retrieving actual block id
               note.known_blocks.ids.push_back(make_block_id(cc.earliest_available_block_num()));
            }
            c->enqueue( note );
            c->peer_syncing_from_us = true;
         }
         return;
      }

      if (chain_info.fork_db_head_num + nblk_combined_latency < msg.fork_db_head_num ) {
         peer_dlog( p2p_blk_log, c, "handshake msg.froot ${fr}, msg.fhead ${fh}, msg.id ${id}.. sync 3, fhead ${h}, froot ${r}",
                    ("fr", msg.fork_db_root_num)("fh", msg.fork_db_head_num)("id", msg.fork_db_head_id.str().substr(8,16))
                    ("h", chain_info.fork_db_head_num)("r", chain_info.fork_db_root_num) );
         c->peer_syncing_from_us = false;
         verify_catchup(c, msg.fork_db_head_num, msg.fork_db_head_id);
         return;
      } else if(chain_info.fork_db_head_num >= msg.fork_db_head_num + nblk_combined_latency) {
         peer_dlog( p2p_blk_log, c, "handshake msg.froot ${fr}, msg.fhead ${fh}, msg.id ${id}.. sync 4, fhead ${h}, froot ${r}",
                    ("fr", msg.fork_db_root_num)("fh", msg.fork_db_head_num)("id", msg.fork_db_head_id.str().substr(8,16))
                    ("h", chain_info.fork_db_head_num)("r", chain_info.fork_db_root_num) );
         if (msg.generation > 1 ||  c->protocol_version > proto_version_t::base) {
            controller& cc = my_impl->chain_plug->chain();
            notice_message note;
            note.known_trx.mode = none;
            note.known_blocks.mode = catch_up;
            note.known_blocks.pending = chain_info.fork_db_head_num;
            note.known_blocks.ids.push_back(chain_info.fork_db_head_id);
            if (c->protocol_version >= proto_version_t::block_range) {
               // begin, more efficient to encode a block num instead of retrieving actual block id
               note.known_blocks.ids.push_back(make_block_id(cc.earliest_available_block_num()));
            }
            c->enqueue( note );
         }
         c->peer_syncing_from_us = false;
         try {
            auto [on_fork, unknown_block] = block_on_fork(msg.fork_db_head_id); // thread safe
            if (on_fork) { // possible for fork_db_root to move and fork_db_head_num not be found if running with no block-log
               peer_dlog(p2p_msg_log, c, "Sending catch_up request_message sync 4, msg.fhead ${mfh} on fork", ("mfh", msg.fork_db_head_id));
               request_message req;
               req.req_blocks.mode = catch_up;
               req.req_trx.mode = none;
               c->enqueue( req );
            }
         } catch( ... ) {}
         return;
      } else {
         c->peer_syncing_from_us = false;
         peer_dlog( p2p_blk_log, c, "Block discrepancy is within network latency range.");
      }
   }

   // called from c's connection strand
   bool sync_manager::verify_catchup(const connection_ptr& c, uint32_t num, const block_id_type& id) {
      request_message req;
      req.req_blocks.mode = catch_up;
      auto is_fork_db_head_greater = [num, &id, &req]( const auto& cc ) {
         fc::lock_guard g_conn( cc->conn_mtx );
         if( cc->conn_fork_db_head_num > num || cc->conn_fork_db_head == id ) {
            req.req_blocks.mode = none;
            return true;
         }
         return false;
      };
      if (my_impl->connections.any_of_block_connections(is_fork_db_head_greater)) {
         req.req_blocks.mode = none;
      }
      if( req.req_blocks.mode == catch_up ) {
         {
            fc::lock_guard g( sync_mtx );
            peer_ilog( p2p_blk_log, c, "catch_up while in ${s}, fhead = ${hn} "
                          "target froot = ${fr} next_expected = ${ne}, id ${id}...",
                     ("s", stage_str( sync_state ))("hn", num)("fr", sync_known_fork_db_root_num)
                     ("ne", sync_next_expected_num)("id", id.str().substr( 8, 16 )) );
         }
         auto chain_info = my_impl->get_chain_info();
         if( sync_state == lib_catchup || num < chain_info.fork_db_root_num ) {
            c->send_handshake();
            return false;
         }
         set_state( head_catchup );
         {
            fc::lock_guard g_conn( c->conn_mtx );
            c->conn_fork_db_head = id;
            c->conn_fork_db_head_num = num;
         }

         req.req_blocks.ids.emplace_back( chain_info.fork_db_head_id );
      } else {
         peer_ilog( p2p_blk_log, c, "none notice while in ${s}, fhead = ${hn}, id ${id}...",
                  ("s", stage_str( sync_state ))("hn", num)("id", id.str().substr(8,16)) );
         fc::lock_guard g_conn( c->conn_mtx );
         c->conn_fork_db_head = block_id_type();
         c->conn_fork_db_head_num = 0;
      }
      req.req_trx.mode = none;
      c->enqueue( req );
      return true;
   }

   // called from c's connection strand
   void sync_manager::sync_recv_notice( const connection_ptr& c, const notice_message& msg) {
      peer_dlog( p2p_blk_log, c, "sync_manager got ${m} block notice", ("m", modes_str( msg.known_blocks.mode )) );
      EOS_ASSERT( msg.known_blocks.mode == catch_up || msg.known_blocks.mode == last_irr_catch_up, plugin_exception,
                  "sync_recv_notice only called on catch_up" );
      if (msg.known_blocks.mode == catch_up) {
         if (msg.known_blocks.ids.empty()) {
            peer_wlog( p2p_blk_log, c, "got a catch up with ids size = 0" );
         } else {
            const block_id_type& id = msg.known_blocks.ids.front();
            peer_ilog( p2p_blk_log, c, "notice_message, pending ${p}, blk_num ${n}, id ${id}...",
                     ("p", msg.known_blocks.pending)("n", block_header::num_from_id(id))("id",id.str().substr(8,16)) );
            if( !my_impl->dispatcher.have_block( id ) ) {
               verify_catchup( c, msg.known_blocks.pending, id );
            } else {
               // we already have the block, so update peer with our view of the world
               peer_dlog(p2p_blk_log, c, "Already have block, sending handshake");
               c->send_handshake();
            }
         }
      } else if (msg.known_blocks.mode == last_irr_catch_up) {
         {
            c->peer_fork_db_root_num = msg.known_trx.pending;
            fc::lock_guard g_conn( c->conn_mtx );
            c->last_handshake_recv.fork_db_root_num = msg.known_trx.pending;
         }
         sync_reset_fork_db_root_num(c, false);
         start_sync(c, msg.known_trx.pending);
      }
   }

   // called from connection strand
   void sync_manager::rejected_block( const connection_ptr& c, uint32_t blk_num, closing_mode mode ) {
      c->block_status_monitor_.rejected();
      {
         // reset sync on rejected block
         fc::lock_guard g( sync_mtx );
         if (sync_last_requested_num != 0 && blk_num <= sync_next_expected_num-1) { // no need to reset if we already reset and are syncing again
            sync_last_requested_num = 0;
            sync_next_expected_num = my_impl->get_fork_db_root_num() + 1;
         }
      }
      if( mode == closing_mode::immediately || c->block_status_monitor_.max_events_violated()) {
         peer_wlog(p2p_blk_log, c, "block ${bn} not accepted, closing connection ${d}",
                   ("d", mode == closing_mode::immediately ? "immediately" : "max violations reached")("bn", blk_num));
         c->close(mode != closing_mode::immediately);
      } else {
         peer_dlog(p2p_blk_log, c, "rejected block ${bn}, sending handshake", ("bn", blk_num));
         c->send_handshake();
      }
   }

   // called from c's connection strand if c != nullptr,
   // otherwise c == nullptr which implies blk_applied == false and called from dispatcher strand
   void sync_manager::sync_recv_block(const connection_ptr& c, const block_id_type& blk_id, uint32_t blk_num,
                                      const fc::microseconds& blk_latency) {
      // no connection means called when block is applied
      const bool blk_applied = !c;

      if (c) {
         peer_dlog(p2p_blk_log, c, "got block ${bn}:${id}.. latency ${l}ms",
                   ("bn", blk_num)("id", blk_id.str().substr(8,16))("l", blk_latency.count()/1000) );
      } else {
         fc_dlog(p2p_blk_log, "applied block ${bn}:${id}.. latency ${l}ms",
                 ("bn", blk_num)("id", blk_id.str().substr(8,16))("l", blk_latency.count()/1000) );
      }
      if( app().is_quiting() ) {
         if (c)
            c->close( false, true );
         return;
      }
      if (c) {
         c->latest_blk_time = sync_active_time = std::chrono::steady_clock::now(); // reset when we receive a block
         if (blk_latency.count() < config::block_interval_us && c->peer_syncing_from_us) {
            // a peer will not send us a recent block unless it is synced
            c->peer_syncing_from_us = false;
         }
      } else {
         // reset when we apply a block as well so we don't time out just because applying blocks takes too long
         sync_active_time = std::chrono::steady_clock::now();
      }
      stages state = sync_state;
      fc_dlog(p2p_blk_log, "sync_state ${s}", ("s", stage_str(state)));
      if( state == head_catchup ) {
         fc_dlog(p2p_blk_log, "sync_manager in head_catchup state");
         fc::unique_lock g_sync( sync_mtx );
         sync_source.reset();
         g_sync.unlock();

         block_id_type null_id;
         bool set_state_to_head_catchup = false;
         my_impl->connections.for_each_block_connection( [&null_id, blk_num, &blk_id, &c, &set_state_to_head_catchup]( const auto& cp ) {
            fc::unique_lock g_cp_conn( cp->conn_mtx );
            uint32_t fork_db_head_num = cp->conn_fork_db_head_num;
            block_id_type fork_db_head_id = cp->conn_fork_db_head;
            g_cp_conn.unlock();
            if( fork_db_head_id == null_id ) {
               // continue
            } else if( fork_db_head_num < blk_num || fork_db_head_id == blk_id ) {
               if (c) {
                  fc::lock_guard g_conn( c->conn_mtx );
                  c->conn_fork_db_head = null_id;
                  c->conn_fork_db_head_num = 0;
               }
            } else {
               set_state_to_head_catchup = true;
            }
         } );

         if( set_state_to_head_catchup ) {
            if( set_state( head_catchup ) ) {
               fc_dlog(p2p_blk_log, "Switching to head_catchup, sending handshakes");
               send_handshakes();
            }
         } else {
            set_state( in_sync );
            fc_dlog(p2p_blk_log, "Switching to not lib_catchup, will send handshakes when caught up");
            send_handshakes_when_synced = true;
         }
      } else if( state == lib_catchup ) {
         fc::unique_lock g_sync( sync_mtx );
         if( blk_applied && blk_num >= sync_known_fork_db_root_num ) {
            fc_dlog(p2p_blk_log, "All caught up ${b} with last known froot ${r} resending handshake",
                    ("b", blk_num)("r", sync_known_fork_db_root_num));
            set_state( head_catchup );
            g_sync.unlock();
            send_handshakes();
         } else {
            if (!blk_applied) {
               if (blk_num >= c->sync_last_requested_block) {
                  peer_dlog(p2p_blk_log, c, "calling cancel_sync_wait, block ${b}, sync_last_requested_block ${lrb}",
                            ("b", blk_num)("lrb", c->sync_last_requested_block));
                  sync_source.reset();
                  c->cancel_sync_wait();
               } else {
                  peer_dlog(p2p_blk_log, c, "calling sync_wait, block ${b}", ("b", blk_num));
                  c->sync_wait();
               }

               if (sync_last_requested_num == 0) { // block was rejected
                  sync_next_expected_num = my_impl->get_fork_db_root_num() + 1;
                  peer_dlog(p2p_blk_log, c, "Reset sync_next_expected_num to ${n}", ("n", sync_next_expected_num));
               } else {
                  if (blk_num == sync_next_expected_num) {
                     ++sync_next_expected_num;
                  }
               }
               if (blk_num >= sync_known_fork_db_root_num) {
                  peer_dlog(p2p_blk_log, c, "received non-applied block ${bn} >= ${kn}, will send handshakes when caught up",
                            ("bn", blk_num)("kn", sync_known_fork_db_root_num));
                  send_handshakes_when_synced = true;
               } else {
                  if (is_sync_request_ahead_allowed(blk_num)) {
                     // block was not applied, possibly because we already have the block
                     fc_dlog(p2p_blk_log, "Requesting ${fs} blocks ahead, head: ${h} fhead ${fh} blk_num: ${bn} sync_next_expected_num ${nen} "
                                          "sync_last_requested_num: ${lrn}, sync_last_requested_block: ${lrb}",
                             ("fs", sync_fetch_span)("h", my_impl->get_chain_head_num())("fh", my_impl->get_fork_db_head_num())
                             ("bn", blk_num)("nen", sync_next_expected_num)
                             ("lrn", sync_last_requested_num)("lrb", c->sync_last_requested_block));
                     request_next_chunk();
                  }
               }
            } else { // blk_applied
               controller& cc = my_impl->chain_plug->chain();
               if (cc.get_read_mode() == db_read_mode::IRREVERSIBLE) {
                  // When in irreversible mode, we do not get an accepted_block signal until the block is irreversible.
                  // Use last received number instead so when end of range is reached we check the IRREVERSIBLE conditions below.
                  blk_num = sync_next_expected_num-1;
               }
               if (is_sync_request_ahead_allowed(blk_num)) {
                  fc_dlog(p2p_blk_log, "Requesting blocks, head: ${h} fhead ${fh} blk_num: ${bn} sync_next_expected_num ${nen} "
                                       "sync_last_requested_num: ${lrn}",
                          ("h", my_impl->get_chain_head_num())("fh", my_impl->get_fork_db_head_num())
                          ("bn", blk_num)("nen", sync_next_expected_num)("lrn", sync_last_requested_num));
                  request_next_chunk();
               }
            }
         }
      } else { // not lib_catchup
         if (blk_applied) {
            send_handshakes_if_synced(blk_latency);
         }
      }
   }

   // thread safe, called when block received
   void sync_manager::send_handshakes_if_synced(const fc::microseconds& blk_latency) {
      sync_active_time = std::chrono::steady_clock::now(); // reset when we receive a block
      if (blk_latency.count() < config::block_interval_us && send_handshakes_when_synced) {
         fc_dlog(p2p_blk_log, "Block latency within block interval, synced, sending handshakes");
         send_handshakes();
         send_handshakes_when_synced = false;
      }
   }

   //------------------------------------------------------------------------
   // thread safe

   bool dispatch_manager::add_peer_block( const block_id_type& blkid, connection_id_t connection_id) {
      block_num_type block_num = block_header::num_from_id(blkid);
      fc::lock_guard g( blk_state_mtx );
      auto bptr = blk_state.get<by_connection_id>().find( std::forward_as_tuple(block_num, blkid, connection_id) );
      bool added = (bptr == blk_state.end());
      if( added ) {
         blk_state.insert( {blkid, connection_id} );
      }
      return added;
   }

   bool dispatch_manager::peer_has_block( const block_id_type& blkid, connection_id_t connection_id ) const {
      block_num_type block_num = block_header::num_from_id(blkid);
      fc::lock_guard g(blk_state_mtx);
      const auto blk_itr = blk_state.get<by_connection_id>().find( std::forward_as_tuple(block_num, blkid, connection_id) );
      return blk_itr != blk_state.end();
   }

   bool dispatch_manager::have_block( const block_id_type& blkid ) const {
      block_num_type block_num = block_header::num_from_id(blkid);
      fc::lock_guard g(blk_state_mtx);
      const auto& index = blk_state.get<by_connection_id>();
      auto blk_itr = index.find( std::forward_as_tuple(block_num, blkid) );
      return blk_itr != index.end();
   }

   void dispatch_manager::rm_block( const block_id_type& blkid ) {
      block_num_type block_num = block_header::num_from_id(blkid);
      fc_dlog( p2p_blk_log, "rm_block ${n}, id: ${id}", ("n", block_num)("id", blkid));
      fc::lock_guard g(blk_state_mtx);
      auto& index = blk_state.get<by_connection_id>();
      auto p = index.equal_range( std::forward_as_tuple(block_num, blkid) );
      index.erase(p.first, p.second);
   }

   dispatch_manager::add_peer_txn_info dispatch_manager::add_peer_txn( const transaction_id_type& id, const time_point_sec& trx_expires, connection& c )
   {
      fc::lock_guard g( local_txns_mtx );

      auto& id_idx = local_txns.get<by_id>();
      bool already_have_trx = false;
      if (auto tptr = id_idx.find( id ); tptr != id_idx.end()) {
         if (tptr->connection_ids.insert(c.connection_id).second)
            c.trx_entries_size += connection::trx_conn_entry_size;
         already_have_trx = tptr->have_trx;
         if (!already_have_trx) {
            time_point_sec expires{fc::time_point::now() + my_impl->p2p_dedup_cache_expire_time_us};
            expires = std::min( trx_expires, expires );
            local_txns.modify(tptr, [&](auto& v) {
               v.expires = expires;
               v.have_trx = true;
            });
         }
      } else {
         // expire at either transaction expiration or configured max expire time whichever is less
         time_point_sec expires{fc::time_point::now() + my_impl->p2p_dedup_cache_expire_time_us};
         expires = std::min( trx_expires, expires );
         local_txns.insert( node_transaction_state{
            .id = id,
            .expires = expires,
            .connection_ids = {c.connection_id},
            .have_trx = true } );
         c.trx_entries_size += connection::trx_full_entry_size;
      }

      if (c.trx_entries_size > def_max_trx_entries_per_conn_size) {
         auto now = fc::time_point::now();
         if (now - c.trx_entries_reset > my_impl->p2p_dedup_cache_expire_time_us) {
            c.trx_entries_size = 0;
            c.trx_entries_reset = now;
         }
      }
      return {c.trx_entries_size, already_have_trx};
   }

   size_t dispatch_manager::add_peer_txn_notice( const transaction_id_type& id, connection& c )
   {
      fc::lock_guard g( local_txns_mtx );

      auto& id_idx = local_txns.get<by_id>();
      if (auto tptr = id_idx.find( id ); tptr != id_idx.end()) {
         if (tptr->connection_ids.insert(c.connection_id).second)
            c.trx_entries_size += connection::trx_conn_entry_size;
      } else {
         time_point_sec expires{fc::time_point::now() + my_impl->p2p_dedup_cache_expire_time_us};
         local_txns.insert( node_transaction_state{
            .id = id,
            .expires = expires,
            .connection_ids = {c.connection_id},
            .have_trx = false } );
         c.trx_entries_size += connection::trx_full_entry_size;
      }

      if (c.trx_entries_size > def_max_trx_entries_per_conn_size) {
         auto now = fc::time_point::now();
         if (now - c.trx_entries_reset > my_impl->p2p_dedup_cache_expire_time_us) {
            c.trx_entries_size = 0;
            c.trx_entries_reset = now;
         }
      }
      return c.trx_entries_size;
   }

   connection_id_set dispatch_manager::peer_connections(const transaction_id_type& id) const {
      fc::lock_guard g( local_txns_mtx );
      auto& id_idx = local_txns.get<by_id>();
      if (auto tptr = id_idx.find(id); tptr != id_idx.end()) {
         return tptr->connection_ids;
      }
      return {};
   }

   void dispatch_manager::expire_txns() {
      fc::time_point now = time_point::now();

      size_t start_size = 0, end_size = 0;
      {
         fc::lock_guard g( local_txns_mtx );
         start_size = local_txns.size();
         auto& old = local_txns.get<by_expiry>();
         auto ex_lo = old.lower_bound( fc::time_point_sec( 0 ) );
         auto ex_up = old.upper_bound( fc::time_point_sec{now - def_allowed_clock_skew} ); // allow for some clock-skew
         old.erase( ex_lo, ex_up );
         end_size = local_txns.size();
      }

      fc_dlog( p2p_trx_log, "expire_local_txns size ${s} removed ${r} in ${t}us", ("s", start_size)("r", start_size - end_size)("t", fc::time_point::now() - now) );
   }

   void dispatch_manager::expire_blocks( uint32_t fork_db_root_num ) {
      fc::lock_guard g( blk_state_mtx );
      auto& stale_blk = blk_state.get<by_connection_id>();
      stale_blk.erase( stale_blk.lower_bound( 1 ), stale_blk.upper_bound( fork_db_root_num ) );
   }

   // thread safe
   void dispatch_manager::bcast_block(const signed_block_ptr& b, const block_id_type& id) {
      fc_dlog( p2p_blk_log, "bcast block ${b}:${id}", ("b", b->block_num())("id", id.str().substr(8,16)) );

      if(my_impl->sync_master->syncing_from_peer() ) return;

      block_buffer_factory buff_factory;
      buffer_factory block_notice_buff_factory;
      const auto bnum = b->block_num();
      my_impl->connections.for_each_block_connection( [this, &id, &bnum, &b, &buff_factory, &block_notice_buff_factory]( auto& cp ) {
         fc_dlog( p2p_blk_log, "socket_is_open ${s}, state ${c}, syncing ${ss}, connection - ${cid}",
                  ("s", cp->socket_is_open())("c", connection::state_str(cp->state()))("ss", cp->peer_syncing_from_us.load())("cid", cp->connection_id) );
         if( !cp->current() ) return;

         if( !add_peer_block( id, cp->connection_id ) ) {
            fc_dlog( p2p_blk_log, "not bcast block ${b} to connection - ${cid}", ("b", bnum)("cid", cp->connection_id) );
            return;
         }

         if (cp->protocol_version >= proto_version_t::block_nack && !my_impl->p2p_disable_block_nack) {
            if (cp->consecutive_blocks_nacks > connection::consecutive_block_nacks_threshold) {
               // only send block_notice if we didn't produce the block, otherwise broadcast the block below
               if (!my_impl->producer_plug->producer_accounts().contains(b->producer)) {
                  const auto& send_buffer = block_notice_buff_factory.get_send_buffer( block_notice_message{b->previous, id} );
                  boost::asio::post(cp->strand, [cp, send_buffer, bnum]() {
                     cp->latest_blk_time = std::chrono::steady_clock::now();
                     peer_dlog( p2p_blk_log, cp, "bcast block_notice ${b}", ("b", bnum) );
                     cp->enqueue_buffer( msg_type_t::block_notice_message, std::nullopt, queued_buffer::queue_t::general, send_buffer, go_away_reason::no_reason );
                  });
                  return;
               }
            }
         }

         const send_buffer_type& sb = buff_factory.get_send_buffer( b );

         boost::asio::post(cp->strand, [cp, bnum, sb]() {
            cp->latest_blk_time = std::chrono::steady_clock::now();
            bool has_block = cp->peer_fork_db_root_num >= bnum;
            if( !has_block ) {
               peer_dlog( p2p_blk_log, cp, "bcast block ${b}", ("b", bnum) );
               cp->enqueue_buffer( msg_type_t::signed_block, bnum, queued_buffer::queue_t::general, sb, go_away_reason::no_reason );
            }
         });
      } );
   }

   void dispatch_manager::bcast_vote_msg( connection_id_t exclude_peer, const send_buffer_type& msg ) {
      my_impl->connections.for_each_block_connection( [exclude_peer, msg]( auto& cp ) {
         if( !cp->current() ) return true;
         if( cp->connection_id == exclude_peer ) return true;
         if (cp->protocol_version < proto_version_t::savanna) return true;
         boost::asio::post(cp->strand, [cp, msg]() {
            peer_dlog(vote_logger, cp, "sending vote msg");
            cp->enqueue_buffer( msg_type_t::vote_message, std::nullopt, queued_buffer::queue_t::general, msg, go_away_reason::no_reason );
         });
         return true;
      } );
   }

   // called from any thread
   void dispatch_manager::bcast_transaction(const packed_transaction_ptr& trx) {
      trx_buffer_factory buff_factory;
      std::optional<connection_id_set> trx_connections;
      my_impl->connections.for_each_connection( [&]( const connection_ptr& cp ) {
         if( !cp->is_transactions_connection() || !cp->current() ) {
            return;
         }
         if (!trx_connections)
            trx_connections = peer_connections(trx->id());
         if( trx_connections->contains(cp->connection_id) ) {
            return;
         }

         const send_buffer_type& sb = buff_factory.get_send_buffer( trx );
         fc_dlog( p2p_trx_log, "sending trx: ${id}, to connection - ${cid}, size ${s}", ("id", trx->id())("cid", cp->connection_id)("s", sb->size()) );
         boost::asio::post(cp->strand, [cp, sb]() {
            cp->enqueue_buffer( msg_type_t::packed_transaction, std::nullopt, queued_buffer::queue_t::general, sb, go_away_reason::no_reason );
         } );
      } );
   }

   // called from any thread
   void dispatch_manager::bcast_transaction_notify(const packed_transaction_ptr& trx) {
      trx_buffer_factory buff_factory;
      my_impl->connections.for_each_connection( [&]( const connection_ptr& cp ) {
         if( cp->protocol_version < proto_version_t::trx_notice || !cp->is_transactions_connection() || !cp->current() ) {
            return;
         }

         const send_buffer_type& sb = buff_factory.get_notice_send_buffer( trx );
         fc_dlog( p2p_trx_log, "sending trx notice: ${id}, to connection - ${cid}", ("id", trx->id())("cid", cp->connection_id) );
         boost::asio::post(cp->strand, [cp, sb]() {
            cp->enqueue_buffer( msg_type_t::transaction_notice_message, std::nullopt, queued_buffer::queue_t::general, sb, go_away_reason::no_reason );
         } );
      } );
   }

   // called from any thread
   void dispatch_manager::rejected_transaction(const packed_transaction_ptr& trx) {
      fc_dlog( p2p_trx_log, "not sending rejected transaction ${tid}", ("tid", trx->id()) );
      // keep rejected transaction around for awhile so we don't broadcast it, don't remove from local_txns
   }

   // called from c's connection strand
   void dispatch_manager::recv_notice(const connection_ptr& c, const notice_message& msg, bool generated) {
      if (msg.known_trx.mode == normal) {
      } else if (msg.known_trx.mode != none) {
         peer_wlog( p2p_msg_log, c, "passed a notice_message with something other than a normal on none known_trx" );
         return;
      }
      if (msg.known_blocks.mode == normal) {
         return;
      } else if (msg.known_blocks.mode != none) {
         peer_wlog( p2p_msg_log, c, "passed a notice_message with something other than a normal on none known_blocks" );
         return;
      }
   }

   //------------------------------------------------------------------------

   // called from connection strand
   void connection::connect( const tcp::resolver::results_type& endpoints ) {
      set_state(connection_state::connecting);
      pending_message_buffer.reset();
      buffer_queue.reset();
      boost::asio::async_connect( *socket, endpoints,
         boost::asio::bind_executor( strand,
               [c = shared_from_this(), socket=socket]( const boost::system::error_code& err, const tcp::endpoint& endpoint ) {
            if( !err && socket->is_open() && socket == c->socket ) {
               c->update_endpoints(endpoint);
               if( c->start_session() ) {
                  c->send_handshake();
                  c->send_time();
               }
            } else {
               fc_ilog( p2p_conn_log, "connection failed to ${a}, ${error}", ("a", c->peer_address())( "error", err.message()));
               c->close( false );
               if (my_impl->increment_failed_p2p_connections) {
                  my_impl->increment_failed_p2p_connections();
               }
            }
      } ) );
   }


   // thread safe, only modified in plugin startup
   const string& net_plugin_impl::get_first_p2p_address() const {
      return p2p_addresses.size() > 0 ? *p2p_addresses.begin() : empty;
   }

   void net_plugin_impl::create_session(tcp::socket&& socket, string listen_address, size_t limit) {
      boost::system::error_code rec;
      const auto&               rend = socket.remote_endpoint(rec);
      if (rec) {
         fc_ilog(p2p_conn_log, "Unable to get remote endpoint: ${m}", ("m", rec.message()));
      } else {
         uint32_t                  visitors  = 0;
         uint32_t                  from_addr = 0;
         const auto&               paddr_add = rend.address();
         const auto                paddr_port = rend.port();
         string                    paddr_str  = paddr_add.to_string();
         string                    paddr_desc = paddr_str + ":" + std::to_string(paddr_port);
         connections.for_each_connection([&visitors, &from_addr, &paddr_str](const connection_ptr& conn) {
            if (conn->socket_is_open()) {
               if (conn->incoming()) {
                  ++visitors;
                  fc::lock_guard g_conn(conn->conn_mtx);
                  if (paddr_str == conn->remote_endpoint_ip) {
                     ++from_addr;
                  }
               }
            }
         });
         if (from_addr < max_nodes_per_host &&
               (auto_bp_peering_enabled() || connections.get_max_client_count() == 0 ||
               visitors < connections.get_max_client_count())) {
            fc_ilog(p2p_conn_log, "Accepted new connection: " + paddr_str);

            connections.any_of_supplied_peers([&listen_address, &paddr_str, &paddr_desc, &limit](const string& peer_addr) {
               if (auto [host, port, type] = net_utils::split_host_port_type(peer_addr); host == paddr_str) {
                  if (limit > 0) {
                     fc_dlog(p2p_conn_log, "Connection inbound to ${la} from ${a} is a configured p2p-peer-address and will not be throttled", ("la", listen_address)("a", paddr_desc));
                  }
                  limit = 0;
                  return true;
               }
               return false;
            });

            connection_ptr new_connection = std::make_shared<connection>(std::move(socket), listen_address, limit);
            boost::asio::post(new_connection->strand, [new_connection, this]() {
               if (new_connection->start_session()) {
                  connections.add(new_connection);
               }
            });

         } else {
            if (from_addr >= max_nodes_per_host) {
               fc_dlog(p2p_conn_log, "Number of connections (${n}) from ${ra} exceeds limit ${l}, closing",
                        ("n", from_addr + 1)("ra", paddr_desc)("l", max_nodes_per_host));
            } else {
               fc_dlog(p2p_conn_log, "max_client_count ${m} exceeded, closing: ${ra}", ("m", connections.get_max_client_count())("ra", paddr_desc));
            }
            // new_connection never added to connections and start_session not called, lifetime will end
            boost::system::error_code ec;
            socket.shutdown(tcp::socket::shutdown_both, ec);
            socket.close(ec);
         }
      }
   }

   // only called from strand thread
   void connection::start_read_message() {
      try {
         std::size_t minimum_read = outstanding_read_bytes != 0 ? outstanding_read_bytes : message_header_size;
         outstanding_read_bytes = 0;

         if (my_impl->use_socket_read_watermark) {
            const size_t max_socket_read_watermark = 4096;
            std::size_t socket_read_watermark = std::min<std::size_t>(minimum_read, max_socket_read_watermark);
            boost::asio::socket_base::receive_low_watermark read_watermark_opt(socket_read_watermark);
            boost::system::error_code ec;
            socket->set_option( read_watermark_opt, ec );
            if( ec ) {
               peer_elog( p2p_conn_log, this, "unable to set read watermark: ${e1}", ("e1", ec.message()) );
            }
         }

         auto completion_handler = [minimum_read](boost::system::error_code ec, std::size_t bytes_transferred) -> std::size_t {
            if (ec || bytes_transferred >= minimum_read ) {
               return 0;
            } else {
               return minimum_read - bytes_transferred;
            }
         };

         uint32_t write_queue_size = buffer_queue.write_queue_size();
         if( write_queue_size > def_max_write_queue_size ) {
            peer_elog( p2p_conn_log, this, "write queue full ${s} bytes, giving up on connection, closing", ("s", write_queue_size) );
            close( false );
            return;
         }

         boost::asio::async_read( *socket,
            pending_message_buffer.get_buffer_sequence_for_boost_async_read(), completion_handler,
            boost::asio::bind_executor( strand,
              [conn = shared_from_this(), socket=socket]( boost::system::error_code ec, std::size_t bytes_transferred ) {
               // may have closed connection and cleared pending_message_buffer
               if (!conn->socket->is_open() && conn->socket_is_open()) { // if socket_open then close not called
                  peer_dlog( p2p_conn_log, conn, "async_read socket not open, closing");
                  conn->close();
                  return;
               }
               if (socket != conn->socket ) { // different socket, conn must have created a new socket, make sure previous is closed
                  peer_dlog( p2p_conn_log, conn, "async_read diff socket closing");
                  boost::system::error_code ec;
                  socket->shutdown( tcp::socket::shutdown_both, ec );
                  socket->close( ec );
                  return;
               }

               bool close_connection = false;
               try {
                  if( !ec ) {
                     if (bytes_transferred > conn->pending_message_buffer.bytes_to_write()) {
                        peer_elog( p2p_conn_log, conn, "async_read_some callback: bytes_transfered = ${bt}, buffer.bytes_to_write = ${btw}",
                                   ("bt",bytes_transferred)("btw",conn->pending_message_buffer.bytes_to_write()) );
                     }
                     EOS_ASSERT(bytes_transferred <= conn->pending_message_buffer.bytes_to_write(), plugin_exception, "");
                     conn->pending_message_buffer.advance_write_ptr(bytes_transferred);
                     while (conn->pending_message_buffer.bytes_to_read() > 0) {
                        uint32_t bytes_in_buffer = conn->pending_message_buffer.bytes_to_read();

                        if (bytes_in_buffer < message_header_size) {
                           conn->outstanding_read_bytes = message_header_size - bytes_in_buffer;
                           break;
                        } else {
                           uint32_t message_length;
                           auto index = conn->pending_message_buffer.read_index();
                           conn->pending_message_buffer.peek(&message_length, sizeof(message_length), index);
                           if(message_length > def_send_buffer_size*2 || message_length == 0) {
                              peer_elog( p2p_conn_log, conn, "incoming message length unexpected (${i})", ("i", message_length) );
                              close_connection = true;
                              break;
                           }

                           auto total_message_bytes = message_length + message_header_size;

                           if (bytes_in_buffer >= total_message_bytes) {
                              conn->pending_message_buffer.advance_read_ptr(message_header_size);
                              conn->consecutive_immediate_connection_close = 0;
                              if (!conn->process_next_message(message_length)) {
                                 return;
                              }
                           } else {
                              auto outstanding_message_bytes = total_message_bytes - bytes_in_buffer;
                              auto available_buffer_bytes = conn->pending_message_buffer.bytes_to_write();
                              if (outstanding_message_bytes > available_buffer_bytes) {
                                 conn->pending_message_buffer.add_space( outstanding_message_bytes - available_buffer_bytes );
                              }

                              conn->outstanding_read_bytes = outstanding_message_bytes;
                              break;
                           }
                        }
                     }
                     if( !close_connection ) conn->start_read_message();
                  } else {
                     if (ec.value() != boost::asio::error::eof) {
                        peer_elog( p2p_conn_log, conn, "Error reading message: ${m}", ( "m", ec.message() ) );
                     } else {
                        peer_ilog( p2p_conn_log, conn, "Peer closed connection" );
                     }
                     close_connection = true;
                  }
               }
               catch ( const std::bad_alloc& )
               {
                 throw;
               }
               catch ( const boost::interprocess::bad_alloc& )
               {
                 throw;
               }
               catch(const fc::exception &ex)
               {
                  peer_elog( p2p_conn_log, conn, "Exception in handling read data ${s}", ("s",ex.to_string()) );
                  close_connection = true;
               }
               catch(const std::exception &ex) {
                  peer_elog( p2p_conn_log, conn, "Exception in handling read data: ${s}", ("s",ex.what()) );
                  close_connection = true;
               }
               catch (...) {
                  peer_elog( p2p_conn_log, conn, "Undefined exception handling read data" );
                  close_connection = true;
               }

               if( close_connection ) {
                  peer_dlog( p2p_conn_log, conn, "Closing connection" );
                  conn->close();
               }
         }));
      } catch (...) {
         peer_elog( p2p_conn_log, this, "Undefined exception in start_read_message, closing connection" );
         close();
      }
   }

   // called from connection strand
   bool connection::process_next_message( uint32_t message_length ) {
      bytes_received += message_length;
      last_bytes_received = get_time();
      try {
         auto now = latest_msg_time = std::chrono::steady_clock::now();

         // if next message is a block we already have, exit early
         auto peek_ds = pending_message_buffer.create_peek_datastream();
         unsigned_int which{};
         fc::raw::unpack( peek_ds, which );

         msg_type_t net_msg = to_msg_type_t(which.value);

         if( net_msg == msg_type_t::signed_block ) {
            latest_blk_time = now;
            return process_next_block_message( message_length );
         } else if( net_msg == msg_type_t::packed_transaction ) {
            return process_next_trx_message( message_length );
         } else if( net_msg == msg_type_t::transaction_notice_message ) {
            return process_next_trx_notice_message( message_length );
         } else if( net_msg == msg_type_t::vote_message ) {
            return process_next_vote_message( message_length );
         } else {
            auto ds = pending_message_buffer.create_datastream();
            net_message msg;
            fc::raw::unpack( ds, msg );
            msg_handler m( shared_from_this() );
            std::visit( m, msg );
         }

      } catch( const fc::exception& e ) {
         peer_wlog( p2p_msg_log, this, "Exception in handling message: ${s}", ("s", e.to_detail_string()) );
         close();
         return false;
      }
      return true;
   }

   // called from connection strand
   bool connection::process_next_block_message(uint32_t message_length) {
      auto peek_ds = pending_message_buffer.create_peek_datastream();
      unsigned_int which{};
      fc::raw::unpack( peek_ds, which ); // throw away
      block_header bh;
      fc::raw::unpack( peek_ds, bh );
      const block_id_type blk_id = bh.calculate_id();
      const uint32_t blk_num = last_received_block_num = block_header::num_from_id(blk_id);
      const fc::time_point now = fc::time_point::now();
      my_impl->last_block_received_time = last_received_block_time = now;
      const fc::microseconds age(now - bh.timestamp);
      if( my_impl->dispatcher.have_block( blk_id ) ) {
         pending_message_buffer.advance_read_ptr( message_length ); // advance before any send

         // if we have the block then it has been header validated, add for this connection_id
         my_impl->dispatcher.add_peer_block(blk_id, connection_id);
         send_block_nack(blk_id);

         peer_dlog( p2p_blk_log, this, "already received block ${num}, id ${id}..., latency ${l}ms",
                    ("num", blk_num)("id", blk_id.str().substr(8,16))("l", age.count()/1000) );
         my_impl->sync_master->sync_recv_block( shared_from_this(), blk_id, blk_num, age );

         return true;
      }
      peer_dlog( p2p_blk_log, this, "received block ${num}, id ${id}..., latency: ${l}ms, head ${h}, fhead ${f}",
                 ("num", bh.block_num())("id", blk_id.str().substr(8,16))("l", age.count()/1000)
                 ("h", my_impl->get_chain_head_num())("f", my_impl->get_fork_db_head_num()));
      if( !my_impl->sync_master->syncing_from_peer() ) { // guard against peer thinking it needs to send us old blocks
         block_num_type fork_db_root_num = my_impl->get_fork_db_root_num();
         if( blk_num <= fork_db_root_num ) {
            pending_message_buffer.advance_read_ptr( message_length ); // advance before any send
            peer_dlog( p2p_blk_log, this, "received block ${n} less than froot ${fr}", ("n", blk_num)("fr", fork_db_root_num) );
            send_block_nack(blk_id);
            cancel_sync_wait();

            return true;
         }
      } else {
         block_sync_bytes_received += message_length;
         uint32_t fork_db_root_num = my_impl->get_fork_db_root_num();
         const bool block_le_lib = blk_num <= fork_db_root_num;
         if (block_le_lib) {
            peer_dlog( p2p_blk_log, this, "received block ${n} less than froot ${fr} while syncing", ("n", blk_num)("fr", fork_db_root_num) );
            pending_message_buffer.advance_read_ptr( message_length ); // advance before any send
         }
         my_impl->sync_master->sync_recv_block(shared_from_this(), blk_id, blk_num, age);
         if (block_le_lib)
            return true;
      }

      auto mb_ds = pending_message_buffer.create_datastream();
      fc::raw::unpack( mb_ds, which );

      fc::datastream_mirror ds(mb_ds, message_length);
      shared_ptr<signed_block> ptr = std::make_shared<signed_block>();
      fc::raw::unpack( ds, *ptr );

      bool has_webauthn_sig = ptr->producer_signature.is_webauthn();

      constexpr auto additional_sigs_eid = additional_block_signatures_extension::extension_id();
      auto exts = ptr->validate_and_extract_extensions();
      if( exts.count( additional_sigs_eid ) ) {
         const auto &additional_sigs = std::get<additional_block_signatures_extension>(exts.lower_bound( additional_sigs_eid )->second).signatures;
         has_webauthn_sig |= std::ranges::any_of(additional_sigs, [](const auto& sig) { return sig.is_webauthn(); });
      }

      if( has_webauthn_sig ) {
         peer_dlog( p2p_blk_log, this, "WebAuthn signed block received, closing connection" );
         close();
         return false;
      }

      handle_message( blk_id, std::move( ptr ) );
      return true;
   }

   // called from connection strand
   bool connection::process_next_trx_message(uint32_t message_length) {
      if( !my_impl->p2p_accept_transactions ) {
         peer_dlog( p2p_trx_log, this, "p2p-accept-transaction=false - dropping trx" );
         pending_message_buffer.advance_read_ptr( message_length );
         return true;
      }
      if (my_impl->sync_master->syncing_from_peer()) {
         peer_dlog(p2p_trx_log, this, "syncing, dropping trx");
         pending_message_buffer.advance_read_ptr( message_length );
         return true;
      }

      const uint32_t trx_in_progress_sz = this->trx_in_progress_size.load();

      auto now = fc::time_point::now();
      auto ds = pending_message_buffer.create_datastream();
      unsigned_int which{};
      fc::raw::unpack( ds, which );
      // shared_ptr<packed_transaction> needed here because packed_transaction_ptr is shared_ptr<const packed_transaction>
      std::shared_ptr<packed_transaction> ptr = std::make_shared<packed_transaction>();
      fc::raw::unpack( ds, *ptr );
      if( trx_in_progress_sz > def_max_trx_in_progress_size) {
         char reason[72];
         snprintf(reason, 72, "Dropping trx, too many trx in progress %u bytes", trx_in_progress_sz);
         my_impl->producer_plug->log_failed_transaction(ptr->id(), ptr, reason);
         if (now - fc::seconds(1) >= last_dropped_trx_msg_time) {
            last_dropped_trx_msg_time = now;
            peer_wlog(p2p_trx_log, this, reason);
         }
         if (my_impl->increment_dropped_trxs) {
            my_impl->increment_dropped_trxs();
         }
         return true;
      }

      auto[trx_entries_sz, have_trx] = my_impl->dispatcher.add_peer_txn( ptr->id(), ptr->expiration(), *this );
      if (trx_entries_sz > def_max_trx_entries_per_conn_size) {
         peer_wlog(p2p_conn_log, this, "Max tracked trx reached ${c}, closing", ("c", trx_entries_sz));
         close();
         return true;
      }

      if( have_trx ) {
         peer_dlog( p2p_trx_log, this, "got a duplicate transaction - dropping" );
         return true;
      }

      const auto& tid = ptr->id();
      peer_dlog( p2p_trx_log, this, "received packed_transaction ${id}", ("id", tid) );

      if (message_length < def_trx_notice_min_size) {
         // transfer packed transaction is ~170 bytes, transaction notice is 41 bytes
         fc_dlog( p2p_trx_log, "trx notice not sent, trx size ${s}", ("s", message_length));
      } else {
         fc_dlog( p2p_trx_log, "send trx notice, trx size ${s}", ("s", message_length));
         my_impl->dispatcher.bcast_transaction_notify(ptr);
      }

      handle_message( ptr );
      return true;
   }

   // called from connection strand
   bool connection::process_next_trx_notice_message(uint32_t message_length) {
      if( !my_impl->p2p_accept_transactions ) {
         peer_dlog( p2p_trx_log, this, "p2p-accept-transaction=false - dropping trx notice" );
         pending_message_buffer.advance_read_ptr( message_length );
         return true;
      }
      if (my_impl->sync_master->syncing_from_peer()) {
         peer_dlog(p2p_trx_log, this, "syncing, dropping trx notice");
         pending_message_buffer.advance_read_ptr( message_length );
         return true;
      }

      auto ds = pending_message_buffer.create_datastream();
      unsigned_int which{};
      fc::raw::unpack( ds, which );
      transaction_notice_message msg;
      fc::raw::unpack( ds, msg );

      size_t trx_entries_sz = my_impl->dispatcher.add_peer_txn_notice( msg.id, *this );
      if (trx_entries_sz > def_max_trx_entries_per_conn_size) {
         peer_wlog(p2p_conn_log, this, "Max tracked trx reached ${c}, closing", ("c", trx_entries_sz));
         close();
      }

      handle_message( msg );

      return true;
   }

// called from connection strand
   bool connection::process_next_vote_message(uint32_t message_length) {
      if( !my_impl->p2p_accept_votes ) {
         peer_dlog( p2p_trx_log, this, "p2p_accept_votes=false - dropping vote" );
         pending_message_buffer.advance_read_ptr( message_length );
         return true;
      }

      auto ds = pending_message_buffer.create_datastream();
      unsigned_int which{};
      fc::raw::unpack( ds, which );
      assert(to_msg_type_t(which) == msg_type_t::vote_message); // verified by caller
      vote_message_ptr ptr = std::make_shared<vote_message>();
      fc::raw::unpack( ds, *ptr );

      handle_message( ptr );
      return true;
   }

   // called from connection strand
   void connection::send_block_nack(const block_id_type& block_id) {
      if (protocol_version < proto_version_t::block_nack || my_impl->p2p_disable_block_nack)
         return;

      if (my_impl->sync_master->syncing_from_peer())
         return;

      peer_dlog(p2p_blk_log, this, "Sending nack ${n}", ("n", block_header::num_from_id(block_id)));

      buffer_factory buff_factory;
      const auto& send_buffer = buff_factory.get_send_buffer( block_nack_message{block_id} );

      enqueue_buffer( msg_type_t::block_nack_message, std::nullopt, queued_buffer::queue_t::general, send_buffer, go_away_reason::no_reason );
   }

   void net_plugin_impl::plugin_shutdown() {
      thread_pool.stop();
   }

   // call only from main application thread
   void net_plugin_impl::update_chain_info() {
      controller& cc = chain_plug->chain();
      uint32_t fork_db_root_num = 0, head_num = 0, fork_db_head_num = 0;
      auto head = cc.head();
      {
         fc::lock_guard g( chain_info_mtx );
         chain_info.fork_db_root_id = cc.fork_db_root().id();
         chain_info.fork_db_root_num = fork_db_root_num = block_header::num_from_id(chain_info.fork_db_root_id);
         chain_info.head_id = head.id();
         chain_info.head_num = head_num = block_header::num_from_id(chain_info.head_id);
         chain_info.fork_db_head_id = cc.fork_db_head().id();
         chain_info.fork_db_head_num = fork_db_head_num = block_header::num_from_id(chain_info.fork_db_head_id);
      }
      head_block_time = head.block_time();
      fc_dlog( p2p_log, "updating chain info froot ${fr} head ${h} fhead ${f}", ("fr", fork_db_root_num)("h", head_num)("f", fork_db_head_num) );
   }

   // call only from main application thread
   // called from irreversible block signal
   void net_plugin_impl::update_chain_info(const block_id_type& fork_db_root_id) {
      controller& cc = chain_plug->chain();
      uint32_t fork_db_root_num = 0, head_num = 0, fork_db_head_num = 0;
      auto head = cc.head();
      {
         fc::lock_guard g( chain_info_mtx );
         chain_info.fork_db_root_id = fork_db_root_id;
         chain_info.fork_db_root_num = fork_db_root_num = block_header::num_from_id(fork_db_root_id);
         chain_info.head_id = head.id();
         chain_info.head_num = head_num = block_header::num_from_id(chain_info.head_id);
         chain_info.fork_db_head_id = cc.fork_db_head().id();
         chain_info.fork_db_head_num = fork_db_head_num = block_header::num_from_id(chain_info.fork_db_head_id);
      }
      head_block_time = head.block_time();
      fc_dlog( p2p_log, "updating chain info froot ${fr} head ${h} fhead ${f}", ("fr", fork_db_root_num)("h", head_num)("f", fork_db_head_num) );
   }

   net_plugin_impl::chain_info_t net_plugin_impl::get_chain_info() const {
      fc::lock_guard g( chain_info_mtx );
      return chain_info;
   }

   uint32_t net_plugin_impl::get_fork_db_root_num() const {
      fc::lock_guard g( chain_info_mtx );
      return chain_info.fork_db_root_num;
   }

   uint32_t net_plugin_impl::get_chain_head_num() const {
      fc::lock_guard g( chain_info_mtx );
      return chain_info.head_num;
   }

   uint32_t net_plugin_impl::get_fork_db_head_num() const {
      fc::lock_guard g( chain_info_mtx );
      return chain_info.fork_db_head_num;
   }

   bool connection::is_valid( const handshake_message& msg ) const {
      // Do some basic validation of an incoming handshake_message, so things
      // that really aren't handshake messages can be quickly discarded without
      // affecting state.
      bool valid = true;
      if (msg.fork_db_root_num > msg.fork_db_head_num) {
         peer_wlog( p2p_msg_log, this, "Handshake message validation: last irreversible (${i}) is greater than fhead (${h})",
                  ("i", msg.fork_db_root_num)("h", msg.fork_db_head_num) );
         valid = false;
      }
      if (msg.p2p_address.empty()) {
         peer_wlog( p2p_msg_log, this, "Handshake message validation: p2p_address is null string" );
         valid = false;
      } else if( msg.p2p_address.length() > net_utils::max_handshake_str_length ) {
         // see max_handshake_str_length comment in protocol.hpp
         peer_wlog( p2p_msg_log, this, "Handshake message validation: p2p_address too large: ${p}",
                    ("p", msg.p2p_address.substr(0, net_utils::max_handshake_str_length) + "...") );
         valid = false;
      }
      if (msg.os.empty()) {
         peer_wlog( p2p_msg_log, this, "Handshake message validation: os field is null string" );
         valid = false;
      } else if( msg.os.length() > net_utils::max_handshake_str_length ) {
         peer_wlog( p2p_msg_log, this, "Handshake message validation: os field too large: ${p}",
                    ("p", msg.os.substr(0, net_utils::max_handshake_str_length) + "...") );
         valid = false;
      }
      if( msg.agent.length() > net_utils::max_handshake_str_length ) {
         peer_wlog( p2p_msg_log, this, "Handshake message validation: agent field too large: ${p}",
                  ("p", msg.agent.substr(0, net_utils::max_handshake_str_length) + "...") );
         valid = false;
      }
      if ((msg.sig != chain::signature_type() || msg.token != sha256()) && (msg.token != fc::sha256::hash(msg.time))) {
         peer_wlog( p2p_msg_log, this, "Handshake message validation: token field invalid" );
         valid = false;
      }
      return valid;
   }

   void connection::handle_message( const chain_size_message& msg ) {
      peer_dlog(p2p_msg_log, this, "received chain_size_message");
   }

   // called from connection strand
   void connection::handle_message( const handshake_message& msg ) {
      if( !is_valid( msg ) ) {
         peer_wlog( p2p_msg_log, this, "bad handshake message");
         no_retry = go_away_reason::fatal_other;
         enqueue( go_away_message{ go_away_reason::fatal_other } );
         return;
      }
      peer_dlog( p2p_msg_log, this, "received handshake gen ${g}, froot ${fr}, fhead ${fh}",
                 ("g", msg.generation)("fr", msg.fork_db_root_num)("fh", msg.fork_db_head_num) );

      peer_fork_db_root_num = msg.fork_db_root_num;
      peer_fork_db_head_block_num = msg.fork_db_head_num;
      fc::unique_lock g_conn( conn_mtx );
      last_handshake_recv = msg;
      g_conn.unlock();

      set_state(connection_state::connected);
      if (msg.generation == 1) {
         if( msg.node_id == my_impl->node_id) {
            peer_ilog( p2p_conn_log, this, "Self connection detected node_id ${id}. Closing connection", ("id", msg.node_id) );
            no_retry = go_away_reason::self;
            enqueue( go_away_message( go_away_reason::self ) );
            return;
         }

         short_agent_name = msg.agent.substr( msg.agent.size() > 1 && msg.agent[0] == '"' ? 1 : 0, 15);
         log_p2p_address = msg.p2p_address;
         fc::unique_lock g_conn( conn_mtx );
         p2p_address = msg.p2p_address;
         unique_conn_node_id = msg.node_id.str().substr( 0, 7 );
         g_conn.unlock();

         my_impl->mark_configured_bp_connection(this);
         if (my_impl->exceeding_connection_limit(shared_from_this())) {
            // When auto bp peering is enabled, create_session() check doesn't have enough information to determine
            // if a client is a BP peer. In create_session(), it only has the peer address which a node is connecting
            // from, but it would be different from the address it is listening. The only way to make sure is when the
            // first handshake message is received with the p2p_address information in the message. Thus the connection
            // limit checking has to be here when auto bp peering is enabled.
            fc_dlog(p2p_conn_log, "max_client_count ${m} exceeded", ("m", my_impl->connections.get_max_client_count()));
            my_impl->connections.disconnect(peer_address());
            return;
         }

         if( incoming() ) {
            if (auto [host, port, type] = net_utils::split_host_port_type(msg.p2p_address); !host.empty())
               set_connection_type( msg.p2p_address);
            else
               peer_dlog(p2p_msg_log, this, "Invalid handshake p2p_address ${p}", ("p", msg.p2p_address));
         } else {
            // peer p2p_address may contain trx or blk only request, honor requested connection type
            set_peer_connection_type(msg.p2p_address);
         }

         peer_dlog( p2p_conn_log, this, "checking for duplicate" );
         auto is_duplicate = [&](const connection_ptr& check) {
            if(check.get() == this)
               return false;
            fc::unique_lock g_check_conn( check->conn_mtx );
            fc_dlog( p2p_conn_log, "dup check: connected ${c}, ${l} =? ${r}",
                     ("c", check->connected())("l", check->last_handshake_recv.node_id)("r", msg.node_id) );
            if(check->connected() && check->last_handshake_recv.node_id == msg.node_id) {
               if (my_impl->node_id < msg.node_id) {
                  fc_dlog( p2p_conn_log, "not duplicate, my_impl->node_id '${lhs}' < msg.node_id '${rhs}'",
                           ("lhs", my_impl->node_id)("rhs", msg.node_id) );
                  // only the connection from lower node_id to higher node_id will be considered as a duplicate,
                  // so there is no chance for both connections to be closed
                  return false;
               }
               return true;
            }
            return false;
         };
         if (my_impl->connections.any_of_connections(is_duplicate)) {
            peer_dlog( p2p_conn_log, this, "sending go_away duplicate, msg.p2p_address: ${add}", ("add", msg.p2p_address) );
            enqueue(go_away_message{go_away_reason::duplicate, conn_node_id});
            no_retry = go_away_reason::duplicate;
            return;
         }

         if( msg.chain_id != my_impl->chain_id ) {
            peer_ilog( p2p_conn_log, this, "Peer on a different chain. Closing connection" );
            no_retry = go_away_reason::wrong_chain;
            enqueue( go_away_message(go_away_reason::wrong_chain) );
            return;
         }
         protocol_version = net_plugin_impl::to_protocol_version(msg.network_version);
         if( protocol_version != net_version ) {
            peer_ilog( p2p_conn_log, this, "Local network version different: ${nv} Remote version: ${mnv}",
                       ("nv", static_cast<uint16_t>(net_version))("mnv", static_cast<uint16_t>(protocol_version.load())) );
         } else {
            peer_dlog( p2p_conn_log, this, "Local network version: ${nv}", ("nv", static_cast<uint16_t>(net_version)) );
         }

         conn_node_id = msg.node_id;
         short_conn_node_id = conn_node_id.str().substr( 0, 7 );

         if( !my_impl->authenticate_peer( msg ) ) {
            peer_wlog( p2p_conn_log, this, "Peer not authenticated.  Closing connection." );
            no_retry = go_away_reason::authentication;
            enqueue( go_away_message( go_away_reason::authentication ) );
            return;
         }

         uint32_t peer_fork_db_root_num = msg.fork_db_root_num;
         uint32_t fork_db_root_num = my_impl->get_fork_db_root_num();

         peer_dlog( p2p_blk_log, this, "handshake check froot ${n}, peer_froot ${pn}", ("n", fork_db_root_num)("pn", peer_fork_db_root_num) );

         if( peer_fork_db_root_num <= fork_db_root_num && peer_fork_db_root_num > 0 ) {
            try {
               auto [on_fork, unknown_block] = block_on_fork(msg.fork_db_root_id); // thread safe
               if (unknown_block) {
                  // can be not found if running with a truncated block log
                  peer_dlog( p2p_blk_log, this, "peer froot block ${n} is unknown", ("n", peer_fork_db_root_num) );
               } else if (on_fork) {
                  peer_wlog( p2p_conn_log, this, "Peer chain is forked, sending: forked go away" );
                  no_retry = go_away_reason::forked;
                  enqueue( go_away_message( go_away_reason::forked ) );
               }
            } catch( ... ) {
               peer_wlog( p2p_blk_log, this, "caught an exception getting block id for ${pl}", ("pl", peer_fork_db_root_num) );
            }
         }

         // we don't support the 2.1 packed_transaction & signed_block, so tell 2.1 clients we are 2.0
         if( protocol_version >= proto_version_t::pruned_types && protocol_version < proto_version_t::leap_initial ) {
            sent_handshake_count = 0;
            net_version = proto_version_t::explicit_sync;
            send_handshake();
            return;
         }

         if( sent_handshake_count == 0 ) {
            send_handshake();
         }

         send_gossip_bp_peers_initial_message();
      }

      uint32_t nblk_combined_latency = calc_block_latency();
      my_impl->sync_master->recv_handshake( shared_from_this(), msg, nblk_combined_latency );
   }

   // called from connection strand
   uint32_t connection::calc_block_latency() {
      uint32_t nblk_combined_latency = 0;
      if (peer_ping_time_ns != std::numeric_limits<uint64_t>::max()) {
         // number of blocks syncing node is behind from a peer node, round up
         uint32_t nblk_behind_by_net_latency = std::lround(static_cast<double>(peer_ping_time_ns.load()) / static_cast<double>(block_interval_ns));
         // peer_ping_time_ns includes time there and back, include round trip time as the block latency is used to compensate for communication back
         nblk_combined_latency = nblk_behind_by_net_latency;
         // message in the log below is used in p2p_high_latency_test.py test
         peer_dlog(p2p_conn_log, this, "Network latency is ${lat}ms, ${num} blocks discrepancy by network latency, ${tot_num} blocks discrepancy expected once message received",
                   ("lat", peer_ping_time_ns / 2 / 1000000)("num", nblk_behind_by_net_latency)("tot_num", nblk_combined_latency));
      }
      return nblk_combined_latency;
   }

   void connection::handle_message( const go_away_message& msg ) {
      peer_wlog( p2p_conn_log, this, "received go_away_message, reason = ${r}", ("r", reason_str( msg.reason )) );

      bool retry = no_retry == go_away_reason::no_reason; // if no previous go away message
      no_retry = msg.reason;
      if( msg.reason == go_away_reason::duplicate ) {
         conn_node_id = msg.node_id;
      }
      if( msg.reason == go_away_reason::wrong_version ) {
         if( !retry ) no_retry = go_away_reason::fatal_other; // only retry once on wrong version
      }
      else if ( msg.reason == go_away_reason::benign_other ) {
         if ( retry ) peer_dlog( p2p_conn_log, this, "received benign_other reason, retrying to connect");
      }
      else {
         retry = false;
      }
      flush_queues();

      close( retry ); // reconnect if wrong_version
   }

   // some clients before leap 5.0 provided microsecond epoch instead of nanosecond epoch
   std::chrono::nanoseconds normalize_epoch_to_ns(int64_t x) {
      //        1686211688888 milliseconds - 2023-06-08T08:08:08.888, 5yrs from EOS genesis 2018-06-08T08:08:08.888
      //     1686211688888000 microseconds
      //  1686211688888000000 nanoseconds
      if (x >= 1686211688888000000) // nanoseconds
         return std::chrono::nanoseconds{x};
      if (x >= 1686211688888000) // microseconds
         return std::chrono::nanoseconds{x*1000};
      if (x >= 1686211688888) // milliseconds
         return std::chrono::nanoseconds{x*1000*1000};
      if (x >= 1686211688) // seconds
         return std::chrono::nanoseconds{x*1000*1000*1000};
      return std::chrono::nanoseconds{0}; // unknown or is zero
   }

   void connection::handle_message( const time_message& msg ) {
      peer_dlog( p2p_msg_log, this, "received time_message: ${t}, org: ${o}", ("t", msg)("o", org.count()) );

      // If the transmit timestamp is zero, the peer is horribly broken.
      if(msg.xmt == 0)
         return; // invalid timestamp

      // We've already lost however many microseconds it took to dispatch the message, but it can't be helped.
      msg.dst = get_time().count();

      if (msg.org != 0) {
         if (msg.org == org.count()) {
            auto ping = msg.dst - msg.org;
            peer_dlog(p2p_msg_log, this, "send_time ping ${p}us", ("p", ping / 1000));
            peer_ping_time_ns = ping;
         } else {
            // a diff time loop is in progress, ignore this message as it is not the one we want
            return;
         }
      }

      auto msg_xmt = normalize_epoch_to_ns(msg.xmt);
      if (msg_xmt == xmt)
         return; // duplicate packet

      xmt = msg_xmt;

      if( msg.org == 0 ) {
         send_time( msg );
         return;  // We don't have enough data to perform the calculation yet.
      }

      if (org != std::chrono::nanoseconds{0}) {
         auto rec = normalize_epoch_to_ns(msg.rec);
         int64_t offset = (double((rec - org).count()) + double(msg_xmt.count() - msg.dst)) / 2.0;

         if (std::abs(offset) > block_interval_ns) {
            peer_wlog(p2p_msg_log, this, "Clock offset is ${of}us, calculation: (rec ${r} - org ${o} + xmt ${x} - dst ${d})/2",
                      ("of", offset / 1000)("r", rec.count())("o", org.count())("x", msg_xmt.count())("d", msg.dst));
         }
      }
      org = std::chrono::nanoseconds{0};

      fc::unique_lock g_conn( conn_mtx );
      if( last_handshake_recv.generation == 0 ) {
         g_conn.unlock();
         send_handshake();
      }

      // make sure we also get the latency we need
      if (peer_ping_time_ns == std::numeric_limits<uint64_t>::max()) {
         send_time();
      }
   }

   void connection::handle_message( const notice_message& msg ) {
      // peer tells us about one or more blocks or txns. When done syncing, forward on
      // notices of previously unknown blocks or txns,
      //
      set_state(connection_state::connected);
      if( msg.known_blocks.ids.size() > 2 ) {
         peer_wlog( p2p_msg_log, this, "Invalid notice_message, known_blocks.ids.size ${s}, closing connection",
                    ("s", msg.known_blocks.ids.size()) );
         close( false );
         return;
      }
      if( msg.known_trx.mode != none ) {
         if( p2p_msg_log.is_enabled( fc::log_level::debug ) ) {
            const block_id_type& blkid = msg.known_blocks.ids.empty() ? block_id_type{} : msg.known_blocks.ids.front();
            peer_dlog( p2p_msg_log, this, "this is a ${m} notice with ${n} pending blocks: ${num} ${id}...",
                       ("m", modes_str( msg.known_blocks.mode ))("n", msg.known_blocks.pending)
                       ("num", block_header::num_from_id( blkid ))("id", blkid.str().substr( 8, 16 )) );
         }
      }
      switch (msg.known_trx.mode) {
      case none:
      case last_irr_catch_up: {
         fc::unique_lock g_conn( conn_mtx );
         last_handshake_recv.fork_db_head_num = std::max(msg.known_blocks.pending, last_handshake_recv.fork_db_head_num);
         g_conn.unlock();
         break;
      }
      case catch_up:
         break;
      case normal: {
         my_impl->dispatcher.recv_notice( shared_from_this(), msg, false );
      }
      }

      if( msg.known_blocks.mode != none ) {
         peer_dlog( p2p_msg_log, this, "this is a ${m} notice with ${n} blocks",
                    ("m", modes_str( msg.known_blocks.mode ))( "n", msg.known_blocks.pending ) );
      }
      switch (msg.known_blocks.mode) {
      case none : {
         break;
      }
      case last_irr_catch_up:
      case catch_up: {
         if (msg.known_blocks.ids.size() > 1) {
            peer_start_block_num = block_header::num_from_id(msg.known_blocks.ids[1]);
         }
         if (msg.known_blocks.ids.size() > 0) {
            peer_fork_db_head_block_num = block_header::num_from_id(msg.known_blocks.ids[0]);
         }
         my_impl->sync_master->sync_recv_notice( shared_from_this(), msg );
         break;
      }
      case normal : {
         my_impl->dispatcher.recv_notice( shared_from_this(), msg, false );
         break;
      }
      default: {
         peer_wlog( p2p_msg_log, this, "bad notice_message : invalid known_blocks.mode ${m}",
                    ("m", static_cast<uint32_t>(msg.known_blocks.mode)) );
      }
      }
   }

   void connection::handle_message( const request_message& msg ) {
      if( msg.req_blocks.ids.size() > 2 ) {
         peer_wlog( p2p_blk_log, this, "Invalid request_message, req_blocks.ids.size ${s}, closing",
                    ("s", msg.req_blocks.ids.size()) );
         close();
         return;
      }

      switch (msg.req_blocks.mode) {
      case catch_up : {
         const block_id_type& id = msg.req_blocks.ids.empty() ? block_id_type() : msg.req_blocks.ids.back();
         peer_dlog( p2p_blk_log, this, "${d} request_message:catch_up #${bn}:${id}",
                    ("d", is_blocks_connection() ? "received" : "ignoring")("bn", block_header::num_from_id(id))("id",id) );
         if (!is_blocks_connection())
            return;
         blk_send_branch( id );
         return;
      }
      case normal : {
         if (protocol_version >= proto_version_t::block_nack) {
            if (msg.req_blocks.ids.size() == 2 && msg.req_trx.ids.empty()) {
               const block_id_type& req_id = msg.req_blocks.ids[0]; // 0 - req_id, 1 - peer_head_id
               peer_dlog( p2p_blk_log, this, "${d} request_message:normal #${bn}:${id}",
                          ("d", is_blocks_connection() ? "received" : "ignoring")("bn", block_header::num_from_id(req_id))("id",req_id) );
               if (!is_blocks_connection())
                  return;
               const block_id_type& peer_head_id = msg.req_blocks.ids[1];
               blk_send_branch_from_nack_request(req_id, peer_head_id);
               return;
            }
         }
         peer_wlog( p2p_blk_log, this, "Invalid request_message, req_blocks.mode = normal" );
         close();
         return;
      }
      default:;
      }


      switch (msg.req_trx.mode) {
      case catch_up :
         break;
      case none :
         if( msg.req_blocks.mode == none ) {
            peer_syncing_from_us = false;
         }
         if( !msg.req_trx.ids.empty() ) {
            peer_wlog( p2p_msg_log, this, "Invalid request_message, req_trx.mode=none, req_trx.ids.size ${s}", ("s", msg.req_trx.ids.size()) );
            close();
         }
         break;
      case normal :
         peer_wlog( p2p_msg_log, this, "Invalid request_message, req_trx.mode=normal" );
         close();
         break;
      default:;
      }
   }

   void connection::handle_message( const sync_request_message& msg ) {
      peer_dlog( p2p_blk_log, this, "peer requested ${start} to ${end}", ("start", msg.start_block)("end", msg.end_block) );
      if( msg.end_block == 0 ) {
         peer_requested.reset();
         flush_queues();
      } else {
         if (!is_blocks_connection()) {
            peer_dlog(p2p_conn_log, this, "received sync_request_message ${m} on transaction only connection, ignoring", ("m", msg));
            return;
         }

         if (peer_requested) {
            // This happens when peer already requested some range and sync is still in progress
            // It could be higher in case of peer requested head catchup and current request is lib catchup
            // So to make sure peer will receive all requested blocks we assign end_block to highest value
            peer_requested->end_block = std::max(msg.end_block, peer_requested->end_block);
         }
         else {
            peer_requested = peer_sync_state(msg.start_block, msg.end_block, msg.start_block-1, peer_sync_state::sync_t::peer_sync);
         }
         enqueue_sync_block();
      }
   }

   // called from connection strand
   void connection::handle_message( const vote_message_ptr& msg ) {
      last_vote_received = fc::time_point::now();
      peer_dlog(vote_logger, this, "received vote: block #${bn}:${id}.., ${v}, key ${k}..",
                ("bn", block_header::num_from_id(msg->block_id))("id", msg->block_id.str().substr(8,16))
                ("v", msg->strong ? "strong" : "weak")("k", msg->finalizer_key.to_string().substr(8, 16)));
      controller& cc = my_impl->chain_plug->chain();
      cc.process_vote_message(connection_id, msg);
   }

   // called from connection strand
   void connection::handle_message( const block_nack_message& msg ) {
      auto block_num = block_header::num_from_id(msg.id);

      if (block_num == 0) { // peer requested reset
         consecutive_blocks_nacks = 0;
         last_block_nack = msg.id;
         peer_dlog(p2p_blk_log, this, "received block nack reset");
         return;
      }

      latest_blk_time = std::chrono::steady_clock::now();
      auto fork_db_root_num = my_impl->get_fork_db_root_num();
      const bool before_lib = block_header::num_from_id(msg.id) <= fork_db_root_num;

      if (before_lib || my_impl->dispatcher.have_block(msg.id)) {
         if (block_num - 1 == block_header::num_from_id(last_block_nack)) {
            ++consecutive_blocks_nacks;
         } else {
            consecutive_blocks_nacks = 0;
         }
         if (!before_lib) {
            my_impl->dispatcher.add_peer_block(msg.id, connection_id);
         }
      }
      last_block_nack = msg.id;

      peer_dlog(p2p_blk_log, this, "received block nack #${bn}:${id}, consecutive ${c}", ("bn", block_num)("id", msg.id)("c", consecutive_blocks_nacks));
   }

   // called from connection strand
   void connection::handle_message( const block_notice_message& msg ) {
      if (block_header::num_from_id(msg.id)-1 != block_header::num_from_id(msg.previous)) {
         peer_dlog(p2p_blk_log, this, "Invalid block_notice_message ${id}, closing", ("id", msg.id));
         close();
         return;
      }

      auto fork_db_root_num = my_impl->get_fork_db_root_num();
      if (block_header::num_from_id(msg.id) <= fork_db_root_num)
         return;

      latest_blk_time = std::chrono::steady_clock::now();
      if (my_impl->dispatcher.have_block(msg.id)) {
         my_impl->dispatcher.add_peer_block(msg.id, connection_id);
      } else if (!my_impl->dispatcher.have_block(msg.previous)) { // still don't have previous block
         peer_dlog(p2p_blk_log, this, "Received unknown block notice, checking already requested");
         request_message req;
         req.req_blocks.mode = normal;
         req.req_blocks.ids.push_back(msg.previous);
         bool already_requested = my_impl->connections.any_of_block_connections([&req](const auto& c) {
            fc::lock_guard g_conn(c->conn_mtx);
            return c->last_block_nack_request_message_id == req.req_blocks.ids[0];
         });
         if (!already_requested) {
            peer_ilog(p2p_blk_log, this, "Received unknown block notice, requesting blocks from ${bn}",
                      ("bn", block_header::num_from_id(msg.previous)));
            block_id_type head_id = my_impl->get_chain_info().head_id;
            req.req_blocks.ids.push_back(head_id);
            send_block_nack({});
            {
               fc::lock_guard g_conn(conn_mtx);
               last_block_nack_request_message_id = req.req_blocks.ids[0];
            }
            enqueue(req);
         }
      }
   }

   // called from connection strand
   void connection::handle_message( const transaction_notice_message& msg ) {
      peer_dlog( p2p_trx_log, this, "received transaction_notice_message ${id}", ("id", msg.id) );
   }

   digest_type gossip_bp_peers_message::bp_peer::digest(const chain_id_type& chain_id) const {
      digest_type::encoder enc;
      fc::raw::pack(enc, chain_id);
      fc::raw::pack(enc, *this);
      return enc.result();
   }

   // called from connection strand
   void connection::handle_message( gossip_bp_peers_message& msg ) {
      if (!my_impl->bp_gossip_enabled())
         return;

      if (!my_impl->bp_gossip_initialized()) {
         bp_connection = bp_connection_type::bp_gossip_validating;
         peer_dlog(p2p_msg_log, this, "received gossip_bp_peers_message before bp gossip initialized");
         return;
      }

      const bool first_msg = msg.peers.size() == 1 && msg.peers[0].bp_peer_info.empty();
      if (!my_impl->validate_gossip_bp_peers_message(msg)) {
         peer_wlog( p2p_msg_log, this, "bad gossip_bp_peers_message, closing");
         no_retry = go_away_reason::fatal_other;
         enqueue( go_away_message( go_away_reason::fatal_other ) );
         return;
      }

      if (msg.peers.empty())
         return; // no current top producers in msg

      // valid gossip peer connection
      if (bp_connection != bp_connection_type::bp_gossip) {
         peer_dlog(p2p_msg_log, this, "bp gossip connection");
         bp_connection = bp_connection_type::bp_gossip;
      }

      if (first_msg) {
         // initial message case, send back our entire collection
         send_gossip_bp_peers_message();
      } else {
         bool diff = my_impl->update_gossip_bps(msg);
         if (diff) { // update, let all our peers know about it
            my_impl->connect_to_active_bp_peers();
            send_gossip_bp_peers_message_to_bp_peers();
         }
      }
   }

   // called from connection strand
   void connection::send_gossip_bp_peers_initial_message() {
      if (protocol_version < proto_version_t::gossip_bp_peers || !my_impl->bp_gossip_enabled())
         return;
      peer_dlog(p2p_msg_log, this, "sending initial gossip_bp_peers_message");
      const auto& sb = my_impl->get_gossip_bp_initial_send_buffer();
      if (sb) {
         enqueue_buffer(msg_type_t::gossip_bp_peers_message, {}, queued_buffer::queue_t::general, sb, go_away_reason::no_reason);
      } else {
         peer_ilog(p2p_msg_log, this, "no initial gossip_bp_peers_message to send");
      }
   }

   // called from connection strand
   void connection::send_gossip_bp_peers_message() {
      assert(my_impl->bp_gossip_enabled());
      gossip_buffer_factory factory;
      const send_buffer_type& sb = my_impl->get_gossip_bp_send_buffer(factory);
      peer_dlog(p2p_msg_log, this, "sending gossip_bp_peers_message");
      enqueue_buffer(msg_type_t::gossip_bp_peers_message, {}, queued_buffer::queue_t::general, sb, go_away_reason::no_reason);
   }

   void connection::send_gossip_bp_peers_message_to_bp_peers() {
      assert(my_impl->bp_gossip_enabled());
      my_impl->connections.for_each_connection([](const connection_ptr& c) {
         gossip_buffer_factory factory;
         if (c->protocol_version >= proto_version_t::gossip_bp_peers && c->socket_is_open()) {
            if (c->bp_connection == bp_connection_type::bp_gossip) {
               const send_buffer_type& sb = my_impl->get_gossip_bp_send_buffer(factory);
               boost::asio::post(c->strand, [sb, c]() {
                  peer_dlog(p2p_msg_log, c, "sending gossip_bp_peers_message");
                  c->enqueue_buffer(msg_type_t::gossip_bp_peers_message, {}, queued_buffer::queue_t::general, sb, go_away_reason::no_reason);
               });
            } else if (c->bp_connection == bp_connection_type::bp_config || c->bp_connection == bp_connection_type::bp_gossip_validating) {
               boost::asio::post(c->strand, [c]() {
                  c->send_gossip_bp_peers_initial_message();
               });
            }
         }
      });
   }

   size_t calc_trx_size( const packed_transaction_ptr& trx ) {
      return trx->get_estimated_size();
   }

   // called from connection strand
   void connection::handle_message( const packed_transaction_ptr& trx ) {
      size_t trx_size = calc_trx_size( trx );
      trx_in_progress_size += trx_size;
      my_impl->chain_plug->accept_transaction( trx,
         [weak = weak_from_this(), trx_size](const next_function_variant<transaction_trace_ptr>& result) mutable {
         // next (this lambda) called from application thread
         if (std::holds_alternative<fc::exception_ptr>(result)) {
            fc_dlog( p2p_trx_log, "bad packed_transaction : ${m}", ("m", std::get<fc::exception_ptr>(result)->what()) );
         } else {
            const transaction_trace_ptr& trace = std::get<transaction_trace_ptr>(result);
            if( !trace->except ) {
               fc_dlog( p2p_trx_log, "chain accepted transaction, bcast ${id}", ("id", trace->id) );
            } else {
               fc_ilog( p2p_trx_log, "bad packed_transaction : ${m}", ("m", trace->except->what()));
            }
         }
         connection_ptr conn = weak.lock();
         if( conn ) {
            conn->trx_in_progress_size -= trx_size;
         }
      });
   }

   // called from connection strand
   void connection::handle_message( const block_id_type& id, signed_block_ptr ptr ) {
      // post to dispatcher strand so that we don't have multiple threads validating the block header
      peer_dlog(p2p_blk_log, this, "posting block ${n} to dispatcher strand", ("n", ptr->block_num()));
      my_impl->dispatcher.strand.dispatch([id, c{shared_from_this()}, ptr{std::move(ptr)}, cid=connection_id]() mutable {
         if (app().is_quiting()) // large sync span can have many of these queued up, exit quickly
            return;
         controller& cc = my_impl->chain_plug->chain();

         // proper_svnn_block_seen is for integration tests that verify low number of `unlinkable_blocks` logs.
         // Because we now process blocks immediately into the fork database, during savanna transition the first proper
         // savanna block will be reported as unlinkable when lib syncing. We will request that block again and by then
         // the main thread will have finished transitioning and will be linkable. This is a bit of a hack but seems
         // like an okay compromise for a condition, outside of testing, will rarely happen.
         static bool proper_svnn_block_seen = false;

         std::optional<block_handle> obh;
         bool exception = false;
         fork_db_add_t fork_db_add_result = fork_db_add_t::failure;
         bool unlinkable = false;
         sync_manager::closing_mode close_mode = sync_manager::closing_mode::immediately;
         try {
            if (cc.is_producer_node()) {
               EOS_ASSERT(ptr->timestamp < (fc::time_point::now() + def_allowed_clock_skew), block_from_the_future,
                          "received a block from the future, rejecting it: ${id}", ("id", id));
            }
            // this will return empty optional<block_handle> if block is not linkable
            controller::accepted_block_result abh = cc.accept_block( id, ptr );
            fork_db_add_result = abh.add_result;
            obh = std::move(abh.block);
            unlinkable = fork_db_add_result == fork_db_add_t::failure;
            close_mode = sync_manager::closing_mode::handshake;
         } catch( const invalid_qc_claim& ex) {
            exception = true;
            fc_wlog( p2p_blk_log, "invalid QC claim exception, connection - ${cid}: #${n} ${id}...: ${m}",
                     ("cid", cid)("n", ptr->block_num())("id", id.str().substr(8,16))("m",ex.to_string()));
         } catch( const fc::exception& ex ) {
            exception = true;
            fc_ilog( p2p_blk_log, "bad block exception connection - ${cid}: #${n} ${id}...: ${m}",
                     ("cid", cid)("n", ptr->block_num())("id", id.str().substr(8,16))("m",ex.to_string()));
         } catch( ... ) {
            exception = true;
            fc_wlog( p2p_blk_log, "bad block connection - ${cid}: #${n} ${id}...: unknown exception",
                     ("cid", cid)("n", ptr->block_num())("id", id.str().substr(8,16)));
         }
         if( exception || unlinkable) {
            const bool first_proper_svnn_block = !proper_svnn_block_seen && ptr->is_proper_svnn_block();
            if (unlinkable && !first_proper_svnn_block) {
               fc_dlog(p2p_blk_log, "unlinkable_block ${bn} : ${id}, previous ${pn} : ${pid}",
                       ("bn", ptr->block_num())("id", id)("pn", block_header::num_from_id(ptr->previous))("pid", ptr->previous));
            }
            boost::asio::post(c->strand, [c, id, blk_num=ptr->block_num(), close_mode]() {
               peer_dlog( p2p_blk_log, c, "rejected block ${bn} ${id}", ("bn", blk_num)("id", id) );
               my_impl->sync_master->rejected_block( c, blk_num, close_mode );
            });
            return;
         }

         assert(obh);
         uint32_t block_num = obh->block_num();
         proper_svnn_block_seen = obh->header().is_proper_svnn_block();

         fc_dlog( p2p_blk_log, "validated block header, forkdb add ${bt}, broadcasting immediately, connection - ${cid}, blk num = ${num}, id = ${id}",
                  ("bt", fork_db_add_result)("cid", cid)("num", block_num)("id", obh->id()) );
         my_impl->dispatcher.add_peer_block( obh->id(), cid ); // no need to send back to sender
         c->block_status_monitor_.accepted();

         if (my_impl->chain_plug->chain().get_read_mode() == db_read_mode::IRREVERSIBLE) {
            // non-irreversible notifies sync_manager when block is applied, call on dispatcher strand
            const fc::microseconds age(fc::time_point::now() - obh->timestamp());
            my_impl->sync_master->sync_recv_block(connection_ptr{}, obh->id(), obh->block_num(), age);
         }

         if (fork_db_add_result == fork_db_add_t::appended_to_head || fork_db_add_result == fork_db_add_t::fork_switch) {
            ++c->unique_blocks_rcvd_count;

            // ready to process immediately, so signal producer to interrupt start_block
            // call before process_blocks to avoid interrupting process_blocks
            my_impl->producer_plug->received_block(block_num, fork_db_add_result);

            fc_dlog(p2p_blk_log, "post process_incoming_block to app thread, block ${n}", ("n", ptr->block_num()));
            my_impl->producer_plug->process_blocks();
         }
         my_impl->dispatcher.bcast_block( obh->block(), obh->id() );
      });
   }

   // thread safe
   void net_plugin_impl::start_expire_timer() {
      fc::lock_guard g( expire_timer_mtx );
      expire_timer.expires_from_now( expire_timer_period);
      expire_timer.async_wait( [my = shared_from_this()]( boost::system::error_code ec ) {
         if( !ec ) {
            my->expire();
         }
      } );
   }

   // thread safe
   void net_plugin_impl::ticker() {
      fc::lock_guard g( keepalive_timer_mtx );
      keepalive_timer.expires_from_now(keepalive_interval);
      keepalive_timer.async_wait( [my = shared_from_this()]( boost::system::error_code ec ) {
            my->ticker();
            if( ec ) {
               return;
            }

            auto current_time = std::chrono::steady_clock::now();
            my->connections.for_each_connection( [current_time]( const connection_ptr& c ) {
               if( c->socket_is_open() ) {
                  boost::asio::post(c->strand, [c, current_time]() {
                     c->check_heartbeat(current_time);
                  } );
               }
            } );
         } );
   }

   void net_plugin_impl::start_monitors() {
      connections.start_conn_timers();
      start_expire_timer();
   }

   void net_plugin_impl::expire() {
      auto now = time_point::now();
      uint32_t fork_db_root_num = get_fork_db_root_num();
      dispatcher.expire_blocks( fork_db_root_num );
      dispatcher.expire_txns();
      if (expire_gossip_bp_peers()) {
         update_bp_producer_peers();
         connection::send_gossip_bp_peers_message_to_bp_peers();
      }
      fc_dlog( p2p_log, "expire run time ${n}us", ("n", time_point::now() - now) );

      start_expire_timer();
   }

   // called from application thread
   void net_plugin_impl::on_accepted_block_header(const signed_block_ptr& block, const block_id_type& id) {
      fc_dlog(p2p_blk_log, "on_accepted_block_header ${bn} ${id}", ("bn", block->block_num())("id", id));
      update_chain_info();

      boost::asio::post( thread_pool.get_executor(), [block, id, this]() {
         fc_dlog(p2p_blk_log, "signaled accepted_block_header, blk num = ${num}, id = ${id}", ("num", block->block_num())("id", id));
         dispatcher.bcast_block(block, id);
      });
   }

   void net_plugin_impl::on_accepted_block( const signed_block_ptr& block, const block_id_type& id) {
      fc_dlog(p2p_blk_log, "on_accepted_block ${bn} ${id}", ("bn", block->block_num())("id", id));
      update_chain_info();

      if (chain_plug->chain().get_read_mode() != db_read_mode::IRREVERSIBLE) {
         // irreversible notifies sync_manager when added to fork_db, non-irreversible notifies when applied
         dispatcher.strand.post([sync_master = sync_master.get(), block, id]() {
            const fc::microseconds age(fc::time_point::now() - block->timestamp);
            sync_master->sync_recv_block(connection_ptr{}, id, block->block_num(), age);
         });
      }

      sync_master->send_handshakes_if_synced(fc::time_point::now() - block->timestamp);
      if (const auto* pending_producers = chain_plug->chain().pending_producers()) {
         on_pending_schedule(*pending_producers);
      }
      on_active_schedule(chain_plug->chain().active_producers());

      // update peer public keys from chainbase db
      chain::controller& cc = chain_plug->chain();
      if (cc.configured_peer_keys_updated()) {
         boost::asio::post(thread_pool.get_executor(), [this]() {
            try {
               update_bp_producer_peers();
               my_impl->connect_to_active_bp_peers();
               connection::send_gossip_bp_peers_message_to_bp_peers();
            } catch (fc::exception& e) {
               fc_elog( p2p_log, "Unable to update bp producer peers, error: ${e}", ("e", e.to_detail_string()));
            }
         });
      }
   }

   // called from application thread
   void net_plugin_impl::on_irreversible_block( const signed_block_ptr& block, const block_id_type& id ) {
      fc_dlog( p2p_blk_log, "on_irreversible_block, blk num = ${num}, id = ${id}", ("num", block->block_num())("id", id) );
      update_chain_info(id);

      chain::controller& cc = chain_plug->chain();
      if (cc.get_read_mode() == db_read_mode::IRREVERSIBLE) {
         // irreversible notifies sync_manager when added to fork_db, non-irreversible notifies when applied
         dispatcher.strand.post([sync_master = sync_master.get(), block, id]() {
            const fc::microseconds age(fc::time_point::now() - block->timestamp);
            sync_master->sync_recv_block(connection_ptr{}, id, block->block_num(), age);
         });
      }
   }

   // called from other threads including net threads
   void net_plugin_impl::broadcast_vote_message(connection_id_t connection_id, vote_result_t status,
                                                const vote_message_ptr& msg,
                                                const finalizer_authority_ptr& active_auth,
                                                const finalizer_authority_ptr& pending_auth) {
      auto get_desc = [&]() -> std::string {
         if (active_auth)
            return active_auth->description;
         if (pending_auth)
            return pending_auth->description;
         return std::string{"unknown"};
      };

      fc_dlog(vote_logger, "connection - ${c} on voted signal: ${s} block #${bn} ${id}.., ${t}, ${d}, key ${k}..",
                ("c", connection_id)("s", status)("bn", block_header::num_from_id(msg->block_id))
                ("id", msg->block_id.str().substr(8,16))("t", msg->strong ? "strong" : "weak")
                ("d", get_desc())("k", msg->finalizer_key.to_string().substr(8, 16)));

      switch( status ) {
      case vote_result_t::success:
         bcast_vote_message(connection_id, msg);
         break;
      case vote_result_t::unknown_public_key:
      case vote_result_t::invalid_signature:
      case vote_result_t::max_exceeded:  // close peer immediately
         fc_elog(vote_logger, "Invalid vote(s), closing connection - ${c}", ("c", connection_id));
         connections.any_of_connections([connection_id](const connection_ptr& c) {
            if (c->connection_id == connection_id) {
               c->close( false );
               return true;
            }
            return false;
         });
         break;
      case vote_result_t::unknown_block: // track the failure
         fc_dlog(vote_logger, "connection - ${c} vote unknown block #${bn}:${id}..",
                 ("c", connection_id)("bn", block_header::num_from_id(msg->block_id))("id", msg->block_id.str().substr(8,16)));
         connections.any_of_connections([connection_id](const connection_ptr& c) {
            if (c->connection_id == connection_id) {
               boost::asio::post(c->strand, [c]() {
                  c->block_status_monitor_.rejected();
               });
               return true;
            }
            return false;
         });
         break;
      case vote_result_t::duplicate: // do nothing
         break;
      default:
         assert(false); // should never happen
      }
   }

   void net_plugin_impl::bcast_vote_message( uint32_t exclude_peer, const chain::vote_message_ptr& msg ) {
      if (sync_master->syncing_from_peer())
         return;

      fc_dlog(vote_logger, "bcast ${t} vote: block #${bn} ${id}.., ${v}, key ${k}..",
                ("t", exclude_peer ? "received" : "our")("bn", block_header::num_from_id(msg->block_id))("id", msg->block_id.str().substr(8,16))
                ("v", msg->strong ? "strong" : "weak")("k", msg->finalizer_key.to_string().substr(8,16)));

      boost::asio::post( thread_pool.get_executor(), [exclude_peer, msg, this]() mutable {
            buffer_factory buff_factory;
            const auto& send_buffer = buff_factory.get_send_buffer( *msg );

            dispatcher.bcast_vote_msg( exclude_peer, send_buffer );
      });
   }

   // called from application thread
   void net_plugin_impl::transaction_ack(const std::pair<fc::exception_ptr, packed_transaction_ptr>& results) {
      boost::asio::post( thread_pool.get_executor(), [this, results]() {
         const auto& id = results.second->id();
         if (results.first) {
            fc_dlog( p2p_trx_log, "signaled NACK, trx-id = ${id} : ${why}", ("id", id)( "why", results.first->to_detail_string() ) );
            dispatcher.rejected_transaction(results.second);
         } else {
            fc_dlog( p2p_trx_log, "signaled ACK, trx-id = ${id}", ("id", id) );
            dispatcher.bcast_transaction(results.second);
         }
      });
   }

   bool net_plugin_impl::authenticate_peer(const handshake_message& msg) const {
      if(allowed_connections == None)
         return false;

      if(allowed_connections == Any)
         return true;

      if(allowed_connections & (Producers | Specified)) {
         auto allowed_it = std::find(allowed_peers.begin(), allowed_peers.end(), msg.key);
         auto private_it = private_keys.find(msg.key);
         bool found_producer_key = false;
         if(producer_plug != nullptr)
            found_producer_key = producer_plug->is_producer_key(msg.key);
         if( allowed_it == allowed_peers.end() && private_it == private_keys.end() && !found_producer_key) {
            fc_wlog( p2p_conn_log, "Peer ${peer} sent a handshake with an unauthorized key: ${key}.",
                     ("peer", msg.p2p_address)("key", msg.key) );
            return false;
         }
      }

      if(msg.sig != chain::signature_type() && msg.token != sha256()) {
         sha256 hash = fc::sha256::hash(msg.time);
         if(hash != msg.token) {
            fc_wlog( p2p_conn_log, "Peer ${peer} sent a handshake with an invalid token.", ("peer", msg.p2p_address) );
            return false;
         }
         chain::public_key_type peer_key;
         try {
            peer_key = crypto::public_key(msg.sig, msg.token, true);
         }
         catch (const std::exception& /*e*/) {
            fc_wlog( p2p_conn_log, "Peer ${peer} sent a handshake with an unrecoverable key.", ("peer", msg.p2p_address) );
            return false;
         }
         if((allowed_connections & (Producers | Specified)) && peer_key != msg.key) {
            fc_wlog( p2p_conn_log, "Peer ${peer} sent a handshake with an unauthenticated key.", ("peer", msg.p2p_address) );
            return false;
         }
      }
      else if(allowed_connections & (Producers | Specified)) {
         fc_dlog( p2p_conn_log, "Peer sent a handshake with blank signature and token, but this node accepts only authenticated connections." );
         return false;
      }
      return true;
   }

   chain::public_key_type net_plugin_impl::get_authentication_key() const {
      if(!private_keys.empty())
         return private_keys.begin()->first;
      return {};
   }

   chain::signature_type net_plugin_impl::sign_compact(const chain::public_key_type& signer, const fc::sha256& digest) const
   {
      auto private_key_itr = private_keys.find(signer);
      if(private_key_itr != private_keys.end())
         return private_key_itr->second.sign(digest);
      if(producer_plug != nullptr && producer_plug->get_state() == abstract_plugin::started)
         return producer_plug->sign_compact(signer, digest);
      return {};
   }

   // call from connection strand
   bool connection::populate_handshake( handshake_message& hello ) const {
      namespace sc = std::chrono;
      auto chain_info = my_impl->get_chain_info();
      auto now = sc::duration_cast<sc::nanoseconds>(sc::system_clock::now().time_since_epoch()).count();
      constexpr int64_t hs_delay = sc::duration_cast<sc::nanoseconds>(sc::milliseconds(50)).count();
      // nothing as changed since last handshake and one was sent recently, so skip sending
      if (chain_info.fork_db_head_id == hello.fork_db_head_id && (hello.time + hs_delay > now))
         return false;
      hello.network_version = net_version_base + static_cast<uint16_t>(net_version);
      hello.fork_db_root_num = chain_info.fork_db_root_num;
      hello.fork_db_root_id = chain_info.fork_db_root_id;
      hello.fork_db_head_num = chain_info.fork_db_head_num;
      hello.fork_db_head_id = chain_info.fork_db_head_id;
      hello.chain_id = my_impl->chain_id;
      hello.node_id = my_impl->node_id;
      hello.key = my_impl->get_authentication_key();
      hello.time = sc::duration_cast<sc::nanoseconds>(sc::system_clock::now().time_since_epoch()).count();
      hello.token = fc::sha256::hash(hello.time);
      hello.sig = my_impl->sign_compact(hello.key, hello.token);
      // If we couldn't sign, don't send a token.
      if(hello.sig == chain::signature_type())
         hello.token = sha256();
      hello.p2p_address = listen_address;
      if (incoming()) {
         if( is_transactions_only_connection() && hello.p2p_address.find(":trx") == std::string::npos ) hello.p2p_address += ":trx";
         // if we are not accepting transactions tell peer we are blocks only
         if( is_blocks_only_connection() || !my_impl->p2p_accept_transactions )
            if (hello.p2p_address.find(":blk") == std::string::npos)
               hello.p2p_address += ":blk";
         if( !is_blocks_only_connection() && !my_impl->p2p_accept_transactions ) {
            peer_dlog( p2p_msg_log, this, "p2p-accept-transactions=false inform peer blocks only connection ${a}", ("a", hello.p2p_address) );
         }
      }
      hello.p2p_address += " - " + hello.node_id.str().substr(0,7);
#if defined( __APPLE__ )
      hello.os = "osx";
#elif defined( __linux__ )
      hello.os = "linux";
#elif defined( _WIN32 )
      hello.os = "win32";
#else
      hello.os = "other";
#endif
      hello.agent = my_impl->user_agent_name;

      return true;
   }

   net_plugin::net_plugin()
      :my( new net_plugin_impl ) {
      my_impl = my.get();
   }

   net_plugin::~net_plugin() = default;

   void net_plugin::set_program_options( options_description& /*cli*/, options_description& cfg )
   {
      cfg.add_options()
         ( "p2p-listen-endpoint", bpo::value< vector<string> >()->default_value( vector<string>(1, string("0.0.0.0:9876:0")) ),
           "The actual host:port[:trx|:blk][:<rate-cap>] used to listen for incoming p2p connections. May be used multiple times."
           " The optional rate cap will limit per connection block sync bandwidth to the specified rate. Total"
           " allowed bandwidth is the rate-cap multiplied by the connection count limit. A number alone will be"
           " interpreted as bytes per second. The number may be suffixed with units. Supported units are:"
           " 'B/s', 'KB/s', 'MB/s, 'GB/s', 'TB/s', 'KiB/s', 'MiB/s', 'GiB/s', 'TiB/s'."
           " Transactions and blocks outside sync mode are not throttled."
           " The optional 'trx' and 'blk' indicates to peers that only transactions 'trx' or blocks 'blk' should be sent."
           " Examples:\n"
           "   192.168.0.100:9875\n"
           "   192.168.0.101:9876:1MiB/s\n"
           "   node.eos.io:9877:trx:1512KB/s\n"
           "   node.eos.io:9879:0.5GB/s\n"
           "   [2001:db8:85a3:8d3:1319:8a2e:370:7348]:9879:250KB/s")
         ( "p2p-server-address", bpo::value< vector<string> >(),
           "An externally accessible host:port for identifying this node. Defaults to p2p-listen-endpoint."
           " May be used as many times as p2p-listen-endpoint."
           " If provided, the first address will be used in handshakes with other nodes; otherwise the default is used.")
         ( "p2p-peer-address", bpo::value< vector<string> >()->composing(),
           "The public endpoint of a peer node to connect to. Use multiple p2p-peer-address options as needed to compose a network.\n"
           " Syntax: host:port[:trx|:blk]\n"
           " The optional 'trx' and 'blk' indicates to node that only transactions 'trx' or blocks 'blk' should be sent."
           " Examples:\n"
           "   p2p.eos.io:9876\n"
           "   p2p.trx.eos.io:9876:trx\n"
           "   p2p.blk.eos.io:9876:blk\n")
         ( "p2p-max-nodes-per-host", bpo::value<int>()->default_value(def_max_nodes_per_host), "Maximum number of client nodes from any single IP address")
         ( "p2p-accept-transactions", bpo::value<bool>()->default_value(true), "Allow transactions received over p2p network to be evaluated and relayed if valid.")
         ( "p2p-disable-block-nack", bpo::value<bool>()->default_value(false),
            "Disable block notice and block nack. All blocks received will be broadcast to all peers unless already received.")
         ( "p2p-auto-bp-peer", bpo::value< vector<string> >()->composing(),
           "The account and public p2p endpoint of a block producer node to automatically connect to when it is in producer schedule. Not gossipped.\n"
           "  Syntax: bp_account,host:port\n"
           "  Example:\n"
           "    producer1,p2p.prod.io:9876\n"
           "    producer2,p2p.trx.myprod.io:9876:trx\n"
           "    producer3,p2p.blk.example.io:9876:blk\n")
         ("p2p-bp-gossip-endpoint", boost::program_options::value<vector<string>>()->composing()->multitoken(),
           "The BP account, inbound connection endpoint, outbound connection IP address. "
           "The BP account is the producer name. Used to retrieve peer-key from on-chain peerkeys table registered on-chain via regpeerkey action. "
           "The inbound connection endpoint is typically the listen endpoint of this node. "
           "The outbound connection IP address is typically the IP address of this node. Peer will use this value to allow access through firewall. "
           "Private key of peer-key should be configured via signature-provider.\n"
           " Syntax: bp_account,inbound_endpoint,outbound_ip_address\n"
           " Example:\n"
           "   myprod,myhostname.com:9876,198.51.100.1\n"
           "   myprod,myhostname2.com:9876,[2001:0db8:85a3:0000:0000:8a2e:0370:7334]"
           )
         ( "agent-name", bpo::value<string>()->default_value("Spring Agent"), "The name supplied to identify this node amongst the peers.")
         ( "allowed-connection", bpo::value<vector<string>>()->multitoken()->default_value({"any"}, "any"), "Can be 'any' or 'producers' or 'specified' or 'none'. If 'specified', peer-key must be specified at least once. If only 'producers', peer-key is not required. 'producers' and 'specified' may be combined.")
         ( "peer-key", bpo::value<vector<string>>()->composing()->multitoken(), "Optional public key of peer allowed to connect.  May be used multiple times.")
         ( "peer-private-key", bpo::value<vector<string>>()->composing()->multitoken(),
           "Tuple of [PublicKey, WIF private key] (may specify multiple times)")
         ( "max-clients", bpo::value<uint32_t>()->default_value(def_max_clients), "Maximum number of clients from which connections are accepted, use 0 for no limit")
         ( "connection-cleanup-period", bpo::value<int>()->default_value(def_conn_retry_wait), "number of seconds to wait before cleaning up dead connections")
         ( "max-cleanup-time-msec", bpo::value<uint32_t>()->default_value(10), "max connection cleanup time per cleanup call in milliseconds")
         ( "p2p-dedup-cache-expire-time-sec", bpo::value<uint32_t>()->default_value(10), "Maximum time to track transaction for duplicate optimization")
         ( "net-threads", bpo::value<uint16_t>()->default_value(my->thread_pool_size),
           "Number of worker threads in net_plugin thread pool" )
         ( "sync-fetch-span", bpo::value<uint32_t>()->default_value(def_sync_fetch_span),
           "Number of blocks to retrieve in a chunk from any individual peer during synchronization")
         ( "sync-peer-limit", bpo::value<uint32_t>()->default_value(3),
           "Number of peers to sync from")
         ( "use-socket-read-watermark", bpo::value<bool>()->default_value(false), "Enable experimental socket read watermark optimization")
         ( "peer-log-format", bpo::value<string>()->default_value( "[\"${_peer}\" - ${_cid} ${_ip}:${_port}] " ),
           "The string used to format peers when logging messages about them.  Variables are escaped with ${<variable name>}.\n"
           "Available Variables:\n"
           "   _peer  \tendpoint name\n\n"
           "   _name  \tself-reported name\n\n"
           "   _cid   \tassigned connection id\n\n"
           "   _id    \tself-reported ID (64 hex characters)\n\n"
           "   _sid   \tfirst 8 characters of _peer.id\n\n"
           "   _ip    \tremote IP address of peer\n\n"
           "   _port  \tremote port number of peer\n\n"
           "   _lip   \tlocal IP address connected to peer\n\n"
           "   _lport \tlocal port number connected to peer\n\n"
           "   _agent \tfirst 15 characters of agent-name of peer\n\n"
           "   _nver  \tp2p protocol version\n\n")
         ( "p2p-keepalive-interval-ms", bpo::value<int>()->default_value(def_keepalive_interval), "peer heartbeat keepalive message interval in milliseconds")

        ;
   }

   template<typename T>
   T dejsonify(const string& s) {
      return fc::json::from_string(s).as<T>();
   }

   void net_plugin_impl::plugin_initialize( const variables_map& options ) {
      try {
         fc_ilog( p2p_log, "Initialize net plugin" );

         chain_plug = app().find_plugin<chain_plugin>();
         EOS_ASSERT( chain_plug, chain::missing_chain_plugin_exception, ""  );

         peer_log_format = options.at( "peer-log-format" ).as<string>();

         expire_timer_period = def_expire_timer_wait;
         p2p_dedup_cache_expire_time_us = fc::seconds( options.at( "p2p-dedup-cache-expire-time-sec" ).as<uint32_t>() );
         resp_expected_period = def_resp_expected_wait;
         max_nodes_per_host = options.at( "p2p-max-nodes-per-host" ).as<int>();
         p2p_accept_transactions = options.at( "p2p-accept-transactions" ).as<bool>();
         p2p_disable_block_nack = options.at( "p2p-disable-block-nack" ).as<bool>();

         use_socket_read_watermark = options.at( "use-socket-read-watermark" ).as<bool>();
         keepalive_interval = std::chrono::milliseconds( options.at( "p2p-keepalive-interval-ms" ).as<int>() );
         EOS_ASSERT( keepalive_interval.count() > 0, chain::plugin_config_exception,
                     "p2p-keepalive_interval-ms must be greater than 0" );

         // To avoid unnecessary transitions between LIB <-> head catchups,
         // min_blocks_distance between fork_db_root and head must be reached.
         // Set it to the number of blocks produced during half of keep alive
         // interval.
         const uint32_t min_blocks_distance = (keepalive_interval.count() / config::block_interval_ms) / 2;
         sync_master = std::make_unique<sync_manager>(
             options.at( "sync-fetch-span" ).as<uint32_t>(),
             options.at( "sync-peer-limit" ).as<uint32_t>(),
             min_blocks_distance);

         connections.init( std::chrono::milliseconds( options.at("p2p-keepalive-interval-ms").as<int>() * 2 ),
                               fc::milliseconds( options.at("max-cleanup-time-msec").as<uint32_t>() ),
                               std::chrono::seconds( options.at("connection-cleanup-period").as<int>() ),
                               options.at("max-clients").as<uint32_t>() );

         if( options.count( "p2p-listen-endpoint" )) {
            auto p2ps =  options.at("p2p-listen-endpoint").as<vector<string>>();
            if (!p2ps.front().empty()) { // "" for p2p-listen-endpoint means to not listen
               p2p_addresses = p2ps;
               auto addr_count = p2p_addresses.size();
               std::sort(p2p_addresses.begin(), p2p_addresses.end());
               auto last = std::unique(p2p_addresses.begin(), p2p_addresses.end());
               p2p_addresses.erase(last, p2p_addresses.end());
               if( size_t addr_diff = addr_count - p2p_addresses.size(); addr_diff != 0) {
                  fc_wlog( p2p_conn_log, "Removed ${count} duplicate p2p-listen-endpoint entries", ("count", addr_diff));
               }
               for( const auto& addr : p2p_addresses ) {
                  EOS_ASSERT( addr.length() <= net_utils::max_p2p_address_length, chain::plugin_config_exception,
                              "p2p-listen-endpoint ${a} too long, must be less than ${m}", 
                              ("a", addr)("m", net_utils::max_p2p_address_length) );
                  const auto& [host, port, type] = net_utils::split_host_port_type(addr);
                  EOS_ASSERT( !host.empty() && !port.empty(), chain::plugin_config_exception,
                              "Invalid p2p-listen-endpoint ${p}, syntax host:port:[trx|blk]", ("p", addr));
               }
            }
         }
         if( options.count( "p2p-server-address" ) ) {
            p2p_server_addresses = options.at( "p2p-server-address" ).as<vector<string>>();
            EOS_ASSERT( p2p_server_addresses.size() <= p2p_addresses.size(), chain::plugin_config_exception,
                        "p2p-server-address may not be specified more times than p2p-listen-endpoint" );
            for( const auto& addr: p2p_server_addresses ) {
               EOS_ASSERT( addr.length() <= net_utils::max_p2p_address_length, chain::plugin_config_exception,
                           "p2p-server-address ${a} too long, must be less than ${m}", 
                           ("a", addr)("m", net_utils::max_p2p_address_length) );
               const auto& [host, port, type] = net_utils::split_host_port_type(addr);
               EOS_ASSERT( !host.empty() && !port.empty(), chain::plugin_config_exception,
                           "Invalid p2p-server-address ${p}, syntax host:port:[trx|blk]", ("p", addr));
            }
         }
         p2p_server_addresses.resize(p2p_addresses.size()); // extend with empty entries as needed

         thread_pool_size = options.at( "net-threads" ).as<uint16_t>();
         EOS_ASSERT( thread_pool_size > 0, chain::plugin_config_exception,
                     "net-threads ${num} must be greater than 0", ("num", thread_pool_size) );

         std::vector<std::string> peers;
         if( options.count( "p2p-peer-address" )) {
            peers = options.at( "p2p-peer-address" ).as<vector<string>>();
            for (const auto& peer : peers) {
               const auto& [host, port, type] = net_utils::split_host_port_type(peer);
               EOS_ASSERT( !host.empty() && !port.empty(), chain::plugin_config_exception,
                           "Invalid p2p-peer-address ${p}, syntax host:port:[trx|blk]", ("p", peer));
            }
            connections.add_supplied_peers(peers);
         }
         if( options.count( "agent-name" )) {
            user_agent_name = options.at( "agent-name" ).as<string>();
            EOS_ASSERT( user_agent_name.length() <= net_utils::max_handshake_str_length, chain::plugin_config_exception,
                        "agent-name too long, must be less than ${m}", ("m", net_utils::max_handshake_str_length) );
         }

         if (options.count( "p2p-auto-bp-peer")) {
            set_configured_bp_peers(options.at( "p2p-auto-bp-peer" ).as<vector<string>>(), peers);
         }

         if ( options.count( "p2p-bp-gossip-endpoint" ) ) {
            set_bp_producer_peers(options.at( "p2p-bp-gossip-endpoint" ).as<vector<string>>());
            EOS_ASSERT(options.count("signature-provider"), chain::plugin_config_exception,
                       "signature-provider of associated key required for p2p-bp-gossip-endpoint");
         }

         if( options.count( "allowed-connection" )) {
            const std::vector<std::string> allowed_remotes = options["allowed-connection"].as<std::vector<std::string>>();
            for( const std::string& allowed_remote : allowed_remotes ) {
               if( allowed_remote == "any" )
                  allowed_connections |= net_plugin_impl::Any;
               else if( allowed_remote == "producers" )
                  allowed_connections |= net_plugin_impl::Producers;
               else if( allowed_remote == "specified" )
                  allowed_connections |= net_plugin_impl::Specified;
               else if( allowed_remote == "none" )
                  allowed_connections = net_plugin_impl::None;
            }
         }

         if( allowed_connections & net_plugin_impl::Specified )
            EOS_ASSERT( options.count( "peer-key" ),
                        plugin_config_exception,
                       "At least one peer-key must accompany 'allowed-connection=specified'" );

         if( options.count( "peer-key" )) {
            const std::vector<std::string> key_strings = options["peer-key"].as<std::vector<std::string>>();
            for( const std::string& key_string : key_strings ) {
               allowed_peers.push_back( dejsonify<chain::public_key_type>( key_string ));
            }
         }

         if( options.count( "peer-private-key" )) {
            const std::vector<std::string> key_id_to_wif_pair_strings = options["peer-private-key"].as<std::vector<std::string>>();
            for( const std::string& key_id_to_wif_pair_string : key_id_to_wif_pair_strings ) {
               auto key_id_to_wif_pair = dejsonify<std::pair<chain::public_key_type, std::string>>(
                     key_id_to_wif_pair_string );
               private_keys[key_id_to_wif_pair.first] = fc::crypto::private_key( key_id_to_wif_pair.second );
            }
         }

         chain_id = chain_plug->get_chain_id();
         fc::rand_pseudo_bytes( node_id.data(), node_id.data_size());

         if( p2p_accept_transactions ) {
            chain_plug->enable_accept_transactions();
         }

      } FC_LOG_AND_RETHROW()
   }

   void net_plugin_impl::plugin_startup() {
      fc_ilog( p2p_conn_log, "my node_id is ${id}", ("id", node_id ));

      producer_plug = app().find_plugin<producer_plugin>();
      assert(producer_plug);

      thread_pool.start( thread_pool_size, []( const fc::exception& e ) {
         elog("Exception in net thread, exiting: ${e}", ("e", e.to_detail_string()));
         app().quit();
      } );

      if( !p2p_accept_transactions && p2p_addresses.size() ) {
         fc_ilog( p2p_log, "\n"
               "***********************************\n"
               "* p2p-accept-transactions = false *\n"
               "*    Transactions not forwarded   *\n"
               "***********************************\n" );
      }

      p2p_accept_votes = chain_plug->accept_votes();

      std::vector<string> listen_addresses = p2p_addresses;

      assert( p2p_addresses.size() == p2p_server_addresses.size() );
      std::transform(p2p_addresses.begin(), p2p_addresses.end(), p2p_server_addresses.begin(), 
                     p2p_addresses.begin(), [](const string& p2p_address, const string& p2p_server_address) {
         if( !p2p_server_address.empty() ) {
            return p2p_server_address;
         }

         const auto& [host, port, type] = net_utils::split_host_port_type(p2p_address);
         if( host.empty() || host == "0.0.0.0" || host == "[::]") {
            boost::system::error_code ec;
            auto hostname = host_name( ec );
            if( ec.value() != boost::system::errc::success ) {

               FC_THROW_EXCEPTION( fc::invalid_arg_exception,
                                    "Unable to retrieve host_name. ${msg}", ("msg", ec.message()));

            }
            return hostname + ":" + port + (type.empty() ? "" : ":" + type);
         }
         return p2p_address;
      });

      {
         chain::controller& cc = chain_plug->chain();
         cc.accepted_block_header().connect( [my = shared_from_this()]( const block_signal_params& t ) {
            const auto& [ block, id ] = t;
            my->on_accepted_block_header( block, id );
         } );

         cc.accepted_block().connect( [my = shared_from_this()]( const block_signal_params& t ) {
            const auto& [ block, id ] = t;
            my->on_accepted_block(block, id);
         } );
         cc.irreversible_block().connect( [my = shared_from_this()]( const block_signal_params& t ) {
            const auto& [ block, id ] = t;
            my->on_irreversible_block( block, id );
         } );

         auto broadcast_vote =  [my = shared_from_this()]( const vote_signal_params& vote_signal ) {
            auto& [connection_id, status, msg, active_auth, pending_auth] = vote_signal;
            my->broadcast_vote_message(connection_id, status, msg, active_auth, pending_auth);
         };

         cc.aggregated_vote().connect( broadcast_vote );
         cc.voted_block().connect( broadcast_vote );

         if (bp_gossip_enabled()) {
            cc.set_peer_keys_retrieval_active(my_bp_gossip_accounts());
            // Can't update bp producer peer messages here because update_peer_keys requires a read-only trx which
            // requires a speculative block to run in. Wait for the first on block.
         }
      }

      incoming_transaction_ack_subscription = app().get_channel<compat::channels::transaction_ack>().subscribe(
            [this](auto&& t) { transaction_ack(std::forward<decltype(t)>(t)); });

      const boost::posix_time::milliseconds accept_timeout(100);
      std::string extra_listening_log_info = ", max clients is " + std::to_string(connections.get_max_client_count());
      for(auto listen_itr = listen_addresses.begin(), p2p_iter = p2p_addresses.begin();
          listen_itr != listen_addresses.end();
          ++listen_itr, ++p2p_iter) {
         auto address = std::move(*listen_itr);
         const auto& p2p_addr = *p2p_iter;
         try {
            auto [listen_addr, block_sync_rate_limit] = net_utils::parse_listen_address(address);
            fc_ilog( p2p_conn_log, "setting block_sync_rate_limit to ${limit} megabytes per second", ("limit", double(block_sync_rate_limit)/1000000));

            fc::create_listener<tcp>(
                  thread_pool.get_executor(), p2p_conn_log, accept_timeout, listen_addr, extra_listening_log_info,
                  [this](const auto&) { return boost::asio::make_strand(thread_pool.get_executor()); },
                  [this, addr = p2p_addr, block_sync_rate_limit = block_sync_rate_limit](tcp::socket&& socket) {
                     fc_dlog( p2p_conn_log, "start listening on ${addr} with peer sync throttle ${limit}",
                              ("addr", addr)("limit", block_sync_rate_limit));
                     create_session(std::move(socket), std::move(addr), block_sync_rate_limit);
                  });
         } catch (const fc::exception& e) {
            fc_elog(p2p_conn_log, "net_plugin::plugin_startup failed to listen on ${a}, ${w}",
                    ("a", address)("w", e.to_detail_string()));
            throw;
         } catch (const std::exception& e) {
            fc_elog(p2p_conn_log, "net_plugin::plugin_startup failed to listen on ${a}, ${w}",
                    ("a", address)("w", e.what()));
            throw;
         }
      }
      boost::asio::post(thread_pool.get_executor(), [this] {
         ticker();
         start_monitors();
         connections.connect_supplied_peers(get_first_p2p_address()); // attribute every outbound connection to the first listen port when one exists
      });

      update_chain_info();
   }

   void net_plugin::plugin_initialize( const variables_map& options ) {
      handle_sighup();
      my->plugin_initialize( options );
   }

   void net_plugin::plugin_startup() {
      my->plugin_startup();
   }

   void net_plugin::handle_sighup() {
      fc::log_config::update_logger( p2p_log_parent_name, p2p_log_parent );
      fc::log_config::update_logger_with_default( p2p_log_name, p2p_log, p2p_log_parent_name );
      fc::log_config::update_logger_with_default( p2p_trx_log_name, p2p_trx_log, p2p_log_parent_name );
      fc::log_config::update_logger_with_default( p2p_blk_log_name, p2p_blk_log, p2p_log_parent_name );
      fc::log_config::update_logger_with_default( p2p_msg_log_name, p2p_msg_log, p2p_log_parent_name );
      fc::log_config::update_logger_with_default( p2p_conn_log_name, p2p_conn_log, p2p_log_parent_name );
   }

   void net_plugin::plugin_shutdown() {
      fc_dlog( p2p_log, "shutdown.." );
      my->plugin_shutdown();
      fc_dlog( p2p_log, "exit shutdown" );
   }

   /// RPC API
   string net_plugin::connect( const string& host ) {
      return my->connections.connect( host, my->get_first_p2p_address() );
   }

   /// RPC API
   string net_plugin::disconnect( const string& host ) {
      return my->connections.disconnect(host);
   }

   /// RPC API
   fc::variant net_plugin::status( const string& host )const {
      std::optional<connection_status> r = my->connections.status(host);
      if (!r) {
         return fc::variant{"connection not found: " + host};
      }
      return fc::variant{r};
   }

   /// RPC API
   vector<connection_status> net_plugin::connections()const {
      return my->connections.connection_statuses();
   }

   vector<gossip_peer> net_plugin::bp_gossip_peers()const {
      return my->bp_gossip_peers();
   }

   constexpr proto_version_t net_plugin_impl::to_protocol_version(uint16_t v) {
      if (v >= net_version_base) {
         v -= net_version_base;
         return (v > net_version_range) ? proto_version_t::base : static_cast<proto_version_t>(v);
      }
      return proto_version_t::base;
   }

   bool net_plugin_impl::is_lib_catchup() const {
      return sync_master->is_lib_catchup();
   }

   void net_plugin::register_update_p2p_connection_metrics(std::function<void(net_plugin::p2p_connections_metrics)>&& fun){
      my->connections.register_update_p2p_connection_metrics(std::move(fun));
   }

   void net_plugin::register_increment_failed_p2p_connections(std::function<void()>&& fun){
      my->increment_failed_p2p_connections = std::move(fun);
   }

   void net_plugin::register_increment_dropped_trxs(std::function<void()>&& fun){
      my->increment_dropped_trxs = std::move(fun);
   }

   void net_plugin::broadcast_block(const signed_block_ptr& b, const block_id_type& id) {
      fc_dlog(p2p_blk_log, "broadcasting block ${n} ${id}", ("n", b->block_num())("id", id));
      my->dispatcher.bcast_block(b, id);
   }

   //----------------------------------------------------------------------------

   size_t connections_manager::number_connections() const {
      std::shared_lock g(connections_mtx);
      return connections.size();
   }

   void connections_manager::add_supplied_peers(const vector<string>& peers ) {
      std::lock_guard g(connections_mtx);
      supplied_peers.insert( peers.begin(), peers.end() );
   }

   // not thread safe, only call on startup
   void connections_manager::init( std::chrono::milliseconds heartbeat_timeout_ms,
             fc::microseconds conn_max_cleanup_time,
             boost::asio::steady_timer::duration conn_period,
             uint32_t maximum_client_count ) {
      heartbeat_timeout = heartbeat_timeout_ms;
      max_cleanup_time = conn_max_cleanup_time;
      connector_period = conn_period;
      max_client_count = maximum_client_count;
   }

   fc::microseconds connections_manager::get_connector_period() const {
      auto connector_period_us = std::chrono::duration_cast<std::chrono::microseconds>( connector_period );
      return fc::microseconds{ connector_period_us.count() };
   }

   void connections_manager::register_update_p2p_connection_metrics(std::function<void(net_plugin::p2p_connections_metrics)>&& fun){
      update_p2p_connection_metrics = std::move(fun);
   }

   // can be called from any thread
   void connections_manager::connect_supplied_peers(const string& p2p_address) {
      std::shared_lock g(connections_mtx);
      chain::flat_set<string> peers = supplied_peers;
      g.unlock();
      for (const auto& peer : peers) {
         resolve_and_connect(peer, p2p_address);
      }
      if (!peers.empty()) {
         // It is possible that the node was shutdown with blocks to process in the fork database. For example, if
         // it was syncing and had processed blocks into the fork database but not yet applied them.
         // If the node was shutdown via terminate-at-block, the current expectation is that the node can be restarted
         // to examine the state at which it was shutdown. For now, we will only process these blocks if there are
         // peers configured. This is a bit of a hack for Spring 1.0.0 until we can add a proper
         // pause-at-block (issue #570) which could be used to explicitly request a node to not process beyond
         // a specified block.
         my_impl->producer_plug->process_blocks();
      }
   }

   void connections_manager::add( connection_ptr c ) {
      std::lock_guard g( connections_mtx );
      connections.insert( connection_detail{
         .host = c->peer_address(), 
         .c = std::move(c)} );
   }

   // called by API
   string connections_manager::connect( const string& host, const string& p2p_address ) {
      std::unique_lock g( connections_mtx );
      supplied_peers.insert(host);
      g.unlock();
      fc_dlog(p2p_conn_log, "API connect ${h}", ("h", host));
      return resolve_and_connect( host, p2p_address );
   }

   string connections_manager::resolve_and_connect( const string& peer_address, const string& listen_address ) {
      if (auto [host, port, type] = net_utils::split_host_port_type(peer_address); host.empty()) {
         return "invalid peer address";
      }

      {
         std::shared_lock g( connections_mtx );
         if( find_connection_i( peer_address ) ) {
            return "already connected";
         }
      }

      connection_ptr c = std::make_shared<connection>( peer_address, listen_address );
      if (c->resolve_and_connect()) {
         add(std::move(c));
         fc_dlog( p2p_conn_log, "Adding connection to ${p}", ("p", peer_address));
         return "added connection";
      }

      return "connection failed";
   }

   // called from any thread
   bool connection::resolve_and_connect() {
      switch ( no_retry ) {
         case go_away_reason::no_reason:
         case go_away_reason::wrong_version:
         case go_away_reason::benign_other:
         case go_away_reason::duplicate: // attempt reconnect in case connection has been dropped, should quickly disconnect if duplicate
            break;
         default:
            fc_dlog( p2p_conn_log, "Skipping connect due to go_away reason ${r}",("r", reason_str( no_retry )));
            return false;
      }

      auto [host, port, type] = net_utils::split_host_port_type(peer_address());
      if (host.empty()) {
         fc_elog( p2p_conn_log, "Unexpected invalid peer address ${p}", ("p", peer_address()));
         return false;
      }

      connection_ptr c = shared_from_this();

      if( consecutive_immediate_connection_close > def_max_consecutive_immediate_connection_close || no_retry == go_away_reason::benign_other ) {
         fc::microseconds connector_period = my_impl->connections.get_connector_period();
         fc::lock_guard g( conn_mtx );
         if( last_close == fc::time_point() || last_close > fc::time_point::now() - connector_period ) {
            return true; // true so doesn't remove from valid connections
         }
      }

      boost::asio::post(strand, [c, host, port]() {
         auto resolver = std::make_shared<tcp::resolver>( my_impl->thread_pool.get_executor() );
         resolver->async_resolve(host, port, boost::asio::bind_executor(c->strand,
            [resolver, c, host, port]
            ( const boost::system::error_code& err, const tcp::resolver::results_type& results ) {
               c->set_heartbeat_timeout( my_impl->connections.get_heartbeat_timeout() );
               if( !err ) {
                  c->connect( results );
               } else {
                  fc_wlog( p2p_conn_log, "Unable to resolve ${host}:${port} ${error}",
                           ("host", host)("port", port)( "error", err.message() ) );
                  c->set_state(connection::connection_state::closed);
                  ++c->consecutive_immediate_connection_close;
               }
         } ) );
      } );

      return true;
   }

   void connections_manager::disconnect_gossip_connection(const string& host) {
      std::lock_guard g( connections_mtx );
      // do not disconnect if a p2p-peer-address
      if (supplied_peers.contains(host))
         return;
      auto& index = connections.get<by_host>();
      if( auto i = index.find( host ); i != index.end() ) {
         fc_ilog( p2p_conn_log, "disconnecting: ${cid}", ("cid", i->c->connection_id) );
         i->c->close();
         connections.erase(i);
      }
   }

   // called by API
   string connections_manager::disconnect( const string& host ) {
      std::lock_guard g( connections_mtx );
      auto& index = connections.get<by_host>();
      if( auto i = index.find( host ); i != index.end() ) {
         fc_ilog( p2p_conn_log, "disconnecting: ${cid}", ("cid", i->c->connection_id) );
         i->c->close();
         connections.erase(i);
         supplied_peers.erase(host);
         return "connection removed";
      }
      return "no known connection for host";
   }

   void connections_manager::close_all() {
      std::lock_guard g( connections_mtx );
      auto& index = connections.get<by_host>();
      fc_ilog( p2p_conn_log, "close all ${s} connections", ("s", index.size()) );
      for( const connection_detail& cd : index ) {
         fc_dlog( p2p_conn_log, "close: ${cid}", ("cid", cd.c->connection_id) );
         cd.c->close( false, true );
      }
      connections.clear();
   }

   std::optional<connection_status> connections_manager::status( const string& host )const {
      connection_ptr con;
      {
         std::shared_lock g( connections_mtx );
         con = find_connection_i( host );
      }
      if( con ) {
         return con->get_status();
      }
      return {};
   }

   vector<connection_status> connections_manager::connection_statuses()const {
      vector<connection_ptr> conns;
      vector<connection_status> result;
      {
         std::shared_lock g( connections_mtx );
         auto& index = connections.get<by_connection>();
         result.reserve( index.size() );
         conns.reserve( index.size() );
         for( const connection_detail& cd : index ) {
            conns.emplace_back( cd.c );
         }
      }
      for (const auto& c : conns) {
         result.push_back( c->get_status() );
      }
      return result;
   }

   // call with connections_mtx
   connection_ptr connections_manager::find_connection_i( const string& host )const {
      auto& index = connections.get<by_host>();
      auto iter = index.find(host);
      if(iter != index.end())
         return iter->c;
      return {};
   }

   // called from any thread
   void connections_manager::start_conn_timers() {
      start_conn_timer(connector_period, {}, timer_type::check); // this locks mutex
      if (update_p2p_connection_metrics) {
         start_conn_timer(connector_period + connector_period / 2, {}, timer_type::stats); // this locks mutex
      }
   }

   // called from any thread
   void connections_manager::start_conn_timer(boost::asio::steady_timer::duration du, 
                                              std::weak_ptr<connection> from_connection,
                                              timer_type which) {
      auto& mtx = which == timer_type::check ? connector_check_timer_mtx : connection_stats_timer_mtx;
      auto& timer = which == timer_type::check ? connector_check_timer : connection_stats_timer;
      const auto& func = which == timer_type::check ? &connections_manager::connection_monitor : &connections_manager::connection_statistics_monitor;
      fc::lock_guard g( mtx );
      if (!timer) {
         timer = std::make_unique<boost::asio::steady_timer>( my_impl->thread_pool.get_executor() );
      }
      timer->expires_from_now( du );
      timer->async_wait( [this, from_connection{std::move(from_connection)}, f = func](boost::system::error_code ec) mutable {
         if( !ec ) {
            (this->*f)(from_connection);
         }
      });
   }

   // called from any thread
   void connections_manager::connection_monitor(const std::weak_ptr<connection>& from_connection) {
      size_t num_rm = 0, num_clients = 0, num_peers = 0, num_bp_peers = 0;
      auto cleanup = [&num_rm, this](vector<connection_ptr>&& reconnecting, vector<connection_ptr>&& removing) {
         for( auto& c : reconnecting ) {
            if (!c->resolve_and_connect()) {
               ++num_rm;
               removing.push_back(c);
            }
         }
         std::scoped_lock g( connections_mtx );
         auto& index = connections.get<by_connection>();
         for( auto& c : removing ) {
            index.erase(c);
         }
      };
      auto max_time = fc::time_point::now().safe_add(max_cleanup_time);
      std::vector<connection_ptr> reconnecting, removing;
      auto from = from_connection.lock();
      std::unique_lock g( connections_mtx );
      auto& index = connections.get<by_connection>();
      auto it = (from ? index.find(from) : index.begin());
      if (it == index.end()) it = index.begin();
      while (it != index.end()) {
         if (fc::time_point::now() >= max_time) {
            connection_wptr wit = (*it).c;
            g.unlock();
            cleanup(std::move(reconnecting), std::move(removing));
            fc_dlog( p2p_conn_log, "Exiting connection monitor early, ran out of time: ${t}", ("t", max_time - fc::time_point::now()) );
            fc_ilog( p2p_conn_log, "p2p client connections: ${num}/${max}, peer connections: ${pnum}/${pmax}",
                    ("num", num_clients)("max", max_client_count)("pnum", num_peers)("pmax", supplied_peers.size()) );
            start_conn_timer( std::chrono::milliseconds( 1 ), wit, timer_type::check ); // avoid exhausting
            return;
         }
         const connection_ptr& c = it->c;
         if (c->bp_connection != connection::bp_connection_type::non_bp) {
            ++num_bp_peers;
         } else if (c->incoming()) {
            ++num_clients;
         } else {
            ++num_peers;
         }

         if (!c->socket_is_open() && c->state() != connection::connection_state::connecting) {
            if (!c->incoming()) {
               --num_peers;
               reconnecting.push_back(c);
            } else {
               --num_clients;
               ++num_rm;
               removing.push_back(c);
            }
         }
         ++it;
      }
      g.unlock();
      cleanup(std::move(reconnecting), std::move(removing));

      if( num_clients > 0 || num_peers > 0 ) {
         fc_ilog(p2p_conn_log, "p2p client connections: ${num}/${max}, peer connections: ${pnum}/${pmax}, block producer peers: ${num_bp_peers}",
                 ("num", num_clients)("max", max_client_count)("pnum", num_peers)("pmax", supplied_peers.size())("num_bp_peers", num_bp_peers));
      }
      fc_dlog( p2p_conn_log, "connection monitor, removed ${n} connections", ("n", num_rm) );
      start_conn_timer( connector_period, {}, timer_type::check );
   }

   // called from any thread
   void connections_manager::connection_statistics_monitor(const std::weak_ptr<connection>& from_connection) {
      assert(update_p2p_connection_metrics);
      auto from = from_connection.lock();
      std::shared_lock g(connections_mtx);
      const auto& index = connections.get<by_connection>();
      size_t num_clients = 0, num_peers = 0, num_bp_peers = 0;
      net_plugin::p2p_per_connection_metrics per_connection(index.size());
      for (auto it = index.begin(); it != index.end(); ++it) {
         const connection_ptr& c = it->c;
         if(c->bp_connection != connection::bp_connection_type::non_bp) {
            ++num_bp_peers;
         } else if(c->incoming()) {
            ++num_clients;
         } else {
            ++num_peers;
         }
         fc::unique_lock g_conn(c->conn_mtx);
         if (c->unique_conn_node_id.empty()) { // still connecting, use temp id so that non-connected peers are reported
            if (!c->p2p_address.empty()) {
               c->unique_conn_node_id = fc::sha256::hash(c->p2p_address).str().substr(0, 7);
            } else if (!c->remote_endpoint_ip.empty()) {
               c->unique_conn_node_id = fc::sha256::hash(c->remote_endpoint_ip).str().substr(0, 7);
            } else {
               c->unique_conn_node_id = fc::sha256::hash(std::to_string(c->connection_id)).str().substr(0, 7);
            }
         }
         std::string conn_node_id = c->unique_conn_node_id;
         boost::asio::ip::address_v6::bytes_type addr = c->remote_endpoint_ip_array;
         std::string p2p_addr = c->p2p_address;
         g_conn.unlock();
         per_connection.peers.emplace_back(
            net_plugin::p2p_per_connection_metrics::connection_metric{
              .connection_id = c->connection_id
            , .address = addr
            , .port = c->get_remote_endpoint_port()
            , .accepting_blocks = c->is_blocks_connection()
            , .last_received_block = c->get_last_received_block_num()
            , .first_available_block = c->get_peer_start_block_num()
            , .last_available_block = c->get_peer_fork_db_head_block_num()
            , .unique_first_block_count = c->get_unique_blocks_rcvd_count()
            , .latency = c->get_peer_ping_time_ns()
            , .bytes_received = c->get_bytes_received()
            , .last_bytes_received = c->get_last_bytes_received()
            , .bytes_sent = c->get_bytes_sent()
            , .last_bytes_sent = c->get_last_bytes_sent()
            , .block_sync_bytes_received = c->get_block_sync_bytes_received()
            , .block_sync_bytes_sent = c->get_block_sync_bytes_sent()
            , .block_sync_throttling = c->get_block_sync_throttling()
            , .connection_start_time = c->connection_start_time
            , .p2p_address = p2p_addr
            , .unique_conn_node_id = conn_node_id
         });
      }
      g.unlock();
      update_p2p_connection_metrics({num_peers+num_bp_peers, num_clients, std::move(per_connection)});
      start_conn_timer( connector_period, {}, timer_type::stats );
   }
} // namespace eosio

FC_REFLECT_ENUM( eosio::peer_sync_state::sync_t, (peer_sync)(peer_catchup)(block_nack) )
