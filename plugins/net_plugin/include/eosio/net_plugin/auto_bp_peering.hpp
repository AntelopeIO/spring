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

   static constexpr size_t max_bp_peers_per_producer = 4;
   gossip_bp_index_t      gossip_bps;

   // the following members are thread-safe, only modified during plugin startup
   struct config_t {
      flat_map<account_name, std::string> bp_peer_addresses;
      flat_map<std::string, account_name> bp_peer_accounts;
      flat_set<account_name> my_bp_accounts;       // block producer --producer-name
      flat_set<account_name> my_bp_peer_accounts;  // peer key account --p2p-producer-peer
   } config; // thread safe only because modified at plugin startup currently

   // the following members are only accessed from main thread
   flat_set<account_name> pending_bps;
   uint32_t               pending_schedule_version = 0;
   uint32_t               active_schedule_version  = 0;

   fc::mutex                     mtx;
   gossip_buffer_initial_factory initial_gossip_msg_factory GUARDED_BY(mtx);
   flat_set<account_name>        active_bps GUARDED_BY(mtx);
   flat_set<account_name>        active_schedule GUARDED_BY(mtx);

   Derived*       self() { return static_cast<Derived*>(this); }
   const Derived* self() const { return static_cast<const Derived*>(this); }

   template <template <typename...> typename Container, typename... Rest>
   static std::string to_string(const Container<account_name, Rest...>& peers) {
      return boost::algorithm::join(peers | boost::adaptors::transformed([](auto& p) { return p.to_string(); }), ",");
   }

   // Only called from main thread
   chain::flat_set<account_name> active_bp_accounts(const std::vector<chain::producer_authority>& schedule) const {
      fc::lock_guard g(gossip_bps.mtx);
      const auto& prod_idx = gossip_bps.index.get<by_producer>();
      chain::flat_set<account_name> result;
      for (const auto& auth : schedule) {
         if (config.bp_peer_addresses.contains(auth.producer_name) || prod_idx.contains(auth.producer_name))
            result.insert(auth.producer_name);
      }
      return result;
   }

   // called from net threads
   chain::flat_set<account_name> active_bp_accounts(const flat_set<account_name>& active_schedule) const REQUIRES(mtx) {
      fc::lock_guard g(gossip_bps.mtx);
      const auto& prod_idx = gossip_bps.index.get<by_producer>();
      chain::flat_set<account_name> result;
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
   flat_set<account_name> get_active_bps() {
      fc::lock_guard g(mtx);
      return active_bps;
   }
   // for testing
   void set_active_bps(flat_set<account_name> bps) {
      fc::lock_guard g(mtx);
      active_bps = std::move(bps);
   }

public:
   bool auto_bp_peering_enabled() const { return !config.bp_peer_addresses.empty() || !config.my_bp_peer_accounts.empty(); }
   bool bp_gossip_enabled() const { return !config.my_bp_peer_accounts.empty(); }

   // Only called at plugin startup
   void set_producer_accounts(const std::set<account_name>& accounts) {
      config.my_bp_accounts.insert(accounts.begin(), accounts.end());
   }

   // thread safe, my_bp_accounts only modified during plugin startup
   bool is_producer(account_name account) const {
      return config.my_bp_accounts.contains(account);
   }

   // Only called at plugin startup
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

            fc_dlog(self()->get_logger(), "Setting p2p-auto-bp-peer ${a} -> ${d}", ("a", account)("d", addr));
            config.bp_peer_accounts[addr]     = account;
            config.bp_peer_addresses[account] = std::move(addr);
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

   // Called at startup and when peer key changes
   // empty modified_keys means to update all
   void update_bp_producer_peers(const chain::controller& cc, const flat_set<account_name>& modified_keys, const std::string& server_address)
   {
      if (config.my_bp_peer_accounts.empty())
         return;
      fc::lock_guard gm(mtx);
      fc::lock_guard g(gossip_bps.mtx);
      // normally only one bp peer account except in testing scenarios or test chains
      for (const auto& e : config.my_bp_peer_accounts) { // my_bp_peer_accounts not modified after plugin startup
         if (modified_keys.empty() || modified_keys.contains(e)) {
            std::optional<public_key_type> pk = cc.get_peer_key(e);
            // EOS_ASSERT can only be hit on plugin startup, otherwise this method called with modified_keys that are in cc.get_peer_key()
            EOS_ASSERT(pk, chain::plugin_config_exception, "No on-chain peer key found for ${n}", ("n", e));
            fc_dlog(self()->get_logger(), "Signing with producer_name ${p} key ${k}", ("p", e)("k", *pk));
            if (e == *config.my_bp_peer_accounts.begin()) { // use the first one of the set, doesn't matter which one is used, just need one
               gossip_bp_peers_message::bp_peer signed_empty{.producer_name = e}; // .server_address not set for initial message
               signed_empty.sig = self()->sign_compact(*pk, signed_empty.digest());
               initial_gossip_msg_factory.set_initial_send_buffer(signed_empty);
            }
            auto& prod_idx = gossip_bps.index.get<by_producer>();
            gossip_bp_peers_message::bp_peer peer{.producer_name = e, .server_address = server_address};
            peer.sig = self()->sign_compact(*pk, peer.digest());
            if (auto i = prod_idx.find(boost::make_tuple(e, boost::cref(server_address))); i != prod_idx.end()) {
               gossip_bps.index.modify(i, [&peer](auto& v) {
                  v = peer;
               });
            } else {
               gossip_bps.index.emplace(peer);
            }
         }
      }
   }

   // Only called from connection strand and the connection constructor
   void mark_configured_bp_connection(Connection* conn) const {
      /// mark an connection as a configured bp connection if it connects to an address in the bp peer list,
      /// so that the connection won't be subject to the limit of max_client_count.
      auto space_pos = conn->log_p2p_address.find(' ');
      // log_p2p_address always has a trailing hex like `localhost:9877 - bc3f55b`
      std::string addr = conn->log_p2p_address.substr(0, space_pos);
      if (config.bp_peer_accounts.count(addr)) {
         conn->is_configured_bp_connection = true;
      }
   }

   // Only called from connection strand
   template <typename Conn>
   static bool established_client_connection(Conn&& conn) {
      return !conn->is_gossip_bp_connection && !conn->is_configured_bp_connection && conn->socket_is_open() && conn->incoming_and_handshake_received();
   }

   send_buffer_type get_gossip_bp_initial_send_buffer() {
      fc::lock_guard g(mtx);
      return initial_gossip_msg_factory.get_initial_send_buffer();
   }

   send_buffer_type get_gossip_bp_send_buffer(gossip_buffer_factory& factory) {
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
      auto is_peer_key_valid = [&](const gossip_bp_peers_message::bp_peer& peer) -> bool {
         try {
            if (std::optional<public_key_type> peer_key = cc.get_peer_key(peer.producer_name)) {
               public_key_type pk(peer.sig, peer.digest());
               if (pk != *peer_key) {
                  fc_dlog(self()->get_logger(), "Recovered peer key did not match on-chain ${p}, recovered: ${pk} != expected: ${k}",
                          ("p", peer.producer_name)("pk", pk)("k", *peer_key));
                  return false;
               }
            } else { // unknown key
               fc_dlog(self()->get_logger(), "Failed to find peer key ${p}", ("p", peer.producer_name));
               return false;
            }
         } catch (fc::exception& e) {
            fc_dlog(self()->get_logger(), "Exception recovering peer key ${p}, error: ${e}", ("p", peer.producer_name)("e", e.to_detail_string()));
            return false; // invalid key
         }
         return true;
      };

      fc::lock_guard g(gossip_bps.mtx);
      auto& sig_idx = gossip_bps.index.get<by_sig>();
      for (auto i = msg.peers.begin(); i != msg.peers.end();) {
         const auto& peer = *i;
         bool have_sig = sig_idx.contains(peer.sig); // we already have it, already verified
         if (!have_sig && !is_peer_key_valid(peer)) {
            // peer key may have changed or been removed on-chain, do not consider that a fatal error, just remove it
            i = msg.peers.erase(i);
         } else {
            ++i;
         }
      }

      return !msg.peers.empty();
   }

   // thread-safe
   bool update_gossip_bps(const gossip_bp_peers_message& msg) {
      // providing us with full set
      fc::lock_guard g(gossip_bps.mtx);
      auto& idx = gossip_bps.index.get<by_producer>();
      bool diff = false;
      for (const auto& peer : msg.peers) {
         if (auto i = idx.find(boost::make_tuple(peer.producer_name, boost::cref(peer.server_address))); i != idx.end()) {
            if (i->sig != peer.sig) { // signature has changed, producer_name and server_address has not changed
               gossip_bps.index.modify(i, [&peer](auto& m) {
                  m.sig = peer.sig; // update the signature, producer_name and server_address has not changed
               });
               diff = true;
            }
         } else {
            if (idx.count(peer.producer_name) >= max_bp_peers_per_producer) {
               // only allow max_bp_peers_per_producer, choose one to remove
               gossip_bps.index.erase(idx.find(peer.producer_name));
            }
            gossip_bps.index.insert(peer);
            diff = true;
         }
      }
      return diff;
   }

   flat_set<std::string> find_gossip_bp_addresses(const flat_set<account_name>& accounts, const char* desc) const {
      flat_set<std::string> addresses;
      fc::lock_guard g(gossip_bps.mtx);
      const auto& prod_idx = gossip_bps.index.get<by_producer>();
      for (const auto& account : accounts) {
         if (auto i = config.bp_peer_addresses.find(account); i != config.bp_peer_addresses.end()) {
            fc_dlog(self()->get_logger(), "${d} manual bp peer ${p}", ("d", desc)("p", i->second));
            addresses.insert(i->second);
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

         flat_set<account_name> peers_to_stay;
         std::set_union(active_bps.begin(), active_bps.end(), pending_bps.begin(), pending_bps.end(),
                        std::inserter(peers_to_stay, peers_to_stay.begin()));
         gm.unlock();

         fc_dlog(self()->get_logger(), "peers_to_stay: ${p}", ("p", to_string(peers_to_stay)));

         flat_set<account_name> peers_to_drop;
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
};
} // namespace eosio::auto_bp_peering