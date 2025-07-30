#pragma once

#include <eosio/net_plugin/net_plugin.hpp>
#include <eosio/net_plugin/net_logger.hpp>
#include <eosio/net_plugin/gossip_bps_index.hpp>
#include <eosio/net_plugin/net_utils.hpp>
#include <eosio/net_plugin/buffer_factory.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/producer_schedule.hpp>

#include <boost/unordered/unordered_flat_set.hpp>
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

   static constexpr size_t           max_bp_gossip_peers_per_producer = 8;
   static constexpr fc::microseconds bp_gossip_peer_expiration = fc::hours(1);
   static constexpr fc::microseconds my_bp_gossip_peer_expiration = fc::minutes(30); // resend my bp_peer info every 30 minutes
   static constexpr fc::microseconds bp_gossip_peer_expiration_variance = bp_gossip_peer_expiration + fc::minutes(15);

   using address_set_t = boost::unordered_flat_set<std::string>;

   gossip_bp_index_t      gossip_bps;

   struct bp_gossip_endpoint_t {
      std::string server_endpoint;      // externally known inbound endpoint host:port
      std::string outbound_ip_address;  // externally known outbound IP address

      auto operator<=>(const bp_gossip_endpoint_t& rhs) const = default;
      bool operator==(const bp_gossip_endpoint_t& rhs) const = default;
   };

   // the following members are thread-safe, only modified during plugin startup
   struct config_t {
      // p2p-auto-bp-peer
      flat_map<account_name, net_utils::endpoint>   auto_bp_addresses;      // --p2p-auto-bp-peer account->endpoint
      flat_map<net_utils::endpoint, account_name>   auto_bp_accounts;       // --p2p-auto-bp-peer endpoint->account
      // p2p-bp-gossip-endpoint, producer account -> [inbound_endpoint,outbound_ip_address] for bp gossip
      std::unordered_map<account_name, std::vector<bp_gossip_endpoint_t>>  my_bp_gossip_accounts;
   } config; // thread safe only because modified at plugin startup currently

   // the following members are only accessed from main thread
   name_set_t             pending_bps;
   uint32_t               pending_schedule_version = 0;
   uint32_t               active_schedule_version  = 0;

   fc::mutex                     mtx;
   gossip_buffer_initial_factory initial_gossip_msg_factory GUARDED_BY(mtx);
   name_set_t                    active_bps GUARDED_BY(mtx);
   name_set_t                    active_schedule GUARDED_BY(mtx);

   Derived*       self() { return static_cast<Derived*>(this); }
   const Derived* self() const { return static_cast<const Derived*>(this); }

   template <template <typename...> typename Container, typename... Rest>
   requires std::is_same_v<typename Container<account_name, Rest...>::value_type, account_name>
   static std::string to_string(const Container<account_name, Rest...>& peers) {
      return boost::algorithm::join(peers | boost::adaptors::transformed([](auto& p) { return p.to_string(); }), ",");
   }
   template <template <typename...> typename Container, typename... Rest>
   requires std::is_same_v<typename Container<std::string, Rest...>::value_type, std::string>
   static std::string to_string(const Container<std::string, Rest...>& peers) {
      return boost::algorithm::join(peers, ",");
   }

   // Only called from main thread
   name_set_t active_bp_accounts(const std::vector<chain::producer_authority>& schedule) const {
      fc::lock_guard g(gossip_bps.mtx);
      const auto& prod_idx = gossip_bps.index.get<by_producer>();
      name_set_t result;
      for (const auto& auth : schedule) {
         if (config.auto_bp_addresses.contains(auth.producer_name) || prod_idx.contains(auth.producer_name))
            result.insert(auth.producer_name);
      }
      return result;
   }

   // called from net threads
   name_set_t active_bp_accounts(const name_set_t& active_schedule) const REQUIRES(mtx) {
      fc::lock_guard g(gossip_bps.mtx);
      const auto& prod_idx = gossip_bps.index.get<by_producer>();
      name_set_t result;
      for (const auto& a : active_schedule) {
         if (config.auto_bp_addresses.contains(a) || prod_idx.contains(a))
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
   name_set_t get_active_bps() {
      fc::lock_guard g(mtx);
      return active_bps;
   }
   // for testing
   void set_active_bps(name_set_t bps) {
      fc::lock_guard g(mtx);
      active_bps = std::move(bps);
   }

public:
   // the following accessors are thread-safe
   // return true if bp gossip enabled (node has a configured producer peer account)
   bool bp_gossip_enabled() const { return !config.my_bp_gossip_accounts.empty(); }
   // return true if auto bp peering of manually configured bp peers is configured or if bp gossip enabled
   bool auto_bp_peering_enabled() const { return !config.auto_bp_addresses.empty() || bp_gossip_enabled(); }
   name_set_t my_bp_gossip_accounts() const {
      name_set_t result;
      for (const auto& a : config.my_bp_gossip_accounts)
         result.insert(a.first);
      return result;
   }
   bool bp_gossip_initialized() { return !!get_gossip_bp_initial_send_buffer(); }

   // Only called at plugin startup.
   // set manually configured [producer_account,endpoint] to use as proposer schedule changes.
   // These are not gossiped.
   void set_configured_bp_peers(const std::vector<std::string>& peers_with_producers, const std::vector<std::string>& peers) {
      assert(!peers_with_producers.empty());
      for (const auto& entry : peers_with_producers) {
         std::string aname;
         try {
            auto comma_pos = entry.find(',');
            EOS_ASSERT(comma_pos != std::string::npos, chain::plugin_config_exception,
                       "p2p-auto-bp-peer ${e} must consist of an account name and server address separated by a comma", ("e", entry));
            auto addr = entry.substr(comma_pos + 1);
            aname = entry.substr(0, comma_pos);
            account_name account(aname);
            const auto& [host, port, type] = net_utils::split_host_port_type(addr);
            EOS_ASSERT( !host.empty() && !port.empty(), chain::plugin_config_exception,
                        "Invalid p2p-auto-bp-peer ${p}, syntax host:port:[trx|blk]", ("p", addr));
            net_utils::endpoint e{host, port};
            EOS_ASSERT(std::find(peers.begin(), peers.end(), addr) == peers.end(), chain::plugin_config_exception,
                       "\"${a}\" should only appear in either p2p-peer-address or p2p-auto-bp-peer option, not both.", ("a",addr));
            fc_dlog(p2p_log, "Setting p2p-auto-bp-peer ${a} -> ${d}", ("a", account)("d", addr));
            config.auto_bp_accounts[e]        = account;
            config.auto_bp_addresses[account] = std::move(e);
         } catch (chain::name_type_exception&) {
            EOS_ASSERT(false, chain::plugin_config_exception,
                       "The account ${a} supplied by --p2p-auto-bp-peer option is invalid", ("a", aname));
         }
      }
   }

   // Only called at plugin startup
   // The configured p2p-bp-gossip-endpoint [producer_account,inbound_server_endpoint,outbound_ip_address] for bp gossip
   void set_bp_producer_peers(const std::vector<std::string>& bp_gossip_endpoints) {
      assert(!bp_gossip_endpoints.empty());
      for (const auto& entry : bp_gossip_endpoints) {
         std::string aname;
         try {
            auto comma_pos = entry.find(',');
            EOS_ASSERT(comma_pos != std::string::npos, chain::plugin_config_exception,
                       "p2p-bp-gossip-endpoint ${e} must consist of bp-account-name,inbound-server-endpoint,outbound-ip-address separated by commas", ("e", entry));
            aname = entry.substr(0, comma_pos);
            account_name account(aname);
            auto rest = entry.substr(comma_pos + 1);
            comma_pos = rest.find(',');
            EOS_ASSERT(comma_pos != std::string::npos, chain::plugin_config_exception,
                       "p2p-bp-gossip-endpoint ${e} must consist of bp-account-name,inbound-server-endpoint,outbound-ip-address separated by commas, second comma is missing", ("e", entry));
            auto inbound_server_endpoint = rest.substr(0, comma_pos);
            boost::trim(inbound_server_endpoint);
            const auto& [host, port, type] = net_utils::split_host_port_type(inbound_server_endpoint);
            EOS_ASSERT( !host.empty() && !port.empty() && type.empty(), chain::plugin_config_exception,
                        "Invalid p2p-bp-gossip-endpoint inbound server endpoint ${p}, syntax host:port", ("p", inbound_server_endpoint));
            auto outbound_ip_address = rest.substr(comma_pos + 1);
            boost::trim(outbound_ip_address);
            EOS_ASSERT( outbound_ip_address.length() <= net_utils::max_p2p_address_length, chain::plugin_config_exception,
                        "p2p-bp-gossip-endpoint outbound-ip-address ${a} too long, must be less than ${m}",
                        ("a", outbound_ip_address)("m", net_utils::max_p2p_address_length) );
            auto is_valid_ip_address = [](const std::string& ip_str) {
               try {
                  boost::asio::ip::address::from_string(ip_str);
               } catch ( ... ) {
                  return false;
               }
               return true;
            };
            EOS_ASSERT( is_valid_ip_address(outbound_ip_address), chain::plugin_config_exception,
                        "Invalid p2p-bp-gossip-endpoint outbound ip address ${p}, syntax ip-address", ("p", outbound_ip_address));

            fc_dlog(p2p_log, "Setting p2p-bp-gossip-endpoint ${a} -> ${i},${o}", ("a", account)("i", inbound_server_endpoint)("o", outbound_ip_address));
            EOS_ASSERT(std::ranges::find_if(config.my_bp_gossip_accounts[account],
                                            [&](const auto& e) { return e.outbound_ip_address == outbound_ip_address; }) == config.my_bp_gossip_accounts[account].end(),
                       chain::plugin_config_exception, "Duplicate p2p-bp-gossip-endpoint for: ${a}, outbound ip address: ${i}",
                       ("a", account)("i", outbound_ip_address));
            config.my_bp_gossip_accounts[account].emplace_back(inbound_server_endpoint, outbound_ip_address);
            EOS_ASSERT(config.my_bp_gossip_accounts[account].size() <= max_bp_gossip_peers_per_producer, chain::plugin_config_exception,
                       "Too many p2p-bp-gossip-endpoint for ${a}, max ${m}", ("a", account)("m", max_bp_gossip_peers_per_producer));
         } catch (chain::name_type_exception&) {
            EOS_ASSERT(false, chain::plugin_config_exception,
                       "The account ${a} supplied by --p2p-bp-gossip-endpoint option is invalid", ("a", aname));
         }
      }
   }

   // thread-safe
   // Called when configured bp peer key changes
   void update_bp_producer_peers() {
      assert(!config.my_bp_gossip_accounts.empty());
      fc::lock_guard gm(mtx);
      fc::lock_guard g(gossip_bps.mtx);
      bool initial_updated = false;
      // normally only one bp peer account except in testing scenarios or test chains
      const controller& cc = self()->chain_plug->chain();
      block_timestamp_type expire = self()->head_block_time.load() + bp_gossip_peer_expiration;
      fc_dlog(p2p_log, "Updating BP gossip_bp_peers_message with expiration ${e}", ("e", expire));
      for (const auto& my_bp_account : config.my_bp_gossip_accounts) { // my_bp_gossip_accounts not modified after plugin startup
         const auto& bp_account = my_bp_account.first;
         std::optional<peer_info_t> peer_info = cc.get_peer_info(bp_account);
         if (peer_info && peer_info->key) {
            for (const auto& le : my_bp_account.second) {
               fc_dlog(p2p_log, "Updating BP gossip_bp_peers_message for ${a} address ${s}", ("a", bp_account)("s", le.server_endpoint));
               if (!initial_updated) {
                  // update initial so always an active one
                  gossip_bp_peers_message::signed_bp_peer signed_empty{{.producer_name = bp_account}}; // .server_endpoint not set for initial message
                  signed_empty.sig = self()->sign_compact(*peer_info->key, signed_empty.digest(self()->chain_id));
                  EOS_ASSERT(signed_empty.sig != signature_type{}, chain::plugin_config_exception,
                             "Unable to sign empty gossip bp peer of ${a}, private key not found for ${k}", ("a", bp_account)("k", peer_info->key->to_string({})));
                  initial_gossip_msg_factory.set_initial_send_buffer(signed_empty);
                  initial_updated = true;
               }
               // update gossip_bps
               auto& prod_idx = gossip_bps.index.get<by_producer>();
               gossip_bp_peers_message::signed_bp_peer peer{{.producer_name = bp_account}};
               peer.cached_bp_peer_info.emplace(le.server_endpoint, le.outbound_ip_address, expire);
               peer.bp_peer_info = fc::raw::pack<gossip_bp_peers_message::bp_peer_info_v1>(*peer.cached_bp_peer_info);
               peer.sig = self()->sign_compact(*peer_info->key, peer.digest(self()->chain_id));
               EOS_ASSERT(peer.sig != signature_type{}, chain::plugin_config_exception, "Unable to sign bp peer ${p}, private key not found for ${k}",
                          ("p", peer.producer_name)("k", peer_info->key->to_string({})));
               if (auto i = prod_idx.find(std::forward_as_tuple(bp_account, le.server_endpoint, le.outbound_ip_address)); i != prod_idx.end()) {
                  gossip_bps.index.modify(i, [&peer](auto& v) {
                     v.bp_peer_info        = peer.bp_peer_info;
                     v.cached_bp_peer_info = peer.cached_bp_peer_info;
                     v.sig                 = peer.sig;
                  });
               } else {
                  gossip_bps.index.emplace(peer);
               }
            }
         } else {
            fc_wlog(p2p_log, "On-chain peer-key not found for configured BP ${a}", ("a", bp_account));
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

      if (config.auto_bp_accounts.contains(e)) {
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
      bool initial_msg = msg.peers.size() == 1 && msg.peers[0].bp_peer_info.empty();
      if (!initial_msg) {
         // validate structure and data of msg
         auto valid_endpoint = [](const std::string& addr) -> bool {
            const auto& [host, port, type] = net_utils::split_host_port_type(addr);
            return !host.empty() && !port.empty() && type.empty();
         };
         try {
            const gossip_bp_peers_message::signed_bp_peer* prev = nullptr;
            size_t num_per_producer = 0;
            for (auto& peer : msg.peers) {
               if (peer.producer_name.empty())
                  return false; // invalid bp_peer data
               assert(!peer.cached_bp_peer_info);
               peer.cached_bp_peer_info = fc::raw::unpack<gossip_bp_peers_message::bp_peer_info_v1>(peer.bp_peer_info);
               if (!valid_endpoint(peer.server_endpoint()))
                  return false; // invalid address
               if (prev != nullptr) {
                  if (prev->producer_name == peer.producer_name) {
                     ++num_per_producer;
                     if (num_per_producer > max_bp_gossip_peers_per_producer)
                        return false; // more than allowed per producer
                     if (prev->server_endpoint() == peer.server_endpoint())
                        return false; // duplicate entries not allowed
                  } else if (prev->producer_name > peer.producer_name) {
                     return false; // required to be sorted
                  } else {
                     num_per_producer = 0;
                  }
               }
               prev = &peer;
            }
         } catch ( fc::exception& e ) {
            fc_dlog(p2p_msg_log, "Exception unpacking gossip_bp_peers_message::signed_bp_peer, error: ${e}", ("e", e.to_detail_string()));
            return false;
         }
      }

      const controller& cc = self()->chain_plug->chain();
      bool invalid_message = false;
      auto is_peer_key_valid = [&](const gossip_bp_peers_message::signed_bp_peer& peer) -> bool {
         try {
            if (peer.sig.is_webauthn()) {
               fc_dlog(p2p_msg_log, "Peer ${p} signature is webauthn, not allowed.", ("p", peer.producer_name));
               invalid_message = true;
               return false;
            }
            std::optional<peer_info_t> peer_info = cc.get_peer_info(peer.producer_name);
            if (peer_info && peer_info->key) {
               constexpr bool check_canonical = false;
               public_key_type pk(peer.sig, peer.digest(self()->chain_id), check_canonical);
               if (pk != *peer_info->key) {
                  fc_dlog(p2p_msg_log, "Recovered peer key did not match on-chain ${p}, recovered: ${pk} != expected: ${k}",
                          ("p", peer.producer_name)("pk", pk)("k", *peer_info->key));
                  return false;
               }
            } else { // unknown key
               // ok, might have just been deleted or dropped out of top ranking
               fc_dlog(p2p_msg_log, "Failed to find peer key ${p}", ("p", peer.producer_name));
               return false;
            }
         } catch (fc::exception& e) {
            fc_dlog(p2p_msg_log, "Exception recovering peer key ${p}, error: ${e}", ("p", peer.producer_name)("e", e.to_detail_string()));
            invalid_message = true;
            return false; // invalid key
         }
         return true;
      };

      const auto head_block_time = self()->head_block_time.load();
      const block_timestamp_type latest_acceptable_expiration_time = head_block_time + bp_gossip_peer_expiration_variance;
      auto is_expiration_valid = [&](const gossip_bp_peers_message::signed_bp_peer& peer) -> bool {
         if (initial_msg)
            return true; // initial message has no expiration
         return peer.expiration() > head_block_time && peer.expiration() < latest_acceptable_expiration_time;
      };

      fc::lock_guard g(gossip_bps.mtx);
      auto& sig_idx = gossip_bps.index.get<by_sig>();
      for (auto i = msg.peers.begin(); i != msg.peers.end();) {
         const auto& peer = *i;
         bool have_sig = sig_idx.contains(peer.sig); // we already have it, already verified
         if (!have_sig && (!is_peer_key_valid(peer) || !is_expiration_valid(peer))) {
            if (invalid_message)
               return false;
            // peer key may have changed or been removed on-chain, do not consider that a fatal error, just remove it
            // may be expired, do not consider that fatal, just remove it
            i = msg.peers.erase(i);
         } else {
            ++i;
         }
      }

      return true; // empty is checked by caller
   }

   // thread-safe
   bool update_gossip_bps(const gossip_bp_peers_message& msg) {
      // providing us with full set
      fc::lock_guard g(gossip_bps.mtx);
      auto& idx = gossip_bps.index.get<by_producer>();
      bool diff = false;
      for (const auto& peer : msg.peers) {
         if (auto i = idx.find(std::forward_as_tuple(peer.producer_name, peer.server_endpoint(), peer.outbound_ip_address())); i != idx.end()) {
            if (i->sig != peer.sig && peer.expiration() >= i->expiration()) { // signature has changed, producer_name and server_endpoint has not changed
               assert(peer.cached_bp_peer_info); // unpacked in validate_gossip_bp_peers_message()
               gossip_bps.index.modify(i, [&peer](auto& m) {
                  m.bp_peer_info = peer.bp_peer_info;
                  m.cached_bp_peer_info = peer.cached_bp_peer_info;
                  m.sig = peer.sig;
               });
               diff = true;
            }
         } else {
            auto r = idx.equal_range(peer.producer_name);
            if (std::cmp_greater_equal(std::distance(r.first, r.second), max_bp_gossip_peers_per_producer)) {
               // remove entry with min expiration
               auto min_expiration_itr = r.first;
               auto min_expiration = min_expiration_itr->expiration();
               ++r.first;
               for (; r.first != r.second; ++r.first) {
                  if (r.first->expiration() < min_expiration) {
                     min_expiration = r.first->expiration();
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
         ex_up = idx.upper_bound(head_block_time + my_bp_gossip_peer_expiration);
         auto my_bp_account_will_expire = std::ranges::any_of(ex_lo, ex_up, [&](const auto& i) {
            return config.my_bp_gossip_accounts.contains(i.producer_name);
         });
         return my_bp_account_will_expire;
      }

      return false;
   }

   address_set_t find_gossip_bp_addresses(const name_set_t& accounts, const char* desc) const {
      address_set_t addresses;
      fc::lock_guard g(gossip_bps.mtx);
      const auto& prod_idx = gossip_bps.index.get<by_producer>();
      for (const auto& account : accounts) {
         if (auto i = config.auto_bp_addresses.find(account); i != config.auto_bp_addresses.end()) {
            fc_dlog(p2p_conn_log, "${d} manual bp peer ${p}", ("d", desc)("p", i->second));
            addresses.insert(i->second.address());
         }
         auto r = prod_idx.equal_range(account);
         for (auto i = r.first; i != r.second; ++i) {
            fc_dlog(p2p_conn_log, "${d} gossip bp peer ${p}", ("d", desc)("p", i->server_endpoint()));
            addresses.insert(i->server_endpoint());
         }
      }
      return addresses;
   }

   address_set_t all_gossip_bp_addresses(const char* desc) const {
      address_set_t addresses;
      fc::lock_guard g(gossip_bps.mtx);
      const auto& prod_idx = gossip_bps.index.get<by_producer>();
      for (auto& i : prod_idx) {
         fc_dlog(p2p_conn_log, "${d} gossip bp peer ${p}", ("d", desc)("p", i.server_endpoint()));
         addresses.insert(i.server_endpoint());
      }
      return addresses;
   }

   // thread-safe
   void connect_to_active_bp_peers() {
      // do not hold mutexes when calling resolve_and_connect which acquires connections mutex since other threads
      // can be holding connections mutex when trying to acquire these mutexes
      address_set_t addresses;
      {
         fc::lock_guard gm(mtx);
         active_bps = active_bp_accounts(active_schedule);
         fc_dlog(p2p_conn_log, "active_bps: ${a}", ("a", to_string(active_bps)));

         addresses = find_gossip_bp_addresses(active_bps, "connect");
         fc_dlog(p2p_conn_log, "active addresses: ${a}", ("a", to_string(addresses)));
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

               fc_dlog(p2p_conn_log, "pending producer schedule switches from version ${old} to ${new}",
                       ("old", pending_schedule_version)("new", schedule.version));

               auto pending_connections = active_bp_accounts(schedule.producers);

               fc_dlog(p2p_conn_log, "pending_connections: ${c}", ("c", to_string(pending_connections)));

               // do not hold mutexes when calling resolve_and_connect which acquires connections mutex since other threads
               // can be holding connections mutex when trying to acquire these mutexes
               address_set_t addresses = find_gossip_bp_addresses(pending_connections, "connect");
               for (const auto& add : addresses) {
                  self()->connections.resolve_and_connect(add, self()->get_first_p2p_address());
               }

               pending_bps = std::move(pending_connections);

               pending_schedule_version = schedule.version;
            }
         } else {
            fc_dlog(p2p_conn_log, "pending producer schedule version ${v} is being cleared", ("v", schedule.version));
            pending_bps.clear();
         }
      }
   }

   // Only called from main thread
   void on_active_schedule(const chain::producer_authority_schedule& schedule) {
      if (auto_bp_peering_enabled() && active_schedule_version != schedule.version && !self()->is_lib_catchup()) {
         /// drops any BP connection which is no longer within our scheduling proximity
         fc_dlog(p2p_conn_log, "active producer schedule switches from version ${old} to ${new}",
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

         fc_dlog(p2p_conn_log, "active_bps: ${a}", ("a", to_string(active_bps)));

         name_set_t peers_to_stay;
         std::set_union(active_bps.begin(), active_bps.end(), pending_bps.begin(), pending_bps.end(),
                        std::inserter(peers_to_stay, peers_to_stay.begin()));
         gm.unlock();

         fc_dlog(p2p_conn_log, "peers_to_stay: ${p}", ("p", to_string(peers_to_stay)));

         name_set_t peers_to_drop;
         std::set_difference(old_bps.begin(), old_bps.end(), peers_to_stay.begin(), peers_to_stay.end(),
                             std::inserter(peers_to_drop, peers_to_drop.end()));
         fc_dlog(p2p_conn_log, "peers to drop: ${p}", ("p", to_string(peers_to_drop)));

         // if we dropped out of active schedule then disconnect from all
         bool disconnect_from_all = !config.my_bp_gossip_accounts.empty() &&
                                    std::all_of(config.my_bp_gossip_accounts.begin(), config.my_bp_gossip_accounts.end(),
                                                [&](const auto& e) { return peers_to_drop.contains(e.first); });

         address_set_t addresses = disconnect_from_all
                                   ? all_gossip_bp_addresses("disconnect")
                                   : find_gossip_bp_addresses(peers_to_drop, "disconnect");
         for (const auto& add : addresses) {
            self()->connections.disconnect_gossip_connection(add);
         }

         active_schedule_version = schedule.version;
      }
   }

   // RPC called from http threads
   vector<gossip_peer> bp_gossip_peers() const {
      fc::lock_guard g(gossip_bps.mtx);
      vector<gossip_peer> peers;
      for (const auto& p : gossip_bps.index) {
         // no need to include sig
         peers.emplace_back(
            p.producer_name,
            p.server_endpoint(),
            p.outbound_ip_address(),
            p.expiration()
         );
      }
      return peers;
   }
};
} // namespace eosio::auto_bp_peering
