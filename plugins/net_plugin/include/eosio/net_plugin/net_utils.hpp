#pragma once

#include <eosio/chain/exceptions.hpp>
#include <boost/numeric/conversion/cast.hpp>

#include <string>
#include <sstream>
#include <regex>

namespace eosio::net_utils {

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
   inline std::tuple<std::string, std::string, std::string> split_host_port_remainder(const std::string& peer_add, bool should_throw) {
      using std::string;
      // host:port[:trx|:blk][:<rate>]
      if (peer_add.empty()) return {};

      auto colon_count = std::count(peer_add.begin(), peer_add.end(), ':');
      string::size_type end_bracket = 0;
      if (peer_add[0] == '[') {
         end_bracket = peer_add.find(']');
         if (end_bracket == string::npos) {
            EOS_ASSERT(!should_throw, chain::plugin_config_exception,
                       "Invalid address specification ${a}, IPv6 no closing square bracket", ("a", peer_add) );
            return {};
         }
      } else if (colon_count >= 7) {
         EOS_ASSERT(!should_throw, chain::plugin_config_exception,
                    "Invalid address specification ${a}; IPv6 addresses must be enclosed in square brackets.", ("a", peer_add));
         return {};

      }
      string::size_type colon = peer_add.find(':', end_bracket+1);
      if (colon == string::npos || colon == 0) {
         return {};
      }
      string::size_type colon2 = peer_add.find(':', colon + 1);
      string host = (end_bracket > 0) ? peer_add.substr( 0, end_bracket+1 ) : peer_add.substr( 0, colon );
      string port = peer_add.substr( colon + 1, colon2 == string::npos ? string::npos : colon2 - (colon + 1));
      string remainder = colon2 == string::npos ? "" : peer_add.substr( colon2 + 1 );
      return {std::move(host), std::move(port), std::move(remainder)};
   }

} // namespace detail

   /// @return host, port, type. returns empty on invalid peer_add, does not throw
   inline std::tuple<std::string, std::string, std::string> split_host_port_type(const std::string& peer_add) {

      using std::string;
      // host:port[:trx|:blk][:<rate>]   // rate is discarded
      if (peer_add.empty()) return {};

      constexpr bool should_throw = false;
      auto [host, port, remainder] = detail::split_host_port_remainder(peer_add, should_throw);
      if (host.empty()) return {};

      string::size_type end = remainder.find_first_of( " :+=.,<>!$%^&(*)|-#@\t" ); // future proof by including most symbols without using regex
      std::string type = remainder.substr(0, end);

      return {std::move(host), std::move(port), std::move(type)};
   }

   /// @return listen address, type [trx|blk], and block sync rate limit (in bytes/sec) of address string
   /// @throws chain::plugin_config_exception on invalid address
   inline std::tuple<std::string, size_t> parse_listen_address( const std::string& address ) {
      constexpr bool should_throw = true;
      auto [host, port, remainder] = detail::split_host_port_remainder(address, should_throw);
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