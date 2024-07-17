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
struct read_only_trx_tester : T {
   read_only_trx_tester() {
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
      T::produce_block();
   }

   void send_action(const action& act) {
      signed_transaction trx;
      trx.actions.push_back( act );
      T::set_transaction_headers( trx );

      T::push_transaction( trx, fc::time_point::maximum(), T::DEFAULT_BILLED_CPU_TIME_US, false, transaction_metadata::trx_type::read_only );
   }

   auto send_db_api_transaction( action_name name, bytes data, const vector<permission_level>& auth={{"alice"_n, config::active_name}}, transaction_metadata::trx_type type=transaction_metadata::trx_type::input, uint32_t delay_sec=0 ) {
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

using read_only_trx_testers = boost::mpl::list<read_only_trx_tester<legacy_validating_tester>,
                                               read_only_trx_tester<savanna_validating_tester>>;

BOOST_AUTO_TEST_SUITE(read_only_trx_tests)

BOOST_AUTO_TEST_CASE_TEMPLATE( newaccount_test, T, read_only_trx_testers ) { try {
   T chain;

   chain.produce_block();

   action act = {
      {},
      newaccount{
         .creator  = config::system_account_name,
         .name     = "alice"_n,
         .owner    = authority( chain.get_public_key( "alice"_n, "owner" ) ),
         .active   = authority( chain.get_public_key( "alice"_n, "active" ) )
      }
   };

   BOOST_CHECK_THROW( chain.send_action(act), action_validate_exception );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( setcode_test, T, read_only_trx_testers ) { try {
   T chain;

   chain.produce_block();

   std::vector<uint8_t> code(10);
   action act = {
      {}, setcode { "eosio"_n, 0, 0, bytes(code.begin(), code.end()) }
   };

   BOOST_CHECK_THROW( chain.send_action(act), action_validate_exception );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( setabi_test, T, read_only_trx_testers ) { try {
   T chain;

   chain.produce_block();

   std::vector<uint8_t> abi(10);
   action act = {
      {},
      setabi {
         .account = "alice"_n, .abi = bytes(abi.begin(), abi.end())
      }
   };

   BOOST_CHECK_THROW( chain.send_action(act), action_validate_exception );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( updateauth_test, T, read_only_trx_testers ) { try {
   T chain;

   chain.produce_block();

   auto auth = authority( chain.get_public_key( "alice"_n, "test" ) );
   action act = {
      vector<permission_level>{{config::system_account_name,config::active_name}},
      updateauth {
         .account = "alice"_n, .permission = "active"_n, .parent = "owner"_n, .auth  = auth
      }
   };

   BOOST_CHECK_THROW( chain.send_action(act), transaction_exception );
} FC_LOG_AND_RETHROW() }


BOOST_AUTO_TEST_CASE_TEMPLATE( deleteauth_test, T, read_only_trx_testers ) { try {
   T chain;

   chain.produce_block();

   name account = "alice"_n;
   name permission = "active"_n;
   action act = {
      vector<permission_level>{{config::system_account_name,config::active_name}},
      deleteauth { account, permission }
   };

   BOOST_CHECK_THROW( chain.send_action(act), transaction_exception );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( linkauth_test, T, read_only_trx_testers ) { try {
   T chain;

   chain.produce_block();

   name account = "alice"_n;
   name code = "eosio_token"_n;
   name type = "transfer"_n;
   name requirement = "first"_n;
   action act = {
      vector<permission_level>{{config::system_account_name,config::active_name}},
      linkauth { account, code, type, requirement }
   };

   BOOST_CHECK_THROW( chain.send_action(act), transaction_exception );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( unlinkauth_test, T, read_only_trx_testers ) { try {
   T chain;

   chain.produce_block();

   name account = "alice"_n;
   name code = "eosio_token"_n;
   name type = "transfer"_n;
   action act = {
      vector<permission_level>{{config::system_account_name,config::active_name}},
      unlinkauth { account, code, type }
   };

   BOOST_CHECK_THROW( chain.send_action(act), transaction_exception );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( canceldelay_test, T, read_only_trx_testers ) { try {
   T chain;

   chain.produce_block();

   permission_level canceling_auth { config::system_account_name,config::active_name };
   transaction_id_type trx_id { "0718886aa8a3895510218b523d3d694280d1dbc1f6d30e173a10b2039fc894f1" };
   action act = {
      vector<permission_level>{{config::system_account_name,config::active_name}},
      canceldelay { canceling_auth, trx_id }
   };

   BOOST_CHECK_THROW( chain.send_action(act), transaction_exception );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( db_read_only_mode_test, T, read_only_trx_testers ) { try {
   T chain;

   chain.set_up_test_contract();

   chain.insert_a_record();

   chain.control->set_db_read_only_mode();
   // verify no write is allowed in read-only mode
   BOOST_CHECK_THROW( chain.create_account("bob"_n), std::exception );

   // verify a read-only transaction in read-only mode
   auto res = chain.send_db_api_transaction("getage"_n, chain.getage_data, {}, transaction_metadata::trx_type::read_only);
   BOOST_CHECK_EQUAL(res->receipt->status, transaction_receipt::executed);
   BOOST_CHECK_EQUAL(res->action_traces[0].return_value[0], 10);
   chain.control->unset_db_read_only_mode();

   // verify db write is allowed in regular mode
   BOOST_REQUIRE_NO_THROW( chain.create_account("bob"_n) );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( db_insert_test, T, read_only_trx_testers ) { try {
   T chain;

   chain.set_up_test_contract();

   // verify DB insert is not allowed by read-only transaction
   BOOST_CHECK_THROW(chain.send_db_api_transaction("insert"_n, chain.insert_data, {}, transaction_metadata::trx_type::read_only), table_operation_not_permitted);

   // verify DB insert still works with non-read-only transaction after read-only
   chain.insert_a_record();
   
   // do a read-only transaction and verify the return value (age) is the same as inserted
   auto res = chain.send_db_api_transaction("getage"_n, chain.getage_data, {}, transaction_metadata::trx_type::read_only);
   BOOST_CHECK_EQUAL(res->receipt->status, transaction_receipt::executed);
   BOOST_CHECK_EQUAL(res->action_traces[0].return_value[0], 10);
   BOOST_CHECK_GT(res->net_usage, 0u);
   BOOST_CHECK_GT(res->elapsed.count(), 0u);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( auth_test, T, read_only_trx_testers ) { try {
   T chain;

   chain.set_up_test_contract();

   // verify read-only transaction does not allow authorizations.
   BOOST_CHECK_THROW(chain.send_db_api_transaction("getage"_n, chain.getage_data, {{"alice"_n, config::active_name}}, transaction_metadata::trx_type::read_only), transaction_exception);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( delay_sec_test, T, read_only_trx_testers ) { try {
   T chain;

   chain.set_up_test_contract();

   // verify read-only transaction does not allow non-zero delay_sec.
   BOOST_CHECK_THROW(chain.send_db_api_transaction("getage"_n, chain.getage_data, {}, transaction_metadata::trx_type::read_only, 3), transaction_exception);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( db_modify_test, T, read_only_trx_testers ) { try {
   T chain;

   chain.set_up_test_contract();

   chain.insert_a_record();

   // verify DB update is not allowed by read-only transaction
   auto modify_data = chain.abi_ser.variant_to_binary("modify", mutable_variant_object()
      ("user", "alice") ("age", 25),
      abi_serializer::create_yield_function( chain.abi_serializer_max_time )
   );
   BOOST_CHECK_THROW(chain.send_db_api_transaction("modify"_n, modify_data, {}, transaction_metadata::trx_type::read_only), table_operation_not_permitted);

   // verify DB update still works in by non-read-only transaction
   auto res = chain.send_db_api_transaction("modify"_n, modify_data);
   BOOST_CHECK_EQUAL(res->receipt->status, transaction_receipt::executed);
   chain.produce_block();

   // verify the value was successfully updated
   res = chain.send_db_api_transaction("getage"_n, chain.getage_data, {}, transaction_metadata::trx_type::read_only);
   BOOST_CHECK_EQUAL(res->receipt->status, transaction_receipt::executed);
   BOOST_CHECK_EQUAL(res->action_traces[0].return_value[0], 25);

   // verify DB update by secondary key is not allowed by read-only transaction
   auto modifybyid_data = chain.abi_ser.variant_to_binary("modifybyid", mutable_variant_object()
      ("id", 1) ("age", 50),
      abi_serializer::create_yield_function( chain.abi_serializer_max_time )
   );
   BOOST_CHECK_THROW(chain.send_db_api_transaction("modifybyid"_n, modifybyid_data, {}, transaction_metadata::trx_type::read_only), table_operation_not_permitted);

   // verify DB update by secondary key still works in by non-read-only transaction
   res = chain.send_db_api_transaction("modifybyid"_n, modifybyid_data);
   BOOST_CHECK_EQUAL(res->receipt->status, transaction_receipt::executed);
   chain.produce_block();

   // verify the value was successfully updated
   res = chain.send_db_api_transaction("getage"_n, chain.getage_data, {}, transaction_metadata::trx_type::read_only);
   BOOST_CHECK_EQUAL(res->receipt->status, transaction_receipt::executed);
   BOOST_CHECK_EQUAL(res->action_traces[0].return_value[0], 50);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( db_erase_test, T, read_only_trx_testers ) { try {
   T chain;

   chain.set_up_test_contract();

   chain.insert_a_record();

   // verify DB erase is not allowed by read-only transaction
   auto erase_data = chain.abi_ser.variant_to_binary("erase", mutable_variant_object()
      ("user", "alice"),
      abi_serializer::create_yield_function( chain.abi_serializer_max_time )
   );
   BOOST_CHECK_THROW(chain.send_db_api_transaction("erase"_n, erase_data, {}, transaction_metadata::trx_type::read_only), table_operation_not_permitted);

   // verify DB erase by secondary key is not allowed by read-only transaction
   auto erasebyid_data = chain.abi_ser.variant_to_binary("erasebyid", mutable_variant_object()
      ("id", 1),
      abi_serializer::create_yield_function( chain.abi_serializer_max_time )
   );
   BOOST_CHECK_THROW(chain.send_db_api_transaction("erasebyid"_n, erasebyid_data, {}, transaction_metadata::trx_type::read_only), table_operation_not_permitted);

   // verify DB erase still works in by non-read-only transaction
   auto res = chain.send_db_api_transaction("erase"_n, erase_data);
   BOOST_CHECK_EQUAL(res->receipt->status, transaction_receipt::executed);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( sequence_numbers_test, T, read_only_trx_testers ) { try {
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

   // verify sequence numbers in state do not change for read-only transactions
   prev_global_action_sequence = p.global_action_sequence;
   prev_recv_sequence = receiver_account->recv_sequence;
   prev_auth_sequence = amo->auth_sequence; 

   chain.send_db_api_transaction("getage"_n, chain.getage_data, {}, transaction_metadata::trx_type::read_only);

   BOOST_CHECK_EQUAL( prev_global_action_sequence, p.global_action_sequence );
   BOOST_CHECK_EQUAL( prev_recv_sequence, receiver_account->recv_sequence );
   BOOST_CHECK_EQUAL( prev_auth_sequence, amo->auth_sequence );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
