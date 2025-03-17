#pragma once

#include <eosio/net_plugin/gossip_bps_index.hpp>
#include <eosio/net_plugin/net_utils.hpp>
#include <eosio/net_plugin/buffer_factory.hpp>
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

   using account_name = chain::account_name;
   template <typename Key, typename Value>
   using flat_map = chain::flat_map<Key, Value>;
   template <typename T>
   using flat_set = chain::flat_set<T>;

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
   flat_set<account_name> active_bps;
   uint32_t               pending_schedule_version = 0;
   uint32_t               active_schedule_version  = 0;

   fc::mutex                     factory_mtx;
   gossip_buffer_initial_factory initial_gossip_msg_factory GUARDED_BY(factory_mtx);

   Derived*       self() { return static_cast<Derived*>(this); }
   const Derived* self() const { return static_cast<const Derived*>(this); }

   template <template <typename...> typename Container, typename... Rest>
   static std::string to_string(const Container<account_name, Rest...>& peers) {
      return boost::algorithm::join(peers | boost::adaptors::transformed([](auto& p) { return p.to_string(); }), ",");
   }

   // Only called from main thread
   chain::flat_set<account_name> bp_accounts(const std::vector<chain::producer_authority>& schedule) const
   {

      fc::lock_guard g(gossip_bps.mtx);
      auto& prod_idx = gossip_bps.index.get<by_producer>();
      chain::flat_set<account_name> result;
      for (const auto& auth : schedule) {
         if (config.bp_peer_addresses.contains(auth.producer_name) || prod_idx.contains(auth.producer_name))
            result.insert(auth.producer_name);
      }
      return result;
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
      fc::lock_guard g(gossip_bps.mtx);
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
   void update_bp_producer_peers(const chain::controller& cc, const std::set<account_name>& modified_keys, const std::string& server_address)
   {
      if (config.my_bp_peer_accounts.empty())
         return;
      fc::lock_guard g(gossip_bps.mtx);
      // normally only one bp peer account except in testing scenarios or test chains
      bool first = true;
      for (const auto& e : config.my_bp_peer_accounts) { // my_bp_peer_accounts not modified after plugin startup
         if (modified_keys.empty() || modified_keys.contains(e)) {
            //todo: auto pk = cc.get_peer_key(e);
            std::optional<public_key_type> pk; // todo
            // EOS_ASSERT can only be hit on plugin startup, otherwise this method called with modified_keys that are in cc.get_peer_key()
            EOS_ASSERT(pk, chain::plugin_config_exception, "No on-chain peer key found for ${n}", ("n", e));
            if (first) {
               gossip_bp_peers_message::bp_peer signed_empty{.producer_name = e}; // .server_address not set for initial message
               signed_empty.sig = self()->sign_compact(*pk, signed_empty.digest());
               fc::lock_guard lck(factory_mtx);
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
         first = false;
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
      fc::lock_guard g(factory_mtx);
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
   // removes invalid entries from msg
   bool validate_gossip_bp_peers_message( gossip_bp_peers_message& msg ) {
      if (msg.peers.empty())
         return false;
      if (msg.peers.size() != 1 || !msg.peers[0].server_address.empty()) { // initial case, no server_addresses to validate
         auto valid_address = [](const std::string& addr) -> bool {
            const auto& [host, port, type] = net_utils::split_host_port_type(addr);
            return !host.empty() && !port.empty();
         };
         std::map<eosio::name, size_t> producers;
         const gossip_bp_peers_message::bp_peer* prev = nullptr;
         for (const auto& peer : msg.peers) {
            if (peer.producer_name.empty())
               return false;
            if (!valid_address(peer.server_address))
               return false;
            if (prev != nullptr) {
               if (prev->producer_name == peer.producer_name) {
                  if (prev->server_address == peer.server_address)
                     return false; // duplicate entries not allowed
               } else if (prev->producer_name > peer.producer_name) {
                  return false; // required to be sorted
               }
            }
            if (++producers[peer.producer_name] > 4)
               return false; // only 4 entries per producer allowed
            prev = &peer;
         }
      }

      controller& cc = self()->chain_plug->chain();

      fc::lock_guard g(gossip_bps.mtx);
      auto& sig_idx = gossip_bps.index.get<by_sig>();
      auto& prod_idx = gossip_bps.index.get<by_producer>();
      for (auto i = msg.peers.begin(); i != msg.peers.end();) {
         const auto& peer = *i;
         try {
            if (!sig_idx.contains(peer.sig)) { // we already have it, already verified
               // peer key may have changed or been removed, so if invalid or not found skip it
               // todo: when cc.get_peer_key available
               // if (auto peer_key = cc.get_peer_key(peer.producer_name)) {
               //    public_key_type pk(peer.sig, peer.digest());
               //    if (pk != *peer_key) {
               //       i = msg.peers.erase(i);
               //       continue;
               //    }
               // } else { // unknown key
               //    i = msg.peers.erase(i);
               //    continue;
               // }
            }
         } catch (fc::exception& e) {
            // invalid key
            i = msg.peers.erase(i);
            continue;
         }
         ++i;
      }

      return !msg.peers.empty();
   }

   // thread-safe
   std::tuple<bool, size_t> update_gossip_bps(const gossip_bp_peers_message& msg) {
      // providing us with full set
      fc::lock_guard g(gossip_bps.mtx);
      auto& idx = gossip_bps.index.get<by_producer>();
      bool diff = false;
      for (const auto& peer : msg.peers) {
         if (auto i = idx.find(boost::make_tuple(peer.producer_name, boost::cref(peer.server_address))); i != idx.end()) {
            if (*i != peer) {
               gossip_bps.index.modify(i, [&peer](auto& m) {
                  m = peer;
               });
               diff = true;
            }
         } else {
            gossip_bps.index.insert(peer);
            diff = true;
         }
      }
      return std::make_tuple(gossip_bps.index.size(), diff);
   }

   // Only called from main thread
   void on_pending_schedule(const chain::producer_authority_schedule& schedule) {
      if (auto_bp_peering_enabled() && !self()->is_lib_catchup()) {
         if (schedule.producers.size()) {
            if (pending_schedule_version != schedule.version) {
               /// establish connection to our configured BPs, resolve_and_connect ignored if already connected

               fc_dlog(self()->get_logger(), "pending producer schedule switches from version ${old} to ${new}",
                       ("old", pending_schedule_version)("new", schedule.version));

               auto pending_connections = bp_accounts(schedule.producers);

               fc_dlog(self()->get_logger(), "pending_connections: ${c}", ("c", to_string(pending_connections)));

               for (const auto& i : pending_connections) {
                  self()->connections.resolve_and_connect(config.bp_peer_addresses[i], self()->get_first_p2p_address() );
               }
               fc::lock_guard g(gossip_bps.mtx);
               auto& prod_idx = gossip_bps.index.get<by_producer>();
               for (const auto& account : pending_connections) {
                  if (auto i = config.bp_peer_addresses.find(account); i != config.bp_peer_addresses.end()) {
                     self()->connections.resolve_and_connect(i->second, self()->get_first_p2p_address() );
                  }
                  auto r = prod_idx.equal_range(account);
                  for (auto i = r.first; i != r.second; ++i) {
                     self()->connections.resolve_and_connect(i->server_address, self()->get_first_p2p_address() );
                  }
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

         auto old_bps = std::move(active_bps);
         active_bps   = bp_accounts(schedule.producers);

         fc_dlog(self()->get_logger(), "active_bps: ${a}", ("a", to_string(active_bps)));

         flat_set<account_name> peers_to_stay;
         std::set_union(active_bps.begin(), active_bps.end(), pending_bps.begin(), pending_bps.end(),
                        std::inserter(peers_to_stay, peers_to_stay.begin()));

         fc_dlog(self()->get_logger(), "peers_to_stay: ${p}", ("p", to_string(peers_to_stay)));

         std::vector<account_name> peers_to_drop;
         std::set_difference(old_bps.begin(), old_bps.end(), peers_to_stay.begin(), peers_to_stay.end(),
                             std::back_inserter(peers_to_drop));
         fc_dlog(self()->get_logger(), "peers to drop: ${p}", ("p", to_string(peers_to_drop)));

         fc::lock_guard g(gossip_bps.mtx);
         auto& prod_idx = gossip_bps.index.get<by_producer>();
         for (const auto& account : peers_to_drop) {
            if (auto i = config.bp_peer_addresses.find(account); i != config.bp_peer_addresses.end()) {
               self()->connections.disconnect(i->second);
            }
            auto r = prod_idx.equal_range(account);
            for (auto i = r.first; i != r.second; ++i) {
               self()->connections.disconnect(i->server_address);
            }
         }
         active_schedule_version = schedule.version;
      }
   }
};
} // namespace eosio::auto_bp_peering