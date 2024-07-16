#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include <boost/test/unit_test.hpp>
#pragma GCC diagnostic pop
#include <boost/algorithm/string/predicate.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/generated_transaction_object.hpp>

#include <fc/variant_object.hpp>
#include <fc/io/json.hpp>

#include <contracts.hpp>
#include <test_contracts.hpp>

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;
using namespace fc;

template<typename T>
class currency_tester : public T {
   public:

      auto push_action(const account_name& signer, const action_name &name, const variant_object &data ) {
         string action_type_name = abi_ser.get_action_type(name);

         action act;
         act.account = "eosio.token"_n;
         act.name = name;
         act.authorization = vector<permission_level>{{signer, config::active_name}};
         act.data = abi_ser.variant_to_binary(action_type_name, data, abi_serializer::create_yield_function( T::abi_serializer_max_time ));

         signed_transaction trx;
         trx.actions.emplace_back(std::move(act));

         T::set_transaction_headers(trx);
         trx.sign(T::get_private_key(signer, "active"), T::get_chain_id());
         return T::push_transaction(trx);
      }

      asset get_balance(const account_name& account) const {
         return T::get_currency_balance("eosio.token"_n, symbol(SY(4,CUR)), account);
      }

      auto transfer(const account_name& from, const account_name& to, const std::string& quantity, const std::string& memo = "") {
         auto trace = push_action(from, "transfer"_n, mutable_variant_object()
                                  ("from",     from)
                                  ("to",       to)
                                  ("quantity", quantity)
                                  ("memo",     memo)
                                  );
         T::produce_block();
         return trace;
      }

      auto issue(const account_name& to, const std::string& quantity, const std::string& memo = "") {
         auto trace = push_action("eosio.token"_n, "issue"_n, mutable_variant_object()
                                  ("to",       to)
                                  ("quantity", quantity)
                                  ("memo",     memo)
                                  );
         T::produce_block();
         return trace;
      }

      currency_tester(setup_policy p = setup_policy::full)
         :T({}, nullptr, p), abi_ser(json::from_string(test_contracts::eosio_token_abi()).as<abi_def>(), abi_serializer::create_yield_function( T::abi_serializer_max_time ))
      {
         T::create_account( "eosio.token"_n);
         T::set_code( "eosio.token"_n, test_contracts::eosio_token_wasm() );

         auto result = push_action("eosio.token"_n, "create"_n, mutable_variant_object()
                 ("issuer",       eosio_token)
                 ("maximum_supply", "1000000000.0000 CUR")
                 ("can_freeze", 0)
                 ("can_recall", 0)
                 ("can_whitelist", 0)
         );
         wdump((result));

         result = push_action("eosio.token"_n, "issue"_n, mutable_variant_object()
                 ("to",       eosio_token)
                 ("quantity", "1000000.0000 CUR")
                 ("memo", "gggggggggggg")
         );
         wdump((result));
         T::produce_block();
      }

      abi_serializer abi_ser;
      static const name eosio_token;
};

using currency_testers = boost::mpl::list<currency_tester<legacy_validating_tester>,
                                          currency_tester<savanna_validating_tester>>;

class pre_disable_deferred_trx_currency_tester : public currency_tester<legacy_validating_tester> {
   public:
      pre_disable_deferred_trx_currency_tester() : currency_tester(setup_policy::full_except_do_not_disable_deferred_trx) {}
};

template <typename T>
const name currency_tester<T>::eosio_token = "eosio.token"_n;

BOOST_AUTO_TEST_SUITE(currency_tests)

BOOST_AUTO_TEST_CASE_TEMPLATE( bootstrap, T, currency_testers ) try {
   auto expected = asset::from_string( "1000000.0000 CUR" );
   T t;
   auto actual = t.get_currency_balance("eosio.token"_n, expected.get_symbol(), "eosio.token"_n);
   BOOST_REQUIRE_EQUAL(expected, actual);
} FC_LOG_AND_RETHROW() /// test_api_bootstrap

BOOST_AUTO_TEST_CASE_TEMPLATE( test_transfer, T, currency_testers ) try {
   T chain;

   chain.create_accounts( {"alice"_n} );

   // make a transfer from the contract to a user
   {
      auto trace = chain.push_action("eosio.token"_n, "transfer"_n, mutable_variant_object()
         ("from", chain.eosio_token)
         ("to",   "alice")
         ("quantity", "100.0000 CUR")
         ("memo", "fund Alice")
      );

      chain.produce_block();

      BOOST_REQUIRE_EQUAL(true, chain.chain_has_transaction(trace->id));
      BOOST_REQUIRE_EQUAL(chain.get_balance("alice"_n), asset::from_string( "100.0000 CUR" ) );
   }
} FC_LOG_AND_RETHROW() /// test_transfer

BOOST_AUTO_TEST_CASE_TEMPLATE( test_duplicate_transfer, T, currency_testers ) {
   T chain;

   chain.create_accounts( {"alice"_n} );

   auto trace = chain.push_action("eosio.token"_n, "transfer"_n, mutable_variant_object()
      ("from", chain.eosio_token)
      ("to",   "alice")
      ("quantity", "100.0000 CUR")
      ("memo", "fund Alice")
   );

   BOOST_REQUIRE_THROW(chain.push_action("eosio.token"_n, "transfer"_n, mutable_variant_object()
                                    ("from", chain.eosio_token)
                                    ("to",   "alice")
                                    ("quantity", "100.0000 CUR")
                                    ("memo", "fund Alice")),
                       tx_duplicate);

   chain.produce_block();

   BOOST_CHECK_EQUAL(true, chain.chain_has_transaction(trace->id));
   BOOST_CHECK_EQUAL(chain.get_balance("alice"_n), asset::from_string( "100.0000 CUR" ) );
}

BOOST_AUTO_TEST_CASE_TEMPLATE( test_addtransfer, T, currency_testers ) try {
   T chain;

   chain.create_accounts( {"alice"_n} );

   // make a transfer from the contract to a user
   {
      auto trace = chain.push_action("eosio.token"_n, "transfer"_n, mutable_variant_object()
         ("from", chain.eosio_token)
         ("to",   "alice")
         ("quantity", "100.0000 CUR")
         ("memo", "fund Alice")
      );

      chain.produce_block();

      BOOST_REQUIRE_EQUAL(true, chain.chain_has_transaction(trace->id));
      BOOST_REQUIRE_EQUAL(chain.get_balance("alice"_n), asset::from_string( "100.0000 CUR" ));
   }

   // make a transfer from the contract to a user
   {
      auto trace = chain.push_action("eosio.token"_n, "transfer"_n, mutable_variant_object()
         ("from", chain.eosio_token)
         ("to",   "alice")
         ("quantity", "10.0000 CUR")
         ("memo", "add Alice")
      );

      chain.produce_block();

      BOOST_REQUIRE_EQUAL(true, chain.chain_has_transaction(trace->id));
      BOOST_REQUIRE_EQUAL(chain.get_balance("alice"_n), asset::from_string( "110.0000 CUR" ));
   }
} FC_LOG_AND_RETHROW() /// test_transfer


BOOST_AUTO_TEST_CASE_TEMPLATE( test_overspend, T, currency_testers ) try {
   T chain;

   chain.create_accounts( {"alice"_n, "bob"_n} );

   // make a transfer from the contract to a user
   {
      auto trace = chain.push_action("eosio.token"_n, "transfer"_n, mutable_variant_object()
         ("from", chain.eosio_token)
         ("to",   "alice")
         ("quantity", "100.0000 CUR")
         ("memo", "fund Alice")
      );

      chain.produce_block();

      BOOST_REQUIRE_EQUAL(true, chain.chain_has_transaction(trace->id));
      BOOST_REQUIRE_EQUAL(chain.get_balance("alice"_n), asset::from_string( "100.0000 CUR" ));
   }

   // Overspend!
   {
      variant_object data = mutable_variant_object()
         ("from", "alice")
         ("to",   "bob")
         ("quantity", "101.0000 CUR")
         ("memo", "overspend! Alice");

      BOOST_CHECK_EXCEPTION( chain.push_action("alice"_n, "transfer"_n, data),
                             eosio_assert_message_exception, eosio_assert_message_is("overdrawn balance") );
      chain.produce_block();

      BOOST_REQUIRE_EQUAL(chain.get_balance("alice"_n), asset::from_string( "100.0000 CUR" ));
      BOOST_REQUIRE_EQUAL(chain.get_balance("bob"_n), asset::from_string( "0.0000 CUR" ));
   }
} FC_LOG_AND_RETHROW() /// test_overspend

BOOST_AUTO_TEST_CASE_TEMPLATE( test_fullspend, T, currency_testers ) try {
   T chain;

   chain.create_accounts( {"alice"_n, "bob"_n} );

   // make a transfer from the contract to a user
   {
      auto trace = chain.push_action("eosio.token"_n, "transfer"_n, mutable_variant_object()
         ("from", chain.eosio_token)
         ("to",   "alice")
         ("quantity", "100.0000 CUR")
         ("memo", "fund Alice")
      );

      chain.produce_block();

      BOOST_REQUIRE_EQUAL(true, chain.chain_has_transaction(trace->id));
      BOOST_REQUIRE_EQUAL(chain.get_balance("alice"_n), asset::from_string( "100.0000 CUR" ));
   }

   // Full spend
   {
      variant_object data = mutable_variant_object()
         ("from", "alice")
         ("to",   "bob")
         ("quantity", "100.0000 CUR")
         ("memo", "all in! Alice");

      auto trace = chain.push_action("alice"_n, "transfer"_n, data);
      chain.produce_block();

      BOOST_REQUIRE_EQUAL(true, chain.chain_has_transaction(trace->id));
      BOOST_REQUIRE_EQUAL(chain.get_balance("alice"_n), asset::from_string( "0.0000 CUR" ));
      BOOST_REQUIRE_EQUAL(chain.get_balance("bob"_n), asset::from_string( "100.0000 CUR" ));
   }

} FC_LOG_AND_RETHROW() /// test_fullspend



BOOST_AUTO_TEST_CASE_TEMPLATE( test_symbol, T, validating_testers ) try {
   T chain;

   {
      symbol dollar(2, "DLLR");
      BOOST_REQUIRE_EQUAL(SY(2, DLLR), dollar.value());
      BOOST_REQUIRE_EQUAL(2, dollar.decimals());
      BOOST_REQUIRE_EQUAL(100, dollar.precision());
      BOOST_REQUIRE_EQUAL("DLLR", dollar.name());
      BOOST_REQUIRE_EQUAL(true, dollar.valid());
   }

   {
      symbol sys(4, "SYS");
      BOOST_REQUIRE_EQUAL(SY(4,SYS), sys.value());
      BOOST_REQUIRE_EQUAL("4,SYS", sys.to_string());
      BOOST_REQUIRE_EQUAL("SYS", sys.name());
      BOOST_REQUIRE_EQUAL(4, sys.decimals());
   }

   // default is "4,${CORE_SYMBOL_NAME}"
   {
      symbol def;
      BOOST_REQUIRE_EQUAL(4, def.decimals());
      BOOST_REQUIRE_EQUAL(CORE_SYMBOL_NAME, def.name());
   }
   // from string
   {
      symbol y = symbol::from_string("3,YEN");
      BOOST_REQUIRE_EQUAL(3, y.decimals());
      BOOST_REQUIRE_EQUAL("YEN", y.name());
   }

   // from empty string
   {
      BOOST_CHECK_EXCEPTION(symbol::from_string(""),
                            symbol_type_exception, fc_exception_message_is("creating symbol from empty string"));
   }

   // precision part missing
   {
      BOOST_CHECK_EXCEPTION(symbol::from_string("RND"),
                            symbol_type_exception, fc_exception_message_is("missing comma in symbol"));
   }

   // 0 decimals part
   {
      symbol sym = symbol::from_string("0,EURO");
      BOOST_REQUIRE_EQUAL(0, sym.decimals());
      BOOST_REQUIRE_EQUAL("EURO", sym.name());
   }

   // invalid - contains lower case characters, no validation
   {
      BOOST_CHECK_EXCEPTION(symbol malformed(SY(6,EoS)),
                            symbol_type_exception, fc_exception_message_is("invalid symbol: EoS"));
   }

   // invalid - contains lower case characters, exception thrown
   {
      BOOST_CHECK_EXCEPTION(symbol(5,"EoS"),
                            symbol_type_exception, fc_exception_message_is("invalid character in symbol name"));
   }

   // Missing decimal point, should create asset with 0 decimals
   {
      asset a = asset::from_string("10 CUR");
      BOOST_REQUIRE_EQUAL(a.get_amount(), 10);
      BOOST_REQUIRE_EQUAL(a.precision(), 1);
      BOOST_REQUIRE_EQUAL(a.decimals(), 0);
      BOOST_REQUIRE_EQUAL(a.symbol_name(), "CUR");
   }

   // Missing space
   {
      BOOST_CHECK_EXCEPTION(asset::from_string("10CUR"),
                            asset_type_exception, fc_exception_message_is("Asset's amount and symbol should be separated with space"));
   }

   // Precision is not specified when decimal separator is introduced
   {
      BOOST_CHECK_EXCEPTION(asset::from_string("10. CUR"),
                            asset_type_exception, fc_exception_message_is("Missing decimal fraction after decimal point"));
   }

   // Missing symbol
   {
      BOOST_CHECK_EXCEPTION(asset::from_string("10"),
                            asset_type_exception, fc_exception_message_is("Asset's amount and symbol should be separated with space"));
   }

   // Multiple spaces
   {
      asset a = asset::from_string("1000000000.00000  CUR");
      BOOST_REQUIRE_EQUAL(a.get_amount(), 100000000000000);
      BOOST_REQUIRE_EQUAL(a.decimals(), 5);
      BOOST_REQUIRE_EQUAL(a.symbol_name(), "CUR");
      BOOST_REQUIRE_EQUAL(a.to_string(), "1000000000.00000 CUR");
   }

   // Valid asset
   {
      asset a = asset::from_string("1000000000.00000 CUR");
      BOOST_REQUIRE_EQUAL(a.get_amount(), 100000000000000);
      BOOST_REQUIRE_EQUAL(a.decimals(), 5);
      BOOST_REQUIRE_EQUAL(a.symbol_name(), "CUR");
      BOOST_REQUIRE_EQUAL(a.to_string(), "1000000000.00000 CUR");
   }

   // Negative asset
   {
      asset a = asset::from_string("-001000000.00010 CUR");
      BOOST_REQUIRE_EQUAL(a.get_amount(), -100000000010);
      BOOST_REQUIRE_EQUAL(a.decimals(), 5);
      BOOST_REQUIRE_EQUAL(a.symbol_name(), "CUR");
      BOOST_REQUIRE_EQUAL(a.to_string(), "-1000000.00010 CUR");
   }

   // Negative asset below 1
   {
      asset a = asset::from_string("-000000000.00100 CUR");
      BOOST_REQUIRE_EQUAL(a.get_amount(), -100);
      BOOST_REQUIRE_EQUAL(a.decimals(), 5);
      BOOST_REQUIRE_EQUAL(a.symbol_name(), "CUR");
      BOOST_REQUIRE_EQUAL(a.to_string(), "-0.00100 CUR");
   }

   // Negative asset below 1
   {
      asset a = asset::from_string("-0.0001 PPP");
      BOOST_REQUIRE_EQUAL(a.get_amount(), -1);
      BOOST_REQUIRE_EQUAL(a.decimals(), 4);
      BOOST_REQUIRE_EQUAL(a.symbol_name(), "PPP");
      BOOST_REQUIRE_EQUAL(a.to_string(), "-0.0001 PPP");
   }

} FC_LOG_AND_RETHROW() /// test_symbol

BOOST_FIXTURE_TEST_CASE( test_proxy_deferred, pre_disable_deferred_trx_currency_tester ) try {
   create_accounts( {"alice"_n, "proxy"_n} );
   produce_block();

   set_code("proxy"_n, test_contracts::proxy_wasm());
   produce_block();

   abi_serializer proxy_abi_ser(json::from_string(test_contracts::proxy_abi()).as<abi_def>(), abi_serializer::create_yield_function( abi_serializer_max_time ));

   // set up proxy owner
   {
      signed_transaction trx;
      action setowner_act;
      setowner_act.account = "proxy"_n;
      setowner_act.name = "setowner"_n;
      setowner_act.authorization = vector<permission_level>{{"proxy"_n, config::active_name}};
      setowner_act.data = proxy_abi_ser.variant_to_binary("setowner", mutable_variant_object()
         ("owner", "alice")
         ("delay", 10),
         abi_serializer::create_yield_function( abi_serializer_max_time )
      );
      trx.actions.emplace_back(std::move(setowner_act));

      set_transaction_headers(trx);
      trx.sign(get_private_key("proxy"_n, "active"), get_chain_id());
      push_transaction(trx);
      produce_block();
      BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trx.id()));
   }

   // for now wasm "time" is in seconds, so we have to truncate off any parts of a second that may have applied
   fc::time_point expected_delivery(fc::seconds(head().block_time().sec_since_epoch()) + fc::seconds(10));
   {
      auto trace = push_action("eosio.token"_n, "transfer"_n, mutable_variant_object()
         ("from", eosio_token)
         ("to",   "proxy")
         ("quantity", "5.0000 CUR")
         ("memo", "fund Proxy")
      );
   }

   while(head().block_time() < expected_delivery) {
      produce_block();
      BOOST_REQUIRE_EQUAL(get_balance( "proxy"_n), asset::from_string("5.0000 CUR"));
      BOOST_REQUIRE_EQUAL(get_balance( "alice"_n),   asset::from_string("0.0000 CUR"));
   }

   produce_block();
   BOOST_REQUIRE_EQUAL(get_balance( "proxy"_n), asset::from_string("0.0000 CUR"));
   BOOST_REQUIRE_EQUAL(get_balance( "alice"_n),   asset::from_string("5.0000 CUR"));

} FC_LOG_AND_RETHROW() /// test_proxy_deferred

BOOST_FIXTURE_TEST_CASE( test_deferred_failure, pre_disable_deferred_trx_currency_tester ) try {
   produce_block();

   create_accounts( {"alice"_n, "bob"_n, "proxy"_n} );
   produce_block();

   set_code("proxy"_n, test_contracts::proxy_wasm());
   set_code("bob"_n, test_contracts::proxy_wasm());
   produce_block();

   abi_serializer proxy_abi_ser(json::from_string(test_contracts::proxy_abi()).as<abi_def>(), abi_serializer::create_yield_function( abi_serializer_max_time ));

   // set up proxy owner
   {
      signed_transaction trx;
      action setowner_act;
      setowner_act.account = "proxy"_n;
      setowner_act.name = "setowner"_n;
      setowner_act.authorization = vector<permission_level>{{"proxy"_n, config::active_name}};
      setowner_act.data = proxy_abi_ser.variant_to_binary("setowner", mutable_variant_object()
         ("owner", "bob")
         ("delay", 10),
         abi_serializer::create_yield_function( abi_serializer_max_time )
      );
      trx.actions.emplace_back(std::move(setowner_act));

      set_transaction_headers(trx);
      trx.sign(get_private_key("proxy"_n, "active"), get_chain_id());
      push_transaction(trx);
      produce_block();
      BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trx.id()));
   }
   const auto& index = control->db().get_index<generated_transaction_multi_index,by_trx_id>();
   BOOST_REQUIRE_EQUAL(0, index.size());

   auto trace = push_action("eosio.token"_n, "transfer"_n, mutable_variant_object()
      ("from", eosio_token)
      ("to",   "proxy")
      ("quantity", "5.0000 CUR")
      ("memo", "fund Proxy")
   );
   fc::time_point expected_delivery = control->pending_block_time() + fc::seconds(10);

   BOOST_REQUIRE_EQUAL(1, index.size());
   auto deferred_id = index.begin()->trx_id;
   BOOST_REQUIRE_EQUAL(false, chain_has_transaction(deferred_id));

   while( control->pending_block_time() < expected_delivery ) {
      produce_block();
      BOOST_REQUIRE_EQUAL(get_balance( "proxy"_n), asset::from_string("5.0000 CUR"));
      BOOST_REQUIRE_EQUAL(get_balance( "bob"_n),   asset::from_string("0.0000 CUR"));
      BOOST_REQUIRE_EQUAL(1, index.size());
      BOOST_REQUIRE_EQUAL(false, chain_has_transaction(deferred_id));
   }

   fc::time_point expected_redelivery = control->pending_block_time() + fc::seconds(10);
   // First deferred transaction should be retired in this block.
   // It will fail, and its onerror handler will reschedule the transaction for 10 seconds later.
   produce_block();
   BOOST_REQUIRE_EQUAL(1, index.size()); // Still one because the first deferred transaction retires but the second is created at the same time.
   BOOST_REQUIRE_EQUAL(get_transaction_receipt(deferred_id).status, transaction_receipt::soft_fail);
   auto deferred2_id = index.begin()->trx_id;

   // set up alice owner
   {
      signed_transaction trx;
      action setowner_act;
      setowner_act.account = "bob"_n;
      setowner_act.name = "setowner"_n;
      setowner_act.authorization = vector<permission_level>{{"bob"_n, config::active_name}};
      setowner_act.data = proxy_abi_ser.variant_to_binary("setowner", mutable_variant_object()
         ("owner", "alice")
         ("delay", 0),
         abi_serializer::create_yield_function( abi_serializer_max_time )
      );
      trx.actions.emplace_back(std::move(setowner_act));

      set_transaction_headers(trx);
      trx.sign(get_private_key("bob"_n, "active"), get_chain_id());
      push_transaction(trx);
      produce_block();
      BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trx.id()));
   }

   while( control->pending_block_time() < expected_redelivery ) {
      produce_block();
      BOOST_REQUIRE_EQUAL(get_balance( "proxy"_n), asset::from_string("5.0000 CUR"));
      BOOST_REQUIRE_EQUAL(get_balance( "alice"_n),   asset::from_string("0.0000 CUR"));
      BOOST_REQUIRE_EQUAL(get_balance( "bob"_n),   asset::from_string("0.0000 CUR"));
      BOOST_REQUIRE_EQUAL(1, index.size());
      BOOST_REQUIRE_EQUAL(false, chain_has_transaction(deferred2_id));
   }

   BOOST_REQUIRE_EQUAL(1, index.size());

   // Second deferred transaction should be retired in this block and should succeed,
   // which should move tokens from the proxy contract to the bob contract, thereby trigger the bob contract to
   // schedule a third deferred transaction with no delay.
   // That third deferred transaction (which moves tokens from the bob contract to account alice) should be executed immediately
   // after in the same block (note that this is the current deferred transaction scheduling policy in tester and it may change).
   produce_block();
   BOOST_REQUIRE_EQUAL(0, index.size());
   BOOST_REQUIRE_EQUAL(get_transaction_receipt(deferred2_id).status, transaction_receipt::executed);

   BOOST_REQUIRE_EQUAL(get_balance( "proxy"_n), asset::from_string("0.0000 CUR"));
   BOOST_REQUIRE_EQUAL(get_balance( "alice"_n), asset::from_string("5.0000 CUR"));
   BOOST_REQUIRE_EQUAL(get_balance( "bob"_n),   asset::from_string("0.0000 CUR"));

} FC_LOG_AND_RETHROW() /// test_currency

BOOST_AUTO_TEST_CASE_TEMPLATE( test_input_quantity, T, currency_testers ) try {
   T chain;

   chain.produce_block();

   chain.create_accounts( {"alice"_n, "bob"_n, "carl"_n} );

   // transfer to alice using right precision
   {
      auto trace = chain.transfer(chain.eosio_token, "alice"_n, "100.0000 CUR");

      BOOST_CHECK_EQUAL(true, chain.chain_has_transaction(trace->id));
      BOOST_CHECK_EQUAL(asset::from_string( "100.0000 CUR"), chain.get_balance("alice"_n));
      BOOST_CHECK_EQUAL(1000000, chain.get_balance("alice"_n).get_amount());
   }

   // transfer using different symbol name fails
   {
      BOOST_REQUIRE_THROW(chain.transfer("alice"_n, "carl"_n, "20.50 USD"), eosio_assert_message_exception);
   }

   // issue to alice using right precision
   {
      auto trace = chain.issue("alice"_n, "25.0256 CUR");

      BOOST_CHECK_EQUAL(true, chain.chain_has_transaction(trace->id));
      BOOST_CHECK_EQUAL(asset::from_string("125.0256 CUR"), chain.get_balance("alice"_n));
   }


} FC_LOG_AND_RETHROW() /// test_currency

BOOST_AUTO_TEST_SUITE_END()
