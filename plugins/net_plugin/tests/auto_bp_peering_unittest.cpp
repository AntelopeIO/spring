#include <boost/test/unit_test.hpp>
#include <eosio/net_plugin/auto_bp_peering.hpp>

using namespace eosio::net_utils;

struct mock_connection {
   enum class bp_connection_type { non_bp, bp_config, bp_gossip };
   bp_connection_type bp_connection = bp_connection_type::non_bp;

   bool is_open            = false;
   bool handshake_received = false;
   mock_connection(bool bp_connection, bool open, bool received)
     : bp_connection(bp_connection ? bp_connection_type::bp_config : bp_connection_type::non_bp)
     , is_open(open)
     , handshake_received(received)
   {}

   bool socket_is_open() const { return is_open; }
   bool incoming_and_handshake_received() const { return handshake_received; }
};

using namespace eosio::chain::literals;
using namespace std::literals::string_literals;

struct mock_connections_manager {
   uint32_t                     max_client_count = 0;
   std::vector<std::shared_ptr<mock_connection>> connections;

   std::function<void(std::string, std::string)> resolve_and_connect;
   std::function<void(std::string)> disconnect_gossip_connection;

   uint32_t get_max_client_count() const { return max_client_count; }

   template <typename Function>
   void for_each_connection(Function&& func) const {
      for (auto c : connections) {
         if (!func(c))
            return;
      }
   }
};

struct mock_net_plugin : eosio::auto_bp_peering::bp_connection_manager<mock_net_plugin, mock_connection> {

   bool                         lib_catchup = true;
   mock_connections_manager     connections;
   std::vector<std::string>     p2p_addresses{"0.0.0.0:9876"};
   const std::string&           get_first_p2p_address() const { return *p2p_addresses.begin(); }

   bool is_lib_catchup() { return lib_catchup; }

   void setup_test_peers() {
      set_configured_bp_peers({ "proda,127.0.0.1:8001:blk"s, "prodb,127.0.0.1:8002:trx"s, "prodc,127.0.0.1:8003"s,
                     "prodd,127.0.0.1:8004"s, "prode,127.0.0.1:8005"s, "prodf,127.0.0.1:8006"s, "prodg,127.0.0.1:8007"s,
                     "prodh,127.0.0.1:8008"s, "prodi,127.0.0.1:8009"s, "prodj,127.0.0.1:8010"s,
                     // prodk is intentionally skipped
                     "prodl,127.0.0.1:8012"s, "prodm,127.0.0.1:8013"s, "prodn,127.0.0.1:8014"s, "prodo,127.0.0.1:8015"s,
                     "prodp,127.0.0.1:8016"s, "prodq,127.0.0.1:8017"s, "prodr,127.0.0.1:8018"s, "prods,127.0.0.1:8019"s,
                     "prodt,127.0.0.1:8020"s, "produ,127.0.0.1:8021"s }, {});
   }

   fc::logger get_logger() const { return fc::logger::get(DEFAULT_LOGGER); }
};

const std::vector<std::string> peer_addresses{
   "127.0.0.1:8001"s, "127.0.0.1:8002"s, "127.0.0.1:8003"s,
   "127.0.0.1:8004"s, "127.0.0.1:8005"s, "127.0.0.1:8006"s, "127.0.0.1:8007"s,
   "127.0.0.1:8008"s, "127.0.0.1:8009"s, "127.0.0.1:8010"s,
   // prodk is intentionally skipped
   "127.0.0.1:8012"s, "127.0.0.1:8013"s, "127.0.0.1:8014"s, "127.0.0.1:8015"s,
   "127.0.0.1:8016"s, "127.0.0.1:8017"s, "127.0.0.1:8018"s, "127.0.0.1:8019"s,
   // "prodt,127.0.0.1:8020"s - prodt is not included in the schedules
   "127.0.0.1:8021"s};

BOOST_AUTO_TEST_CASE(test_set_bp_peers) {

   mock_net_plugin plugin;
   BOOST_CHECK_THROW(plugin.set_configured_bp_peers({ "producer17,127.0.0.1:8888"s }, {}), eosio::chain::plugin_config_exception);
   BOOST_CHECK_THROW(plugin.set_configured_bp_peers({ "producer1"s }, {}), eosio::chain::plugin_config_exception);

   plugin.set_configured_bp_peers({
         "producer1,127.0.0.1:8888:blk"s,
         "producer2,127.0.0.1:8889:trx"s,
         "producer3,127.0.0.1:8890"s,
         "producer4,127.0.0.1:8891"s
   }, {});
   BOOST_CHECK_EQUAL(plugin.config.auto_bp_addresses["producer1"_n], endpoint("127.0.0.1", "8888"));
   BOOST_CHECK_EQUAL(plugin.config.auto_bp_addresses["producer2"_n], endpoint("127.0.0.1", "8889"));
   BOOST_CHECK_EQUAL(plugin.config.auto_bp_addresses["producer3"_n], endpoint("127.0.0.1", "8890"));
   BOOST_CHECK_EQUAL(plugin.config.auto_bp_addresses["producer4"_n], endpoint("127.0.0.1", "8891"));

   BOOST_CHECK_EQUAL(plugin.config.auto_bp_accounts[endpoint("127.0.0.1", "8888")], "producer1"_n);
   BOOST_CHECK_EQUAL(plugin.config.auto_bp_accounts[endpoint("127.0.0.1", "8889")], "producer2"_n);
   BOOST_CHECK_EQUAL(plugin.config.auto_bp_accounts[endpoint("127.0.0.1", "8890")], "producer3"_n);
   BOOST_CHECK_EQUAL(plugin.config.auto_bp_accounts[endpoint("127.0.0.1", "8891")], "producer4"_n);
}

bool operator==(const eosio::chain::name_set_t& a, const eosio::chain::name_set_t& b) {
   return std::equal(a.begin(), a.end(), b.begin(), b.end());
}

bool operator==(const std::vector<std::string>& a, const std::vector<std::string>& b) {
   return std::equal(a.begin(), a.end(), b.begin(), b.end());
}

namespace boost::container {
std::ostream& boost_test_print_type(std::ostream& os, const eosio::chain::name_set_t& accounts) {
   os << "{";
   const char* sep = "";
   for (auto e : accounts) {
      os << sep << e.to_string();
      sep = ", ";
   }
   os << "}";
   return os;
}
} // namespace boost::container

namespace std {
std::ostream& boost_test_print_type(std::ostream& os, const std::vector<std::string>& content) {
   os << "{";
   const char* sep = "";
   for (auto e : content) {
      os << sep << e;
      sep = ", ";
   }
   os << "}";
   return os;
}
} // namespace std

const eosio::chain::producer_authority_schedule test_schedule1{
   1,
   { { "proda"_n, {} }, { "prodb"_n, {} }, { "prodc"_n, {} }, { "prodd"_n, {} }, { "prode"_n, {} }, { "prodf"_n, {} },
     { "prodg"_n, {} }, { "prodh"_n, {} }, { "prodi"_n, {} }, { "prodj"_n, {} }, { "prodk"_n, {} }, { "prodl"_n, {} },
     { "prodm"_n, {} }, { "prodn"_n, {} }, { "prodo"_n, {} }, { "prodp"_n, {} }, { "prodq"_n, {} }, { "prodr"_n, {} },
     { "prods"_n, {} },
     // { "prodt"_n, {} }, pick one to skip so not a full schedule
     { "produ"_n, {} } }
};

const eosio::chain::producer_authority_schedule test_schedule2{
   2,
   { { "proda"_n, {} }, { "prode"_n, {} }, { "prodi"_n, {} }, { "prodm"_n, {} }, { "prodp"_n, {} }, { "prods"_n, {} },
     { "prodb"_n, {} }, { "prodf"_n, {} }, { "prodj"_n, {} }, { "prodn"_n, {} }, { "prodq"_n, {} },
     // { "prodt"_n, {} }, pick one to skip so not a full schedule
     { "prodc"_n, {} }, { "prodg"_n, {} }, { "prodk"_n, {} }, { "prodo"_n, {} }, { "prodr"_n, {} }, { "produ"_n, {} },
     { "prodd"_n, {} }, { "prodh"_n, {} }, { "prodl"_n, {} } }
};

const eosio::chain::name_set_t producers_minus_prodkt{
   "proda"_n, "prodb"_n, "prodc"_n, "prodd"_n, "prode"_n, "prodf"_n,
   "prodg"_n, "prodh"_n, "prodi"_n, "prodj"_n,
   // "prodk"_n, not part of the peer addresses
   "prodl"_n,
   "prodm"_n, "prodn"_n, "prodo"_n, "prodp"_n, "prodq"_n, "prodr"_n,
   "prods"_n,
   // "prodt"_n, not part of the schedules, see above
   "produ"_n };

const eosio::chain::producer_authority_schedule reset_schedule1{ 1, {} };

BOOST_AUTO_TEST_CASE(test_on_pending_schedule) {

   mock_net_plugin plugin;
   plugin.setup_test_peers();
   plugin.pending_bps = { "prodj"_n, "prodm"_n };

   std::vector<std::string> connected_hosts;

   plugin.connections.resolve_and_connect = [&connected_hosts](std::string host, std::string p2p_address) { connected_hosts.push_back(host); };

   // make sure nothing happens when it is not in_sync
   plugin.lib_catchup = true;
   plugin.on_pending_schedule(test_schedule1);

   BOOST_CHECK_EQUAL(connected_hosts, (std::vector<std::string>{}));
   BOOST_TEST(plugin.pending_bps == (eosio::chain::name_set_t{ "prodj"_n, "prodm"_n }));
   BOOST_CHECK_EQUAL(plugin.pending_schedule_version, 0u);

   // when it is in sync and on_pending_schedule is called
   plugin.lib_catchup = false;
   plugin.on_pending_schedule(test_schedule1);

   // the pending are connected to
   BOOST_TEST(plugin.pending_bps == producers_minus_prodkt);

   // all connect to bp peers should be invoked
   std::ranges::sort(connected_hosts);
   BOOST_CHECK_EQUAL(connected_hosts, peer_addresses);

   BOOST_CHECK_EQUAL(plugin.pending_schedule_version, 1u);

   // make sure we don't change the active_schedule_version
   BOOST_CHECK_EQUAL(plugin.active_schedule_version, 0u);

   // Let's call on_pending_schedule() again, and connect shouldn't be called again
   connected_hosts.clear();
   plugin.on_pending_schedule(test_schedule1);
   BOOST_CHECK_EQUAL(connected_hosts, (std::vector<std::string>{}));

   plugin.on_pending_schedule(reset_schedule1);
   BOOST_TEST(plugin.pending_bps == eosio::chain::name_set_t{});
}

BOOST_AUTO_TEST_CASE(test_on_active_schedule1) {

   mock_net_plugin plugin;
   plugin.setup_test_peers();

   plugin.set_active_bps( { "proda"_n, "prodh"_n, "prodn"_n, "prodt"_n } );
   plugin.connections.resolve_and_connect = [](std::string host, std::string p2p_address) {};

   std::vector<std::string> disconnected_hosts;
   plugin.connections.disconnect_gossip_connection = [&disconnected_hosts](std::string host) { disconnected_hosts.push_back(host); };

   // make sure nothing happens when it is not in_sync
   plugin.lib_catchup = true;
   plugin.on_active_schedule(test_schedule1);

   BOOST_CHECK_EQUAL(disconnected_hosts, (std::vector<std::string>{}));
   BOOST_TEST(plugin.get_active_bps() == (eosio::chain::name_set_t{ "proda"_n, "prodh"_n, "prodn"_n, "prodt"_n }));
   BOOST_CHECK_EQUAL(plugin.active_schedule_version, 0u);

   // when it is in sync and on_active_schedule is called
   plugin.lib_catchup = false;
   plugin.on_pending_schedule(test_schedule1);
   plugin.on_active_schedule(test_schedule1);
   // then disconnect to prodt
   BOOST_CHECK_EQUAL(disconnected_hosts, (std::vector<std::string>{ "127.0.0.1:8020"s }));

   BOOST_TEST(plugin.get_active_bps() == producers_minus_prodkt);

   // make sure we change the active_schedule_version
   BOOST_CHECK_EQUAL(plugin.active_schedule_version, 1u);
}

BOOST_AUTO_TEST_CASE(test_on_active_schedule2) {

   mock_net_plugin plugin;
   plugin.setup_test_peers();

   plugin.set_active_bps( { "proda"_n, "prodh"_n, "prodn"_n, "prodt"_n } );
   plugin.connections.resolve_and_connect = [](std::string host, std::string p2p_address) {};
   std::vector<std::string> disconnected_hosts;
   plugin.connections.disconnect_gossip_connection = [&disconnected_hosts](std::string host) { disconnected_hosts.push_back(host); };

   // when pending and active schedules are changed simultaneously
   plugin.lib_catchup = false;
   plugin.on_pending_schedule(test_schedule2);
   plugin.on_active_schedule(test_schedule1);
   // then disconnect prodt
   BOOST_CHECK_EQUAL(disconnected_hosts, (std::vector<std::string>{ "127.0.0.1:8020"s }));

   BOOST_TEST(plugin.get_active_bps() == producers_minus_prodkt);

   // make sure we change the active_schedule_version
   BOOST_CHECK_EQUAL(plugin.active_schedule_version, 1u);
}

BOOST_AUTO_TEST_CASE(test_exceeding_connection_limit) {
   mock_net_plugin plugin;
   plugin.setup_test_peers();
   plugin.connections.max_client_count = 1;
   plugin.connections.connections = {
      std::make_shared<mock_connection>( true, true, true ),   // 0
      std::make_shared<mock_connection>( true, true, false ),  // 1
      std::make_shared<mock_connection>( true, false, true ),  // 2
      std::make_shared<mock_connection>( true, false, false ), // 3
      std::make_shared<mock_connection>( false, true, true ),  // 4
      std::make_shared<mock_connection>( false, true, false ), // 5
      std::make_shared<mock_connection>( false, true, true ),  // 6
      std::make_shared<mock_connection>( false, false, false ) // 7
   };

   BOOST_CHECK_EQUAL(plugin.num_established_clients(), 2u);

   BOOST_CHECK(!plugin.exceeding_connection_limit(plugin.connections.connections[0]));
   BOOST_CHECK(!plugin.exceeding_connection_limit(plugin.connections.connections[1]));
   BOOST_CHECK(!plugin.exceeding_connection_limit(plugin.connections.connections[2]));
   BOOST_CHECK(!plugin.exceeding_connection_limit(plugin.connections.connections[3]));
   BOOST_CHECK(plugin.exceeding_connection_limit(plugin.connections.connections[4]));
   BOOST_CHECK(!plugin.exceeding_connection_limit(plugin.connections.connections[5]));
   BOOST_CHECK(plugin.exceeding_connection_limit(plugin.connections.connections[6]));
   BOOST_CHECK(!plugin.exceeding_connection_limit(plugin.connections.connections[7]));
}

struct bp_peer_info_v2 : eosio::gossip_bp_peers_message::bp_peer_info_v1 {
   std::string extra;
};

FC_REFLECT_DERIVED(bp_peer_info_v2, (eosio::gossip_bp_peers_message::bp_peer_info_v1), (extra))

BOOST_AUTO_TEST_CASE(test_bp_peer_info_v2) {

   const eosio::chain_id_type chain_id = eosio::chain_id_type::empty_chain_id();
   fc::crypto::private_key pk = fc::crypto::private_key::generate();
   auto public_key = pk.get_public_key();

   bp_peer_info_v2 v2{{"hostname.com", "127.0.0.1", eosio::block_timestamp_type{7}}, "extra"};

   std::vector<char> packed_msg;
   {
      eosio::gossip_bp_peers_message msg;
      eosio::gossip_bp_peers_message::signed_bp_peer peer{{.version = 2, .producer_name = eosio::name("producer")}};
      peer.bp_peer_info = fc::raw::pack(v2);
      peer.sig = pk.sign(peer.digest(chain_id));
      msg.peers.emplace_back(peer);
      packed_msg = fc::raw::pack(msg);
   }

   auto msg = fc::raw::unpack<eosio::gossip_bp_peers_message>(packed_msg);

   auto& peer = msg.peers[0];

   // verify v1 can process data
   fc::crypto::public_key v1k(peer.sig, peer.digest(chain_id));
   BOOST_TEST(v1k == public_key);
   BOOST_TEST(peer.version.value == 2u);
   BOOST_TEST(peer.producer_name == eosio::name("producer"));

   // verify can unpack v1
   eosio::gossip_bp_peers_message::bp_peer_info_v1 v1 = fc::raw::unpack<eosio::gossip_bp_peers_message::bp_peer_info_v1>(peer.bp_peer_info);
   BOOST_TEST(v1.server_endpoint == "hostname.com");
   BOOST_TEST(v1.outbound_ip_address == "127.0.0.1");
   BOOST_TEST(v1.expiration == eosio::block_timestamp_type{7});

   // verify v2 can process data
   fc::crypto::public_key v2k(peer.sig, peer.digest(chain_id));
   BOOST_TEST(v2k == public_key);

   bp_peer_info_v2 v2b = fc::raw::unpack<bp_peer_info_v2>(peer.bp_peer_info);
   BOOST_TEST(v2b.server_endpoint == "hostname.com");
   BOOST_TEST(v2b.outbound_ip_address == "127.0.0.1");
   BOOST_TEST(v2b.expiration == eosio::block_timestamp_type{7});
   BOOST_TEST(v2b.expiration == eosio::block_timestamp_type{7});
   BOOST_TEST(v2b.extra == "extra");
}
