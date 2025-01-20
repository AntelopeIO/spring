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

} // namespace detail

   /// @return listen address and block sync rate limit (in bytes/sec) of address string
   inline std::tuple<std::string, size_t> parse_listen_address( const std::string& address ) {
      auto listen_addr = address;
      auto limit = std::string("0");
      auto last_colon_location = address.rfind(':');
      if( auto right_bracket_location = address.find(']'); right_bracket_location != address.npos ) {
         if( std::count(address.begin()+right_bracket_location, address.end(), ':') > 1 ) {
            listen_addr = std::string(address, 0, last_colon_location);
            limit = std::string(address, last_colon_location+1);
         }
      } else {
         if( auto colon_count = std::count(address.begin(), address.end(), ':'); colon_count > 1 ) {
            EOS_ASSERT( colon_count <= 2, chain::plugin_config_exception,
                        "Invalid address specification ${addr}; IPv6 addresses must be enclosed in square brackets.", ("addr", address));
            listen_addr = std::string(address, 0, last_colon_location);
            limit = std::string(address, last_colon_location+1);
         }
      }
      auto block_sync_rate_limit = detail::parse_connection_rate_limit(limit);

      return {listen_addr, block_sync_rate_limit};
   }

   inline std::tuple<std::string, std::string, std::string> split_host_port_type(const std::string& peer_add) {
      using std::string;
      // host:port:[<trx>|<blk>]
      if (peer_add.empty()) return {};

      string::size_type p = peer_add[0] == '[' ? peer_add.find(']') : 0;
      string::size_type colon = p != string::npos ? peer_add.find(':', p) : string::npos;
      if (colon == string::npos || colon == 0) {
         return {};
      }
      string::size_type colon2 = peer_add.find(':', colon + 1);
      string::size_type end = colon2 == string::npos
            ? string::npos : peer_add.find_first_of( " :+=.,<>!$%^&(*)|-#@\t", colon2 + 1 ); // future proof by including most symbols without using regex
      string host = (p > 0) ? peer_add.substr( 1, p-1 ) : peer_add.substr( 0, colon );
      string port = peer_add.substr( colon + 1, colon2 == string::npos ? string::npos : colon2 - (colon + 1));
      string type = colon2 == string::npos ? "" : end == string::npos ?
         peer_add.substr( colon2 + 1 ) : peer_add.substr( colon2 + 1, end - (colon2 + 1) );
      return {std::move(host), std::move(port), std::move(type)};
   }

} // namespace eosio::net_utils