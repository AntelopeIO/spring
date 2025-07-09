#pragma once

#include <eosio/chain/exceptions.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <boost/algorithm/string.hpp>

#include <string>
#include <sstream>
#include <regex>

namespace eosio::net_utils {

// Longest domain name is 253 characters according to wikipedia.
// Addresses include ":port" where max port is 65535, which adds 6 chars.
// Addresses may also include ":bitrate" with suffix and separators, which adds 30 chars,
// for the maximum comma-separated value that fits in a size_t expressed in decimal plus a suffix.
// We also add our own extentions of "[:trx|:blk] - xxxxxxx", which adds 14 chars, total= 273.
// Allow for future extentions as well, hence 384.
constexpr size_t max_p2p_address_length = 253 + 6 + 30;
constexpr size_t max_handshake_str_length = 384;

namespace detail {

   inline static const std::map<std::string, size_t> prefix_multipliers{
      {"",1},{"K",pow(10,3)},{"M",pow(10,6)},{"G",pow(10, 9)},{"T",pow(10, 12)},
             {"Ki",pow(2,10)},{"Mi",pow(2,20)},{"Gi",pow(2,30)},{"Ti",pow(2,40)}
   };

   inline size_t parse_connection_rate_limit( const std::string& limit_str) {
      std::istringstream in(limit_str);
      double limit{0};
      in >> limit;
      EOS_ASSERT(limit >= 0.0, chain::plugin_config_exception, "block sync rate limit must not be negative: ${limit}", ("limit", limit_str));
      size_t block_sync_rate_limit = 0;
      if( limit > 0.0 ) {
         std::string units;
         in >> units;
         std::regex units_regex{"([KMGT]?[i]?)B/s"};
         std::smatch units_match;
         std::regex_match(units, units_match, units_regex);
         if( units.length() > 0 ) {
            EOS_ASSERT(units_match.size() == 2, chain::plugin_config_exception, "invalid block sync rate limit specification: ${limit}", ("limit", units));
            try {
               block_sync_rate_limit = boost::numeric_cast<size_t>(limit * prefix_multipliers.at(units_match[1].str()));
            } catch (boost::numeric::bad_numeric_cast&) {
               EOS_THROW(chain::plugin_config_exception, "block sync rate limit specification overflowed: ${limit}", ("limit", limit_str));
            }
         }
      }
      return block_sync_rate_limit;
   }

   /// @return host, port, remainder
   inline std::tuple<std::string, std::string, std::string> split_host_port_remainder(const std::string& endpoint_input, bool should_throw) {
      using std::string;
      // host:port[:trx|:blk][:<rate>]
      if (endpoint_input.size() > max_p2p_address_length) {
         EOS_ASSERT(!should_throw, chain::plugin_config_exception, "Address specification exceeds max p2p address length" );
         return {};
      }
      string endpoint = endpoint_input;
      boost::trim(endpoint);
      if (endpoint.empty()) {
         EOS_ASSERT(!should_throw, chain::plugin_config_exception, "Address specification is empty" );
         return {};
      }
      auto colon_count = std::count(endpoint.begin(), endpoint.end(), ':');
      string::size_type end_bracket = 0;
      if (endpoint[0] == '[') {
         end_bracket = endpoint.find(']');
         if (end_bracket == string::npos) {
            EOS_ASSERT(!should_throw, chain::plugin_config_exception,
                       "Invalid address specification ${a}, IPv6 no closing square bracket", ("a", endpoint) );
            return {};
         }
      } else if (colon_count >= 7) {
         EOS_ASSERT(!should_throw, chain::plugin_config_exception,
                    "Invalid address specification ${a}; IPv6 addresses must be enclosed in square brackets.", ("a", endpoint));
         return {};

      } else if (colon_count < 1 || colon_count > 3) {
         EOS_ASSERT(!should_throw, chain::plugin_config_exception,
                    "Invalid address specification ${a}; unexpected number of colons.", ("a", endpoint));
         return {};
      }
      string::size_type colon = endpoint.find(':', end_bracket+1);
      if (colon == string::npos) {
         EOS_ASSERT(!should_throw, chain::plugin_config_exception,
                    "Invalid address specification ${a}; missing port specification.", ("a", endpoint));
         return {};
      }
      if (end_bracket != 0 && end_bracket+1 != colon) {
         EOS_ASSERT(!should_throw, chain::plugin_config_exception,
                    "Invalid address specification ${a}; unexpected character after ']'.", ("a", endpoint));
         return {};
      }
      string::size_type colon2 = endpoint.find(':', colon + 1);
      string host = end_bracket != 0 ? endpoint.substr( 1, end_bracket - 1 ) : endpoint.substr( 0, colon );
      string port = endpoint.substr( colon + 1, colon2 == string::npos ? string::npos : colon2 - (colon + 1));
      string remainder;
      if (colon2 == string::npos) {
         auto port_end = port.find_first_not_of("0123456789");
         if (port_end != string::npos) {
            port = port.substr(0, port_end);
            remainder = port.substr( port_end );
         }
      } else {
         remainder = endpoint.substr( colon2 + 1 );
      }
      return {std::move(host), std::move(port), std::move(remainder)};
   }

} // namespace detail

   struct endpoint {
      std::string host;
      std::string port;

      std::string address() const { return host + ":" + port; }

      friend std::ostream& operator<<(std::ostream& os, const endpoint& e) { return os << e.host << ":" << e.port; }

      bool operator==(const endpoint& lhs) const = default;
      auto operator<=>(const endpoint& lhs) const = default;
   };

   /// @return host, port, type. returns empty on invalid endpoint, does not throw
   inline std::tuple<std::string, std::string, std::string> split_host_port_type(const std::string& endpoint) {
      // host:port[:trx|:blk][:<rate>]   // rate is discarded
      constexpr bool should_throw = false;
      auto [host, port, remainder] = detail::split_host_port_remainder(endpoint, should_throw);
      if (host.empty() || port.empty()) return {};

      std::string type;
      if (remainder.starts_with("blk") || remainder.starts_with("trx")) {
         type = remainder.substr(0, 3);
      }

      return {std::move(host), std::move(port), std::move(type)};
   }

   /// @return listen address and block sync rate limit (in bytes/sec) of address string
   /// @throws chain::plugin_config_exception on invalid address
   inline std::tuple<std::string, size_t> parse_listen_address( const std::string& address ) {
      constexpr bool should_throw = true;
      auto [host, port, remainder] = detail::split_host_port_remainder(address, should_throw);
      EOS_ASSERT(!host.empty() && !port.empty(), chain::plugin_config_exception,
                 "Invalid address specification ${a}; host or port missing.", ("a", address));
      auto listen_addr = host + ":" + port;
      auto limit = remainder;
      auto last_colon_location = remainder.rfind(':');
      if (last_colon_location != std::string::npos) {
         limit = std::string(remainder, last_colon_location+1);
      }
      auto block_sync_rate_limit = detail::parse_connection_rate_limit(limit);

      return {std::move(listen_addr), block_sync_rate_limit};
   }

} // namespace eosio::net_utils

FC_REFLECT(eosio::net_utils::endpoint, (host)(port))
