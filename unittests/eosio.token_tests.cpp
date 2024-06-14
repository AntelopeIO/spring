#include <eosio/chain/abi_serializer.hpp>
#include <eosio/testing/tester.hpp>

#include <fc/variant_object.hpp>

#include <boost/test/unit_test.hpp>

#include <contracts.hpp>
#include <test_contracts.hpp>

using namespace eosio::testing;
using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;
using namespace fc;
using namespace std;

using mvo = fc::mutable_variant_object;

template<typename T>
class eosio_token_tester : public T {
public:

   eosio_token_tester() {
      T::produce_block();

      T::create_accounts( { "alice"_n, "bob"_n, "carol"_n, "eosio.token"_n } );
      T::produce_block();

      T::set_code( "eosio.token"_n, test_contracts::eosio_token_wasm() );
      T::set_abi( "eosio.token"_n, test_contracts::eosio_token_abi() );

      T::produce_block();

      const auto& accnt = T::control->db().template get<account_object,by_name>( "eosio.token"_n );
      abi_def abi;
      BOOST_REQUIRE_EQUAL(abi_serializer::to_abi(accnt.abi, abi), true);
      abi_ser.set_abi(std::move(abi), abi_serializer::create_yield_function( T::abi_serializer_max_time ));
   }

   T::action_result push_action( const account_name& signer, const action_name &name, const variant_object &data ) {
      string action_type_name = abi_ser.get_action_type(name);

      action act;
      act.account = "eosio.token"_n;
      act.name    = name;
      act.data    = abi_ser.variant_to_binary( action_type_name, data, abi_serializer::create_yield_function( T::abi_serializer_max_time ) );

      return base_tester::push_action( std::move(act), signer.to_uint64_t() );
   }

   fc::variant get_stats( const string& symbolname )
   {
      auto symb = eosio::chain::symbol::from_string(symbolname);
      auto symbol_code = symb.to_symbol_code().value;
      vector<char> data = T::get_row_by_account( "eosio.token"_n, name(symbol_code), "stat"_n, name(symbol_code) );
      return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "currency_stats", data, abi_serializer::create_yield_function( T::abi_serializer_max_time ) );
   }

   fc::variant get_account( account_name acc, const string& symbolname)
   {
      auto symb = eosio::chain::symbol::from_string(symbolname);
      auto symbol_code = symb.to_symbol_code().value;
      vector<char> data = T::get_row_by_account( "eosio.token"_n, acc, "accounts"_n, name(symbol_code) );
      return data.empty() ? fc::variant() : abi_ser.binary_to_variant( "account", data, abi_serializer::create_yield_function( T::abi_serializer_max_time ) );
   }

   T::action_result create( account_name issuer,
                asset        maximum_supply ) {

      return push_action( "eosio.token"_n, "create"_n, mvo()
           ( "issuer", issuer)
           ( "maximum_supply", maximum_supply)
      );
   }

   T::action_result issue( account_name issuer, account_name to, asset quantity, string memo ) {
      return push_action( issuer, "issue"_n, mvo()
           ( "to", to)
           ( "quantity", quantity)
           ( "memo", memo)
      );
   }

   T::action_result transfer( account_name from,
                  account_name to,
                  asset        quantity,
                  string       memo ) {
      return push_action( from, "transfer"_n, mvo()
           ( "from", from)
           ( "to", to)
           ( "quantity", quantity)
           ( "memo", memo)
      );
   }

   abi_serializer abi_ser;
};

using eosio_token_testers = boost::mpl::list<eosio_token_tester<legacy_tester>,
                                             eosio_token_tester<savanna_tester>>;

BOOST_AUTO_TEST_SUITE(eosio_token_tests)

BOOST_AUTO_TEST_CASE_TEMPLATE( create_tests, T, eosio_token_testers ) try {
   T chain;

   auto token = chain.create( "alice"_n, asset::from_string("1000.000 TKN"));
   auto stats = chain.get_stats("3,TKN");
   REQUIRE_MATCHING_OBJECT( stats, mvo()
      ("supply", "0.000 TKN")
      ("max_supply", "1000.000 TKN")
      ("issuer", "alice")
   );
   chain.produce_block();

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( create_negative_max_supply, T, eosio_token_testers ) try {
   T chain;

   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "max-supply must be positive" ),
      chain.create( "alice"_n, asset::from_string("-1000.000 TKN"))
   );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( symbol_already_exists, T, eosio_token_testers ) try {
   T chain;

   auto token = chain.create( "alice"_n, asset::from_string("100 TKN"));
   auto stats = chain.get_stats("0,TKN");
   REQUIRE_MATCHING_OBJECT( stats, mvo()
      ("supply", "0 TKN")
      ("max_supply", "100 TKN")
      ("issuer", "alice")
   );
   chain.produce_block();

   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "token with symbol already exists" ),
                        chain.create( "alice"_n, asset::from_string("100 TKN"))
   );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( create_max_supply, T, eosio_token_testers ) try {
   T chain;

   auto token = chain.create( "alice"_n, asset::from_string("4611686018427387903 TKN"));
   auto stats = chain.get_stats("0,TKN");
   REQUIRE_MATCHING_OBJECT( stats, mvo()
      ("supply", "0 TKN")
      ("max_supply", "4611686018427387903 TKN")
      ("issuer", "alice")
   );
   chain.produce_block();

   asset max(10, symbol(SY(0, NKT)));
   share_type amount = 4611686018427387904;
   static_assert(sizeof(share_type) <= sizeof(asset), "asset changed so test is no longer valid");
   static_assert(std::is_trivially_copyable<asset>::value, "asset is not trivially copyable");
   // OK to cast as this is a test and it is a hack to construct an invalid amount
   memcpy((char*)&max, (char*)&amount, sizeof(share_type)); // hack in an invalid amount.

   BOOST_CHECK_EXCEPTION( chain.create( "alice"_n, max) , asset_type_exception, [](const asset_type_exception& e) {
      return expect_assert_message(e, "magnitude of asset amount must be less than 2^62");
   });


} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( create_max_decimals, T, eosio_token_testers ) try {
   T chain;

   auto token = chain.create( "alice"_n, asset::from_string("1.000000000000000000 TKN"));
   auto stats = chain.get_stats("18,TKN");
   REQUIRE_MATCHING_OBJECT( stats, mvo()
      ("supply", "0.000000000000000000 TKN")
      ("max_supply", "1.000000000000000000 TKN")
      ("issuer", "alice")
   );
   chain.produce_block();

   asset max(10, symbol(SY(0, NKT)));
   //1.0000000000000000000 => 0x8ac7230489e80000L
   share_type amount = 0x8ac7230489e80000L;
   static_assert(sizeof(share_type) <= sizeof(asset), "asset changed so test is no longer valid");
   static_assert(std::is_trivially_copyable<asset>::value, "asset is not trivially copyable");
   // OK to cast as this is a test and it is a hack to construct an invalid amount
   memcpy((char*)&max, (char*)&amount, sizeof(share_type)); // hack in an invalid amount

   BOOST_CHECK_EXCEPTION( chain.create( "alice"_n, max) , asset_type_exception, [](const asset_type_exception& e) {
      return expect_assert_message(e, "magnitude of asset amount must be less than 2^62");
   });

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( issue_tests, T, eosio_token_testers ) try {
   T chain;

   auto token = chain.create( "alice"_n, asset::from_string("1000.000 TKN"));
   chain.produce_block();

   chain.issue( "alice"_n, "alice"_n, asset::from_string("500.000 TKN"), "hola" );

   auto stats = chain.get_stats("3,TKN");
   REQUIRE_MATCHING_OBJECT( stats, mvo()
      ("supply", "500.000 TKN")
      ("max_supply", "1000.000 TKN")
      ("issuer", "alice")
   );

   auto alice_balance = chain.get_account("alice"_n, "3,TKN");
   REQUIRE_MATCHING_OBJECT( alice_balance, mvo()
      ("balance", "500.000 TKN")
   );

   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "quantity exceeds available supply" ),
      chain.issue( "alice"_n, "alice"_n, asset::from_string("500.001 TKN"), "hola" )
   );

   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "must issue positive quantity" ),
      chain.issue( "alice"_n, "alice"_n, asset::from_string("-1.000 TKN"), "hola" )
   );

   BOOST_REQUIRE_EQUAL( chain.success(),
      chain.issue( "alice"_n, "alice"_n, asset::from_string("1.000 TKN"), "hola" )
   );


} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( transfer_tests, T, eosio_token_testers ) try {
   T chain;

   auto token = chain.create( "alice"_n, asset::from_string("1000 CERO"));
   chain.produce_block();

   chain.issue( "alice"_n, "alice"_n, asset::from_string("1000 CERO"), "hola" );

   auto stats = chain.get_stats("0,CERO");
   REQUIRE_MATCHING_OBJECT( stats, mvo()
      ("supply", "1000 CERO")
      ("max_supply", "1000 CERO")
      ("issuer", "alice")
   );

   auto alice_balance = chain.get_account("alice"_n, "0,CERO");
   REQUIRE_MATCHING_OBJECT( alice_balance, mvo()
      ("balance", "1000 CERO")
   );

   chain.transfer( "alice"_n, "bob"_n, asset::from_string("300 CERO"), "hola" );

   alice_balance = chain.get_account("alice"_n, "0,CERO");
   REQUIRE_MATCHING_OBJECT( alice_balance, mvo()
      ("balance", "700 CERO")
      ("frozen", 0)
      ("whitelist", 1)
   );

   auto bob_balance = chain.get_account("bob"_n, "0,CERO");
   REQUIRE_MATCHING_OBJECT( bob_balance, mvo()
      ("balance", "300 CERO")
      ("frozen", 0)
      ("whitelist", 1)
   );

   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "overdrawn balance" ),
      chain.transfer( "alice"_n, "bob"_n, asset::from_string("701 CERO"), "hola" )
   );

   BOOST_REQUIRE_EQUAL( chain.wasm_assert_msg( "must transfer positive quantity" ),
      chain.transfer( "alice"_n, "bob"_n, asset::from_string("-1000 CERO"), "hola" )
   );


} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
