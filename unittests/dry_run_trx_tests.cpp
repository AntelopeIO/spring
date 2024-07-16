#include <eosio/chain/abi_serializer.hpp>
#include <boost/test/unit_test.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <fc/variant_object.hpp>
#include <test_contracts.hpp>

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;
using namespace fc;

using mvo = fc::mutable_variant_object;

template<typename T>
struct dry_run_trx_tester : T {
   dry_run_trx_tester() {
      T::produce_block();
   };

   void set_up_test_contract() {
      T::create_accounts( {"noauthtable"_n, "alice"_n} );
      T::set_code( "noauthtable"_n, test_contracts::no_auth_table_wasm() );
      T::set_abi( "noauthtable"_n, test_contracts::no_auth_table_abi() );
      T::produce_block();

      insert_data = abi_ser.variant_to_binary( "insert", mutable_variant_object()
         ("user", "alice") ("id", 1) ("age", 10),
         abi_serializer::create_yield_function( T::abi_serializer_max_time ) );
      getage_data = abi_ser.variant_to_binary("getage", mutable_variant_object()
         ("user", "alice"),
         abi_serializer::create_yield_function( T::abi_serializer_max_time ));
   }

   void send_action(const action& act, bool sign = false) {
      signed_transaction trx;
      trx.actions.push_back( act );
      T::set_transaction_headers( trx );
      if (sign) // dry-run can contain signature, but not required
         trx.sign(T::get_private_key(act.authorization.at(0).actor, act.authorization.at(0).permission.to_string()), T::get_chain_id());

      T::push_transaction( trx, fc::time_point::maximum(), T::DEFAULT_BILLED_CPU_TIME_US, false, transaction_metadata::trx_type::dry_run );
   }

   auto send_db_api_transaction(action_name name, bytes data, const vector<permission_level>& auth={{"alice"_n, config::active_name}},
                                transaction_metadata::trx_type type=transaction_metadata::trx_type::input, uint32_t delay_sec=0) {
      action act;
      signed_transaction trx;

      act.account = "noauthtable"_n;
      act.name = name;
      act.authorization = auth;
      act.data = data;

      trx.actions.push_back( act );
      T::set_transaction_headers( trx );
      trx.delay_sec = delay_sec;
      if ( type == transaction_metadata::trx_type::input ) {
         trx.sign(T::get_private_key("alice"_n, "active"), T::get_chain_id());
      }

      return T::push_transaction( trx, fc::time_point::maximum(), T::DEFAULT_BILLED_CPU_TIME_US, false, type );
   }

   void insert_a_record() {
      auto res = send_db_api_transaction("insert"_n, insert_data);
      BOOST_CHECK_EQUAL(res->receipt->status, transaction_receipt::executed);
      T::produce_block();
   }

   abi_serializer abi_ser{ json::from_string(test_contracts::no_auth_table_abi()).as<abi_def>(), abi_serializer::create_yield_function(T::abi_serializer_max_time )};
   bytes insert_data;
   bytes getage_data;
};

using dry_run_trx_testers = boost::mpl::list<dry_run_trx_tester<legacy_validating_tester>,
                                             dry_run_trx_tester<savanna_validating_tester>>;

BOOST_AUTO_TEST_SUITE(dry_run_trx_tests)

BOOST_AUTO_TEST_CASE_TEMPLATE( require_authorization, T, dry_run_trx_testers ) { try {
   T chain;

   chain.produce_block();

   action act = {
      {}, // no authorization provided: vector<permission_level>{{config::system_account_name,config::active_name}},
      newaccount{
       .creator  = config::system_account_name,
       .name     = "alice"_n,
       .owner    = authority( chain.get_public_key( "alice"_n, "owner" ) ),
       .active   = authority( chain.get_public_key( "alice"_n, "active" ) )
      }
   };

   // dry-run requires authorization
   BOOST_REQUIRE_THROW(chain.send_action(act, false), tx_no_auths);

   // sign trx with no authorization
   signed_transaction trx;
   trx.actions.push_back( act );
   chain.set_transaction_headers( trx );
   trx.sign(chain.get_private_key("alice"_n, "active"), chain.get_chain_id());
   BOOST_REQUIRE_THROW(chain.push_transaction( trx, fc::time_point::maximum(), chain.DEFAULT_BILLED_CPU_TIME_US, false, transaction_metadata::trx_type::dry_run ), tx_no_auths);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( newaccount_test, T, dry_run_trx_testers ) { try {
   T chain;

   chain.produce_block();

   action act = {
      vector<permission_level>{{config::system_account_name,config::active_name}},
      newaccount{
         .creator  = config::system_account_name,
         .name     = "alice"_n,
         .owner    = authority( chain.get_public_key( "alice"_n, "owner" ) ),
         .active   = authority( chain.get_public_key( "alice"_n, "active" ) )
      }
   };

   chain.send_action(act, false); // should not throw
   chain.send_action(act, false); // should not throw
   chain.send_action(act, true); // should not throw
   BOOST_CHECK_THROW(chain.get_account("alice"_n), fc::exception); // not actually created
   chain.produce_block();
   BOOST_CHECK_THROW(chain.get_account("alice"_n), fc::exception); // not actually created
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( setcode_test, T, dry_run_trx_testers ) { try {
   T chain;

   chain.produce_block();

   chain.create_accounts( {"setcodetest"_n} );

   auto wasm = test_contracts::no_auth_table_wasm();
   action act = {
      vector<permission_level>{{"setcodetest"_n,config::active_name}},
      setcode{
         .account    = "setcodetest"_n,
         .vmtype     = 0,
         .vmversion  = 0,
         .code       = bytes(wasm.begin(), wasm.end())
      }
   };

   chain.send_action(act, false); // should not throw
   chain.send_action(act, true); // should not throw
   BOOST_TEST(!chain.is_code_cached("setcodetest"_n));
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( setabi_test, T, dry_run_trx_testers ) { try {
   T chain;

   chain.produce_block();

   chain.create_accounts( {"setabitest"_n} );

   auto abi = test_contracts::no_auth_table_abi();
   action act = {
      vector<permission_level>{{"setabitest"_n,config::active_name}},
      setabi {
         .account = "setabitest"_n, .abi = bytes(abi.begin(), abi.end())
      }
   };

   chain.send_action(act, false); // should not throw
   chain.send_action(act, true); // should not throw
   const auto* accnt = chain.control->db().template find<chain::account_object, chain::by_name>( "setabitest"_n );
   BOOST_REQUIRE(accnt);
   BOOST_TEST(accnt->abi.size() == 0u); // no abi actually set
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( updateauth_test, T, dry_run_trx_testers ) { try {
   T chain;

   chain.produce_block();

   chain.create_accounts( {"alice"_n} );

   auto auth = authority( chain.get_public_key( "alice"_n, "test" ) );
   action act = {
      vector<permission_level>{{"alice"_n, config::active_name}},
      updateauth {
         .account = "alice"_n, .permission = "active"_n, .parent = "owner"_n, .auth  = auth
      }
   };

   chain.send_action(act, false); // should not throw
   chain.send_action(act, true); // should not throw
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( deleteauth_test, T, dry_run_trx_testers ) { try {
   T chain;

   chain.produce_block();

   chain.create_accounts( {"alice"_n} );

   // update auth
   chain.push_action(config::system_account_name, updateauth::get_name(), "alice"_n, fc::mutable_variant_object()
           ("account", "alice")
           ("permission", "first")
           ("parent", "active")
           ("auth",  authority(chain.get_public_key("alice"_n, "first")))
   );

   name account = "alice"_n;
   name permission = "first"_n;
   action act = {
      vector<permission_level>{{"alice"_n, config::active_name}},
      deleteauth { account, permission }
   };

   chain.send_action(act, false); // should not throw
   chain.send_action(act, true); // should not throw
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( linkauth_test, T, dry_run_trx_testers ) { try {
   T chain;

   chain.produce_block();

   chain.create_account("eosio.token"_n);
   chain.set_code("eosio.token"_n, test_contracts::eosio_token_wasm());
   chain.set_abi("eosio.token"_n, test_contracts::eosio_token_abi());

   chain.create_accounts( {"alice"_n} );

   // update auth
   chain.push_action(config::system_account_name, updateauth::get_name(), "alice"_n, fc::mutable_variant_object()
           ("account", "alice")
           ("permission", "first")
           ("parent", "active")
           ("auth",  authority(chain.get_public_key("alice"_n, "first")))
   );

   name account = "alice"_n;
   name code = "eosio_token"_n;
   name type = "transfer"_n;
   name requirement = "first"_n;
   action act = {
      vector<permission_level>{{"alice"_n, config::active_name}},
      linkauth { account, code, type, requirement }
   };

   chain.send_action(act, false); // should not throw
   chain.send_action(act, true); // should not throw
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( unlinkauth_test, T, dry_run_trx_testers ) { try {
   T chain;

   chain.produce_block();

   chain.create_account("eosio.token"_n);
   chain.set_code("eosio.token"_n, test_contracts::eosio_token_wasm());
   chain.set_abi("eosio.token"_n, test_contracts::eosio_token_abi());

   chain.create_accounts( {"alice"_n} );

   // update auth
   chain.push_action(config::system_account_name, updateauth::get_name(), "alice"_n, fc::mutable_variant_object()
           ("account", "alice")
           ("permission", "first")
           ("parent", "active")
           ("auth",  authority(chain.get_public_key("alice"_n, "first")))
   );

   // link auth
   chain.push_action(config::system_account_name, linkauth::get_name(), "alice"_n, fc::mutable_variant_object()
           ("account", "alice")
           ("code", "eosio.token")
           ("type", "transfer")
           ("requirement", "first"));

   name account = "alice"_n;
   name code = "eosio_token"_n;
   name type = "transfer"_n;
   action act = {
      vector<permission_level>{{"alice"_n, config::active_name}},
      unlinkauth { account, code, type }
   };

   chain.send_action(act, false); // should not throw
   chain.send_action(act, true); // should not throw
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( delay_sec_test, T, dry_run_trx_testers ) { try {
   T chain;

   chain.set_up_test_contract();

   // verify dry-run transaction does not allow non-zero delay_sec.
   BOOST_CHECK_THROW(chain.send_db_api_transaction("getage"_n, chain.getage_data, {}, transaction_metadata::trx_type::dry_run, 3), transaction_exception);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( db_insert_test, T, dry_run_trx_testers ) { try {
   T chain;

   chain.set_up_test_contract();

   // verify DB operation is allowed by dry-run transaction
   chain.send_db_api_transaction("insert"_n, chain.insert_data, vector<permission_level>{{"alice"_n, config::active_name}}, transaction_metadata::trx_type::dry_run);

   // verify the dry-run insert was rolled back, use a read-only trx to query
   BOOST_CHECK_EXCEPTION(chain.send_db_api_transaction("getage"_n, chain.getage_data, {}, transaction_metadata::trx_type::read_only), fc::exception,
                            [](const fc::exception& e) {
                               return expect_assert_message(e, "Record does not exist");
                            });

   chain.insert_a_record();

   // do a dry-run transaction and verify the return value (age) is the same as inserted
   auto res = chain.send_db_api_transaction("getage"_n, chain.getage_data, vector<permission_level>{{"alice"_n, config::active_name}}, transaction_metadata::trx_type::dry_run);
   BOOST_CHECK_EQUAL(res->receipt->status, transaction_receipt::executed);
   BOOST_CHECK_EQUAL(res->action_traces[0].return_value[0], 10);
   BOOST_CHECK_GT(res->net_usage, 0u);
   BOOST_CHECK_GT(res->elapsed.count(), 0u);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( sequence_numbers_test, T, dry_run_trx_testers ) { try {
   T chain;

   chain.set_up_test_contract();

   const auto& p = chain.control->get_dynamic_global_properties();
   auto receiver_account = chain.control->db().template find<account_metadata_object,by_name>("noauthtable"_n);
   auto amo = chain.control->db().template find<account_metadata_object,by_name>("alice"_n);

   // verify sequence numbers in state increment for non-read-only transactions
   auto prev_global_action_sequence = p.global_action_sequence;
   auto prev_recv_sequence = receiver_account->recv_sequence;
   auto prev_auth_sequence = amo->auth_sequence; 

   auto res = chain.send_db_api_transaction("insert"_n, chain.insert_data);
   BOOST_CHECK_EQUAL(res->receipt->status, transaction_receipt::executed);

   BOOST_CHECK_EQUAL( prev_global_action_sequence + 1, p.global_action_sequence );
   BOOST_CHECK_EQUAL( prev_recv_sequence + 1, receiver_account->recv_sequence );
   BOOST_CHECK_EQUAL( prev_auth_sequence + 1, amo->auth_sequence );
   
   chain.produce_block();

   // verify sequence numbers in state do not change for dry-run transactions
   prev_global_action_sequence = p.global_action_sequence;
   prev_recv_sequence = receiver_account->recv_sequence;
   prev_auth_sequence = amo->auth_sequence; 

   chain.send_db_api_transaction("getage"_n, chain.getage_data, vector<permission_level>{{"alice"_n, config::active_name}}, transaction_metadata::trx_type::dry_run);

   BOOST_CHECK_EQUAL( prev_global_action_sequence, p.global_action_sequence );
   BOOST_CHECK_EQUAL( prev_recv_sequence, receiver_account->recv_sequence );
   BOOST_CHECK_EQUAL( prev_auth_sequence, amo->auth_sequence );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
