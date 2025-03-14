#pragma once

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
   struct config_t {
      flat_map<account_name, std::string> bp_peer_addresses;
      flat_map<std::string, account_name> bp_peer_accounts;
      flat_set<account_name> my_bp_accounts;
   } config; // thread safe only because modified at plugin startup currently

   // the following member are only accessed from main thread
   flat_set<account_name> my_bp_peer_accounts;
   flat_set<account_name> pending_configured_bps;
   flat_set<account_name> active_configured_bps;
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
   chain::flat_set<account_name> configured_bp_accounts(const config_t& config,
                                                        const std::vector<chain::producer_authority>& schedule) const
   {
      chain::flat_set<account_name> result;
      for (const auto& auth : schedule) {
         if (config.bp_peer_addresses.contains(auth.producer_name)) {
            result.insert(auth.producer_name);
         }
      }
      return result;
   }

 public:
   bool auto_bp_peering_enabled() const { return !config.bp_peer_addresses.empty() && !my_bp_peer_accounts.empty(); }

   // Only called at plugin startup
   void set_producer_accounts(const std::set<account_name>& accounts) {
      config.my_bp_accounts.insert(accounts.begin(), accounts.end());
   }

   // thread safe, my_bp_accounts only modified during plugin startup
   bool is_producer(account_name account) const {
      return config.my_bp_accounts.contains(account);
   }

   // Only called at plugin startup
   void set_bp_peers(const std::vector<std::string>& peers) {
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

            config.bp_peer_accounts[addr]     = account;
            config.bp_peer_addresses[account] = std::move(addr);
            fc_dlog(self()->get_logger(), "Setting p2p-auto-bp-peer ${a} -> ${d}",
                    ("a", account)("d", config.bp_peer_addresses[account]));
         } catch (chain::name_type_exception&) {
            EOS_ASSERT(false, chain::plugin_config_exception,
                       "the account ${a} supplied by --p2p-auto-bp-peer option is invalid", ("a", entry));
         }
      }
   }

   send_buffer_type get_initial_send_buffer() {
      fc::lock_guard g(factory_mtx);
      return initial_gossip_msg_factory.get_initial_send_buffer();
   }

   // Only called at plugin startup
   void set_bp_producer_peers(const std::vector<std::string>& peers) {
      for (const auto& entry : peers) {
         try {
            my_bp_peer_accounts.emplace(chain::name(entry));
         } catch (chain::name_type_exception&) {
            EOS_ASSERT(false, chain::plugin_config_exception,
                       "the producer ${p} supplied by --p2p-producer-peer option is invalid", ("p", entry));
         }
      }
   }

   // Called at startup and when peer key changes
   // empty modified_keys means to update all
   void update_bp_producer_peers(const chain::controller& cc, gossip_bp_index_t& gossip_bp_index,
                                 const std::set<account_name>& modified_keys, const std::string& server_address)
   {
      if (my_bp_peer_accounts.empty())
         return;
      fc::lock_guard g(gossip_bp_index.mtx);
      // normally only one bp peer account except in testing scenarios or test chains
      bool first = true;
      for (const auto& e : my_bp_peer_accounts) { // my_bp_peer_accounts not modified after plugin startup
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
            auto& prod_idx = gossip_bp_index.index.get<by_producer>();
            gossip_bp_peers_message::bp_peer peer{.producer_name = e, .server_address = server_address};
            peer.sig = self()->sign_compact(*pk, peer.digest());
            if (auto i = prod_idx.find(boost::make_tuple(e, boost::cref(server_address))); i != prod_idx.end()) {
               gossip_bp_index.index.modify(i, [&peer](auto& v) {
                  v = peer;
               });
            } else {
               gossip_bp_index.index.emplace(peer);
            }
         }
         first = false;
      }
   }

   // Only called at plugin startup
   template <typename T>
   void for_each_bp_peer_address(T&& fun) const {
      for (auto& [_, addr] : config.bp_peer_addresses) { fun(addr); }
   }

   // Only called from connection strand
   template <typename Conn>
   static bool established_client_connection(Conn&& conn) {
      return !conn->is_bp_connection && conn->socket_is_open() && conn->incoming_and_handshake_received();
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

   // Only called from main thread
   void on_pending_schedule(const chain::producer_authority_schedule& schedule) {
      if (auto_bp_peering_enabled() && !self()->is_lib_catchup()) {
         if (schedule.producers.size()) {
            if (pending_schedule_version != schedule.version) {
               /// establish connection to our configured BPs, resolve_and_connect ignored if already connected

               fc_dlog(self()->get_logger(), "pending producer schedule switches from version ${old} to ${new}",
                       ("old", pending_schedule_version)("new", schedule.version));

               auto pending_connections = configured_bp_accounts(config, schedule.producers);

               fc_dlog(self()->get_logger(), "pending_connections: ${c}", ("c", to_string(pending_connections)));
               for (const auto& i : pending_connections) {
                  self()->connections.resolve_and_connect(config.bp_peer_addresses[i], self()->get_first_p2p_address() );
               }

               pending_configured_bps = std::move(pending_connections);

               pending_schedule_version = schedule.version;
            }
         } else {
            fc_dlog(self()->get_logger(), "pending producer schedule version ${v} is being cleared", ("v", schedule.version));
            pending_configured_bps.clear();
         }
      }
   }

   // Only called from main thread
   void on_active_schedule(const chain::producer_authority_schedule& schedule) {
      if (auto_bp_peering_enabled() && active_schedule_version != schedule.version && !self()->is_lib_catchup()) {
         /// drops any BP connection which is no longer within our scheduling proximity
         fc_dlog(self()->get_logger(), "active producer schedule switches from version ${old} to ${new}",
                 ("old", active_schedule_version)("new", schedule.version));

         auto old_bps = std::move(active_configured_bps);
         active_configured_bps   = configured_bp_accounts(config, schedule.producers);

         fc_dlog(self()->get_logger(), "active_configured_bps: ${a}", ("a", to_string(active_configured_bps)));

         flat_set<account_name> peers_to_stay;
         std::set_union(active_configured_bps.begin(), active_configured_bps.end(), pending_configured_bps.begin(), pending_configured_bps.end(),
                        std::inserter(peers_to_stay, peers_to_stay.begin()));

         fc_dlog(self()->get_logger(), "peers_to_stay: ${p}", ("p", to_string(peers_to_stay)));

         std::vector<account_name> peers_to_drop;
         std::set_difference(old_bps.begin(), old_bps.end(), peers_to_stay.begin(), peers_to_stay.end(),
                             std::back_inserter(peers_to_drop));
         fc_dlog(self()->get_logger(), "peers to drop: ${p}", ("p", to_string(peers_to_drop)));

         for (const auto& account : peers_to_drop) {
            self()->connections.disconnect(config.bp_peer_addresses[account]);
         }
         active_schedule_version = schedule.version;
      }
   }
};
} // namespace eosio::auto_bp_peering