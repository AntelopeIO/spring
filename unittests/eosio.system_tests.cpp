#include <boost/test/unit_test.hpp>
#include <eosio/chain/contract_table_objects.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/wast_to_wasm.hpp>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <fc/log/logger.hpp>
#include <eosio/chain/exceptions.hpp>

#include "eosio_system_tester.hpp"

struct _abi_hash {
   eosio::chain::name owner;
   fc::sha256 hash;
};
FC_REFLECT( _abi_hash, (owner)(hash) );

struct connector {
   asset balance;
   double weight = .5;
};
FC_REFLECT( connector, (balance)(weight) );

using namespace eosio_system;

bool within_error(int64_t a, int64_t b, int64_t err) { return std::abs(a - b) <= err; };
bool within_one(int64_t a, int64_t b) { return within_error(a, b, 1); }

BOOST_AUTO_TEST_SUITE(eosio_system_part1_tests)

BOOST_AUTO_TEST_CASE_TEMPLATE( buysell, T, eosio_system_testers ) try {
   using namespace eosio::chain;

   T chain;

   chain.transfer( "eosio"_n, "alice1111111"_n, core_from_string("1000.0000"), "eosio"_n );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "eosio"_n, "alice1111111"_n, core_from_string("200.0000"), core_from_string("100.0000") ) );

   auto total = chain.get_total_stake( "alice1111111"_n );
   auto init_bytes =  total["ram_bytes"].as_uint64();

   const asset initial_ram_balance = chain.get_balance("eosio.ram"_n);
   const asset initial_ramfee_balance = chain.get_balance("eosio.ramfee"_n);
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("200.0000") ) );
   BOOST_REQUIRE_EQUAL( core_from_string("800.0000"), chain.get_balance( "alice1111111"_n ) );
   BOOST_REQUIRE_EQUAL( initial_ram_balance + core_from_string("199.0000"), chain.get_balance("eosio.ram"_n) );
   BOOST_REQUIRE_EQUAL( initial_ramfee_balance + core_from_string("1.0000"), chain.get_balance("eosio.ramfee"_n) );

   total = chain.get_total_stake( "alice1111111"_n );
   auto bytes = total["ram_bytes"].as_uint64();
   auto bought_bytes = bytes - init_bytes;
   wdump((init_bytes)(bought_bytes)(bytes) );

   BOOST_REQUIRE_EQUAL( true, 0 < bought_bytes );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.sellram( "alice1111111"_n, bought_bytes ) );
   BOOST_REQUIRE_EQUAL( core_from_string("998.0049"), chain.get_balance( "alice1111111"_n ) );
   total = chain.get_total_stake( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( true, total["ram_bytes"].as_uint64() == init_bytes );

   chain.transfer( "eosio"_n, "alice1111111"_n, core_from_string("100000000.0000"), "eosio"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("100000998.0049"), chain.get_balance( "alice1111111"_n ) );
   // alice buys ram for 10000000.0000, 0.5% = 50000.0000 go to ramfee
   // after fee 9950000.0000 go to bought bytes
   // when selling back bought bytes, pay 0.5% fee and get back 99.5% of 9950000.0000 = 9900250.0000
   // expected account after that is 90000998.0049 + 9900250.0000 = 99901248.0049 with a difference
   // of order 0.0001 due to rounding errors
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("10000000.0000") ) );
   BOOST_REQUIRE_EQUAL( core_from_string("90000998.0049"), chain.get_balance( "alice1111111"_n ) );

   total = chain.get_total_stake( "alice1111111"_n );
   bytes = total["ram_bytes"].as_uint64();
   bought_bytes = bytes - init_bytes;
   wdump((init_bytes)(bought_bytes)(bytes) );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.sellram( "alice1111111"_n, bought_bytes ) );
   total = chain.get_total_stake( "alice1111111"_n );

   bytes = total["ram_bytes"].as_uint64();
   bought_bytes = bytes - init_bytes;
   wdump((init_bytes)(bought_bytes)(bytes) );

   BOOST_REQUIRE_EQUAL( true, total["ram_bytes"].as_uint64() == init_bytes );
   BOOST_REQUIRE_EQUAL( core_from_string("99901248.0048"), chain.get_balance( "alice1111111"_n ) );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("10.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("10.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("10.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("30.0000") ) );
   BOOST_REQUIRE_EQUAL( core_from_string("99900688.0048"), chain.get_balance( "alice1111111"_n ) );

   auto newtotal = chain.get_total_stake( "alice1111111"_n );

   auto newbytes = newtotal["ram_bytes"].as_uint64();
   bought_bytes = newbytes - bytes;
   wdump((newbytes)(bytes)(bought_bytes) );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.sellram( "alice1111111"_n, bought_bytes ) );
   BOOST_REQUIRE_EQUAL( core_from_string("99901242.4187"), chain.get_balance( "alice1111111"_n ) );

   newtotal = chain.get_total_stake( "alice1111111"_n );
   auto startbytes = newtotal["ram_bytes"].as_uint64();

   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("10000000.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("10000000.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("10000000.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("10000000.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("10000000.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("100000.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("100000.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("100000.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("300000.0000") ) );
   BOOST_REQUIRE_EQUAL( core_from_string("49301242.4187"), chain.get_balance( "alice1111111"_n ) );

   auto finaltotal = chain.get_total_stake( "alice1111111"_n );
   auto endbytes = finaltotal["ram_bytes"].as_uint64();

   bought_bytes = endbytes - startbytes;
   wdump((startbytes)(endbytes)(bought_bytes) );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.sellram( "alice1111111"_n, bought_bytes ) );

   BOOST_REQUIRE_EQUAL( false, chain.get_row_by_account( config::system_account_name, config::system_account_name,
                                                   "rammarket"_n, account_name(symbol{SY(4,RAMCORE)}.value()) ).empty() );

   auto get_ram_market = [&chain]() -> fc::variant {
      vector<char> data = chain.get_row_by_account( config::system_account_name, config::system_account_name,
                                              "rammarket"_n, account_name(symbol{SY(4,RAMCORE)}.value()) );
      BOOST_REQUIRE( !data.empty() );
      return chain.abi_ser.binary_to_variant("exchange_state", data, abi_serializer::create_yield_function(chain.abi_serializer_max_time));
   };

   {
      chain.transfer( config::system_account_name, "alice1111111"_n, core_from_string("10000000.0000"), config::system_account_name );
      uint64_t bytes0 = chain.get_total_stake( "alice1111111"_n )["ram_bytes"].as_uint64();

      auto market = get_ram_market();
      const asset r0 = market["base"].template as<connector>().balance;
      const asset e0 = market["quote"].template as<connector>().balance;
      BOOST_REQUIRE_EQUAL( asset::from_string("0 RAM").get_symbol(),     r0.get_symbol() );
      BOOST_REQUIRE_EQUAL( core_from_string("0.0000").get_symbol(), e0.get_symbol() );

      const asset payment = core_from_string("10000000.0000");
      BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, payment ) );
      uint64_t bytes1 = chain.get_total_stake( "alice1111111"_n )["ram_bytes"].as_uint64();

      const int64_t fee = (payment.get_amount() + 199) / 200;
      const double net_payment = payment.get_amount() - fee;
      const uint64_t expected_delta = net_payment * r0.get_amount() / ( net_payment + e0.get_amount() );

      BOOST_REQUIRE_EQUAL( expected_delta, bytes1 -  bytes0 );
   }

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( stake_unstake, T, eosio_system_testers ) try {
   T chain;
   chain.cross_15_percent_threshold();

   chain.produce_blocks( 10 );
   chain.produce_block( fc::hours(3*24) );

   BOOST_REQUIRE_EQUAL( core_from_string("0.0000"), chain.get_balance( "alice1111111"_n ) );
   chain.transfer( "eosio"_n, "alice1111111"_n, core_from_string("1000.0000"), "eosio"_n );

   BOOST_REQUIRE_EQUAL( core_from_string("1000.0000"), chain.get_balance( "alice1111111"_n ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "eosio"_n, "alice1111111"_n, core_from_string("200.0000"), core_from_string("100.0000") ) );

   auto total = chain.get_total_stake("alice1111111"_n);
   BOOST_REQUIRE_EQUAL( core_from_string("210.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("110.0000"), total["cpu_weight"].template as<asset>());

   const auto init_eosio_stake_balance = chain.get_balance( "eosio.stake"_n );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, "alice1111111"_n, core_from_string("200.0000"), core_from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( core_from_string("700.0000"), chain.get_balance( "alice1111111"_n ) );
   BOOST_REQUIRE_EQUAL( init_eosio_stake_balance + core_from_string("300.0000"), chain.get_balance( "eosio.stake"_n ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.unstake( "alice1111111"_n, "alice1111111"_n, core_from_string("200.0000"), core_from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( core_from_string("700.0000"), chain.get_balance( "alice1111111"_n ) );

   chain.produce_block( fc::hours(3*24-1) );
   chain.produce_blocks(1);
   // testing balance still the same
   BOOST_REQUIRE_EQUAL( core_from_string("700.0000"), chain.get_balance( "alice1111111"_n ) );
   BOOST_REQUIRE_EQUAL( init_eosio_stake_balance + core_from_string("300.0000"), chain.get_balance( "eosio.stake"_n ) );
   // call refund expected to fail too early
   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg("refund is not available yet"),
                       chain.push_action( "alice1111111"_n, "refund"_n, mvo()("owner", "alice1111111"_n) ) );

   // after 1 hour refund ready
   chain.produce_block( fc::hours(1) );
   chain.produce_blocks(1);
   // now we can do the refund
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "refund"_n, mvo()("owner", "alice1111111"_n) ) );
   BOOST_REQUIRE_EQUAL( core_from_string("1000.0000"), chain.get_balance( "alice1111111"_n ) );
   BOOST_REQUIRE_EQUAL( init_eosio_stake_balance, chain.get_balance( "eosio.stake"_n ) );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, "bob111111111"_n, core_from_string("200.0000"), core_from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( core_from_string("700.0000"), chain.get_balance( "alice1111111"_n ) );
   total = chain.get_total_stake("bob111111111"_n);
   BOOST_REQUIRE_EQUAL( core_from_string("210.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("110.0000"), total["cpu_weight"].template as<asset>());

   total = chain.get_total_stake( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("210.0000").get_amount(), total["net_weight"].template as<asset>().get_amount() );
   BOOST_REQUIRE_EQUAL( core_from_string("110.0000").get_amount(), total["cpu_weight"].template as<asset>().get_amount() );

   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n, core_from_string("300.0000")), chain.get_voter_info( "alice1111111"_n ) );

   auto bytes = total["ram_bytes"].as_uint64();
   BOOST_REQUIRE_EQUAL( true, 0 < bytes );

   //unstake from bob111111111
   BOOST_REQUIRE_EQUAL( chain.success(), chain.unstake( "alice1111111"_n, "bob111111111"_n, core_from_string("200.0000"), core_from_string("100.0000") ) );
   total = chain.get_total_stake("bob111111111"_n);
   BOOST_REQUIRE_EQUAL( core_from_string("10.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("10.0000"), total["cpu_weight"].template as<asset>());
   chain.produce_block( fc::hours(3*24-1) );
   chain.produce_blocks(1);
   BOOST_REQUIRE_EQUAL( core_from_string("700.0000"), chain.get_balance( "alice1111111"_n ) );
   //after 3 days funds should be released
   chain.produce_block( fc::hours(1) );
   chain.produce_blocks(1);

   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n, core_from_string("0.0000") ), chain.get_voter_info( "alice1111111"_n ) );
   chain.produce_blocks(1);
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "refund"_n, mvo()("owner", "alice1111111"_n) ) );
   BOOST_REQUIRE_EQUAL( core_from_string("1000.0000"), chain.get_balance( "alice1111111"_n ) );
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( stake_unstake_with_transfer, T, eosio_system_testers ) try {
   T chain;
   chain.cross_15_percent_threshold();

   BOOST_REQUIRE_EQUAL( core_from_string("0.0000"), chain.get_balance( "alice1111111"_n ) );

   //eosio stakes for alice with transfer flag

   chain.transfer( "eosio"_n, "bob111111111"_n, core_from_string("1000.0000"), "eosio"_n );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake_with_transfer( "bob111111111"_n, "alice1111111"_n, core_from_string("200.0000"), core_from_string("100.0000") ) );

   //check that alice has both bandwidth and voting power
   auto total = chain.get_total_stake("alice1111111"_n);
   BOOST_REQUIRE_EQUAL( core_from_string("210.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("110.0000"), total["cpu_weight"].template as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n, core_from_string("300.0000")), chain.get_voter_info( "alice1111111"_n ) );

   BOOST_REQUIRE_EQUAL( core_from_string("0.0000"), chain.get_balance( "alice1111111"_n ) );

   //alice stakes for herself
   chain.transfer("eosio"_n, "alice1111111"_n, core_from_string("1000.0000"), "eosio"_n );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, "alice1111111"_n, core_from_string("200.0000"), core_from_string("100.0000") ) );
   //now alice's stake should be equal to transferred from eosio + own stake
   total = chain.get_total_stake("alice1111111"_n);
   BOOST_REQUIRE_EQUAL( core_from_string("700.0000"), chain.get_balance( "alice1111111"_n ) );
   BOOST_REQUIRE_EQUAL( core_from_string("410.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("210.0000"), total["cpu_weight"].template as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n, core_from_string("600.0000")), chain.get_voter_info( "alice1111111"_n ) );

   //alice can unstake everything (including what was transferred)
   BOOST_REQUIRE_EQUAL( chain.success(), chain.unstake( "alice1111111"_n, "alice1111111"_n, core_from_string("400.0000"), core_from_string("200.0000") ) );
   BOOST_REQUIRE_EQUAL( core_from_string("700.0000"), chain.get_balance( "alice1111111"_n ) );

   chain.produce_block( fc::hours(3*24-1) );
   chain.produce_blocks(1);
   BOOST_REQUIRE_EQUAL( core_from_string("700.0000"), chain.get_balance( "alice1111111"_n ) );
   //after 3 days funds should be released

   chain.produce_block( fc::hours(1) );
   chain.produce_blocks(1);

   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "refund"_n, mvo()("owner", "alice1111111"_n) ) );
   BOOST_REQUIRE_EQUAL( core_from_string("1300.0000"), chain.get_balance( "alice1111111"_n ) );

   //stake should be equal to what was staked in constructor, voting power should be 0
   total = chain.get_total_stake("alice1111111"_n);
   BOOST_REQUIRE_EQUAL( core_from_string("10.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("10.0000"), total["cpu_weight"].template as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n, core_from_string("0.0000")), chain.get_voter_info( "alice1111111"_n ) );

   // Now alice stakes to bob with transfer flag
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake_with_transfer( "alice1111111"_n, "bob111111111"_n, core_from_string("100.0000"), core_from_string("100.0000") ) );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( stake_to_self_with_transfer, T, eosio_system_testers ) try {
   T chain;
   chain.cross_15_percent_threshold();

   BOOST_REQUIRE_EQUAL( core_from_string("0.0000"), chain.get_balance( "alice1111111"_n ) );
   chain.transfer("eosio"_n, "alice1111111"_n, core_from_string("1000.0000"), "eosio"_n );

   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg("cannot use transfer flag if delegating to self"),
                        chain.stake_with_transfer( "alice1111111"_n, "alice1111111"_n, core_from_string("200.0000"), core_from_string("100.0000") )
   );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( stake_while_pending_refund, T, eosio_system_testers ) try {
   T chain;
   chain.cross_15_percent_threshold();

   BOOST_REQUIRE_EQUAL( core_from_string("0.0000"), chain.get_balance( "alice1111111"_n ) );

   //eosio stakes for alice with transfer flag
   chain.transfer("eosio"_n, "bob111111111"_n, core_from_string("1000.0000"), "eosio"_n );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake_with_transfer( "bob111111111"_n, "alice1111111"_n, core_from_string("200.0000"), core_from_string("100.0000") ) );

   //check that alice has both bandwidth and voting power
   auto total = chain.get_total_stake("alice1111111"_n);
   BOOST_REQUIRE_EQUAL( core_from_string("210.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("110.0000"), total["cpu_weight"].template as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n, core_from_string("300.0000")), chain.get_voter_info( "alice1111111"_n ) );

   BOOST_REQUIRE_EQUAL( core_from_string("0.0000"), chain.get_balance( "alice1111111"_n ) );

   //alice stakes for herself
   chain.transfer("eosio"_n, "alice1111111"_n, core_from_string("1000.0000"), "eosio"_n );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, "alice1111111"_n, core_from_string("200.0000"), core_from_string("100.0000") ) );
   //now alice's stake should be equal to transferred from eosio + own stake
   total = chain.get_total_stake("alice1111111"_n);
   BOOST_REQUIRE_EQUAL( core_from_string("700.0000"), chain.get_balance( "alice1111111"_n ) );
   BOOST_REQUIRE_EQUAL( core_from_string("410.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("210.0000"), total["cpu_weight"].template as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n, core_from_string("600.0000")), chain.get_voter_info( "alice1111111"_n ) );

   //alice can unstake everything (including what was transferred)
   BOOST_REQUIRE_EQUAL( chain.success(), chain.unstake( "alice1111111"_n, "alice1111111"_n, core_from_string("400.0000"), core_from_string("200.0000") ) );
   BOOST_REQUIRE_EQUAL( core_from_string("700.0000"), chain.get_balance( "alice1111111"_n ) );

   chain.produce_block( fc::hours(3*24-1) );
   chain.produce_blocks(1);
   BOOST_REQUIRE_EQUAL( core_from_string("700.0000"), chain.get_balance( "alice1111111"_n ) );
   //after 3 days funds should be released

   chain.produce_block( fc::hours(1) );
   chain.produce_blocks(1);

   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "refund"_n, mvo()("owner", "alice1111111"_n) ) );
   BOOST_REQUIRE_EQUAL( core_from_string("1300.0000"), chain.get_balance( "alice1111111"_n ) );

   //stake should be equal to what was staked in constructor, voting power should be 0
   total = chain.get_total_stake("alice1111111"_n);
   BOOST_REQUIRE_EQUAL( core_from_string("10.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("10.0000"), total["cpu_weight"].template as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n, core_from_string("0.0000")), chain.get_voter_info( "alice1111111"_n ) );

   // Now alice stakes to bob with transfer flag
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake_with_transfer( "alice1111111"_n, "bob111111111"_n, core_from_string("100.0000"), core_from_string("100.0000") ) );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( fail_without_auth, T, eosio_system_testers ) try {
   T chain;
   chain.cross_15_percent_threshold();

   chain.issue_and_transfer( "alice1111111"_n, core_from_string("1000.0000"),  config::system_account_name );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "eosio"_n, "alice1111111"_n, core_from_string("2000.0000"), core_from_string("1000.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, "bob111111111"_n, core_from_string("10.0000"), core_from_string("10.0000") ) );

   BOOST_REQUIRE_EQUAL( chain.error("missing authority of alice1111111"),
                        chain.push_action( "alice1111111"_n, "delegatebw"_n, mvo()
                                    ("from",     "alice1111111"_n)
                                    ("receiver", "bob111111111"_n)
                                    ("stake_net_quantity", core_from_string("10.0000"))
                                    ("stake_cpu_quantity", core_from_string("10.0000"))
                                    ("transfer", 0 )
                                    ,false
                        )
   );

   BOOST_REQUIRE_EQUAL( chain.error("missing authority of alice1111111"),
                        chain.push_action("alice1111111"_n, "undelegatebw"_n, mvo()
                                    ("from",     "alice1111111"_n)
                                    ("receiver", "bob111111111"_n)
                                    ("unstake_net_quantity", core_from_string("200.0000"))
                                    ("unstake_cpu_quantity", core_from_string("100.0000"))
                                    ("transfer", 0 )
                                    ,false
                        )
   );
   //REQUIRE_MATCHING_OBJECT( , chain.get_voter_info( "alice1111111"_n ) );
} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_CASE_TEMPLATE( stake_negative, T, eosio_system_testers ) try {
   T chain;
   chain.issue_and_transfer( "alice1111111"_n, core_from_string("1000.0000"),  config::system_account_name );

   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg("must stake a positive amount"),
                        chain.stake( "alice1111111"_n, core_from_string("-0.0001"), core_from_string("0.0000") )
   );

   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg("must stake a positive amount"),
                        chain.stake( "alice1111111"_n, core_from_string("0.0000"), core_from_string("-0.0001") )
   );

   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg("must stake a positive amount"),
                        chain.stake( "alice1111111"_n, core_from_string("00.0000"), core_from_string("00.0000") )
   );

   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg("must stake a positive amount"),
                        chain.stake( "alice1111111"_n, core_from_string("0.0000"), core_from_string("00.0000") )

   );

   BOOST_REQUIRE_EQUAL( true, chain.get_voter_info( "alice1111111"_n ).is_null() );
} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_CASE_TEMPLATE( unstake_negative, T, eosio_system_testers ) try {
   T chain;
   chain.issue_and_transfer( "alice1111111"_n, core_from_string("1000.0000"),  config::system_account_name );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, "bob111111111"_n, core_from_string("200.0001"), core_from_string("100.0001") ) );

   auto total = chain.get_total_stake( "bob111111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("210.0001"), total["net_weight"].template as<asset>());
   auto vinfo = chain.get_voter_info("alice1111111"_n );
   wdump((vinfo));
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n, core_from_string("300.0002") ), chain.get_voter_info( "alice1111111"_n ) );


   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg("must unstake a positive amount"),
                        chain.unstake( "alice1111111"_n, "bob111111111"_n, core_from_string("-1.0000"), core_from_string("0.0000") )
   );

   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg("must unstake a positive amount"),
                        chain.unstake( "alice1111111"_n, "bob111111111"_n, core_from_string("0.0000"), core_from_string("-1.0000") )
   );

   //unstake all zeros
   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg("must unstake a positive amount"),
                        chain.unstake( "alice1111111"_n, "bob111111111"_n, core_from_string("0.0000"), core_from_string("0.0000") )

   );

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_CASE_TEMPLATE( unstake_more_than_at_stake, T, eosio_system_testers ) try {
   T chain;
   chain.cross_15_percent_threshold();

   chain.issue_and_transfer( "alice1111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, core_from_string("200.0000"), core_from_string("100.0000") ) );

   auto total = chain.get_total_stake( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("210.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("110.0000"), total["cpu_weight"].template as<asset>());

   BOOST_REQUIRE_EQUAL( core_from_string("700.0000"), chain.get_balance( "alice1111111"_n ) );

   //trying to unstake more net bandwidth than at stake

   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg("insufficient staked net bandwidth"),
                        chain.unstake( "alice1111111"_n, core_from_string("200.0001"), core_from_string("0.0000") )
   );

   //trying to unstake more cpu bandwidth than at stake
   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg("insufficient staked cpu bandwidth"),
                        chain.unstake( "alice1111111"_n, core_from_string("0.0000"), core_from_string("100.0001") )

   );

   //check that nothing has changed
   total = chain.get_total_stake( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("210.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("110.0000"), total["cpu_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("700.0000"), chain.get_balance( "alice1111111"_n ) );
} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_CASE_TEMPLATE( delegate_to_another_user, T, eosio_system_testers ) try {
   T chain;
   chain.cross_15_percent_threshold();

   chain.issue_and_transfer( "alice1111111"_n, core_from_string("1000.0000"),  config::system_account_name );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake ( "alice1111111"_n, "bob111111111"_n, core_from_string("200.0000"), core_from_string("100.0000") ) );

   auto total = chain.get_total_stake( "bob111111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("210.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("110.0000"), total["cpu_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("700.0000"), chain.get_balance( "alice1111111"_n ) );
   //all voting power goes to alice1111111
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n, core_from_string("300.0000") ), chain.get_voter_info( "alice1111111"_n ) );
   //but not to bob111111111
   BOOST_REQUIRE_EQUAL( true, chain.get_voter_info( "bob111111111"_n ).is_null() );

   //bob111111111 should not be able to unstake what was staked by alice1111111
   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg("insufficient staked cpu bandwidth"),
                        chain.unstake( "bob111111111"_n, core_from_string("0.0000"), core_from_string("10.0000") )

   );
   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg("insufficient staked net bandwidth"),
                        chain.unstake( "bob111111111"_n, core_from_string("10.0000"),  core_from_string("0.0000") )
   );

   chain.issue_and_transfer( "carol1111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "carol1111111"_n, "bob111111111"_n, core_from_string("20.0000"), core_from_string("10.0000") ) );
   total = chain.get_total_stake( "bob111111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("230.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("120.0000"), total["cpu_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("970.0000"), chain.get_balance( "carol1111111"_n ) );
   REQUIRE_MATCHING_OBJECT( voter( "carol1111111"_n, core_from_string("30.0000") ), chain.get_voter_info( "carol1111111"_n ) );

   //alice1111111 should not be able to unstake money staked by carol1111111

   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg("insufficient staked net bandwidth"),
                        chain.unstake( "alice1111111"_n, "bob111111111"_n, core_from_string("2001.0000"), core_from_string("1.0000") )
   );

   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg("insufficient staked cpu bandwidth"),
                        chain.unstake( "alice1111111"_n, "bob111111111"_n, core_from_string("1.0000"), core_from_string("101.0000") )

   );

   total = chain.get_total_stake( "bob111111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("230.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("120.0000"), total["cpu_weight"].template as<asset>());
   //balance should not change after unsuccessful attempts to unstake
   BOOST_REQUIRE_EQUAL( core_from_string("700.0000"), chain.get_balance( "alice1111111"_n ) );
   //voting power too
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n, core_from_string("300.0000") ), chain.get_voter_info( "alice1111111"_n ) );
   REQUIRE_MATCHING_OBJECT( voter( "carol1111111"_n, core_from_string("30.0000") ), chain.get_voter_info( "carol1111111"_n ) );
   BOOST_REQUIRE_EQUAL( true, chain.get_voter_info( "bob111111111"_n ).is_null() );
} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_CASE_TEMPLATE( stake_unstake_separate, T, eosio_system_testers ) try {
   T chain;
   chain.cross_15_percent_threshold();

   chain.issue_and_transfer( "alice1111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( core_from_string("1000.0000"), chain.get_balance( "alice1111111"_n ) );

   //everything at once
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, core_from_string("10.0000"), core_from_string("20.0000") ) );
   auto total = chain.get_total_stake( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("20.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("30.0000"), total["cpu_weight"].template as<asset>());

   //cpu
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, core_from_string("100.0000"), core_from_string("0.0000") ) );
   total = chain.get_total_stake( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("120.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("30.0000"), total["cpu_weight"].template as<asset>());

   //net
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, core_from_string("0.0000"), core_from_string("200.0000") ) );
   total = chain.get_total_stake( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("120.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("230.0000"), total["cpu_weight"].template as<asset>());

   //unstake cpu
   BOOST_REQUIRE_EQUAL( chain.success(), chain.unstake( "alice1111111"_n, core_from_string("100.0000"), core_from_string("0.0000") ) );
   total = chain.get_total_stake( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("20.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("230.0000"), total["cpu_weight"].template as<asset>());

   //unstake net
   BOOST_REQUIRE_EQUAL( chain.success(), chain.unstake( "alice1111111"_n, core_from_string("0.0000"), core_from_string("200.0000") ) );
   total = chain.get_total_stake( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("20.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("30.0000"), total["cpu_weight"].template as<asset>());
} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_CASE_TEMPLATE( adding_stake_partial_unstake, T, eosio_system_testers ) try {
   T chain;
   chain.cross_15_percent_threshold();

   chain.issue_and_transfer( "alice1111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, "bob111111111"_n, core_from_string("200.0000"), core_from_string("100.0000") ) );

   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n, core_from_string("300.0000") ), chain.get_voter_info( "alice1111111"_n ) );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, "bob111111111"_n, core_from_string("100.0000"), core_from_string("50.0000") ) );

   auto total = chain.get_total_stake( "bob111111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("310.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("160.0000"), total["cpu_weight"].template as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n, core_from_string("450.0000") ), chain.get_voter_info( "alice1111111"_n ) );
   BOOST_REQUIRE_EQUAL( core_from_string("550.0000"), chain.get_balance( "alice1111111"_n ) );

   //unstake a share
   BOOST_REQUIRE_EQUAL( chain.success(), chain.unstake( "alice1111111"_n, "bob111111111"_n, core_from_string("150.0000"), core_from_string("75.0000") ) );

   total = chain.get_total_stake( "bob111111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("160.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("85.0000"), total["cpu_weight"].template as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n, core_from_string("225.0000") ), chain.get_voter_info( "alice1111111"_n ) );

   //unstake more
   BOOST_REQUIRE_EQUAL( chain.success(), chain.unstake( "alice1111111"_n, "bob111111111"_n, core_from_string("50.0000"), core_from_string("25.0000") ) );
   total = chain.get_total_stake( "bob111111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("110.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("60.0000"), total["cpu_weight"].template as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n, core_from_string("150.0000") ), chain.get_voter_info( "alice1111111"_n ) );

   //combined amount should be available only in 3 days
   chain.produce_block( fc::days(2) );
   chain.produce_blocks(1);
   BOOST_REQUIRE_EQUAL( core_from_string("550.0000"), chain.get_balance( "alice1111111"_n ) );
   chain.produce_block( fc::days(1) );
   chain.produce_blocks(1);
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "refund"_n, mvo()("owner", "alice1111111"_n) ) );
   BOOST_REQUIRE_EQUAL( core_from_string("850.0000"), chain.get_balance( "alice1111111"_n ) );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( stake_from_refund, T, eosio_system_testers ) try {
   T chain;
   chain.cross_15_percent_threshold();

   chain.issue_and_transfer( "alice1111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, "alice1111111"_n, core_from_string("200.0000"), core_from_string("100.0000") ) );

   auto total = chain.get_total_stake( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("210.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("110.0000"), total["cpu_weight"].template as<asset>());

   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, "bob111111111"_n, core_from_string("50.0000"), core_from_string("50.0000") ) );

   total = chain.get_total_stake( "bob111111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("60.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("60.0000"), total["cpu_weight"].template as<asset>());

   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n, core_from_string("400.0000") ), chain.get_voter_info( "alice1111111"_n ) );
   BOOST_REQUIRE_EQUAL( core_from_string("600.0000"), chain.get_balance( "alice1111111"_n ) );

   //unstake a share
   BOOST_REQUIRE_EQUAL( chain.success(), chain.unstake( "alice1111111"_n, "alice1111111"_n, core_from_string("100.0000"), core_from_string("50.0000") ) );
   total = chain.get_total_stake( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("110.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("60.0000"), total["cpu_weight"].template as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n, core_from_string("250.0000") ), chain.get_voter_info( "alice1111111"_n ) );
   BOOST_REQUIRE_EQUAL( core_from_string("600.0000"), chain.get_balance( "alice1111111"_n ) );
   auto refund = chain.get_refund_request( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("100.0000"), refund["net_amount"].template as<asset>() );
   BOOST_REQUIRE_EQUAL( core_from_string( "50.0000"), refund["cpu_amount"].template as<asset>() );
   //XXX auto request_time = refund["request_time"].as_int64();

   //alice delegates to bob, should pull from liquid balance not refund
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, "bob111111111"_n, core_from_string("50.0000"), core_from_string("50.0000") ) );
   total = chain.get_total_stake( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("110.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("60.0000"), total["cpu_weight"].template as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n, core_from_string("350.0000") ), chain.get_voter_info( "alice1111111"_n ) );
   BOOST_REQUIRE_EQUAL( core_from_string("500.0000"), chain.get_balance( "alice1111111"_n ) );
   refund = chain.get_refund_request( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("100.0000"), refund["net_amount"].template as<asset>() );
   BOOST_REQUIRE_EQUAL( core_from_string( "50.0000"), refund["cpu_amount"].template as<asset>() );

   //stake less than pending refund, entire amount should be taken from refund
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, "alice1111111"_n, core_from_string("50.0000"), core_from_string("25.0000") ) );
   total = chain.get_total_stake( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("160.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("85.0000"), total["cpu_weight"].template as<asset>());
   refund = chain.get_refund_request( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("50.0000"), refund["net_amount"].template as<asset>() );
   BOOST_REQUIRE_EQUAL( core_from_string("25.0000"), refund["cpu_amount"].template as<asset>() );
   //request time should stay the same
   //BOOST_REQUIRE_EQUAL( request_time, refund["request_time"].as_int64() );
   //balance should stay the same
   BOOST_REQUIRE_EQUAL( core_from_string("500.0000"), chain.get_balance( "alice1111111"_n ) );

   //stake exactly pending refund amount
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, "alice1111111"_n, core_from_string("50.0000"), core_from_string("25.0000") ) );
   total = chain.get_total_stake( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("210.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("110.0000"), total["cpu_weight"].template as<asset>());
   //pending refund should be removed
   refund = chain.get_refund_request( "alice1111111"_n );
   BOOST_TEST_REQUIRE( refund.is_null() );
   //balance should stay the same
   BOOST_REQUIRE_EQUAL( core_from_string("500.0000"), chain.get_balance( "alice1111111"_n ) );

   //create pending refund again
   BOOST_REQUIRE_EQUAL( chain.success(), chain.unstake( "alice1111111"_n, "alice1111111"_n, core_from_string("200.0000"), core_from_string("100.0000") ) );
   total = chain.get_total_stake( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("10.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("10.0000"), total["cpu_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("500.0000"), chain.get_balance( "alice1111111"_n ) );
   refund = chain.get_refund_request( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("200.0000"), refund["net_amount"].template as<asset>() );
   BOOST_REQUIRE_EQUAL( core_from_string("100.0000"), refund["cpu_amount"].template as<asset>() );

   //stake more than pending refund
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, "alice1111111"_n, core_from_string("300.0000"), core_from_string("200.0000") ) );
   total = chain.get_total_stake( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("310.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("210.0000"), total["cpu_weight"].template as<asset>());
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n, core_from_string("700.0000") ), chain.get_voter_info( "alice1111111"_n ) );
   refund = chain.get_refund_request( "alice1111111"_n );
   BOOST_TEST_REQUIRE( refund.is_null() );
   //200 core tokens should be taken from alice's account
   BOOST_REQUIRE_EQUAL( core_from_string("300.0000"), chain.get_balance( "alice1111111"_n ) );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( stake_to_another_user_not_from_refund, T, eosio_system_testers ) try {
   T chain;
   chain.cross_15_percent_threshold();

   chain.issue_and_transfer( "alice1111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, core_from_string("200.0000"), core_from_string("100.0000") ) );

   auto total = chain.get_total_stake( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("210.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("110.0000"), total["cpu_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("700.0000"), chain.get_balance( "alice1111111"_n ) );

   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n, core_from_string("300.0000") ), chain.get_voter_info( "alice1111111"_n ) );
   BOOST_REQUIRE_EQUAL( core_from_string("700.0000"), chain.get_balance( "alice1111111"_n ) );

   //unstake
   BOOST_REQUIRE_EQUAL( chain.success(), chain.unstake( "alice1111111"_n, core_from_string("200.0000"), core_from_string("100.0000") ) );
   auto refund = chain.get_refund_request( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("200.0000"), refund["net_amount"].template as<asset>() );
   BOOST_REQUIRE_EQUAL( core_from_string("100.0000"), refund["cpu_amount"].template as<asset>() );
   //auto orig_request_time = refund["request_time"].as_int64();

   //stake to another user
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, "bob111111111"_n, core_from_string("200.0000"), core_from_string("100.0000") ) );
   total = chain.get_total_stake( "bob111111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("210.0000"), total["net_weight"].template as<asset>());
   BOOST_REQUIRE_EQUAL( core_from_string("110.0000"), total["cpu_weight"].template as<asset>());
   //stake should be taken from alice's balance, and refund request should stay the same
   BOOST_REQUIRE_EQUAL( core_from_string("400.0000"), chain.get_balance( "alice1111111"_n ) );
   refund = chain.get_refund_request( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( core_from_string("200.0000"), refund["net_amount"].template as<asset>() );
   BOOST_REQUIRE_EQUAL( core_from_string("100.0000"), refund["cpu_amount"].template as<asset>() );
   //BOOST_REQUIRE_EQUAL( orig_request_time, refund["request_time"].as_int64() );

} FC_LOG_AND_RETHROW()

// Tests for voting
BOOST_AUTO_TEST_CASE_TEMPLATE( producer_register_unregister, T, eosio_system_testers ) try {
   T chain;
   chain.issue_and_transfer( "alice1111111"_n, core_from_string("1000.0000"),  config::system_account_name );

   //fc::variant params = producer_parameters_example(1);
   auto key =  fc::crypto::public_key( std::string("EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV") ); // cspell:disable-line
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action("alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111"_n)
                                               ("producer_key", key )
                                               ("url", "http://block.one")
                                               ("location", 1)
                        )
   );

   auto info = chain.get_producer_info( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( "alice1111111", info["owner"].as_string() );
   BOOST_REQUIRE_EQUAL( 0, info["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( "http://block.one", info["url"].as_string() );

   //change parameters one by one to check for things like #3783
   //fc::variant params2 = producer_parameters_example(2);
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action("alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111"_n)
                                               ("producer_key", key )
                                               ("url", "http://block.two")
                                               ("location", 1)
                        )
   );
   info = chain.get_producer_info( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( "alice1111111", info["owner"].as_string() );
   BOOST_REQUIRE_EQUAL( key, fc::crypto::public_key(info["producer_key"].as_string()) );
   BOOST_REQUIRE_EQUAL( "http://block.two", info["url"].as_string() );
   BOOST_REQUIRE_EQUAL( 1, info["location"].as_int64() );

   auto key2 =  fc::crypto::public_key( std::string("EOS5jnmSKrzdBHE9n8hw58y7yxFWBC8SNiG7m8S1crJH3KvAnf9o6") ); // cspell:disable-line
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action("alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111"_n)
                                               ("producer_key", key2 )
                                               ("url", "http://block.two")
                                               ("location", 2)
                        )
   );
   info = chain.get_producer_info( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( "alice1111111", info["owner"].as_string() );
   BOOST_REQUIRE_EQUAL( key2, fc::crypto::public_key(info["producer_key"].as_string()) );
   BOOST_REQUIRE_EQUAL( "http://block.two", info["url"].as_string() );
   BOOST_REQUIRE_EQUAL( 2, info["location"].as_int64() );

   //unregister producer
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action("alice1111111"_n, "unregprod"_n, mvo()
                                               ("producer",  "alice1111111"_n)
                        )
   );
   info = chain.get_producer_info( "alice1111111"_n );
   //key should be empty
   BOOST_REQUIRE_EQUAL( fc::crypto::public_key(), fc::crypto::public_key(info["producer_key"].as_string()) );
   //everything else should stay the same
   BOOST_REQUIRE_EQUAL( "alice1111111", info["owner"].as_string() );
   BOOST_REQUIRE_EQUAL( 0, info["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( "http://block.two", info["url"].as_string() );

   //unregister bob111111111 who is not a producer
   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "producer not found" ),
                        chain.push_action( "bob111111111"_n, "unregprod"_n, mvo()
                                     ("producer",  "bob111111111"_n)
                        )
   );

} FC_LOG_AND_RETHROW()



BOOST_AUTO_TEST_CASE_TEMPLATE( vote_for_producer, T, eosio_system_testers ) try {
   T chain;
   chain.cross_15_percent_threshold();

   chain.issue_and_transfer( "alice1111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   fc::variant params = chain.producer_parameters_example(1);
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111"_n)
                                               ("producer_key", chain.get_public_key( "alice1111111"_n, "active") )
                                               ("url", "http://block.one")
                                               ("location", 0 )
                        )
   );
   auto prod = chain.get_producer_info( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( "alice1111111", prod["owner"].as_string() );
   BOOST_REQUIRE_EQUAL( 0, prod["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( "http://block.one", prod["url"].as_string() );

   chain.issue_and_transfer( "bob111111111"_n, core_from_string("2000.0000"),  config::system_account_name );
   chain.issue_and_transfer( "carol1111111"_n, core_from_string("3000.0000"),  config::system_account_name );

   //bob111111111 makes stake
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "bob111111111"_n, core_from_string("11.0000"), core_from_string("0.1111") ) );
   BOOST_REQUIRE_EQUAL( core_from_string("1988.8889"), chain.get_balance( "bob111111111"_n ) );
   REQUIRE_MATCHING_OBJECT( voter( "bob111111111"_n, core_from_string("11.1111") ), chain.get_voter_info( "bob111111111"_n ) );

   //bob111111111 votes for alice1111111
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "bob111111111"_n, { "alice1111111"_n } ) );

   //check that producer parameters stay the same after voting
   prod = chain.get_producer_info( "alice1111111"_n );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("11.1111")) == prod["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( "alice1111111", prod["owner"].as_string() );
   BOOST_REQUIRE_EQUAL( "http://block.one", prod["url"].as_string() );

   //carol1111111 makes stake
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "carol1111111"_n, core_from_string("22.0000"), core_from_string("0.2222") ) );
   REQUIRE_MATCHING_OBJECT( voter( "carol1111111"_n, core_from_string("22.2222") ), chain.get_voter_info( "carol1111111"_n ) );
   BOOST_REQUIRE_EQUAL( core_from_string("2977.7778"), chain.get_balance( "carol1111111"_n ) );
   //carol1111111 votes for alice1111111
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "carol1111111"_n, { "alice1111111"_n } ) );

   //new stake votes be added to alice1111111's total_votes
   prod = chain.get_producer_info( "alice1111111"_n );
   BOOST_CHECK_CLOSE( chain.stake2votes(core_from_string("33.3333")), prod["total_votes"].as_double(), 0.001 );

   //bob111111111 increases his stake
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "bob111111111"_n, core_from_string("33.0000"), core_from_string("0.3333") ) );
   //alice1111111 stake with transfer to bob111111111
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake_with_transfer( "alice1111111"_n, "bob111111111"_n, core_from_string("22.0000"), core_from_string("0.2222") ) );
   //should increase alice1111111's total_votes
   prod = chain.get_producer_info( "alice1111111"_n );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("88.8888")) == prod["total_votes"].as_double() );

   //carol1111111 unstakes part of the stake
   BOOST_REQUIRE_EQUAL( chain.success(), chain.unstake( "carol1111111"_n, core_from_string("2.0000"), core_from_string("0.0002")/*"2.0000 EOS", "0.0002 EOS"*/ ) );

   //should decrease alice1111111's total_votes
   prod = chain.get_producer_info( "alice1111111"_n );
   wdump((prod));
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("86.8886")) == prod["total_votes"].as_double() );

   //bob111111111 revokes his vote
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "bob111111111"_n, vector<account_name>() ) );

   //should decrease alice1111111's total_votes
   prod = chain.get_producer_info( "alice1111111"_n );
   BOOST_CHECK_CLOSE( chain.stake2votes(core_from_string("20.2220")), prod["total_votes"].as_double(), 0.001 );
   //but eos should still be at stake
   BOOST_REQUIRE_EQUAL( core_from_string("1955.5556"), chain.get_balance( "bob111111111"_n ) );

   //carol1111111 unstakes rest of eos
   BOOST_REQUIRE_EQUAL( chain.success(), chain.unstake( "carol1111111"_n, core_from_string("20.0000"), core_from_string("0.2220") ) );
   //should decrease alice1111111's total_votes to zero
   prod = chain.get_producer_info( "alice1111111"_n );
   // tolerance check does not test anything additional in nodeos
   //BOOST_TEST_REQUIRE( 0.0 == prod["total_votes"].as_double() );

   //carol1111111 should receive funds in 3 days
   chain.produce_block( fc::days(3) );
   chain.produce_block();

   // do a bid refund for carol
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "carol1111111"_n, "refund"_n, mvo()("owner", "carol1111111"_n) ) );
   BOOST_REQUIRE_EQUAL( core_from_string("3000.0000"), chain.get_balance( "carol1111111"_n ) );

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_CASE_TEMPLATE( unregistered_producer_voting, T, eosio_system_testers ) try {
   T chain;
   chain.issue_and_transfer( "bob111111111"_n, core_from_string("2000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "bob111111111"_n, core_from_string("13.0000"), core_from_string("0.5791") ) );
   // tolerance compare doesn't check anything useful in nodeos
   //REQUIRE_MATCHING_OBJECT( voter( "bob111111111"_n, core_from_string("13.5791") ), chain.get_voter_info( "bob111111111"_n ) );

   //bob111111111 should not be able to vote for alice1111111 who is not a producer
   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "producer is not registered" ),
                        chain.vote( "bob111111111"_n, { "alice1111111"_n } ) );

   //alice1111111 registers as a producer
   chain.issue_and_transfer( "alice1111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   fc::variant params = chain.producer_parameters_example(1);
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111"_n)
                                               ("producer_key", chain.get_public_key( "alice1111111"_n, "active") )
                                               ("url", "")
                                               ("location", 0)
                        )
   );
   //and then unregisters
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "unregprod"_n, mvo()
                                               ("producer",  "alice1111111"_n)
                        )
   );
   //key should be empty
   auto prod = chain.get_producer_info( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( fc::crypto::public_key(), fc::crypto::public_key(prod["producer_key"].as_string()) );

   //bob111111111 should not be able to vote for alice1111111 who is an unregistered producer
   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "producer is not currently registered" ),
                        chain.vote( "bob111111111"_n, { "alice1111111"_n } ) );

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_CASE_TEMPLATE( more_than_30_producer_voting, T, eosio_system_testers ) try {
   T chain;
   chain.issue_and_transfer( "bob111111111"_n, core_from_string("2000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "bob111111111"_n, core_from_string("13.0000"), core_from_string("0.5791") ) );
   REQUIRE_MATCHING_OBJECT( voter( "bob111111111"_n, core_from_string("13.5791") ), chain.get_voter_info( "bob111111111"_n ) );

   //bob111111111 should not be able to vote for alice1111111 who is not a producer
   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "attempt to vote for too many producers" ),
                        chain.vote( "bob111111111"_n, vector<account_name>(31, "alice1111111"_n) ) );

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_CASE_TEMPLATE( vote_same_producer_30_times, T, eosio_system_testers ) try {
   T chain;
   chain.issue_and_transfer( "bob111111111"_n, core_from_string("2000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "bob111111111"_n, core_from_string("50.0000"), core_from_string("50.0000") ) );
   REQUIRE_MATCHING_OBJECT( voter( "bob111111111"_n, core_from_string("100.0000") ), chain.get_voter_info( "bob111111111"_n ) );

   //alice1111111 becomes a producer
   chain.issue_and_transfer( "alice1111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   fc::variant params = chain.producer_parameters_example(1);
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111"_n)
                                               ("producer_key", chain.get_public_key("alice1111111"_n, "active") )
                                               ("url", "")
                                               ("location", 0)
                        )
   );

   //bob111111111 should not be able to vote for alice1111111 who is not a producer
   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "producer votes must be unique and sorted" ),
                        chain.vote( "bob111111111"_n, vector<account_name>(30, "alice1111111"_n) ) );

   auto prod = chain.get_producer_info( "alice1111111"_n );
   BOOST_TEST_REQUIRE( 0 == prod["total_votes"].as_double() );

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_CASE_TEMPLATE( producer_keep_votes, T, eosio_system_testers ) try {
   T chain;
   chain.issue_and_transfer( "alice1111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   fc::variant params = chain.producer_parameters_example(1);
   vector<char> key = fc::raw::pack( chain.get_public_key( "alice1111111"_n, "active" ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111"_n)
                                               ("producer_key", chain.get_public_key( "alice1111111"_n, "active") )
                                               ("url", "")
                                               ("location", 0)
                        )
   );

   //bob111111111 makes stake
   chain.issue_and_transfer( "bob111111111"_n, core_from_string("2000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "bob111111111"_n, core_from_string("13.0000"), core_from_string("0.5791") ) );
   // tolerance compare doesn't check anything useful in nodeos
   //REQUIRE_MATCHING_OBJECT( voter( "bob111111111"_n, core_from_string("13.5791") ), chain.get_voter_info( "bob111111111"_n ) );

   //bob111111111 votes for alice1111111
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote("bob111111111"_n, { "alice1111111"_n } ) );

   auto prod = chain.get_producer_info( "alice1111111"_n );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("13.5791")) == prod["total_votes"].as_double() );

   //unregister producer
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action("alice1111111"_n, "unregprod"_n, mvo()
                                               ("producer",  "alice1111111"_n)
                        )
   );
   prod = chain.get_producer_info( "alice1111111"_n );
   //key should be empty
   BOOST_REQUIRE_EQUAL( fc::crypto::public_key(), fc::crypto::public_key(prod["producer_key"].as_string()) );
   //check parameters just in case
   //REQUIRE_MATCHING_OBJECT( params, prod["prefs"]);
   //votes should stay the same
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("13.5791")), prod["total_votes"].as_double() );

   //regtister the same producer again
   params = chain.producer_parameters_example(2);
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111"_n)
                                               ("producer_key", chain.get_public_key( "alice1111111"_n, "active") )
                                               ("url", "")
                                               ("location", 0)
                        )
   );
   prod = chain.get_producer_info( "alice1111111"_n );
   //votes should stay the same
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("13.5791")), prod["total_votes"].as_double() );

   //change parameters
   params = chain.producer_parameters_example(3);
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111"_n)
                                               ("producer_key", chain.get_public_key( "alice1111111"_n, "active") )
                                               ("url","")
                                               ("location", 0)
                        )
   );
   prod = chain.get_producer_info( "alice1111111"_n );
   //votes should stay the same
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("13.5791")), prod["total_votes"].as_double() );
   //check parameters just in case
   //REQUIRE_MATCHING_OBJECT( params, prod["prefs"]);

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_CASE_TEMPLATE( vote_for_two_producers, T, eosio_system_testers ) try {
   T chain;
   //alice1111111 becomes a producer
   fc::variant params = chain.producer_parameters_example(1);
   auto key = chain.get_public_key( "alice1111111"_n, "active" );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "alice1111111"_n)
                                               ("producer_key", chain.get_public_key( "alice1111111"_n, "active") )
                                               ("url","")
                                               ("location", 0)
                        )
   );
   //bob111111111 becomes a producer
   params = chain.producer_parameters_example(2);
   key = chain.get_public_key( "bob111111111"_n, "active" );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "bob111111111"_n, "regproducer"_n, mvo()
                                               ("producer",  "bob111111111"_n)
                                               ("producer_key", chain.get_public_key( "alice1111111"_n, "active") )
                                               ("url","")
                                               ("location", 0)
                        )
   );

   //carol1111111 votes for alice1111111 and bob111111111
   chain.issue_and_transfer( "carol1111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "carol1111111"_n, core_from_string("15.0005"), core_from_string("5.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "carol1111111"_n, { "alice1111111"_n, "bob111111111"_n } ) );

   auto alice_info = chain.get_producer_info( "alice1111111"_n );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("20.0005")) == alice_info["total_votes"].as_double() );
   auto bob_info = chain.get_producer_info( "bob111111111"_n );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("20.0005")) == bob_info["total_votes"].as_double() );

   //carol1111111 votes for alice1111111 (but revokes vote for bob111111111)
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "carol1111111"_n, { "alice1111111"_n } ) );

   alice_info = chain.get_producer_info( "alice1111111"_n );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("20.0005")) == alice_info["total_votes"].as_double() );
   bob_info = chain.get_producer_info( "bob111111111"_n );
   BOOST_TEST_REQUIRE( 0 == bob_info["total_votes"].as_double() );

   //alice1111111 votes for herself and bob111111111
   chain.issue_and_transfer( "alice1111111"_n, core_from_string("2.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, core_from_string("1.0000"), core_from_string("1.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote("alice1111111"_n, { "alice1111111"_n, "bob111111111"_n } ) );

   alice_info = chain.get_producer_info( "alice1111111"_n );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("22.0005")) == alice_info["total_votes"].as_double() );

   bob_info = chain.get_producer_info( "bob111111111"_n );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("2.0000")) == bob_info["total_votes"].as_double() );

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_CASE_TEMPLATE( proxy_register_unregister_keeps_stake, T, eosio_system_testers ) try {
   T chain;
   //register proxy by first action for this user ever
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action("alice1111111"_n, "regproxy"_n, mvo()
                                               ("proxy",  "alice1111111"_n)
                                               ("isproxy", true )
                        )
   );
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111"_n ), chain.get_voter_info( "alice1111111"_n ) );

   //unregister proxy
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action("alice1111111"_n, "regproxy"_n, mvo()
                                               ("proxy",  "alice1111111"_n)
                                               ("isproxy", false)
                        )
   );
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n ), chain.get_voter_info( "alice1111111"_n ) );

   //stake and then register as a proxy
   chain.issue_and_transfer( "bob111111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "bob111111111"_n, core_from_string("200.0002"), core_from_string("100.0001") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "bob111111111"_n, "regproxy"_n, mvo()
                                               ("proxy",  "bob111111111"_n)
                                               ("isproxy", true)
                        )
   );
   REQUIRE_MATCHING_OBJECT( proxy( "bob111111111"_n )( "staked", 3000003 ), chain.get_voter_info( "bob111111111"_n ) );
   //unrgister and check that stake is still in place
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "bob111111111"_n, "regproxy"_n, mvo()
                                               ("proxy",  "bob111111111"_n)
                                               ("isproxy", false)
                        )
   );
   REQUIRE_MATCHING_OBJECT( voter( "bob111111111"_n, core_from_string("300.0003") ), chain.get_voter_info( "bob111111111"_n ) );

   //register as a proxy and then stake
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "carol1111111"_n, "regproxy"_n, mvo()
                                               ("proxy",  "carol1111111"_n)
                                               ("isproxy", true)
                        )
   );
   chain.issue_and_transfer( "carol1111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "carol1111111"_n, core_from_string("246.0002"), core_from_string("531.0001") ) );
   //check that both proxy flag and stake a correct
   REQUIRE_MATCHING_OBJECT( proxy( "carol1111111"_n )( "staked", 7770003 ), chain.get_voter_info( "carol1111111"_n ) );

   //unregister
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "carol1111111"_n, "regproxy"_n, mvo()
                                                ("proxy",  "carol1111111"_n)
                                                ("isproxy", false)
                        )
   );
   REQUIRE_MATCHING_OBJECT( voter( "carol1111111"_n, core_from_string("777.0003") ), chain.get_voter_info( "carol1111111"_n ) );

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_CASE_TEMPLATE( proxy_stake_unstake_keeps_proxy_flag, T, eosio_system_testers ) try {
   T chain;
   chain.cross_15_percent_threshold();

   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "regproxy"_n, mvo()
                                               ("proxy",  "alice1111111"_n)
                                               ("isproxy", true)
                        )
   );
   chain.issue_and_transfer( "alice1111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111"_n ), chain.get_voter_info( "alice1111111"_n ) );

   //stake
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, core_from_string("100.0000"), core_from_string("50.0000") ) );
   //check that account is still a proxy
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111"_n )( "staked", 1500000 ), chain.get_voter_info( "alice1111111"_n ) );

   //stake more
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, core_from_string("30.0000"), core_from_string("20.0000") ) );
   //check that account is still a proxy
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111"_n )("staked", 2000000 ), chain.get_voter_info( "alice1111111"_n ) );

   //unstake more
   BOOST_REQUIRE_EQUAL( chain.success(), chain.unstake( "alice1111111"_n, core_from_string("65.0000"), core_from_string("35.0000") ) );
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111"_n )("staked", 1000000 ), chain.get_voter_info( "alice1111111"_n ) );

   //unstake the rest
   BOOST_REQUIRE_EQUAL( chain.success(), chain.unstake( "alice1111111"_n, core_from_string("65.0000"), core_from_string("35.0000") ) );
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111"_n )( "staked", 0 ), chain.get_voter_info( "alice1111111"_n ) );

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_CASE_TEMPLATE( proxy_actions_affect_producers, T, eosio_system_testers ) try {
   T chain;
   chain.cross_15_percent_threshold();

   chain.create_accounts_with_resources( {  "defproducer1"_n, "defproducer2"_n, "defproducer3"_n } );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer( "defproducer1"_n, 1) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer( "defproducer2"_n, 2) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer( "defproducer3"_n, 3) );

   //register as a proxy
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "regproxy"_n, mvo()
                                                ("proxy",  "alice1111111"_n)
                                                ("isproxy", true)
                        )
   );

   //accumulate proxied votes
   chain.issue_and_transfer( "bob111111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "bob111111111"_n, core_from_string("100.0002"), core_from_string("50.0001") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote("bob111111111"_n, vector<account_name>(), "alice1111111"_n ) );
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111"_n )( "proxied_vote_weight", chain.stake2votes(core_from_string("150.0003")) ), chain.get_voter_info( "alice1111111"_n ) );

   //vote for producers
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote("alice1111111"_n, { "defproducer1"_n, "defproducer2"_n } ) );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("150.0003")) == chain.get_producer_info( "defproducer1"_n )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("150.0003")) == chain.get_producer_info( "defproducer2"_n )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( 0 == chain.get_producer_info( "defproducer3"_n )["total_votes"].as_double() );

   //vote for another producers
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "alice1111111"_n, { "defproducer1"_n, "defproducer3"_n } ) );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("150.0003")) == chain.get_producer_info( "defproducer1"_n )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, chain.get_producer_info( "defproducer2"_n )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("150.0003")) == chain.get_producer_info( "defproducer3"_n )["total_votes"].as_double() );

   //unregister proxy
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "regproxy"_n, mvo()
                                                ("proxy",  "alice1111111"_n)
                                                ("isproxy", false)
                        )
   );
   //REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n )( "proxied_vote_weight", chain.stake2votes(core_from_string("150.0003")) ), chain.get_voter_info( "alice1111111"_n ) );
   BOOST_REQUIRE_EQUAL( 0, chain.get_producer_info( "defproducer1"_n )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, chain.get_producer_info( "defproducer2"_n )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, chain.get_producer_info( "defproducer3"_n )["total_votes"].as_double() );

   //register proxy again
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "regproxy"_n, mvo()
                                                ("proxy",  "alice1111111"_n)
                                                ("isproxy", true)
                        )
   );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("150.0003")) == chain.get_producer_info( "defproducer1"_n )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, chain.get_producer_info( "defproducer2"_n )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("150.0003")) == chain.get_producer_info( "defproducer3"_n )["total_votes"].as_double() );

   //stake increase by proxy itself affects producers
   chain.issue_and_transfer( "alice1111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, core_from_string("30.0001"), core_from_string("20.0001") ) );
   BOOST_REQUIRE_EQUAL( chain.stake2votes(core_from_string("200.0005")), chain.get_producer_info( "defproducer1"_n )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, chain.get_producer_info( "defproducer2"_n )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( chain.stake2votes(core_from_string("200.0005")), chain.get_producer_info( "defproducer3"_n )["total_votes"].as_double() );

   //stake decrease by proxy itself affects producers
   BOOST_REQUIRE_EQUAL( chain.success(), chain.unstake( "alice1111111"_n, core_from_string("10.0001"), core_from_string("10.0001") ) );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("180.0003")) == chain.get_producer_info( "defproducer1"_n )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, chain.get_producer_info( "defproducer2"_n )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("180.0003")) == chain.get_producer_info( "defproducer3"_n )["total_votes"].as_double() );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(eosio_system_part2_tests)

BOOST_AUTO_TEST_CASE_TEMPLATE( multiple_producer_votepay_share, T, eosio_system_testers ) try {
   T chain;

   const asset net = core_from_string("80.0000");
   const asset cpu = core_from_string("80.0000");
   const std::vector<account_name> voters = { "producvotera"_n, "producvoterb"_n, "producvoterc"_n, "producvoterd"_n };
   for (const auto& v: voters) {
      chain.create_account_with_resources( v, config::system_account_name, core_from_string("1.0000"), false, net, cpu );
      chain.transfer(config::system_account_name, v, core_from_string("100000000.0000"), config::system_account_name );
      BOOST_REQUIRE_EQUAL(chain.success(), chain.stake(v, core_from_string("30000000.0000"), core_from_string("30000000.0000")) );
   }

   // create accounts {defproducera, defproducerb, ..., defproducerz, abcproducera, ..., defproducern} and register as producers
   std::vector<account_name> producer_names;
   {
      producer_names.reserve('z' - 'a' + 1);
      {
         const std::string root("defproducer");
         for ( char c = 'a'; c <= 'z'; ++c ) {
            producer_names.emplace_back(root + std::string(1, c));
         }
      }
      {
         const std::string root("abcproducer");
         for ( char c = 'a'; c <= 'n'; ++c ) {
            producer_names.emplace_back(root + std::string(1, c));
         }
      }
      chain.setup_producer_accounts(producer_names);
      for (const auto& p: producer_names) {
         BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer(p) );
         chain.produce_blocks(1);
         ilog( "------ get pro----------" );
         wdump((p));
         BOOST_TEST_REQUIRE(0 == chain.get_producer_info(p)["total_votes"].as_double());
         BOOST_TEST_REQUIRE(0 == chain.get_producer_info2(p)["votepay_share"].as_double());
         BOOST_REQUIRE(0 < chain.microseconds_since_epoch_of_iso_string( chain.get_producer_info2(p)["last_votepay_share_update"] ));
      }
   }

   chain.produce_block( fc::hours(24) );

   // producvotera votes for defproducera ... defproducerj
   // producvoterb votes for defproducera ... defproduceru
   // producvoterc votes for defproducera ... defproducerz
   // producvoterd votes for abcproducera ... abcproducern
   {
      BOOST_TEST_REQUIRE( 0 == chain.get_global_state3()["total_vpay_share_change_rate"].as_double() );
      BOOST_REQUIRE_EQUAL( chain.success(), chain.vote("producvotera"_n, vector<account_name>(producer_names.begin(), producer_names.begin()+10)) );
      chain.produce_block( fc::hours(10) );
      BOOST_TEST_REQUIRE( 0 == chain.get_global_state2()["total_producer_votepay_share"].as_double() );
      const auto& init_info  = chain.get_producer_info(producer_names[0]);
      const auto& init_info2 = chain.get_producer_info2(producer_names[0]);
      uint64_t init_update = chain.microseconds_since_epoch_of_iso_string( init_info2["last_votepay_share_update"] );
      double   init_votes  = init_info["total_votes"].as_double();
      BOOST_REQUIRE_EQUAL( chain.success(), chain.vote("producvoterb"_n, vector<account_name>(producer_names.begin(), producer_names.begin()+21)) );
      const auto& info  = chain.get_producer_info(producer_names[0]);
      const auto& info2 = chain.get_producer_info2(producer_names[0]);
      BOOST_TEST_REQUIRE( ((chain.microseconds_since_epoch_of_iso_string( info2["last_votepay_share_update"] ) - init_update)/double(1E6)) * init_votes == info2["votepay_share"].as_double() );
      BOOST_CHECK_CLOSE( info2["votepay_share"].as_double() * 10, chain.get_global_state2()["total_producer_votepay_share"].as_double(), 0.1 );

      BOOST_TEST_REQUIRE( 0 == chain.get_producer_info2(producer_names[11])["votepay_share"].as_double() );
      chain.produce_block( fc::hours(13) );
      BOOST_REQUIRE_EQUAL( chain.success(), chain.vote("producvoterc"_n, vector<account_name>(producer_names.begin(), producer_names.begin()+26)) );
      BOOST_REQUIRE( 0 < chain.get_producer_info2(producer_names[11])["votepay_share"].as_double() );
      chain.produce_block( fc::hours(1) );
      BOOST_REQUIRE_EQUAL( chain.success(), chain.vote("producvoterd"_n, vector<account_name>(producer_names.begin()+26, producer_names.end())) );
      BOOST_TEST_REQUIRE( 0 == chain.get_producer_info2(producer_names[26])["votepay_share"].as_double() );
   }

   {
      auto proda = chain.get_producer_info( "defproducera"_n );
      auto prodj = chain.get_producer_info( "defproducerj"_n );
      auto prodk = chain.get_producer_info( "defproducerk"_n );
      auto produ = chain.get_producer_info( "defproduceru"_n );
      auto prodv = chain.get_producer_info( "defproducerv"_n );
      auto prodz = chain.get_producer_info( "defproducerz"_n );

      BOOST_REQUIRE (0 == proda["unpaid_blocks"].template as<uint32_t>() && 0 == prodz["unpaid_blocks"].template as<uint32_t>());

      // check vote ratios
      BOOST_REQUIRE ( 0 < proda["total_votes"].as_double() && 0 < prodz["total_votes"].as_double() );
      BOOST_TEST_REQUIRE( proda["total_votes"].as_double() == prodj["total_votes"].as_double() );
      BOOST_TEST_REQUIRE( prodk["total_votes"].as_double() == produ["total_votes"].as_double() );
      BOOST_TEST_REQUIRE( prodv["total_votes"].as_double() == prodz["total_votes"].as_double() );
      BOOST_TEST_REQUIRE( 2 * proda["total_votes"].as_double() == 3 * produ["total_votes"].as_double() );
      BOOST_TEST_REQUIRE( proda["total_votes"].as_double() ==  3 * prodz["total_votes"].as_double() );
   }

   std::vector<double> vote_shares(producer_names.size());
   {
      double total_votes = 0;
      for (uint32_t i = 0; i < producer_names.size(); ++i) {
         vote_shares[i] = chain.get_producer_info(producer_names[i])["total_votes"].as_double();
         total_votes += vote_shares[i];
      }
      BOOST_CHECK_CLOSE( total_votes, chain.get_global_state()["total_producer_vote_weight"].as_double(), 0.1 );
      BOOST_CHECK_CLOSE( total_votes, chain.get_global_state3()["total_vpay_share_change_rate"].as_double(), 0.1 );
      BOOST_REQUIRE_EQUAL( chain.microseconds_since_epoch_of_iso_string( chain.get_producer_info2(producer_names.back())["last_votepay_share_update"] ),
                           chain.microseconds_since_epoch_of_iso_string( chain.get_global_state3()["last_vpay_state_update"] ) );

      std::for_each( vote_shares.begin(), vote_shares.end(), [total_votes](double& x) { x /= total_votes; } );
      BOOST_CHECK_CLOSE( double(1), std::accumulate(vote_shares.begin(), vote_shares.end(), double(0)), 0.1 );
      BOOST_CHECK_CLOSE( double(3./71.), vote_shares.front(), 0.1 );
      BOOST_CHECK_CLOSE( double(1./71.), vote_shares.back(), 0.1 );
   }

   std::vector<double> votepay_shares(producer_names.size());
   {
      const auto& gs3 = chain.get_global_state3();
      double total_votepay_shares          = 0;
      double expected_total_votepay_shares = 0;
      for (uint32_t i = 0; i < producer_names.size() ; ++i) {
         const auto& info  = chain.get_producer_info(producer_names[i]);
         const auto& info2 = chain.get_producer_info2(producer_names[i]);
         votepay_shares[i] = info2["votepay_share"].as_double();
         total_votepay_shares          += votepay_shares[i];
         expected_total_votepay_shares += votepay_shares[i];
         expected_total_votepay_shares += info["total_votes"].as_double()
                                           * double( ( chain.microseconds_since_epoch_of_iso_string( gs3["last_vpay_state_update"] )
                                                        - chain.microseconds_since_epoch_of_iso_string( info2["last_votepay_share_update"] )
                                                     ) / 1E6 );
      }
      BOOST_TEST( expected_total_votepay_shares > total_votepay_shares );
      BOOST_CHECK_CLOSE( expected_total_votepay_shares, chain.get_global_state2()["total_producer_votepay_share"].as_double(), 0.1 );
   }

   {
      const uint32_t prod_index = 15;
      const account_name prod_name = producer_names[prod_index];
      const auto& init_info        = chain.get_producer_info(prod_name);
      const auto& init_info2       = chain.get_producer_info2(prod_name);
      BOOST_REQUIRE( 0 < init_info2["votepay_share"].as_double() );
      BOOST_REQUIRE( 0 < chain.microseconds_since_epoch_of_iso_string( init_info2["last_votepay_share_update"] ) );

      BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action(prod_name, "claimrewards"_n, mvo()("owner", prod_name)) );

      BOOST_CHECK_CLOSE( 0, chain.get_producer_info2(prod_name)["votepay_share"].as_double(), 0.1 );
      BOOST_REQUIRE_EQUAL( chain.get_producer_info(prod_name)["last_claim_time"].as_string(),
                           chain.get_producer_info2(prod_name)["last_votepay_share_update"].as_string() );
      BOOST_REQUIRE_EQUAL( chain.get_producer_info(prod_name)["last_claim_time"].as_string(),
                           chain.get_global_state3()["last_vpay_state_update"].as_string() );
      const auto& gs3 = chain.get_global_state3();
      double expected_total_votepay_shares = 0;
      for (uint32_t i = 0; i < producer_names.size(); ++i) {
         const auto& info  = chain.get_producer_info(producer_names[i]);
         const auto& info2 = chain.get_producer_info2(producer_names[i]);
         expected_total_votepay_shares += info2["votepay_share"].as_double();
         expected_total_votepay_shares += info["total_votes"].as_double()
                                           * double( ( chain.microseconds_since_epoch_of_iso_string( gs3["last_vpay_state_update"] )
                                                        - chain.microseconds_since_epoch_of_iso_string( info2["last_votepay_share_update"] )
                                                     ) / 1E6 );
      }
      BOOST_CHECK_CLOSE( expected_total_votepay_shares, chain.get_global_state2()["total_producer_votepay_share"].as_double(), 0.1 );
   }

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( votepay_share_invariant, T, eosio_system_testers ) try {
   T chain;

   chain.cross_15_percent_threshold();

   const asset net = core_from_string("80.0000");
   const asset cpu = core_from_string("80.0000");
   const std::vector<account_name> accounts = { "aliceaccount"_n, "bobbyaccount"_n, "carolaccount"_n, "emilyaccount"_n };
   for (const auto& a: accounts) {
      chain.create_account_with_resources( a, config::system_account_name, core_from_string("1.0000"), false, net, cpu );
      chain.transfer(config::system_account_name, a, core_from_string("1000.0000"), config::system_account_name );
   }
   const auto vota  = accounts[0];
   const auto votb  = accounts[1];
   const auto proda = accounts[2];
   const auto prodb = accounts[3];

   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( vota, core_from_string("100.0000"), core_from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( votb, core_from_string("100.0000"), core_from_string("100.0000") ) );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer( proda ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer( prodb ) );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( vota, { proda } ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( votb, { prodb } ) );

   chain.produce_block( fc::hours(25) );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( vota, { proda } ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( votb, { prodb } ) );

   chain.produce_block( fc::hours(1) );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action(proda, "claimrewards"_n, mvo()("owner", proda)) );
   BOOST_TEST_REQUIRE( 0 == chain.get_producer_info2(proda)["votepay_share"].as_double() );

   chain.produce_block( fc::hours(24) );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( vota, { proda } ) );

   chain.produce_block( fc::hours(24) );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action(prodb, "claimrewards"_n, mvo()("owner", prodb)) );
   BOOST_TEST_REQUIRE( 0 == chain.get_producer_info2(prodb)["votepay_share"].as_double() );

   chain.produce_block( fc::hours(10) );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( votb, { prodb } ) );

   chain.produce_block( fc::hours(16) );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( votb, { prodb } ) );
   chain.produce_block( fc::hours(2) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( vota, { proda } ) );

   const auto& info  = chain.get_producer_info(prodb);
   const auto& info2 = chain.get_producer_info2(prodb);
   const auto& gs2   = chain.get_global_state2();
   const auto& gs3   = chain.get_global_state3();

   double expected_total_vpay_share = info2["votepay_share"].as_double()
                                       + info["total_votes"].as_double()
                                          * ( chain.microseconds_since_epoch_of_iso_string( gs3["last_vpay_state_update"] )
                                               - chain.microseconds_since_epoch_of_iso_string( info2["last_votepay_share_update"] ) ) / 1E6;

   BOOST_CHECK_CLOSE( expected_total_vpay_share, gs2["total_producer_votepay_share"].as_double(), 0.1 );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( votepay_share_proxy, T, eosio_system_testers ) try {
   T chain;

   chain.cross_15_percent_threshold();

   const asset net = core_from_string("80.0000");
   const asset cpu = core_from_string("80.0000");
   const std::vector<account_name> accounts = { "aliceaccount"_n, "bobbyaccount"_n, "carolaccount"_n, "emilyaccount"_n };
   for (const auto& a: accounts) {
      chain.create_account_with_resources( a, config::system_account_name, core_from_string("1.0000"), false, net, cpu );
      chain.transfer(config::system_account_name, a, core_from_string("1000.0000"), config::system_account_name );
   }
   const auto alice = accounts[0];
   const auto bob   = accounts[1];
   const auto carol = accounts[2];
   const auto emily = accounts[3];

   // alice becomes a proxy
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( alice, "regproxy"_n, mvo()("proxy", alice)("isproxy", true) ) );
   REQUIRE_MATCHING_OBJECT( proxy( alice ), chain.get_voter_info( alice ) );

   // carol and emily become producers
   BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer( carol, 1) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer( emily, 1) );

   // bob chooses alice as proxy
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( bob, core_from_string("100.0002"), core_from_string("50.0001") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( alice, core_from_string("150.0000"), core_from_string("150.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( bob, { }, alice ) );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("150.0003")) == chain.get_voter_info(alice)["proxied_vote_weight"].as_double() );

   // alice (proxy) votes for carol
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( alice, { carol } ) );
   double total_votes = chain.get_producer_info(carol)["total_votes"].as_double();
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("450.0003")) == total_votes );
   BOOST_TEST_REQUIRE( 0 == chain.get_producer_info2(carol)["votepay_share"].as_double() );
   uint64_t last_update_time = chain.microseconds_since_epoch_of_iso_string( chain.get_producer_info2(carol)["last_votepay_share_update"] );

   chain.produce_block( fc::hours(15) );

   // alice (proxy) votes again for carol
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( alice, { carol } ) );
   auto cur_info2 = chain.get_producer_info2(carol);
   double expected_votepay_share = double( (chain.microseconds_since_epoch_of_iso_string( cur_info2["last_votepay_share_update"] ) - last_update_time) / 1E6 ) * total_votes;
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("450.0003")) == chain.get_producer_info(carol)["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( expected_votepay_share == cur_info2["votepay_share"].as_double() );
   BOOST_TEST_REQUIRE( expected_votepay_share == chain.get_global_state2()["total_producer_votepay_share"].as_double() );
   last_update_time = chain.microseconds_since_epoch_of_iso_string( cur_info2["last_votepay_share_update"] );
   total_votes      = chain.get_producer_info(carol)["total_votes"].as_double();

   chain.produce_block( fc::hours(40) );

   // bob unstakes
   BOOST_REQUIRE_EQUAL( chain.success(), chain.unstake( bob, core_from_string("10.0002"), core_from_string("10.0001") ) );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("430.0000")), chain.get_producer_info(carol)["total_votes"].as_double() );

   cur_info2 = chain.get_producer_info2(carol);
   expected_votepay_share += double( (chain.microseconds_since_epoch_of_iso_string( cur_info2["last_votepay_share_update"] ) - last_update_time) / 1E6 ) * total_votes;
   BOOST_TEST_REQUIRE( expected_votepay_share == cur_info2["votepay_share"].as_double() );
   BOOST_TEST_REQUIRE( expected_votepay_share == chain.get_global_state2()["total_producer_votepay_share"].as_double() );
   last_update_time = chain.microseconds_since_epoch_of_iso_string( cur_info2["last_votepay_share_update"] );
   total_votes      = chain.get_producer_info(carol)["total_votes"].as_double();

   // carol claims rewards
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action(carol, "claimrewards"_n, mvo()("owner", carol)) );

   chain.produce_block( fc::hours(20) );

   // bob votes for carol
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( bob, { carol } ) );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("430.0000")), chain.get_producer_info(carol)["total_votes"].as_double() );
   cur_info2 = chain.get_producer_info2(carol);
   expected_votepay_share = double( (chain.microseconds_since_epoch_of_iso_string( cur_info2["last_votepay_share_update"] ) - last_update_time) / 1E6 ) * total_votes;
   BOOST_CHECK_CLOSE( expected_votepay_share, cur_info2["votepay_share"].as_double(), 0.1 );
   BOOST_CHECK_CLOSE( expected_votepay_share, chain.get_global_state2()["total_producer_votepay_share"].as_double(), 0.1 );

   chain.produce_block( fc::hours(54) );

   // bob votes for carol again
   // carol hasn't claimed rewards in over 3 days
   total_votes = chain.get_producer_info(carol)["total_votes"].as_double();
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( bob, { carol } ) );
   BOOST_REQUIRE_EQUAL( chain.get_producer_info2(carol)["last_votepay_share_update"].as_string(),
                        chain.get_global_state3()["last_vpay_state_update"].as_string() );
   BOOST_TEST_REQUIRE( 0 == chain.get_producer_info2(carol)["votepay_share"].as_double() );
   BOOST_TEST_REQUIRE( 0 == chain.get_global_state2()["total_producer_votepay_share"].as_double() );
   BOOST_TEST_REQUIRE( 0 == chain.get_global_state3()["total_vpay_share_change_rate"].as_double() );

   chain.produce_block( fc::hours(20) );

   // bob votes for carol again
   // carol still hasn't claimed rewards
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( bob, { carol } ) );
   BOOST_REQUIRE_EQUAL(chain.get_producer_info2(carol)["last_votepay_share_update"].as_string(),
                       chain.get_global_state3()["last_vpay_state_update"].as_string() );
   BOOST_TEST_REQUIRE( 0 == chain.get_producer_info2(carol)["votepay_share"].as_double() );
   BOOST_TEST_REQUIRE( 0 == chain.get_global_state2()["total_producer_votepay_share"].as_double() );
   BOOST_TEST_REQUIRE( 0 == chain.get_global_state3()["total_vpay_share_change_rate"].as_double() );

   chain.produce_block( fc::hours(24) );

   // carol finally claims rewards
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( carol, "claimrewards"_n, mvo()("owner", carol) ) );
   BOOST_TEST_REQUIRE( 0           == chain.get_producer_info2(carol)["votepay_share"].as_double() );
   BOOST_TEST_REQUIRE( 0           == chain.get_global_state2()["total_producer_votepay_share"].as_double() );
   BOOST_TEST_REQUIRE( total_votes == chain.get_global_state3()["total_vpay_share_change_rate"].as_double() );

   chain.produce_block( fc::hours(5) );

   // alice votes for carol and emily
   // emily hasn't claimed rewards in over 3 days
   last_update_time = chain.microseconds_since_epoch_of_iso_string( chain.get_producer_info2(carol)["last_votepay_share_update"] );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( alice, { carol, emily } ) );
   cur_info2 = chain.get_producer_info2(carol);
   auto cur_info2_emily = chain.get_producer_info2(emily);

   expected_votepay_share = double( (chain.microseconds_since_epoch_of_iso_string( cur_info2["last_votepay_share_update"] ) - last_update_time) / 1E6 ) * total_votes;
   BOOST_TEST_REQUIRE( expected_votepay_share == cur_info2["votepay_share"].as_double() );
   BOOST_TEST_REQUIRE( 0                      == cur_info2_emily["votepay_share"].as_double() );
   BOOST_TEST_REQUIRE( expected_votepay_share == chain.get_global_state2()["total_producer_votepay_share"].as_double() );
   BOOST_TEST_REQUIRE( chain.get_producer_info(carol)["total_votes"].as_double() ==
                       chain.get_global_state3()["total_vpay_share_change_rate"].as_double() );
   BOOST_REQUIRE_EQUAL( cur_info2["last_votepay_share_update"].as_string(),
                        chain.get_global_state3()["last_vpay_state_update"].as_string() );
   BOOST_REQUIRE_EQUAL( cur_info2_emily["last_votepay_share_update"].as_string(),
                        chain.get_global_state3()["last_vpay_state_update"].as_string() );

   chain.produce_block( fc::hours(10) );

   // bob chooses alice as proxy
   // emily still hasn't claimed rewards
   last_update_time = chain.microseconds_since_epoch_of_iso_string( chain.get_producer_info2(carol)["last_votepay_share_update"] );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( bob, { }, alice ) );
   cur_info2 = chain.get_producer_info2(carol);
   cur_info2_emily = chain.get_producer_info2(emily);

   expected_votepay_share += double( (chain.microseconds_since_epoch_of_iso_string( cur_info2["last_votepay_share_update"] ) - last_update_time) / 1E6 ) * total_votes;
   BOOST_TEST_REQUIRE( expected_votepay_share == cur_info2["votepay_share"].as_double() );
   BOOST_TEST_REQUIRE( 0                      == cur_info2_emily["votepay_share"].as_double() );
   BOOST_TEST_REQUIRE( expected_votepay_share == chain.get_global_state2()["total_producer_votepay_share"].as_double() );
   BOOST_TEST_REQUIRE( chain.get_producer_info(carol)["total_votes"].as_double() ==
                       chain.get_global_state3()["total_vpay_share_change_rate"].as_double() );
   BOOST_REQUIRE_EQUAL( cur_info2["last_votepay_share_update"].as_string(),
                        chain.get_global_state3()["last_vpay_state_update"].as_string() );
   BOOST_REQUIRE_EQUAL( cur_info2_emily["last_votepay_share_update"].as_string(),
                        chain.get_global_state3()["last_vpay_state_update"].as_string() );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( votepay_share_update_order, T, eosio_system_testers ) try {
   T chain;
   chain.cross_15_percent_threshold();

   const asset net = core_from_string("80.0000");
   const asset cpu = core_from_string("80.0000");
   const std::vector<account_name> accounts = { "aliceaccount"_n, "bobbyaccount"_n, "carolaccount"_n, "emilyaccount"_n };
   for (const auto& a: accounts) {
      chain.create_account_with_resources( a, config::system_account_name, core_from_string("1.0000"), false, net, cpu );
      chain.transfer(config::system_account_name, a, core_from_string("1000.0000"), config::system_account_name );
   }
   const auto alice = accounts[0];
   const auto bob   = accounts[1];
   const auto carol = accounts[2];
   const auto emily = accounts[3];

   BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer( carol ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer( emily ) );

   chain.produce_block( fc::hours(24) );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( alice, core_from_string("100.0000"), core_from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( bob,   core_from_string("100.0000"), core_from_string("100.0000") ) );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( alice, { carol, emily } ) );


   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( carol, "claimrewards"_n, mvo()("owner", carol) ) );
   chain.produce_block( fc::hours(1) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( emily, "claimrewards"_n, mvo()("owner", emily) ) );

   chain.produce_block( fc::hours(3 * 24 + 1) );

   {
      signed_transaction trx;
      chain.set_transaction_headers(trx);

      trx.actions.emplace_back( chain.get_action( config::system_account_name, "claimrewards"_n, { {carol, config::active_name} },
                                            mvo()("owner", carol) ) );

      std::vector<account_name> prods = { carol, emily };
      trx.actions.emplace_back( chain.get_action( config::system_account_name, "voteproducer"_n, { {alice, config::active_name} },
                                            mvo()("voter", alice)("proxy", name(0))("producers", prods) ) );

      trx.actions.emplace_back( chain.get_action( config::system_account_name, "claimrewards"_n, { {emily, config::active_name} },
                                            mvo()("owner", emily) ) );

      trx.sign( chain.get_private_key( carol, "active" ), chain.control->get_chain_id() );
      trx.sign( chain.get_private_key( alice, "active" ), chain.control->get_chain_id() );
      trx.sign( chain.get_private_key( emily, "active" ), chain.control->get_chain_id() );

      chain.push_transaction( trx );
   }

   const auto& carol_info  = chain.get_producer_info(carol);
   const auto& carol_info2 = chain.get_producer_info2(carol);
   const auto& emily_info  = chain.get_producer_info(emily);
   const auto& emily_info2 = chain.get_producer_info2(emily);
   const auto& gs3         = chain.get_global_state3();
   BOOST_REQUIRE_EQUAL( carol_info2["last_votepay_share_update"].as_string(), gs3["last_vpay_state_update"].as_string() );
   BOOST_REQUIRE_EQUAL( emily_info2["last_votepay_share_update"].as_string(), gs3["last_vpay_state_update"].as_string() );
   BOOST_TEST_REQUIRE( 0  == carol_info2["votepay_share"].as_double() );
   BOOST_TEST_REQUIRE( 0  == emily_info2["votepay_share"].as_double() );
   BOOST_REQUIRE( 0 < carol_info["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( carol_info["total_votes"].as_double() == emily_info["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( gs3["total_vpay_share_change_rate"].as_double() == 2 * carol_info["total_votes"].as_double() );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( votepay_transition, T, eosio_system_testers ) try {
   T chain;

   const asset net = core_from_string("80.0000");
   const asset cpu = core_from_string("80.0000");
   const std::vector<account_name> voters = { "producvotera"_n, "producvoterb"_n, "producvoterc"_n, "producvoterd"_n };
   for (const auto& v: voters) {
      chain.create_account_with_resources( v, config::system_account_name, core_from_string("1.0000"), false, net, cpu );
      chain.transfer(config::system_account_name, v, core_from_string("100000000.0000"), config::system_account_name );
      BOOST_REQUIRE_EQUAL(chain.success(), chain.stake(v, core_from_string("30000000.0000"), core_from_string("30000000.0000")) );
   }

   // create accounts {defproducera, defproducerb, ..., defproducerz} and register as producers
   std::vector<account_name> producer_names;
   {
      producer_names.reserve('z' - 'a' + 1);
      {
         const std::string root("defproducer");
         for ( char c = 'a'; c <= 'd'; ++c ) {
            producer_names.emplace_back(root + std::string(1, c));
         }
      }
      chain.setup_producer_accounts(producer_names);
      for (const auto& p: producer_names) {
         BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer(p) );
         BOOST_TEST_REQUIRE(0 == chain.get_producer_info(p)["total_votes"].as_double());
         BOOST_TEST_REQUIRE(0 == chain.get_producer_info2(p)["votepay_share"].as_double());
         BOOST_REQUIRE(0 < chain.microseconds_since_epoch_of_iso_string( chain.get_producer_info2(p)["last_votepay_share_update"] ));
      }
   }

   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote("producvotera"_n, vector<account_name>(producer_names.begin(), producer_names.end())) );
   auto* tbl = chain.control->db().template find<eosio::chain::table_id_object, eosio::chain::by_code_scope_table>(
                  boost::make_tuple( config::system_account_name,
                                     config::system_account_name,
                                     "producers2"_n ) );
   BOOST_REQUIRE( tbl );
   BOOST_REQUIRE( 0 < chain.microseconds_since_epoch_of_iso_string( chain.get_producer_info2("defproducera"_n)["last_votepay_share_update"] ) );

   // const_cast hack for now
   const_cast<chainbase::database&>(chain.control->db()).remove( *tbl );
   tbl = chain.control->db().template find<eosio::chain::table_id_object, eosio::chain::by_code_scope_table>(
                  boost::make_tuple( config::system_account_name,
                                     config::system_account_name,
                                     "producers2"_n ) );
   BOOST_REQUIRE( !tbl );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote("producvoterb"_n, vector<account_name>(producer_names.begin(), producer_names.end())) );
   tbl = chain.control->db().template find<eosio::chain::table_id_object, eosio::chain::by_code_scope_table>(
            boost::make_tuple( config::system_account_name,
                               config::system_account_name,
                               "producers2"_n ) );
   BOOST_REQUIRE( !tbl );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer("defproducera"_n) );
   BOOST_REQUIRE( chain.microseconds_since_epoch_of_iso_string( chain.get_producer_info("defproducera"_n)["last_claim_time"] ) < chain.microseconds_since_epoch_of_iso_string( chain.get_producer_info2("defproducera"_n)["last_votepay_share_update"] ) );

   chain.create_account_with_resources( "defproducer1"_n, config::system_account_name, core_from_string("1.0000"), false, net, cpu );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer("defproducer1"_n) );
   BOOST_REQUIRE( 0 < chain.microseconds_since_epoch_of_iso_string( chain.get_producer_info("defproducer1"_n)["last_claim_time"] ) );
   BOOST_REQUIRE_EQUAL( chain.get_producer_info("defproducer1"_n)["last_claim_time"].as_string(),
                        chain.get_producer_info2("defproducer1"_n)["last_votepay_share_update"].as_string() );

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_CASE_TEMPLATE( producers_upgrade_system_contract, T, eosio_system_testers ) try {
   T chain;
   //install multisig contract
   abi_serializer msig_abi_ser = chain.initialize_multisig();
   auto producer_names = chain.active_and_vote_producers();

   //change `default_max_inline_action_size` to 512 KB
   eosio::chain::chain_config params = chain.control->get_global_properties().configuration;
   params.max_inline_action_size = 512 * 1024;
   chain.base_tester::push_action( config::system_account_name, "setparams"_n, config::system_account_name, mutable_variant_object()
                                  ("params", params) );

   chain.produce_blocks();

   //helper function
   auto push_action_msig = [&]( const account_name& signer, const action_name &name, const variant_object &data, bool auth = true ) -> T::action_result {
         string action_type_name = msig_abi_ser.get_action_type(name);

         action act;
         act.account = "eosio.msig"_n;
         act.name = name;
         act.data = msig_abi_ser.variant_to_binary( action_type_name, data, abi_serializer::create_yield_function(chain.abi_serializer_max_time) );

         return chain.base_tester::push_action( std::move(act), (auth ? signer : signer == "bob111111111"_n ? "alice1111111"_n : "bob111111111"_n).to_uint64_t() );
   };
   // test begins
   vector<permission_level> prod_perms;
   for ( auto& x : producer_names ) {
      prod_perms.push_back( { name(x), config::active_name } );
   }

   transaction trx;
   {
      //prepare system contract with different hash (contract differs in one byte)
      auto code = test_contracts::eosio_system_wasm();
      string msg = "producer votes must be unique and sorted";
      auto it = std::search( code.begin(), code.end(), msg.begin(), msg.end() );
      BOOST_REQUIRE( it != code.end() );
      msg[0] = 'P';
      std::copy( msg.begin(), msg.end(), it );

      fc::variant pretty_trx = fc::mutable_variant_object()
         ("expiration", "2020-01-01T00:30")
         ("ref_block_num", 2)
         ("ref_block_prefix", 3)
         ("net_usage_words", 0)
         ("max_cpu_usage_ms", 0)
         ("delay_sec", 0)
         ("actions", fc::variants({
               fc::mutable_variant_object()
                  ("account", name(config::system_account_name))
                  ("name", "setcode")
                  ("authorization", vector<permission_level>{ { config::system_account_name, config::active_name } })
                  ("data", fc::mutable_variant_object() ("account", name(config::system_account_name))
                   ("vmtype", 0)
                   ("vmversion", "0")
                   ("code", bytes( code.begin(), code.end() ))
                  )
                  })
         );
      abi_serializer::from_variant(pretty_trx, trx, chain.get_resolver(), abi_serializer::create_yield_function(chain.abi_serializer_max_time));
   }

   BOOST_REQUIRE_EQUAL(chain.success(), push_action_msig( "alice1111111"_n, "propose"_n, mvo()
                                                    ("proposer",      "alice1111111"_n)
                                                    ("proposal_name", "upgrade1")
                                                    ("trx",           trx)
                                                    ("requested", prod_perms)
                       )
   );

   // get 15 approvals
   for ( size_t i = 0; i < 14; ++i ) {
      BOOST_REQUIRE_EQUAL(chain.success(), push_action_msig( name(producer_names[i]), "approve"_n, mvo()
                                                       ("proposer",      "alice1111111"_n)
                                                       ("proposal_name", "upgrade1")
                                                       ("level",         permission_level{ name(producer_names[i]), config::active_name })
                          )
      );
   }

   //should fail
   BOOST_REQUIRE_EQUAL(chain.wasm_assert_msg("transaction authorization failed"),
                       push_action_msig( "alice1111111"_n, "exec"_n, mvo()
                                         ("proposer",      "alice1111111"_n)
                                         ("proposal_name", "upgrade1")
                                         ("executer",      "alice1111111"_n)
                       )
   );

   // one more approval
   BOOST_REQUIRE_EQUAL(chain.success(), push_action_msig( name(producer_names[14]), "approve"_n, mvo()
                                                    ("proposer",      "alice1111111"_n)
                                                    ("proposal_name", "upgrade1")
                                                    ("level",         permission_level{ name(producer_names[14]), config::active_name })
                          )
   );

   transaction_trace_ptr trace;
   chain.control->applied_transaction().connect(
   [&]( std::tuple<const transaction_trace_ptr&, const packed_transaction_ptr&> p ) {
      trace = std::get<0>(p);
   } );

   BOOST_REQUIRE_EQUAL(chain.success(), push_action_msig( "alice1111111"_n, "exec"_n, mvo()
                                                    ("proposer",      "alice1111111"_n)
                                                    ("proposal_name", "upgrade1")
                                                    ("executer",      "alice1111111"_n)
                       )
   );

   BOOST_REQUIRE( bool(trace) );
   BOOST_REQUIRE_EQUAL( 1u, trace->action_traces.size() );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );

   chain.produce_blocks( 250 );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( producer_onblock_check, T, eosio_system_testers ) try {
   T chain;

   const asset large_asset = core_from_string("80.0000");
   chain.create_account_with_resources( "producvotera"_n, config::system_account_name, core_from_string("1.0000"), false, large_asset, large_asset );
   chain.create_account_with_resources( "producvoterb"_n, config::system_account_name, core_from_string("1.0000"), false, large_asset, large_asset );
   chain.create_account_with_resources( "producvoterc"_n, config::system_account_name, core_from_string("1.0000"), false, large_asset, large_asset );

   // create accounts {defproducera, defproducerb, ..., defproducerz} and register as producers
   std::vector<account_name> producer_names;
   producer_names.reserve('z' - 'a' + 1);
   const std::string root("defproducer");
   for ( char c = 'a'; c <= 'z'; ++c ) {
      producer_names.emplace_back(root + std::string(1, c));
   }
   chain.setup_producer_accounts(producer_names);

   for (auto a:producer_names)
      chain.regproducer(a);

   chain.produce_block(fc::hours(24));

   BOOST_REQUIRE_EQUAL(0, chain.get_producer_info( producer_names.front() )["total_votes"].template as<double>());
   BOOST_REQUIRE_EQUAL(0, chain.get_producer_info( producer_names.back() )["total_votes"].template as<double>());


   chain.transfer(config::system_account_name, "producvotera"_n, core_from_string("200000000.0000"), config::system_account_name);
   BOOST_REQUIRE_EQUAL(chain.success(), chain.stake("producvotera"_n, core_from_string("70000000.0000"), core_from_string("70000000.0000") ));
   BOOST_REQUIRE_EQUAL(chain.success(), chain.vote( "producvotera"_n, vector<account_name>(producer_names.begin(), producer_names.begin()+10)));
   BOOST_CHECK_EQUAL( chain.wasm_assert_msg( "cannot undelegate bandwidth until the chain is activated (at least 15% of all tokens participate in voting)" ),
                      chain.unstake( "producvotera"_n, core_from_string("50.0000"), core_from_string("50.0000") ) );

   // give a chance for everyone to produce blocks
   {
      chain.produce_blocks(21 * 12);
      bool all_21_produced = true;
      for (uint32_t i = 0; i < 21; ++i) {
         if (0 == chain.get_producer_info(producer_names[i])["unpaid_blocks"].template as<uint32_t>()) {
            all_21_produced= false;
         }
      }
      bool rest_didnt_produce = true;
      for (uint32_t i = 21; i < producer_names.size(); ++i) {
         if (0 < chain.get_producer_info(producer_names[i])["unpaid_blocks"].template as<uint32_t>()) {
            rest_didnt_produce = false;
         }
      }
      BOOST_REQUIRE_EQUAL(false, all_21_produced);
      BOOST_REQUIRE_EQUAL(true, rest_didnt_produce);
   }

   {
      const char* claimrewards_activation_error_message = "cannot claim rewards until the chain is activated (at least 15% of all tokens participate in voting)";
      BOOST_CHECK_EQUAL(0u, chain.get_global_state()["total_unpaid_blocks"].template as<uint32_t>());
      BOOST_REQUIRE_EQUAL(chain.wasm_assert_msg( claimrewards_activation_error_message ),
                          chain.push_action(producer_names.front(), "claimrewards"_n, mvo()("owner", producer_names.front())));
      BOOST_REQUIRE_EQUAL(0, chain.get_balance(producer_names.front()).get_amount());
      BOOST_REQUIRE_EQUAL(chain.wasm_assert_msg( claimrewards_activation_error_message ),
                          chain.push_action(producer_names.back(), "claimrewards"_n, mvo()("owner", producer_names.back())));
      BOOST_REQUIRE_EQUAL(0, chain.get_balance(producer_names.back()).get_amount());
   }

   // stake across 15% boundary
   chain.transfer(config::system_account_name, "producvoterb"_n, core_from_string("100000000.0000"), config::system_account_name);
   BOOST_REQUIRE_EQUAL(chain.success(), chain.stake("producvoterb"_n, core_from_string("4000000.0000"), core_from_string("4000000.0000")));
   chain.transfer(config::system_account_name, "producvoterc"_n, core_from_string("100000000.0000"), config::system_account_name);
   BOOST_REQUIRE_EQUAL(chain.success(), chain.stake("producvoterc"_n, core_from_string("2000000.0000"), core_from_string("2000000.0000")));

   BOOST_REQUIRE_EQUAL(chain.success(), chain.vote( "producvoterb"_n, vector<account_name>(producer_names.begin(), producer_names.begin()+21)));
   BOOST_REQUIRE_EQUAL(chain.success(), chain.vote( "producvoterc"_n, vector<account_name>(producer_names.begin(), producer_names.end())));

   // give a chance for everyone to produce blocks
   {
      chain.produce_blocks(21 * 12);
      bool all_21_produced = true;
      for (uint32_t i = 0; i < 21; ++i) {
         if (0 == chain.get_producer_info(producer_names[i])["unpaid_blocks"].template as<uint32_t>()) {
            all_21_produced= false;
         }
      }
      bool rest_didnt_produce = true;
      for (uint32_t i = 21; i < producer_names.size(); ++i) {
         if (0 < chain.get_producer_info(producer_names[i])["unpaid_blocks"].template as<uint32_t>()) {
            rest_didnt_produce = false;
         }
      }
      BOOST_REQUIRE_EQUAL(true, all_21_produced);
      BOOST_REQUIRE_EQUAL(true, rest_didnt_produce);
      BOOST_REQUIRE_EQUAL(chain.success(),
                          chain.push_action(producer_names.front(), "claimrewards"_n, mvo()("owner", producer_names.front())));
      BOOST_REQUIRE(0 < chain.get_balance(producer_names.front()).get_amount());
   }

   BOOST_CHECK_EQUAL( chain.success(), chain.unstake( "producvotera"_n, core_from_string("50.0000"), core_from_string("50.0000") ) );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( voters_actions_affect_proxy_and_producers, T, eosio_system_testers ) try {
   T chain;
   chain.cross_15_percent_threshold();

   chain.create_accounts_with_resources( { "donald111111"_n, "defproducer1"_n, "defproducer2"_n, "defproducer3"_n } );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer( "defproducer1"_n, 1) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer( "defproducer2"_n, 2) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer( "defproducer3"_n, 3) );

   //alice1111111 becomes a producer
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "regproxy"_n, mvo()
                                                ("proxy",  "alice1111111"_n)
                                                ("isproxy", true)
                        )
   );
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111"_n ), chain.get_voter_info( "alice1111111"_n ) );

   //alice1111111 makes stake and votes
   chain.issue_and_transfer( "alice1111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, core_from_string("30.0001"), core_from_string("20.0001") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "alice1111111"_n, { "defproducer1"_n, "defproducer2"_n } ) );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("50.0002")) == chain.get_producer_info( "defproducer1"_n )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("50.0002")) == chain.get_producer_info( "defproducer2"_n )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, chain.get_producer_info( "defproducer3"_n )["total_votes"].as_double() );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "donald111111"_n, "regproxy"_n, mvo()
                                                ("proxy",  "donald111111")
                                                ("isproxy", true)
                        )
   );
   REQUIRE_MATCHING_OBJECT( proxy( "donald111111"_n ), chain.get_voter_info( "donald111111"_n ) );

   //bob111111111 chooses alice1111111 as a proxy
   chain.issue_and_transfer( "bob111111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "bob111111111"_n, core_from_string("100.0002"), core_from_string("50.0001") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "bob111111111"_n, vector<account_name>(), "alice1111111"_n ) );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("150.0003")) == chain.get_voter_info( "alice1111111"_n )["proxied_vote_weight"].as_double() );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("200.0005")) == chain.get_producer_info( "defproducer1"_n )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("200.0005")) == chain.get_producer_info( "defproducer2"_n )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, chain.get_producer_info( "defproducer3"_n )["total_votes"].as_double() );

   //carol1111111 chooses alice1111111 as a proxy
   chain.issue_and_transfer( "carol1111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "carol1111111"_n, core_from_string("30.0001"), core_from_string("20.0001") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "carol1111111"_n, vector<account_name>(), "alice1111111"_n ) );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("200.0005")) == chain.get_voter_info( "alice1111111"_n )["proxied_vote_weight"].as_double() );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("250.0007")) == chain.get_producer_info( "defproducer1"_n )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("250.0007")) == chain.get_producer_info( "defproducer2"_n )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, chain.get_producer_info( "defproducer3"_n )["total_votes"].as_double() );

   //proxied voter carol1111111 increases stake
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "carol1111111"_n, core_from_string("50.0000"), core_from_string("70.0000") ) );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("320.0005")) == chain.get_voter_info( "alice1111111"_n )["proxied_vote_weight"].as_double() );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("370.0007")) == chain.get_producer_info( "defproducer1"_n )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("370.0007")) == chain.get_producer_info( "defproducer2"_n )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, chain.get_producer_info( "defproducer3"_n )["total_votes"].as_double() );

   //proxied voter bob111111111 decreases stake
   BOOST_REQUIRE_EQUAL( chain.success(), chain.unstake( "bob111111111"_n, core_from_string("50.0001"), core_from_string("50.0001") ) );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("220.0003")) == chain.get_voter_info( "alice1111111"_n )["proxied_vote_weight"].as_double() );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("270.0005")) == chain.get_producer_info( "defproducer1"_n )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("270.0005")) == chain.get_producer_info( "defproducer2"_n )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, chain.get_producer_info( "defproducer3"_n )["total_votes"].as_double() );

   //proxied voter carol1111111 chooses another proxy
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "carol1111111"_n, vector<account_name>(), "donald111111"_n ) );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("50.0001")), chain.get_voter_info( "alice1111111"_n )["proxied_vote_weight"].as_double() );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("170.0002")), chain.get_voter_info( "donald111111"_n )["proxied_vote_weight"].as_double() );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("100.0003")), chain.get_producer_info( "defproducer1"_n )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("100.0003")), chain.get_producer_info( "defproducer2"_n )["total_votes"].as_double() );
   BOOST_REQUIRE_EQUAL( 0, chain.get_producer_info( "defproducer3"_n )["total_votes"].as_double() );

   //bob111111111 switches to direct voting and votes for one of the same producers, but not for another one
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "bob111111111"_n, { "defproducer2"_n } ) );
   // tolerance check not a useful nodeos test
   // BOOST_TEST_REQUIRE( 0.0 == get_voter_info( "alice1111111"_n )["proxied_vote_weight"].as_double() );
   BOOST_TEST_REQUIRE(  chain.stake2votes(core_from_string("50.0002")), chain.get_producer_info( "defproducer1"_n )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( chain.stake2votes(core_from_string("100.0003")), chain.get_producer_info( "defproducer2"_n )["total_votes"].as_double() );
   BOOST_TEST_REQUIRE( 0.0 == chain.get_producer_info( "defproducer3"_n )["total_votes"].as_double() );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( vote_both_proxy_and_producers, T, eosio_system_testers ) try {
   T chain;
   //alice1111111 becomes a proxy
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "regproxy"_n, mvo()
                                                ("proxy",  "alice1111111"_n)
                                                ("isproxy", true)
                        )
   );
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111"_n ), chain.get_voter_info( "alice1111111"_n ) );

   //carol1111111 becomes a producer
   BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer( "carol1111111"_n, 1) );

   //bob111111111 chooses alice1111111 as a proxy

   chain.issue_and_transfer( "bob111111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "bob111111111"_n, core_from_string("100.0002"), core_from_string("50.0001") ) );
   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg("cannot vote for producers and proxy at same time"),
                        chain.vote( "bob111111111"_n, { "carol1111111"_n }, "alice1111111"_n ) );

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_CASE_TEMPLATE( select_invalid_proxy, T, eosio_system_testers ) try {
   T chain;
   //accumulate proxied votes
   chain.issue_and_transfer( "bob111111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "bob111111111"_n, core_from_string("100.0002"), core_from_string("50.0001") ) );

   //selecting account not registered as a proxy
   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "invalid proxy specified" ),
                        chain.vote( "bob111111111"_n, vector<account_name>(), "alice1111111"_n ) );

   //selecting not existing account as a proxy
   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "invalid proxy specified" ),
                        chain.vote( "bob111111111"_n, vector<account_name>(), "notexist"_n ) );

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_CASE_TEMPLATE( double_register_unregister_proxy_keeps_votes, T, eosio_system_testers ) try {
   T chain;
   //alice1111111 becomes a proxy
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "regproxy"_n, mvo()
                                                ("proxy",  "alice1111111"_n)
                                                ("isproxy",  1)
                        )
   );
   chain.issue_and_transfer( "alice1111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, core_from_string("5.0000"), core_from_string("5.0000") ) );
   edump((chain.get_voter_info("alice1111111"_n)));
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111"_n )( "staked", 100000 ), chain.get_voter_info( "alice1111111"_n ) );

   //bob111111111 stakes and selects alice1111111 as a proxy
   chain.issue_and_transfer( "bob111111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "bob111111111"_n, core_from_string("100.0002"), core_from_string("50.0001") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "bob111111111"_n, vector<account_name>(), "alice1111111"_n ) );
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111"_n )( "proxied_vote_weight", chain.stake2votes( core_from_string("150.0003") ))( "staked", 100000 ), chain.get_voter_info( "alice1111111"_n ) );

   //double regestering should fail without affecting total votes and stake
   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "action has no effect" ),
                        chain.push_action( "alice1111111"_n, "regproxy"_n, mvo()
                                     ("proxy",  "alice1111111"_n)
                                     ("isproxy",  1)
                        )
   );
   REQUIRE_MATCHING_OBJECT( proxy( "alice1111111"_n )( "proxied_vote_weight", chain.stake2votes(core_from_string("150.0003")) )( "staked", 100000 ), chain.get_voter_info( "alice1111111"_n ) );

   //uregister
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "regproxy"_n, mvo()
                                                ("proxy",  "alice1111111"_n)
                                                ("isproxy",  0)
                        )
   );
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n )( "proxied_vote_weight", chain.stake2votes(core_from_string("150.0003")) )( "staked", 100000 ), chain.get_voter_info( "alice1111111"_n ) );

   //double unregistering should not affect proxied_votes and stake
   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "action has no effect" ),
                        chain.push_action( "alice1111111"_n, "regproxy"_n, mvo()
                                     ("proxy",  "alice1111111"_n)
                                     ("isproxy",  0)
                        )
   );
   REQUIRE_MATCHING_OBJECT( voter( "alice1111111"_n )( "proxied_vote_weight", chain.stake2votes(core_from_string("150.0003")))( "staked", 100000 ), chain.get_voter_info( "alice1111111"_n ) );

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_CASE_TEMPLATE( proxy_cannot_use_another_proxy, T, eosio_system_testers ) try {
   T chain;
   //alice1111111 becomes a proxy
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "alice1111111"_n, "regproxy"_n, mvo()
                                                ("proxy",  "alice1111111"_n)
                                                ("isproxy",  1)
                        )
   );

   //bob111111111 becomes a proxy
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "bob111111111"_n, "regproxy"_n, mvo()
                                                ("proxy",  "bob111111111"_n)
                                                ("isproxy",  1)
                        )
   );

   //proxy should not be able to use a proxy
   chain.issue_and_transfer( "bob111111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "bob111111111"_n, core_from_string("100.0002"), core_from_string("50.0001") ) );
   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "account registered as a proxy is not allowed to use a proxy" ),
                        chain.vote( "bob111111111"_n, vector<account_name>(), "alice1111111"_n ) );

   //voter that uses a proxy should not be allowed to become a proxy
   chain.issue_and_transfer( "carol1111111"_n, core_from_string("1000.0000"),  config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "carol1111111"_n, core_from_string("100.0002"), core_from_string("50.0001") ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "carol1111111"_n, vector<account_name>(), "alice1111111"_n ) );
   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "account that uses a proxy is not allowed to become a proxy" ),
                        chain.push_action( "carol1111111"_n, "regproxy"_n, mvo()
                                     ("proxy",  "carol1111111"_n)
                                     ("isproxy",  1)
                        )
   );

   //proxy should not be able to use itself as a proxy
   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "cannot proxy to self" ),
                        chain.vote( "bob111111111"_n, vector<account_name>(), "bob111111111"_n ) );

} FC_LOG_AND_RETHROW()

fc::mutable_variant_object config_to_variant( const eosio::chain::chain_config& config ) {
   return mutable_variant_object()
      ( "max_block_net_usage", config.max_block_net_usage )
      ( "target_block_net_usage_pct", config.target_block_net_usage_pct )
      ( "max_transaction_net_usage", config.max_transaction_net_usage )
      ( "base_per_transaction_net_usage", config.base_per_transaction_net_usage )
      ( "context_free_discount_net_usage_num", config.context_free_discount_net_usage_num )
      ( "context_free_discount_net_usage_den", config.context_free_discount_net_usage_den )
      ( "max_block_cpu_usage", config.max_block_cpu_usage )
      ( "target_block_cpu_usage_pct", config.target_block_cpu_usage_pct )
      ( "max_transaction_cpu_usage", config.max_transaction_cpu_usage )
      ( "min_transaction_cpu_usage", config.min_transaction_cpu_usage )
      ( "max_transaction_lifetime", config.max_transaction_lifetime )
      ( "deferred_trx_expiration_window", config.deferred_trx_expiration_window )
      ( "max_transaction_delay", config.max_transaction_delay )
      ( "max_inline_action_size", config.max_inline_action_size )
      ( "max_inline_action_depth", config.max_inline_action_depth )
      ( "max_authority_depth", config.max_authority_depth );
}

BOOST_AUTO_TEST_CASE_TEMPLATE( elect_producers /*_and_parameters*/, T, eosio_system_testers ) try {
   T chain;
   chain.create_accounts_with_resources( {  "defproducer1"_n, "defproducer2"_n, "defproducer3"_n } );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer( "defproducer1"_n, 1) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer( "defproducer2"_n, 2) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer( "defproducer3"_n, 3) );

   //stake more than 15% of total EOS supply to activate chain
   chain.transfer("eosio"_n, "alice1111111"_n, core_from_string("600000000.0000"), "eosio"_n );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "alice1111111"_n, "alice1111111"_n, core_from_string("300000000.0000"), core_from_string("300000000.0000") ) );
   //vote for producers
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "alice1111111"_n, { "defproducer1"_n } ) );
   chain.produce_blocks(250);
   auto producer_keys = chain.control->active_producers();
   BOOST_REQUIRE_EQUAL( 1u, producer_keys.producers.size() );
   BOOST_REQUIRE_EQUAL( name("defproducer1"_n), producer_keys.producers[0].producer_name );

   //auto config = config_to_variant( chain.control->get_global_properties().configuration );
   //auto prod1_config = testing::filter_fields( config, producer_parameters_example( 1 ) );
   //REQUIRE_EQUAL_OBJECTS(prod1_config, config);

   // elect 2 producers
   chain.issue_and_transfer( "bob111111111"_n, core_from_string("80000.0000"),  config::system_account_name );
   ilog("stake");
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "bob111111111"_n, core_from_string("40000.0000"), core_from_string("40000.0000") ) );
   ilog("start vote");
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "bob111111111"_n, { "defproducer2"_n } ) );
   ilog(".");
   chain.produce_blocks(250);
   producer_keys = chain.control->active_producers();
   BOOST_REQUIRE_EQUAL( 2u, producer_keys.producers.size() );
   BOOST_REQUIRE_EQUAL( name("defproducer1"_n), producer_keys.producers[0].producer_name );
   BOOST_REQUIRE_EQUAL( name("defproducer2"_n), producer_keys.producers[1].producer_name );
   //config = config_to_variant( chain.control->get_global_properties().configuration );
   //auto prod2_config = testing::filter_fields( config, producer_parameters_example( 2 ) );
   //REQUIRE_EQUAL_OBJECTS(prod2_config, config);

   // elect 3 producers
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "bob111111111"_n, { "defproducer2"_n, "defproducer3"_n } ) );
   chain.produce_blocks(250);
   producer_keys = chain.control->active_producers();
   BOOST_REQUIRE_EQUAL( 3u, producer_keys.producers.size() );
   BOOST_REQUIRE_EQUAL( name("defproducer1"_n), producer_keys.producers[0].producer_name );
   BOOST_REQUIRE_EQUAL( name("defproducer2"_n), producer_keys.producers[1].producer_name );
   BOOST_REQUIRE_EQUAL( name("defproducer3"_n), producer_keys.producers[2].producer_name );
   //config = config_to_variant( chain.control->get_global_properties().configuration );
   //REQUIRE_EQUAL_OBJECTS(prod2_config, config);

   // try to go back to 2 producers and fail
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "bob111111111"_n, { "defproducer3"_n } ) );
   chain.produce_blocks(250);
   producer_keys = chain.control->active_producers();
   BOOST_REQUIRE_EQUAL( 3u, producer_keys.producers.size() );

   // The test below is invalid now, producer schedule is not updated if there are
   // fewer producers in the new schedule
   /*
   BOOST_REQUIRE_EQUAL( 2, producer_keys.size() );
   BOOST_REQUIRE_EQUAL( name("defproducer1"_n), producer_keys[0].producer_name );
   BOOST_REQUIRE_EQUAL( name("defproducer3"_n), producer_keys[1].producer_name );
   //config = config_to_variant( chain.control->get_global_properties().configuration );
   //auto prod3_config = testing::filter_fields( config, producer_parameters_example( 3 ) );
   //REQUIRE_EQUAL_OBJECTS(prod3_config, config);
   */

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_CASE_TEMPLATE( buyname, T, eosio_system_testers ) try {
   T chain;
   chain.create_accounts_with_resources( { "dan"_n, "sam"_n } );
   chain.transfer(config::system_account_name, "dan"_n, core_from_string( "10000.0000" ) );
   chain.transfer(config::system_account_name, "sam"_n, core_from_string( "10000.0000" ) );
   chain.stake_with_transfer( config::system_account_name, "sam"_n, core_from_string( "80000000.0000" ), core_from_string( "80000000.0000" ) );
   chain.stake_with_transfer( config::system_account_name, "dan"_n, core_from_string( "80000000.0000" ), core_from_string( "80000000.0000" ) );

   chain.regproducer( config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "sam"_n, { config::system_account_name } ) );
   // wait 14 days after min required amount has been staked
   chain.produce_block( fc::days(7) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "dan"_n, { config::system_account_name } ) );
   chain.produce_block( fc::days(7) );
   chain.produce_block();

   BOOST_REQUIRE_EXCEPTION( chain.create_accounts_with_resources( { "fail"_n }, "dan"_n ), // dan shouldn't be able to create fail
                            eosio_assert_message_exception, eosio_assert_message_is( "no active bid for name" ) );
   chain.bidname( "dan"_n, "nofail"_n, core_from_string( "1.0000" ) );
   BOOST_REQUIRE_EQUAL( "assertion failure with message: must increase bid by 10%", chain.bidname( "sam"_n, "nofail"_n, core_from_string( "1.0000" ) )); // didn't increase bid by 10%
   BOOST_REQUIRE_EQUAL( chain.success(), chain.bidname( "sam"_n, "nofail"_n, core_from_string( "2.0000" ) )); // didn't increase bid by 10%
   chain.produce_block( fc::days(1) );
   chain.produce_block();

   BOOST_REQUIRE_EXCEPTION( chain.create_accounts_with_resources( { "nofail"_n }, "dan"_n ), // dan shoudn't be able to do this, sam won
                            eosio_assert_message_exception, eosio_assert_message_is( "only highest bidder can claim" ) );
   //wlog( "verify sam can create nofail" );
   chain.create_accounts_with_resources( { "nofail"_n }, "sam"_n ); // sam should be able to do this, he won the bid
   //wlog( "verify nofail can create test.nofail" );
   chain.transfer("eosio"_n, "nofail"_n, core_from_string( "1000.0000" ) );
   chain.create_accounts_with_resources( { "test.nofail"_n }, "nofail"_n ); // only nofail can create test.nofail
   //wlog( "verify dan cannot create test.fail" );
   BOOST_REQUIRE_EXCEPTION( chain.create_accounts_with_resources( { "test.fail"_n }, "dan"_n ), // dan shouldn't be able to do this
                            eosio_assert_message_exception, eosio_assert_message_is( "only suffix may create this account" ) );

   chain.create_accounts_with_resources( { "goodgoodgood"_n }, "dan"_n ); /// 12 char names should succeed
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( bid_invalid_names, T, eosio_system_testers ) try {
   T chain;
   chain.create_accounts_with_resources( { "dan"_n } );

   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "you can only bid on top-level suffix" ),
                        chain.bidname( "dan"_n, "abcdefg.12345"_n, core_from_string( "1.0000" ) ) );

   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "the empty name is not a valid account name to bid on" ),
                        chain.bidname( "dan"_n, ""_n, core_from_string( "1.0000" ) ) );

   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "13 character names are not valid account names to bid on" ),
                        chain.bidname( "dan"_n, "abcdefgh12345"_n, core_from_string( "1.0000" ) ) );

   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "accounts with 12 character names and no dots can be created without bidding required" ),
                        chain.bidname( "dan"_n, "abcdefg12345"_n, core_from_string( "1.0000" ) ) );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( multiple_namebids, T, eosio_system_testers ) try {
   T chain;

   const std::string not_closed_message("auction for name is not closed yet");

   std::vector<account_name> accounts = { "alice"_n, "bob"_n, "carl"_n, "david"_n, "eve"_n };
   chain.create_accounts_with_resources( accounts );
   for ( const auto& a: accounts ) {
      chain.transfer(config::system_account_name, a, core_from_string( "10000.0000" ) );
      BOOST_REQUIRE_EQUAL( core_from_string( "10000.0000" ), chain.get_balance(a) );
   }
   chain.create_accounts_with_resources( { "producer"_n } );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer( "producer"_n ) );

   chain.produce_block();
   // stake but not enough to go live
   chain.stake_with_transfer( config::system_account_name, "bob"_n,  core_from_string( "35000000.0000" ), core_from_string( "35000000.0000" ) );
   chain.stake_with_transfer( config::system_account_name, "carl"_n, core_from_string( "35000000.0000" ), core_from_string( "35000000.0000" ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "bob"_n, { "producer"_n } ) );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "carl"_n, { "producer"_n } ) );

   // start bids
   chain.bidname( "bob"_n,  "prefa"_n, core_from_string("1.0003") );
   BOOST_REQUIRE_EQUAL( core_from_string( "9998.9997" ), chain.get_balance("bob"_n) );
   chain.bidname( "bob"_n,  "prefb"_n, core_from_string("1.0000") );
   chain.bidname( "bob"_n,  "prefc"_n, core_from_string("1.0000") );
   BOOST_REQUIRE_EQUAL( core_from_string( "9996.9997" ), chain.get_balance("bob"_n) );

   chain.bidname( "carl"_n, "prefd"_n, core_from_string("1.0000") );
   chain.bidname( "carl"_n, "prefe"_n, core_from_string("1.0000") );
   BOOST_REQUIRE_EQUAL( core_from_string( "9998.0000" ), chain.get_balance("carl"_n) );

   BOOST_REQUIRE_EQUAL( chain.error("assertion failure with message: account is already highest bidder"),
                        chain.bidname( "bob"_n, "prefb"_n, core_from_string("1.1001") ) );
   BOOST_REQUIRE_EQUAL( chain.error("assertion failure with message: must increase bid by 10%"),
                        chain.bidname( "alice"_n, "prefb"_n, core_from_string("1.0999") ) );
   BOOST_REQUIRE_EQUAL( core_from_string( "9996.9997" ), chain.get_balance("bob"_n) );
   BOOST_REQUIRE_EQUAL( core_from_string( "10000.0000" ), chain.get_balance("alice"_n) );


   // alice outbids bob on prefb
   {
      const asset initial_names_balance = chain.get_balance("eosio.names"_n);
      BOOST_REQUIRE_EQUAL( chain.success(),
                           chain.bidname( "alice"_n, "prefb"_n, core_from_string("1.1001") ) );
      // refund bob's failed bid on prefb
      BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "bob"_n, "bidrefund"_n, mvo()("bidder","bob")("newname", "prefb") ) );
      BOOST_REQUIRE_EQUAL( core_from_string( "9997.9997" ), chain.get_balance("bob"_n) );
      BOOST_REQUIRE_EQUAL( core_from_string( "9998.8999" ), chain.get_balance("alice"_n) );
      BOOST_REQUIRE_EQUAL( initial_names_balance + core_from_string("0.1001"), chain.get_balance("eosio.names"_n) );
   }

   // david outbids carl on prefd
   {
      BOOST_REQUIRE_EQUAL( core_from_string( "9998.0000" ), chain.get_balance("carl"_n) );
      BOOST_REQUIRE_EQUAL( core_from_string( "10000.0000" ), chain.get_balance("david"_n) );
      BOOST_REQUIRE_EQUAL( chain.success(),
                           chain.bidname( "david"_n, "prefd"_n, core_from_string("1.9900") ) );
      // refund carls's failed bid on prefd
      BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( "carl"_n, "bidrefund"_n, mvo()("bidder","carl")("newname", "prefd") ) );
      BOOST_REQUIRE_EQUAL( core_from_string( "9999.0000" ), chain.get_balance("carl"_n) );
      BOOST_REQUIRE_EQUAL( core_from_string( "9998.0100" ), chain.get_balance("david"_n) );
   }

   // eve outbids carl on prefe
   {
      BOOST_REQUIRE_EQUAL( chain.success(),
                           chain.bidname( "eve"_n, "prefe"_n, core_from_string("1.7200") ) );
   }

   chain.produce_block( fc::days(14) );
   chain.produce_block();

   // highest bid is from david for prefd but no bids can be closed yet
   BOOST_REQUIRE_EXCEPTION( chain.create_account_with_resources( "prefd"_n, "david"_n ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );

   // stake enough to go above the 15% threshold
   chain.stake_with_transfer( config::system_account_name, "alice"_n, core_from_string( "10000000.0000" ), core_from_string( "10000000.0000" ) );
   BOOST_REQUIRE_EQUAL(0u, chain.get_producer_info("producer"_n)["unpaid_blocks"].template as<uint32_t>());
   BOOST_REQUIRE_EQUAL( chain.success(), chain.vote( "alice"_n, { "producer"_n } ) );

   // need to wait for 14 days after going live
   chain.produce_blocks(10);
   chain.produce_block( fc::days(2) );
   chain.produce_blocks( 10 );
   BOOST_REQUIRE_EXCEPTION( chain.create_account_with_resources( "prefd"_n, "david"_n ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );
   // it's been 14 days, auction for prefd has been closed
   chain.produce_block( fc::days(12) );
   chain.create_account_with_resources( "prefd"_n, "david"_n );
   chain.produce_blocks(2);
   chain.produce_block( fc::hours(23) );
   // auctions for prefa, prefb, prefc, prefe haven't been closed
   BOOST_REQUIRE_EXCEPTION( chain.create_account_with_resources( "prefa"_n, "bob"_n ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );
   BOOST_REQUIRE_EXCEPTION( chain.create_account_with_resources( "prefb"_n, "alice"_n ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );
   BOOST_REQUIRE_EXCEPTION( chain.create_account_with_resources( "prefc"_n, "bob"_n ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );
   BOOST_REQUIRE_EXCEPTION( chain.create_account_with_resources( "prefe"_n, "eve"_n ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );
   // attemp to create account with no bid
   BOOST_REQUIRE_EXCEPTION( chain.create_account_with_resources( "prefg"_n, "alice"_n ),
                            fc::exception, fc_assert_exception_message_is( "no active bid for name" ) );
   // changing highest bid pushes auction closing time by 24 hours
   BOOST_REQUIRE_EQUAL( chain.success(),
                        chain.bidname( "eve"_n,  "prefb"_n, core_from_string("2.1880") ) );

   chain.produce_block( fc::hours(22) );
   chain.produce_blocks(2);

   BOOST_REQUIRE_EXCEPTION( chain.create_account_with_resources( "prefb"_n, "eve"_n ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );
   // but changing a bid that is not the highest does not push closing time
   BOOST_REQUIRE_EQUAL( chain.success(),
                        chain.bidname( "carl"_n, "prefe"_n, core_from_string("2.0980") ) );
   chain.produce_block( fc::hours(2) );
   chain.produce_blocks(2);
   // bid for prefb has closed, only highest bidder can claim
   BOOST_REQUIRE_EXCEPTION( chain.create_account_with_resources( "prefb"_n, "alice"_n ),
                            eosio_assert_message_exception, eosio_assert_message_is( "only highest bidder can claim" ) );
   BOOST_REQUIRE_EXCEPTION( chain.create_account_with_resources( "prefb"_n, "carl"_n ),
                            eosio_assert_message_exception, eosio_assert_message_is( "only highest bidder can claim" ) );
   chain.create_account_with_resources( "prefb"_n, "eve"_n );

   BOOST_REQUIRE_EXCEPTION( chain.create_account_with_resources( "prefe"_n, "carl"_n ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );
   chain.produce_block();
   chain.produce_block( fc::hours(24) );
   // by now bid for prefe has closed
   chain.create_account_with_resources( "prefe"_n, "carl"_n );
   // prefe can now create *.prefe
   BOOST_REQUIRE_EXCEPTION( chain.create_account_with_resources( "xyz.prefe"_n, "carl"_n ),
                            fc::exception, fc_assert_exception_message_is("only suffix may create this account") );
   chain.transfer(config::system_account_name, "prefe"_n, core_from_string("10000.0000") );
   chain.create_account_with_resources( "xyz.prefe"_n, "prefe"_n );

   // other auctions haven't closed
   BOOST_REQUIRE_EXCEPTION( chain.create_account_with_resources( "prefa"_n, "bob"_n ),
                            fc::exception, fc_assert_exception_message_is( not_closed_message ) );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( vote_producers_in_and_out, T, eosio_system_testers ) try {
   T chain;

   const asset net = core_from_string("80.0000");
   const asset cpu = core_from_string("80.0000");
   std::vector<account_name> voters = { "producvotera"_n, "producvoterb"_n, "producvoterc"_n, "producvoterd"_n };
   for (const auto& v: voters) {
      chain.create_account_with_resources(v, config::system_account_name, core_from_string("1.0000"), false, net, cpu);
   }

   // create accounts {defproducera, defproducerb, ..., defproducerz} and register as producers
   std::vector<account_name> producer_names;
   {
      producer_names.reserve('z' - 'a' + 1);
      const std::string root("defproducer");
      for ( char c = 'a'; c <= 'z'; ++c ) {
         producer_names.emplace_back(root + std::string(1, c));
      }
      chain.setup_producer_accounts(producer_names);
      for (const auto& p: producer_names) {
         BOOST_REQUIRE_EQUAL( chain.success(), chain.regproducer(p) );
         chain.produce_blocks(1);
         ilog( "------ get pro----------" );
         wdump((p));
         BOOST_TEST(0 == chain.get_producer_info(p)["total_votes"].template as<double>());
      }
   }

   for (const auto& v: voters) {
      chain.transfer(config::system_account_name, v, core_from_string("200000000.0000"), config::system_account_name );
      BOOST_REQUIRE_EQUAL(chain.success(), chain.stake(v, core_from_string("30000000.0000"), core_from_string("30000000.0000")) );
   }

   {
      BOOST_REQUIRE_EQUAL(chain.success(), chain.vote("producvotera"_n, vector<account_name>(producer_names.begin(), producer_names.begin()+20)));
      BOOST_REQUIRE_EQUAL(chain.success(), chain.vote("producvoterb"_n, vector<account_name>(producer_names.begin(), producer_names.begin()+21)));
      BOOST_REQUIRE_EQUAL(chain.success(), chain.vote("producvoterc"_n, vector<account_name>(producer_names.begin(), producer_names.end())));
   }

   // give a chance for everyone to produce blocks
   {
      chain.produce_blocks(23 * 12 + 20);
      bool all_21_produced = true;
      for (uint32_t i = 0; i < 21; ++i) {
         if (0 == chain.get_producer_info(producer_names[i])["unpaid_blocks"].template as<uint32_t>()) {
            all_21_produced = false;
         }
      }
      bool rest_didnt_produce = true;
      for (uint32_t i = 21; i < producer_names.size(); ++i) {
         if (0 < chain.get_producer_info(producer_names[i])["unpaid_blocks"].template as<uint32_t>()) {
            rest_didnt_produce = false;
         }
      }
      BOOST_REQUIRE(all_21_produced && rest_didnt_produce);
   }

   {
      chain.produce_block(fc::hours(7));
      const uint32_t voted_out_index = 20;
      const uint32_t new_prod_index  = 23;
      BOOST_REQUIRE_EQUAL(chain.success(), chain.stake("producvoterd"_n, core_from_string("40000000.0000"), core_from_string("40000000.0000")));
      BOOST_REQUIRE_EQUAL(chain.success(), chain.vote("producvoterd"_n, { producer_names[new_prod_index] }));
      BOOST_REQUIRE_EQUAL(0u, chain.get_producer_info(producer_names[new_prod_index])["unpaid_blocks"].template as<uint32_t>());
      chain.produce_blocks(4 * 12 * 21);
      BOOST_REQUIRE(0 < chain.get_producer_info(producer_names[new_prod_index])["unpaid_blocks"].template as<uint32_t>());
      const uint32_t initial_unpaid_blocks = chain.get_producer_info(producer_names[voted_out_index])["unpaid_blocks"].template as<uint32_t>();
      chain.produce_blocks(2 * 12 * 21);
      BOOST_REQUIRE_EQUAL(initial_unpaid_blocks, chain.get_producer_info(producer_names[voted_out_index])["unpaid_blocks"].template as<uint32_t>());
      chain.produce_block(fc::hours(24));
      BOOST_REQUIRE_EQUAL(chain.success(), chain.vote("producvoterd"_n, { producer_names[voted_out_index] }));
      chain.produce_blocks(2 * 12 * 21);
      BOOST_REQUIRE(fc::crypto::public_key() != fc::crypto::public_key(chain.get_producer_info(producer_names[voted_out_index])["producer_key"].as_string()));
      BOOST_REQUIRE_EQUAL(chain.success(), chain.push_action(producer_names[voted_out_index], "claimrewards"_n, mvo()("owner", producer_names[voted_out_index])));
   }

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( setparams, T, eosio_system_testers ) try {
   T chain;
   //install multisig contract
   abi_serializer msig_abi_ser = chain.initialize_multisig();
   auto producer_names = chain.active_and_vote_producers();

   //helper function
   auto push_action_msig = [&]( const account_name& signer, const action_name &name, const variant_object &data, bool auth = true ) -> T::action_result {
         string action_type_name = msig_abi_ser.get_action_type(name);

         action act;
         act.account = "eosio.msig"_n;
         act.name = name;
         act.data = msig_abi_ser.variant_to_binary( action_type_name, data, abi_serializer::create_yield_function(chain.abi_serializer_max_time) );

         return chain.base_tester::push_action( std::move(act), (auth ? signer : signer == "bob111111111"_n ? "alice1111111"_n : "bob111111111"_n).to_uint64_t() );
   };

   // test begins
   vector<permission_level> prod_perms;
   for ( auto& x : producer_names ) {
      prod_perms.push_back( { name(x), config::active_name } );
   }

   eosio::chain::chain_config params;
   params = chain.control->get_global_properties().configuration;
   //change some values
   params.max_block_net_usage += 10;
   params.max_transaction_lifetime += 1;

   transaction trx;
   {
      fc::variant pretty_trx = fc::mutable_variant_object()
         ("expiration", "2020-01-01T00:30")
         ("ref_block_num", 2)
         ("ref_block_prefix", 3)
         ("net_usage_words", 0)
         ("max_cpu_usage_ms", 0)
         ("delay_sec", 0)
         ("actions", fc::variants({
               fc::mutable_variant_object()
                  ("account", name(config::system_account_name))
                  ("name", "setparams")
                  ("authorization", vector<permission_level>{ { config::system_account_name, config::active_name } })
                  ("data", fc::mutable_variant_object()
                   ("params", params)
                  )
                  })
         );
      abi_serializer::from_variant(pretty_trx, trx, chain.get_resolver(), abi_serializer::create_yield_function(chain.abi_serializer_max_time));
   }

   BOOST_REQUIRE_EQUAL(chain.success(), push_action_msig( "alice1111111"_n, "propose"_n, mvo()
                                                    ("proposer",      "alice1111111"_n)
                                                    ("proposal_name", "setparams1")
                                                    ("trx",           trx)
                                                    ("requested", prod_perms)
                       )
   );

   // get 16 approvals
   for ( size_t i = 0; i < 15; ++i ) {
      BOOST_REQUIRE_EQUAL(chain.success(), push_action_msig( name(producer_names[i]), "approve"_n, mvo()
                                                       ("proposer",      "alice1111111"_n)
                                                       ("proposal_name", "setparams1")
                                                       ("level",         permission_level{ name(producer_names[i]), config::active_name })
                          )
      );
   }

   transaction_trace_ptr trace;
   chain.control->applied_transaction().connect(
   [&]( std::tuple<const transaction_trace_ptr&, const packed_transaction_ptr&> p ) {
      trace = std::get<0>(p);
   } );

   BOOST_REQUIRE_EQUAL(chain.success(), push_action_msig( "alice1111111"_n, "exec"_n, mvo()
                                                    ("proposer",      "alice1111111"_n)
                                                    ("proposal_name", "setparams1")
                                                    ("executer",      "alice1111111"_n)
                       )
   );

   BOOST_REQUIRE( bool(trace) );
   BOOST_REQUIRE_EQUAL( 1u, trace->action_traces.size() );
   BOOST_REQUIRE_EQUAL( transaction_receipt::executed, trace->receipt->status );

   chain.produce_blocks( 250 );

   // make sure that changed parameters were applied
   auto active_params = chain.control->get_global_properties().configuration;
   BOOST_REQUIRE_EQUAL( params.max_block_net_usage, active_params.max_block_net_usage );
   BOOST_REQUIRE_EQUAL( params.max_transaction_lifetime, active_params.max_transaction_lifetime );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( setram_effect, T, eosio_system_testers ) try {
   T chain;

   const asset net = core_from_string("8.0000");
   const asset cpu = core_from_string("8.0000");
   std::vector<account_name> accounts = { "aliceaccount"_n, "bobbyaccount"_n };
   for (const auto& a: accounts) {
      chain.create_account_with_resources(a, config::system_account_name, core_from_string("1.0000"), false, net, cpu);
   }

   {
      const auto name_a = accounts[0];
      chain.transfer(config::system_account_name, name_a, core_from_string("1000.0000") );
      BOOST_REQUIRE_EQUAL( core_from_string("1000.0000"), chain.get_balance(name_a) );
      const uint64_t init_bytes_a = chain.get_total_stake(name_a)["ram_bytes"].as_uint64();
      BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( name_a, name_a, core_from_string("300.0000") ) );
      BOOST_REQUIRE_EQUAL( core_from_string("700.0000"), chain.get_balance(name_a) );
      const uint64_t bought_bytes_a = chain.get_total_stake(name_a)["ram_bytes"].as_uint64() - init_bytes_a;

      // after buying and selling balance should be 700 + 300 * 0.995 * 0.995 = 997.0075 (actually 997.0074 due to rounding fees up)
      BOOST_REQUIRE_EQUAL( chain.success(), chain.sellram(name_a, bought_bytes_a ) );
      BOOST_REQUIRE_EQUAL( core_from_string("997.0074"), chain.get_balance(name_a) );
   }

   {
      const auto name_b = accounts[1];
      chain.transfer(config::system_account_name, name_b, core_from_string("1000.0000") );
      BOOST_REQUIRE_EQUAL( core_from_string("1000.0000"), chain.get_balance(name_b) );
      const uint64_t init_bytes_b = chain.get_total_stake(name_b)["ram_bytes"].as_uint64();
      // name_b buys ram at current price
      BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( name_b, name_b, core_from_string("300.0000") ) );
      BOOST_REQUIRE_EQUAL( core_from_string("700.0000"), chain.get_balance(name_b) );
      const uint64_t bought_bytes_b = chain.get_total_stake(name_b)["ram_bytes"].as_uint64() - init_bytes_b;

      // increase max_ram_size, ram bought by name_b loses part of its value
      BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg("ram may only be increased"),
                           chain.push_action(config::system_account_name, "setram"_n, mvo()("max_ram_size", 64ll*1024 * 1024 * 1024)) );
      BOOST_REQUIRE_EQUAL( chain.error("missing authority of eosio"),
                           chain.push_action(name_b, "setram"_n, mvo()("max_ram_size", 80ll*1024 * 1024 * 1024)) );
      BOOST_REQUIRE_EQUAL( chain.success(),
                           chain.push_action(config::system_account_name, "setram"_n, mvo()("max_ram_size", 80ll*1024 * 1024 * 1024)) );

      BOOST_REQUIRE_EQUAL( chain.success(), chain.sellram(name_b, bought_bytes_b ) );
      BOOST_REQUIRE( core_from_string("900.0000") < chain.get_balance(name_b) );
      BOOST_REQUIRE( core_from_string("950.0000") > chain.get_balance(name_b) );
   }

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( ram_inflation, T, eosio_system_testers ) try {
   T chain;

   const uint64_t init_max_ram_size = 64ll*1024 * 1024 * 1024;

   BOOST_REQUIRE_EQUAL( init_max_ram_size, chain.get_global_state()["max_ram_size"].as_uint64() );
   chain.produce_blocks(20);
   BOOST_REQUIRE_EQUAL( init_max_ram_size, chain.get_global_state()["max_ram_size"].as_uint64() );
   chain.transfer(config::system_account_name, "alice1111111"_n, core_from_string("1000.0000"), config::system_account_name );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("100.0000") ) );
   chain.produce_blocks(3);
   BOOST_REQUIRE_EQUAL( init_max_ram_size, chain.get_global_state()["max_ram_size"].as_uint64() );
   uint16_t rate = 1000;
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( config::system_account_name, "setramrate"_n, mvo()("bytes_per_block", rate) ) );
   BOOST_REQUIRE_EQUAL( rate, chain.get_global_state2()["new_ram_per_block"].template as<uint16_t>() );
   // last time update_ram_supply called is in buyram, num of blocks since then to
   // the block that includes the setramrate action is 1 + 3 = 4.
   // However, those 4 blocks were accumulating at a rate of 0, so the max_ram_size should not have changed.
   BOOST_REQUIRE_EQUAL( init_max_ram_size, chain.get_global_state()["max_ram_size"].as_uint64() );
   // But with additional blocks, it should start accumulating at the new rate.
   uint64_t cur_ram_size = chain.get_global_state()["max_ram_size"].as_uint64();
   chain.produce_blocks(10);
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("100.0000") ) );
   BOOST_REQUIRE_EQUAL( cur_ram_size + 11 * rate, chain.get_global_state()["max_ram_size"].as_uint64() );
   cur_ram_size = chain.get_global_state()["max_ram_size"].as_uint64();
   chain.produce_blocks(5);
   BOOST_REQUIRE_EQUAL( cur_ram_size, chain.get_global_state()["max_ram_size"].as_uint64() );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.sellram( "alice1111111"_n, 100 ) );
   BOOST_REQUIRE_EQUAL( cur_ram_size + 6 * rate, chain.get_global_state()["max_ram_size"].as_uint64() );
   cur_ram_size = chain.get_global_state()["max_ram_size"].as_uint64();
   chain.produce_blocks();
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyrambytes( "alice1111111"_n, "alice1111111"_n, 100 ) );
   BOOST_REQUIRE_EQUAL( cur_ram_size + 2 * rate, chain.get_global_state()["max_ram_size"].as_uint64() );

   BOOST_REQUIRE_EQUAL( chain.error("missing authority of eosio"),
                        chain.push_action( "alice1111111"_n, "setramrate"_n, mvo()("bytes_per_block", rate) ) );

   cur_ram_size = chain.get_global_state()["max_ram_size"].as_uint64();
   chain.produce_blocks(10);
   uint16_t old_rate = rate;
   rate = 5000;
   BOOST_REQUIRE_EQUAL( chain.success(), chain.push_action( config::system_account_name, "setramrate"_n, mvo()("bytes_per_block", rate) ) );
   BOOST_REQUIRE_EQUAL( cur_ram_size + 11 * old_rate, chain.get_global_state()["max_ram_size"].as_uint64() );
   chain.produce_blocks(5);
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyrambytes( "alice1111111"_n, "alice1111111"_n, 100 ) );
   BOOST_REQUIRE_EQUAL( cur_ram_size + 11 * old_rate + 6 * rate, chain.get_global_state()["max_ram_size"].as_uint64() );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( eosioram_ramusage, T, eosio_system_testers ) try {
   T chain;
   BOOST_REQUIRE_EQUAL( core_from_string("0.0000"), chain.get_balance( "alice1111111"_n ) );
   chain.transfer("eosio"_n, "alice1111111"_n, core_from_string("1000.0000"), "eosio"_n );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "eosio"_n, "alice1111111"_n, core_from_string("200.0000"), core_from_string("100.0000") ) );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("1000.0000") ) );

   BOOST_REQUIRE_EQUAL( false, chain.get_row_by_account( "eosio.token"_n, "alice1111111"_n, "accounts"_n, account_name(symbol{}.to_symbol_code()) ).empty() );

   //remove row
   chain.base_tester::push_action( "eosio.token"_n, "close"_n, "alice1111111"_n, mvo()
                             ( "owner", "alice1111111"_n )
                             ( "symbol", symbol{} )
   );
   BOOST_REQUIRE_EQUAL( true, chain.get_row_by_account( "eosio.token"_n, "alice1111111"_n, "accounts"_n, account_name(symbol{}.to_symbol_code()) ).empty() );

   auto rlm = chain.control->get_resource_limits_manager();
   auto eosioram_ram_usage = rlm.get_account_ram_usage("eosio.ram"_n);
   auto alice_ram_usage = rlm.get_account_ram_usage("alice1111111"_n);

   BOOST_REQUIRE_EQUAL( chain.success(), chain.sellram( "alice1111111"_n, 2048 ) );

   //make sure that ram was billed to alice, not to eosio.ram
   BOOST_REQUIRE_EQUAL( true, alice_ram_usage < rlm.get_account_ram_usage("alice1111111"_n) );
   BOOST_REQUIRE_EQUAL( eosioram_ram_usage, rlm.get_account_ram_usage("eosio.ram"_n) );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( ram_gift, T, eosio_system_testers ) try {
   T chain;
   chain.active_and_vote_producers();

   auto rlm = chain.control->get_resource_limits_manager();
   int64_t ram_bytes_orig, net_weight, cpu_weight;
   rlm.get_account_limits( "alice1111111"_n, ram_bytes_orig, net_weight, cpu_weight );

   /*
    * It seems impossible to write this test, because buyrambytes action doesn't give you exact amount of bytes requested
    *
   //check that it's possible to create account bying required_bytes(2724) + userres table(112) + userres row(160) - ram_gift_bytes(1400)
   create_account_with_resources( "abcdefghklmn"_n, "alice1111111"_n, 2724 + 112 + 160 - 1400 );

   //check that one byte less is not enough
   BOOST_REQUIRE_THROW( create_account_with_resources( "abcdefghklmn"_n, "alice1111111"_n, 2724 + 112 + 160 - 1400 - 1 ),
                        ram_usage_exceeded );
   */

   //check that stake/unstake keeps the gift
   chain.transfer("eosio"_n, "alice1111111"_n, core_from_string("1000.0000"), "eosio"_n );
   BOOST_REQUIRE_EQUAL( chain.success(), chain.stake( "eosio"_n, "alice1111111"_n, core_from_string("200.0000"), core_from_string("100.0000") ) );
   int64_t ram_bytes_after_stake;
   rlm.get_account_limits( "alice1111111"_n, ram_bytes_after_stake, net_weight, cpu_weight );
   BOOST_REQUIRE_EQUAL( ram_bytes_orig, ram_bytes_after_stake );

   BOOST_REQUIRE_EQUAL( chain.success(), chain.unstake( "eosio"_n, "alice1111111"_n, core_from_string("20.0000"), core_from_string("10.0000") ) );
   int64_t ram_bytes_after_unstake;
   rlm.get_account_limits( "alice1111111"_n, ram_bytes_after_unstake, net_weight, cpu_weight );
   BOOST_REQUIRE_EQUAL( ram_bytes_orig, ram_bytes_after_unstake );

   uint64_t ram_gift = 1400;

   int64_t ram_bytes;
   BOOST_REQUIRE_EQUAL( chain.success(), chain.buyram( "alice1111111"_n, "alice1111111"_n, core_from_string("1000.0000") ) );
   rlm.get_account_limits( "alice1111111"_n, ram_bytes, net_weight, cpu_weight );
   auto userres = chain.get_total_stake( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( userres["ram_bytes"].as_uint64() + ram_gift, static_cast<uint64_t>(ram_bytes) ); // safe to cast in this case

   BOOST_REQUIRE_EQUAL( chain.success(), chain.sellram( "alice1111111"_n, 1024 ) );
   rlm.get_account_limits( "alice1111111"_n, ram_bytes, net_weight, cpu_weight );
   userres = chain.get_total_stake( "alice1111111"_n );
   BOOST_REQUIRE_EQUAL( userres["ram_bytes"].as_uint64() + ram_gift, static_cast<uint64_t>(ram_bytes) );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( change_limited_account_back_to_unlimited, T, eosio_system_testers ) try {
   T chain;
   BOOST_REQUIRE( chain.get_total_stake( "eosio"_n ).is_null() );

   chain.transfer("eosio"_n, "alice1111111"_n, core_from_string("1.0000") );

   auto error_msg = chain.stake( "alice1111111"_n, "eosio"_n, core_from_string("0.0000"), core_from_string("1.0000") );
   auto semicolon_pos = error_msg.find(';');

   BOOST_REQUIRE_EQUAL( chain.error("account eosio has insufficient ram"),
                        error_msg.substr(0, semicolon_pos) );

   int64_t ram_bytes_needed = 0;
   {
      std::istringstream s( error_msg );
      s.seekg( semicolon_pos + 7, std::ios_base::beg );
      s >> ram_bytes_needed;
      ram_bytes_needed += 256; // enough room to cover total_resources_table
   }

   chain.push_action( "eosio"_n, "setalimits"_n, mvo()
                                          ("account", "eosio"_n)
                                          ("ram_bytes", ram_bytes_needed)
                                          ("net_weight", -1)
                                          ("cpu_weight", -1)
              );

   chain.stake( "alice1111111"_n, "eosio"_n, core_from_string("0.0000"), core_from_string("1.0000") );

   REQUIRE_MATCHING_OBJECT( chain.get_total_stake( "eosio"_n ), mvo()
      ("owner", "eosio"_n)
      ("net_weight", core_from_string("0.0000"))
      ("cpu_weight", core_from_string("1.0000"))
      ("ram_bytes",  0)
   );

   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "only supports unlimited accounts" ),
                        chain.push_action( "eosio"_n, "setalimits"_n, mvo()
                                          ("account", "eosio"_n)
                                          ("ram_bytes", ram_bytes_needed)
                                          ("net_weight", -1)
                                          ("cpu_weight", -1)
                        )
   );

   BOOST_REQUIRE_EQUAL( chain.error( "transaction net usage is too high: 128 > 0" ),
                        chain.push_action( "eosio"_n, "setalimits"_n, mvo()
                           ("account", "eosio.saving")
                           ("ram_bytes", -1)
                           ("net_weight", -1)
                           ("cpu_weight", -1)
                        )
   );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
