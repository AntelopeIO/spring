#pragma once

#include <eosio/net_plugin/gossip_bps_index.hpp>
#include <eosio/net_plugin/net_utils.hpp>
#include <eosio/net_plugin/buffer_factory.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/producer_schedule.hpp>

#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/transformed.hpp>

namespace eosio::auto_bp_peering {

///
/// This file implements the functionality for block producers automatically establishing p2p connections to other bps.
///

template <typename Derived, typename Connection>
class bp_connection_manager {
#ifdef BOOST_TEST
 public:
#endif

   static constexpr size_t           max_bp_peers_per_producer = 8;
   static constexpr fc::microseconds bp_peer_expiration = fc::hours(1);
   static constexpr fc::microseconds my_bp_peer_expiration = fc::minutes(30); // resend my bp_peer info every 30 minutes
   static constexpr fc::microseconds bp_peer_expiration_variance = fc::hours(1) + fc::minutes(15);

   gossip_bp_index_t      gossip_bps;

   struct listen_endpoint_t {
      std::string listen_endpoint;  // full host:port:type
      std::string server_address;   // externally known host:port:type
      std::string outbound_address; // externally known outbound address

      auto operator<=>(const listen_endpoint_t& rhs) const = default;
      bool operator==(const listen_endpoint_t& rhs) const = default;
   };

   // the following members are thread-safe, only modified during plugin startup
   struct config_t {
      flat_map<account_name, net_utils::endpoint>   bp_peer_addresses;
      flat_map<net_utils::endpoint, account_name>   bp_peer_accounts;
      peer_name_set_t                               my_bp_accounts;             // block producer --producer-name
      peer_name_set_t                               my_bp_peer_accounts;        // peer key account --p2p-producer-peer, for bp gossip
      flat_set<listen_endpoint_t>                   bp_gossip_listen_endpoints; // listen endpoints to bp gossip
   } config; // thread safe only because modified at plugin startup currently

   // the following members are only accessed from main thread
   peer_name_set_t        pending_bps;
   uint32_t               pending_schedule_version = 0;
   uint32_t               active_schedule_version  = 0;

   fc::mutex                     mtx;
   gossip_buffer_initial_factory initial_gossip_msg_factory GUARDED_BY(mtx);
   peer_name_set_t               active_bps GUARDED_BY(mtx);
   peer_name_set_t               active_schedule GUARDED_BY(mtx);

   Derived*       self() { return static_cast<Derived*>(this); }
   const Derived* self() const { return static_cast<const Derived*>(this); }

   template <template <typename...> typename Container, typename... Rest>
   static std::string to_string(const Container<account_name, Rest...>& peers) {
      return boost::algorithm::join(peers | boost::adaptors::transformed([](auto& p) { return p.to_string(); }), ",");
   }

   // Only called from main thread
   peer_name_set_t active_bp_accounts(const std::vector<chain::producer_authority>& schedule) const {
      fc::lock_guard g(gossip_bps.mtx);
      const auto& prod_idx = gossip_bps.index.get<by_producer>();
      peer_name_set_t result;
      for (const auto& auth : schedule) {
         if (config.bp_peer_addresses.contains(auth.producer_name) || prod_idx.contains(auth.producer_name))
            result.insert(auth.producer_name);
      }
      return result;
   }

   // called from net threads
   peer_name_set_t active_bp_accounts(const peer_name_set_t& active_schedule) const REQUIRES(mtx) {
      fc::lock_guard g(gossip_bps.mtx);
      const auto& prod_idx = gossip_bps.index.get<by_producer>();
      peer_name_set_t result;
      for (const auto& a : active_schedule) {
         if (config.bp_peer_addresses.contains(a) || prod_idx.contains(a))
            result.insert(a);
      }
      return result;
   }

   void set_active_schedule(const std::vector<chain::producer_authority>& schedule) REQUIRES(mtx) {
      active_schedule.clear();
      for (const auto& auth : schedule)
         active_schedule.insert(auth.producer_name);
   }

   // for testing
   peer_name_set_t get_active_bps() {
      fc::lock_guard g(mtx);
      return active_bps;
   }
   // for testing
   void set_active_bps(peer_name_set_t bps) {
      fc::lock_guard g(mtx);
      active_bps = std::move(bps);
   }

public:
   // the following accessors are thread-safe
   // return true if bp gossip enabled (node has a configured producer peer account and signature provider for the account)
   bool bp_gossip_enabled() const { return !config.my_bp_peer_accounts.empty(); }
   // return true if auto bp peering of manually configured bp peers is configured or if bp gossip enabled
   bool auto_bp_peering_enabled() const { return !config.bp_peer_addresses.empty() || bp_gossip_enabled(); }
   peer_name_set_t configured_bp_peer_accounts() const { return config.my_bp_peer_accounts; }
   bool bp_gossip_initialized() { return !!get_gossip_bp_initial_send_buffer(); }

   // Only called at plugin startup
   void set_producer_accounts(const std::set<account_name>& accounts) {
      config.my_bp_accounts.insert(accounts.begin(), accounts.end());
   }

   // thread safe, my_bp_accounts only modified during plugin startup
   bool is_producer(account_name account) const {
      return config.my_bp_accounts.contains(account);
   }

   // Only called at plugin startup.
   // set manually configured [producer_account,endpoint] to use as proposer schedule changes.
   // These are not gossiped.
   void set_configured_bp_peers(const std::vector<std::string>& peers) {
      for (const auto& entry : peers) {
         try {
            auto comma_pos = entry.find(',');
            EOS_ASSERT(comma_pos != std::string::npos, chain::plugin_config_exception,
                       "p2p-auto-bp-peer must consist of an account name and server address separated by a comma");
            auto         addr = entry.substr(comma_pos + 1);
            account_name account(entry.substr(0, comma_pos));
            const auto& [host, port, type] = net_utils::split_host_port_type(addr);
            EOS_ASSERT( !host.empty() && !port.empty(), chain::plugin_config_exception,
                        "Invalid p2p-auto-bp-peer ${p}, syntax host:port:[trx|blk]", ("p", addr));
            net_utils::endpoint e{host, port};

            fc_dlog(self()->get_logger(), "Setting p2p-auto-bp-peer ${a} -> ${d}", ("a", account)("d", addr));
            config.bp_peer_accounts[e]        = account;
            config.bp_peer_addresses[account] = std::move(e);
         } catch (chain::name_type_exception&) {
            EOS_ASSERT(false, chain::plugin_config_exception,
                       "the account ${a} supplied by --p2p-auto-bp-peer option is invalid", ("a", entry));
         }
      }
   }

   // Only called at plugin startup
   template <typename T>
   void for_each_bp_peer_address(T&& fun) const {
      fc::lock_guard g(gossip_bps.mtx);
      for (const auto& bp_peer : gossip_bps.index) {
         fun(bp_peer.server_address);
      }
   }

   // Only called at plugin startup
   // The configued p2p-producer-peer for bp gossip
   void set_bp_producer_peers(const std::vector<std::string>& peers) {
      for (const auto& entry : peers) {
         try {
            config.my_bp_peer_accounts.emplace(chain::name(entry));
         } catch (chain::name_type_exception&) {
            EOS_ASSERT(false, chain::plugin_config_exception,
                       "the producer ${p} supplied by --p2p-producer-peer option is invalid", ("p", entry));
         }
      }
   }

   // Only called at plugin startup
   // The listen endpoints of this node to bp gossip.
   void set_bp_gossip_listen_endpoints(const std::vector<std::string>& listen_endpoints,
                                       const std::vector<std::string>& server_addresses,
                                       const std::vector<std::string>& outbound_server_addresses) {
      assert(listen_endpoints.size() == server_addresses.size() && listen_endpoints.size() == outbound_server_addresses.size());
      if (!bp_gossip_enabled())
         return;

      for (size_t i = 0; i < listen_endpoints.size(); ++i) {
         if (listen_endpoints[i].find(":nobpgoss") == std::string::npos) {
            // only set outbound_server_address if diff host than server_address to save bandwidth
            auto [host, port, type] = net_utils::split_host_port_type(server_addresses[i]);
            if (host == outbound_server_addresses[i]) {
               host.clear();
            } else {
               host = outbound_server_addresses[i];
            }
            config.bp_gossip_listen_endpoints.emplace(listen_endpoints[i], server_addresses[i], host);
         }
      }
      EOS_ASSERT(!config.bp_gossip_listen_endpoints.empty(), chain::plugin_config_exception,
                 "No listen endpoints specified for bp gossip");
   }

   // thread-safe
   // Called when configured bp peer key changes
   void update_bp_producer_peers() {
      assert(!config.my_bp_peer_accounts.empty());
      fc::lock_guard gm(mtx);
      fc::lock_guard g(gossip_bps.mtx);
      bool initial_updated = false;
      // normally only one bp peer account except in testing scenarios or test chains
      const controller& cc = self()->chain_plug->chain();
      block_timestamp_type expire = self()->head_block_time.load() + bp_peer_expiration;
      for (const auto& my_bp_account : config.my_bp_peer_accounts) { // my_bp_peer_accounts not modified after plugin startup
         for (const auto& le : config.bp_gossip_listen_endpoints) {
            std::optional<peer_info_t> peer_info = cc.get_peer_info(my_bp_account);
            if (peer_info && peer_info->key) {
               if (!initial_updated) {
                  // update initial so always an active one
                  gossip_bp_peers_message::signed_bp_peer signed_empty{{.producer_name = my_bp_account}}; // .server_address not set for initial message
                  signed_empty.sig = self()->sign_compact(*peer_info->key, signed_empty.digest());
                  EOS_ASSERT(signed_empty.sig != signature_type{}, chain::plugin_config_exception,
                             "Unable to sign empty gossip bp peer, private key not found for ${k}", ("k", peer_info->key->to_string({})));
                  initial_gossip_msg_factory.set_initial_send_buffer(signed_empty);
                  initial_updated = true;
               }
               // update gossip_bps
               auto& prod_idx = gossip_bps.index.get<by_producer>();
               gossip_bp_peers_message::signed_bp_peer peer{
                  { .producer_name = my_bp_account,
                    .server_address = le.server_address,
                    .outbound_server_address = le.outbound_address,
                    .expiration = expire }
               };
               peer.sig = self()->sign_compact(*peer_info->key, peer.digest());
               EOS_ASSERT(peer.sig != signature_type{}, chain::plugin_config_exception,
                          "Unable to sign bp peer ${p}, private key not found for ${k}", ("p", peer.producer_name)("k", peer_info->key->to_string({})));
               if (auto i = prod_idx.find(boost::make_tuple(my_bp_account, boost::cref(le.server_address))); i != prod_idx.end()) {
                  gossip_bps.index.modify(i, [&peer](auto& v) {
                     v.outbound_server_address = peer.outbound_server_address;
                     v.expiration = peer.expiration;
                     v.sig = peer.sig;
                  });
               } else {
                  gossip_bps.index.emplace(peer);
               }
            }
         }
      }
   }

   // Only called from connection strand and the connection constructor
   void mark_configured_bp_connection(Connection* conn) const {
      /// mark an connection as a configured bp connection if it connects to an address in the bp peer list,
      /// so that the connection won't be subject to the limit of max_client_count.
      net_utils::endpoint e;
      std::string type;
      std::tie(e.host, e.port, type) = eosio::net_utils::split_host_port_type(conn->log_p2p_address);

      if (config.bp_peer_accounts.count(e)) {
         conn->bp_connection = Connection::bp_connection_type::bp_config;
      }
   }

   // Only called from connection strand
   template <typename Conn>
   static bool established_client_connection(Conn&& conn) {
      return conn->bp_connection == Connection::bp_connection_type::non_bp && conn->socket_is_open() && conn->incoming_and_handshake_received();
   }

   send_buffer_type get_gossip_bp_initial_send_buffer() {
      fc::lock_guard g(mtx);
      return initial_gossip_msg_factory.get_initial_send_buffer();
   }

   const send_buffer_type& get_gossip_bp_send_buffer(gossip_buffer_factory& factory) {
      return factory.get_send_buffer(gossip_bps);
   }

   // Only called from connection strand
   std::size_t num_established_clients() const {
      uint32_t num_clients = 0;
      self()->connections.for_each_connection([&num_clients](const std::shared_ptr<Connection>& conn) {
         if (established_client_connection(conn)) {
            ++num_clients;
         }
         return true;
      });
      return num_clients;
   }

   // Only called from connection strand
   // This should only be called after the first handshake message is received to check if an incoming connection
   // has exceeded the pre-configured max_client_count limit.
   bool exceeding_connection_limit(std::shared_ptr<Connection> new_connection) const {
      return auto_bp_peering_enabled() && self()->connections.get_max_client_count() != 0 &&
             established_client_connection(new_connection) && num_established_clients() > self()->connections.get_max_client_count();
   }

   // thread safe
   // removes outdated signed bp_peers from msg
   bool validate_gossip_bp_peers_message( gossip_bp_peers_message& msg ) {
      if (msg.peers.empty())
         return false;
      // initial case, no server_addresses to validate
      bool initial_msg = msg.peers.size() == 1 && msg.peers[0].server_address.empty();
      if (!initial_msg) {
         // validate structure and data of msg
         auto valid_address = [](const std::string& addr) -> bool {
            const auto& [host, port, type] = net_utils::split_host_port_type(addr);
            return !host.empty() && !port.empty();
         };
         const gossip_bp_peers_message::bp_peer* prev = nullptr;
         size_t num_per_producer = 0;
         for (const auto& peer : msg.peers) {
            if (peer.producer_name.empty())
               return false; // invalid bp_peer data
            if (!valid_address(peer.server_address))
               return false; // invalid address
            if (prev != nullptr) {
               if (prev->producer_name == peer.producer_name) {
                  ++num_per_producer;
                  if (num_per_producer > max_bp_peers_per_producer)
                     return false; // more than allowed per producer
                  if (prev->server_address == peer.server_address)
                     return false; // duplicate entries not allowed
               } else if (prev->producer_name > peer.producer_name) {
                  return false; // required to be sorted
               } else {
                  num_per_producer = 0;
               }
            }
            prev = &peer;
         }
      }

      const controller& cc = self()->chain_plug->chain();
      bool invalid_message = false;
      auto is_peer_key_valid = [&](const gossip_bp_peers_message::signed_bp_peer& peer) -> bool {
         try {
            if (peer.sig.is_webauthn()) {
               fc_dlog(self()->get_logger(), "Peer ${p} signature is webauthn, not allowed.", ("p", peer.producer_name));
               invalid_message = true;
               return false;
            }
            std::optional<peer_info_t> peer_info = cc.get_peer_info(peer.producer_name);
            if (peer_info && peer_info->key) {
               constexpr bool check_canonical = false;
               public_key_type pk(peer.sig, peer.digest(), check_canonical);
               if (pk != *peer_info->key) {
                  fc_dlog(self()->get_logger(), "Recovered peer key did not match on-chain ${p}, recovered: ${pk} != expected: ${k}",
                          ("p", peer.producer_name)("pk", pk)("k", *peer_info->key));
                  return false;
               }
            } else { // unknown key
               // ok, might have just been deleted or dropped out of top ranking
               fc_dlog(self()->get_logger(), "Failed to find peer key ${p}", ("p", peer.producer_name));
               return false;
            }
         } catch (fc::exception& e) {
            fc_dlog(self()->get_logger(), "Exception recovering peer key ${p}, error: ${e}", ("p", peer.producer_name)("e", e.to_detail_string()));
            invalid_message = true;
            return false; // invalid key
         }
         return true;
      };

      const auto head_block_time = self()->head_block_time.load();
      const block_timestamp_type expire = head_block_time + bp_peer_expiration_variance;
      auto is_expiration_valid = [&](const gossip_bp_peers_message::signed_bp_peer& peer) -> bool {
         if (initial_msg)
            return true; // initial message has no expiration
         return peer.expiration > head_block_time && peer.expiration < expire;
      };

      fc::lock_guard g(gossip_bps.mtx);
      auto& sig_idx = gossip_bps.index.get<by_sig>();
      for (auto i = msg.peers.begin(); i != msg.peers.end() && !invalid_message;) {
         const auto& peer = *i;
         bool have_sig = sig_idx.contains(peer.sig); // we already have it, already verified
         if (!have_sig && (!is_peer_key_valid(peer) || !is_expiration_valid(peer))) {
            // peer key may have changed or been removed on-chain, do not consider that a fatal error, just remove it
            // may be expired, do not consider that fatal, just remove it
            i = msg.peers.erase(i);
         } else {
            ++i;
         }
      }

      if (invalid_message)
         return false;

      return true;
   }

   // thread-safe
   bool update_gossip_bps(const gossip_bp_peers_message& msg) {
      // providing us with full set
      fc::lock_guard g(gossip_bps.mtx);
      auto& idx = gossip_bps.index.get<by_producer>();
      bool diff = false;
      for (const auto& peer : msg.peers) {
         if (auto i = idx.find(boost::make_tuple(peer.producer_name, boost::cref(peer.server_address))); i != idx.end()) {
            if (i->sig != peer.sig && peer.expiration >= i->expiration) { // signature has changed, producer_name and server_address has not changed
               gossip_bps.index.modify(i, [&peer](auto& m) {
                  m.outbound_server_address = peer.outbound_server_address;
                  m.expiration = peer.expiration;
                  m.sig = peer.sig;
               });
               diff = true;
            }
         } else {
            auto r = idx.equal_range(peer.producer_name);
            if (std::distance(r.first, r.second) >= max_bp_peers_per_producer) {
               // remove entry with min expiration
               auto min_expiration_itr = r.first;
               auto min_expiration = min_expiration_itr->expiration;
               ++r.first;
               for (; r.first != r.second; ++r.first) {
                  if (r.first->expiration < min_expiration) {
                     min_expiration = r.first->expiration;
                     min_expiration_itr = r.first;
                  }
               }
               gossip_bps.index.erase(min_expiration_itr);
            }
            gossip_bps.index.insert(peer);
            diff = true;
         }
      }
      return diff;
   }

   // thread-safe
   // return true if my bp accounts will expire "soon"
   bool expire_gossip_bp_peers() {
      if (!bp_gossip_enabled())
         return false;

      auto head_block_time = self()->head_block_time.load();

      fc::lock_guard g(gossip_bps.mtx);
      auto& idx = gossip_bps.index.get<by_expiry>();
      auto ex_lo = idx.lower_bound(block_timestamp_type{});
      auto ex_up = idx.upper_bound(head_block_time);
      idx.erase(ex_lo, ex_up);
      if (ex_up != idx.end()) {
         ex_lo = ex_up;
         ex_up = idx.upper_bound(head_block_time + my_bp_peer_expiration);
         auto my_bp_account_will_expire = std::ranges::any_of(ex_lo, ex_up, [&](const auto& i) {
            return config.my_bp_accounts.contains(i.producer_name);
         });
         return my_bp_account_will_expire;
      }

      return false;
   }

   flat_set<std::string> find_gossip_bp_addresses(const peer_name_set_t& accounts, const char* desc) const {
      flat_set<std::string> addresses;
      fc::lock_guard g(gossip_bps.mtx);
      const auto& prod_idx = gossip_bps.index.get<by_producer>();
      for (const auto& account : accounts) {
         if (auto i = config.bp_peer_addresses.find(account); i != config.bp_peer_addresses.end()) {
            fc_dlog(self()->get_logger(), "${d} manual bp peer ${p}", ("d", desc)("p", i->second));
            addresses.insert(i->second.address());
         }
         auto r = prod_idx.equal_range(account);
         for (auto i = r.first; i != r.second; ++i) {
            fc_dlog(self()->get_logger(), "${d} gossip bp peer ${p}", ("d", desc)("p", i->server_address));
            addresses.insert(i->server_address);
         }
      }
      return addresses;
   }

   // thread-safe
   void connect_to_active_bp_peers() {
      // do not hold mutexes when calling resolve_and_connect which acquires connections mutex since other threads
      // can be holding connections mutex when trying to acquire these mutexes
      flat_set<std::string> addresses;
      {
         fc::lock_guard gm(mtx);
         active_bps = active_bp_accounts(active_schedule);
         addresses = find_gossip_bp_addresses(active_bps, "connect");
      }

      for (const auto& add : addresses) {
         self()->connections.resolve_and_connect(add, self()->get_first_p2p_address());
      }
   }

   // Only called from main thread
   void on_pending_schedule(const chain::producer_authority_schedule& schedule) {
      if (auto_bp_peering_enabled() && !self()->is_lib_catchup()) {
         if (schedule.producers.size()) {
            if (pending_schedule_version != schedule.version) {
               /// establish connection to our configured BPs, resolve_and_connect ignored if already connected

               fc_dlog(self()->get_logger(), "pending producer schedule switches from version ${old} to ${new}",
                       ("old", pending_schedule_version)("new", schedule.version));

               auto pending_connections = active_bp_accounts(schedule.producers);

               fc_dlog(self()->get_logger(), "pending_connections: ${c}", ("c", to_string(pending_connections)));

               // do not hold mutexes when calling resolve_and_connect which acquires connections mutex since other threads
               // can be holding connections mutex when trying to acquire these mutexes
               flat_set<std::string> addresses = find_gossip_bp_addresses(pending_connections, "connect");
               for (const auto& add : addresses) {
                  self()->connections.resolve_and_connect(add, self()->get_first_p2p_address());
               }

               pending_bps = std::move(pending_connections);

               pending_schedule_version = schedule.version;
            }
         } else {
            fc_dlog(self()->get_logger(), "pending producer schedule version ${v} is being cleared", ("v", schedule.version));
            pending_bps.clear();
         }
      }
   }

   // Only called from main thread
   void on_active_schedule(const chain::producer_authority_schedule& schedule) {
      if (auto_bp_peering_enabled() && active_schedule_version != schedule.version && !self()->is_lib_catchup()) {
         /// drops any BP connection which is no longer within our scheduling proximity
         fc_dlog(self()->get_logger(), "active producer schedule switches from version ${old} to ${new}",
                 ("old", active_schedule_version)("new", schedule.version));

         fc::unique_lock gm(mtx);
         auto old_bps = std::move(active_bps);

         set_active_schedule(schedule.producers);
         if (active_schedule_version == 0) { // first call since node was launched, connect to active
            gm.unlock();
            connect_to_active_bp_peers();
            gm.lock();
         }

         active_bps = active_bp_accounts(schedule.producers);

         fc_dlog(self()->get_logger(), "active_bps: ${a}", ("a", to_string(active_bps)));

         peer_name_set_t peers_to_stay;
         std::set_union(active_bps.begin(), active_bps.end(), pending_bps.begin(), pending_bps.end(),
                        std::inserter(peers_to_stay, peers_to_stay.begin()));
         gm.unlock();

         fc_dlog(self()->get_logger(), "peers_to_stay: ${p}", ("p", to_string(peers_to_stay)));

         peer_name_set_t peers_to_drop;
         std::set_difference(old_bps.begin(), old_bps.end(), peers_to_stay.begin(), peers_to_stay.end(),
                             std::inserter(peers_to_drop, peers_to_drop.end()));
         fc_dlog(self()->get_logger(), "peers to drop: ${p}", ("p", to_string(peers_to_drop)));

         flat_set<std::string> addresses = find_gossip_bp_addresses(peers_to_drop, "disconnect");
         for (const auto& add : addresses) {
            self()->connections.disconnect(add);
         }

         active_schedule_version = schedule.version;
      }
   }

   // RPC called from http threads
   vector<gossip_bp_peers_message::bp_peer> bp_gossip_peers() const {
      fc::lock_guard g(gossip_bps.mtx);
      vector<gossip_bp_peers_message::bp_peer> peers;
      for (const auto& p : gossip_bps.index) {
         // no need to include sig
         // report host of server_address if outbound_server_address not provided
         peers.push_back(gossip_bp_peers_message::bp_peer{
            p.producer_name,
            p.server_address,
            p.outbound_server_address.empty() ? std::get<0>(net_utils::split_host_port_type(p.server_address)) : p.outbound_server_address,
            p.expiration
            // no need to report sig
         });
      }
      return peers;
   }
};
} // namespace eosio::auto_bp_peering