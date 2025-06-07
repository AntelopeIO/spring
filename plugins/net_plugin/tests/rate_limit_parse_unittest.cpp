#include <boost/test/unit_test.hpp>
#include <eosio/net_plugin/net_utils.hpp>

BOOST_AUTO_TEST_CASE(test_parse_rate_limit) {
   std::vector<std::string> p2p_addresses = {
        "0.0.0.0:9876"
      , "0.0.0.0:9776:0"
      , "0.0.0.0:9877:640KB/s"
      , "192.168.0.1:9878:20MiB/s"
      , "localhost:9879:0.5KB/s"
      , "[2001:db8:85a3:8d3:1319:8a2e:370:7348]:9876:250KB/s"
      , "[::1]:9876:250KB/s"
      , "2001:db8:85a3:8d3:1319:8a2e:370:7348:9876:250KB/s"
      , "[::1]:9876:-250KB/s"
      , "0.0.0.0:9877:640Kb/s"
      , "0.0.0.0:9877:999999999999999999999999999TiB/s"
      , "0.0.0.0:9876:trx"
      , "0.0.0.0:9776:blk:0"
      , "0.0.0.0:9877:trx:640KB/s"
      , "192.168.0.1:9878:blk:20MiB/s"
      , "localhost:9879:trx:0.5KB/s"
      , "[2001:db8:85a3:8d3:1319:8a2e:370:7348]:9876:trx:250KB/s"
      , "[::1]:9876:trx:250KB/s"
      , "2001:db8:85a3:8d3:1319:8a2e:370:7348:9876:trx:250KB/s"
      , "[::1]:9876:trx:-1KB/s"
      , "0.0.0.0:9877:trx:640Kb/s"
      , "0.0.0.0:9877:trx:999999999999999999999999999TiB/s"
      , "0.0.0.0:9875 - 84c470d"
      , "0.0.0.0:9876:trx - 84c470d"
      , "0.0.0.0:9877:trx:640KB/s - additional info"
      , "[2001:db8:85a3:8d3:1319:8a2e:370:7348]additional info:trx:640KB/s"
      , "0.0.0.0"
      , "0.0.0.0:"
      , "0.0.0.0::"
   };
   size_t which = 0;
   auto [listen_addr, block_sync_rate_limit] = eosio::net_utils::parse_listen_address(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(listen_addr, "0.0.0.0:9876");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 0u);
   std::tie(listen_addr, block_sync_rate_limit) = eosio::net_utils::parse_listen_address(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(listen_addr, "0.0.0.0:9776");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 0u);
   std::tie(listen_addr, block_sync_rate_limit) = eosio::net_utils::parse_listen_address(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(listen_addr, "0.0.0.0:9877");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 640000u);
   std::tie(listen_addr, block_sync_rate_limit) = eosio::net_utils::parse_listen_address(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(listen_addr, "192.168.0.1:9878");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 20971520u);
   std::tie(listen_addr, block_sync_rate_limit) = eosio::net_utils::parse_listen_address(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(listen_addr, "localhost:9879");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 500u);
   std::tie(listen_addr, block_sync_rate_limit) = eosio::net_utils::parse_listen_address(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(listen_addr, "2001:db8:85a3:8d3:1319:8a2e:370:7348:9876");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 250000u);
   std::tie(listen_addr, block_sync_rate_limit) = eosio::net_utils::parse_listen_address(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(listen_addr, "::1:9876");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 250000u);
   BOOST_CHECK_EXCEPTION(eosio::net_utils::parse_listen_address(p2p_addresses.at(which++)), eosio::chain::plugin_config_exception,
                         [](const eosio::chain::plugin_config_exception& e)
                         {return std::strstr(e.top_message().c_str(), "IPv6 addresses must be enclosed in square brackets");});
   BOOST_CHECK_EXCEPTION(eosio::net_utils::parse_listen_address(p2p_addresses.at(which++)), eosio::chain::plugin_config_exception,
                         [](const eosio::chain::plugin_config_exception& e)
                         {return std::strstr(e.top_message().c_str(), "block sync rate limit must not be negative");});
   BOOST_CHECK_EXCEPTION(eosio::net_utils::parse_listen_address(p2p_addresses.at(which++)), eosio::chain::plugin_config_exception,
                         [](const eosio::chain::plugin_config_exception& e)
                         {return std::strstr(e.top_message().c_str(), "invalid block sync rate limit specification");});
   BOOST_CHECK_EXCEPTION(eosio::net_utils::parse_listen_address(p2p_addresses.at(which++)), eosio::chain::plugin_config_exception,
                         [](const eosio::chain::plugin_config_exception& e)
                         {return std::strstr(e.top_message().c_str(), "block sync rate limit specification overflowed");});
   std::tie(listen_addr, block_sync_rate_limit) = eosio::net_utils::parse_listen_address(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(listen_addr, "0.0.0.0:9876");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 0u);
   std::tie(listen_addr, block_sync_rate_limit) = eosio::net_utils::parse_listen_address(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(listen_addr, "0.0.0.0:9776");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 0u);
   std::tie(listen_addr, block_sync_rate_limit) = eosio::net_utils::parse_listen_address(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(listen_addr, "0.0.0.0:9877");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 640000u);
   std::tie(listen_addr, block_sync_rate_limit) = eosio::net_utils::parse_listen_address(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(listen_addr, "192.168.0.1:9878");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 20971520u);
   std::tie(listen_addr, block_sync_rate_limit) = eosio::net_utils::parse_listen_address(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(listen_addr, "localhost:9879");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 500u);
   std::tie(listen_addr, block_sync_rate_limit) = eosio::net_utils::parse_listen_address(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(listen_addr, "2001:db8:85a3:8d3:1319:8a2e:370:7348:9876");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 250000u);
   std::tie(listen_addr, block_sync_rate_limit) = eosio::net_utils::parse_listen_address(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(listen_addr, "::1:9876");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 250000u);
   BOOST_CHECK_EXCEPTION(eosio::net_utils::parse_listen_address(p2p_addresses.at(which++)), eosio::chain::plugin_config_exception,
                         [](const eosio::chain::plugin_config_exception& e)
                         {return std::strstr(e.top_message().c_str(), "IPv6 addresses must be enclosed in square brackets");});
   BOOST_CHECK_EXCEPTION(eosio::net_utils::parse_listen_address(p2p_addresses.at(which++)), eosio::chain::plugin_config_exception,
                         [](const eosio::chain::plugin_config_exception& e)
                         {return std::strstr(e.top_message().c_str(), "block sync rate limit must not be negative");});
   BOOST_CHECK_EXCEPTION(eosio::net_utils::parse_listen_address(p2p_addresses.at(which++)), eosio::chain::plugin_config_exception,
                         [](const eosio::chain::plugin_config_exception& e)
                         {return std::strstr(e.top_message().c_str(), "invalid block sync rate limit specification");});
   BOOST_CHECK_EXCEPTION(eosio::net_utils::parse_listen_address(p2p_addresses.at(which++)), eosio::chain::plugin_config_exception,
                         [](const eosio::chain::plugin_config_exception& e)
                         {return std::strstr(e.top_message().c_str(), "block sync rate limit specification overflowed");});
   std::tie(listen_addr, block_sync_rate_limit) = eosio::net_utils::parse_listen_address(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(listen_addr, "0.0.0.0:9875");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 0u);
   std::tie(listen_addr, block_sync_rate_limit) = eosio::net_utils::parse_listen_address(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(listen_addr, "0.0.0.0:9876");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 0u);
   std::tie(listen_addr, block_sync_rate_limit) = eosio::net_utils::parse_listen_address(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(listen_addr, "0.0.0.0:9877");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 640000u);
   BOOST_CHECK_EXCEPTION(eosio::net_utils::parse_listen_address(p2p_addresses.at(which++)), eosio::chain::plugin_config_exception,
                         [](const eosio::chain::plugin_config_exception& e)
                         {return std::strstr(e.top_message().c_str(), "unexpected character after ']'");});
   BOOST_CHECK_EXCEPTION(eosio::net_utils::parse_listen_address(p2p_addresses.at(which++)), eosio::chain::plugin_config_exception,
                         [](const eosio::chain::plugin_config_exception& e)
                         {return std::strstr(e.top_message().c_str(), "unexpected number of colons");});
   BOOST_CHECK_EXCEPTION(eosio::net_utils::parse_listen_address(p2p_addresses.at(which++)), eosio::chain::plugin_config_exception,
                         [](const eosio::chain::plugin_config_exception& e)
                         {return std::strstr(e.top_message().c_str(), "host or port missing");});
   BOOST_CHECK_EXCEPTION(eosio::net_utils::parse_listen_address(p2p_addresses.at(which++)), eosio::chain::plugin_config_exception,
                         [](const eosio::chain::plugin_config_exception& e)
                         {return std::strstr(e.top_message().c_str(), "host or port missing");});
}

BOOST_AUTO_TEST_CASE(test_split_host_port_type) {
   std::vector<std::string> p2p_addresses = {
        "0.0.0.0:9876"
      , "0.0.0.0:9776:0"
      , "0.0.0.0:9877:640KB/s"
      , "192.168.0.1:9878:20MiB/s"
      , "localhost:9879:0.5KB/s"
      , "[2001:db8:85a3:8d3:1319:8a2e:370:7348]:9876:250KB/s"
      , "[::1]:9876:250KB/s"
      , "2001:db8:85a3:8d3:1319:8a2e:370:7348:9876:250KB/s"
      , "[::1]:9876:-250KB/s"
      , "0.0.0.0:9877:640Kb/s"
      , "0.0.0.0:9877:999999999999999999999999999TiB/s"
      , "0.0.0.0:9876:trx"
      , "0.0.0.0:9776:blk:0"
      , "0.0.0.0:9877:trx:640KB/s"
      , "192.168.0.1:9878:blk:20MiB/s"
      , " localhost:9879:trx:0.5KB/s"
      , "[2001:db8:85a3:8d3:1319:8a2e:370:7348]:9876:trx:250KB/s"
      , "[::1]:9876:trx:250KB/s"
      , "2001:db8:85a3:8d3:1319:8a2e:370:7348:9876:trx:250KB/s"
      , "[::1]:9876:trx:-1KB/s"
      , "0.0.0.0:9877:trx:640Kb/s"
      , "0.0.0.0:9877:trx:999999999999999999999999999TiB/s"
      , "0.0.0.0:9876 - 84c470d"
      , "0.0.0.0:9876:trx - 84c470d"
      , "0.0.0.0:9877:trx:640KB/s - additional info"
      , "[2001:db8:85a3:8d3:1319:8a2e:370:7348]additional info:trx:640KB/s"
      , "0.0.0.0"
      , "0.0.0.0:"
      , "0.0.0.0::"
   };
   size_t which = 0;
   auto [host, port, type] = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "0.0.0.0");
   BOOST_CHECK_EQUAL(port, "9876");
   BOOST_CHECK_EQUAL(type, "");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "0.0.0.0");
   BOOST_CHECK_EQUAL(port, "9776");
   BOOST_CHECK_EQUAL(type, "");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "0.0.0.0");
   BOOST_CHECK_EQUAL(port, "9877");
   BOOST_CHECK_EQUAL(type, "");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "192.168.0.1");
   BOOST_CHECK_EQUAL(port, "9878");
   BOOST_CHECK_EQUAL(type, "");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "localhost");
   BOOST_CHECK_EQUAL(port, "9879");
   BOOST_CHECK_EQUAL(type, "");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "2001:db8:85a3:8d3:1319:8a2e:370:7348");
   BOOST_CHECK_EQUAL(port, "9876");
   BOOST_CHECK_EQUAL(type, "");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "::1");
   BOOST_CHECK_EQUAL(port, "9876");
   BOOST_CHECK_EQUAL(type, "");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "");
   BOOST_CHECK_EQUAL(port, "");
   BOOST_CHECK_EQUAL(type, "");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "::1");
   BOOST_CHECK_EQUAL(port, "9876");
   BOOST_CHECK_EQUAL(type, "");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "0.0.0.0");
   BOOST_CHECK_EQUAL(port, "9877");
   BOOST_CHECK_EQUAL(type, "");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "0.0.0.0");
   BOOST_CHECK_EQUAL(port, "9877");
   BOOST_CHECK_EQUAL(type, "");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "0.0.0.0");
   BOOST_CHECK_EQUAL(port, "9876");
   BOOST_CHECK_EQUAL(type, "trx");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "0.0.0.0");
   BOOST_CHECK_EQUAL(port, "9776");
   BOOST_CHECK_EQUAL(type, "blk");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "0.0.0.0");
   BOOST_CHECK_EQUAL(port, "9877");
   BOOST_CHECK_EQUAL(type, "trx");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "192.168.0.1");
   BOOST_CHECK_EQUAL(port, "9878");
   BOOST_CHECK_EQUAL(type, "blk");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "localhost");
   BOOST_CHECK_EQUAL(port, "9879");
   BOOST_CHECK_EQUAL(type, "trx");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "2001:db8:85a3:8d3:1319:8a2e:370:7348");
   BOOST_CHECK_EQUAL(port, "9876");
   BOOST_CHECK_EQUAL(type, "trx");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "::1");
   BOOST_CHECK_EQUAL(port, "9876");
   BOOST_CHECK_EQUAL(type, "trx");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "");
   BOOST_CHECK_EQUAL(port, "");
   BOOST_CHECK_EQUAL(type, "");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "::1");
   BOOST_CHECK_EQUAL(port, "9876");
   BOOST_CHECK_EQUAL(type, "trx");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "0.0.0.0");
   BOOST_CHECK_EQUAL(port, "9877");
   BOOST_CHECK_EQUAL(type, "trx");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "0.0.0.0");
   BOOST_CHECK_EQUAL(port, "9877");
   BOOST_CHECK_EQUAL(type, "trx");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "0.0.0.0");
   BOOST_CHECK_EQUAL(port, "9876");
   BOOST_CHECK_EQUAL(type, "");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "0.0.0.0");
   BOOST_CHECK_EQUAL(port, "9876");
   BOOST_CHECK_EQUAL(type, "trx");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "0.0.0.0");
   BOOST_CHECK_EQUAL(port, "9877");
   BOOST_CHECK_EQUAL(type, "trx");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "");
   BOOST_CHECK_EQUAL(port, "");
   BOOST_CHECK_EQUAL(type, "");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "");
   BOOST_CHECK_EQUAL(port, "");
   BOOST_CHECK_EQUAL(type, "");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "");
   BOOST_CHECK_EQUAL(port, "");
   BOOST_CHECK_EQUAL(type, "");
   std::tie(host, port, type) = eosio::net_utils::split_host_port_type(p2p_addresses.at(which++));
   BOOST_CHECK_EQUAL(host, "");
   BOOST_CHECK_EQUAL(port, "");
   BOOST_CHECK_EQUAL(type, "");
}
