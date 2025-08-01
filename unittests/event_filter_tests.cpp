#include <algorithm>
#include <boost/test/unit_test.hpp>
#include <eosio/chain/event_filter.hpp>

#include <fc/variant.hpp>
#include <fc/io/json.hpp>
#include <fc/exception/exception.hpp>
#include <fc/log/logger.hpp>
#include <fc/time.hpp>

#include <eosio/chain/abi_serializer.hpp>

using namespace eosio;
using namespace chain;

BOOST_AUTO_TEST_SUITE(event_filter_tests)

BOOST_AUTO_TEST_CASE(event_json)
{ try {
      uint64_t tag_xfer = 0x1111;
      uint64_t tag_x    = 0x1112;
      uint64_t tag_y    = 0x1113;

      auto check_filter = [&](const tag_filter_t& tf, std::string_view msg) {
         auto s = fc::json::to_pretty_string(tf);

         auto filter = fc::json::from_string(s).as<tag_filter_t>();
         BOOST_TEST(filter == tf);

         std::cout << "-----------------------------------------------------------------\n";
         std::cout << "// " << msg;
         std::cout << tf << "\n\n";

         // std::cout << s <<  "\n\n";
      };

      // -------------------------------------------------------------------------
      check_filter(
         tag_filter_t{ tag_list_t{ pos_tag_t{{}, tag_x}}},
         "Search for a single tag value in any ordinal position\n");

      // -------------------------------------------------------------------------
      check_filter(
         tag_filter_t{ tag_list_t{ pos_tag_t{2, tag_x}}},
         "Search for a single tag value in absolute ordinal position 2\n");

      // -------------------------------------------------------------------------
      check_filter(
         tag_filter_t{ tag_list_t{ pos_tag_t{{}, tag_x}, tag_list_t::list_t{ offset_tag_t {2, tag_y}} }},
         "Search for a single tag value in relative ordinal positions (tag_x at position X, tag_y at position X+2)\n");

      // -------------------------------------------------------------------------
      check_filter(
         tag_filter_t{ tag_filter_t::and_f{ {
               tag_filter_t{ tag_list_t{ pos_tag_t{1, tag_xfer}}},
               tag_filter_t{ tag_list_t{ pos_tag_t{2, tag_x}}},
               tag_filter_t{ tag_list_t{ pos_tag_t{3, tag_y}}}}}},
         "Search for all transfers where `from == x` and `to == y`\n");

      // -------------------------------------------------------------------------
      check_filter(
         tag_filter_t{ tag_filter_t::and_f{ {
               tag_filter_t{ tag_list_t{ pos_tag_t{1, tag_xfer}}},
               tag_filter_t{ tag_filter_t::or_f{ {
                                    tag_filter_t{ tag_list_t{ pos_tag_t{2, tag_x}}},
                                    tag_filter_t{ tag_list_t{ pos_tag_t{3, tag_x}}}}}}}}},
         "Search for all transfers where `from == x` or `to == x`\n");

      // -------------------------------------------------------------------------
      check_filter(
         tag_filter_t{ tag_filter_t::and_f{ {
               tag_filter_t{ tag_list_t{ pos_tag_t{1, tag_xfer}}},
               tag_filter_t{ tag_filter_t::or_f{ {
                     tag_filter_t{ tag_filter_t::and_f{ {
                                      tag_filter_t{ tag_list_t{ pos_tag_t{2, tag_x}}},
                                      tag_filter_t{ tag_list_t{ pos_tag_t{3, tag_y}}}}}},
                     tag_filter_t{ tag_filter_t::and_f{ {
                                      tag_filter_t{ tag_list_t{ pos_tag_t{2, tag_y}}},
                                      tag_filter_t{ tag_list_t{ pos_tag_t{3, tag_x}}}}}}}}}}}},
         "Search for any transfer (any direction) between accounts `x` and `y`\n");



} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(event_json2)
{ try {

const char* test_filter = R"=====(
{
  "and_or_tags": [
    0,{"filter_list":
       [{ "and_or_tags": [2,{ "pos_tag": { "ord": 1, "tag": 4369 }}] },
        { "and_or_tags": [1,{"filter_list": [
                   {"and_or_tags": [ 2,{ "pos_tag": { "ord": 2, "tag": 4370 }}]},
                   {"and_or_tags": [ 2,{ "pos_tag": { "ord": 3, "tag": 4371 }}]}]}]}]}]}
   )=====";

   auto filter = fc::json::from_string(test_filter).as<tag_filter_t>();

} FC_LOG_AND_RETHROW() }



BOOST_AUTO_TEST_SUITE_END()
