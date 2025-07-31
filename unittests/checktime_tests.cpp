#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include <boost/test/unit_test.hpp>
#pragma GCC diagnostic pop

#include <eosio/testing/tester.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/wasm_interface.hpp>
#include <eosio/chain/resource_limits.hpp>

#include <test_contracts.hpp>
#include <test_utils.hpp>

#include <algorithm>
#include <vector>
#include <iterator>
#include <string>

using namespace eosio;
using namespace eosio::chain::literals;
using namespace eosio::testing;
using namespace eosio::test_utils;
using namespace fc;

BOOST_AUTO_TEST_SUITE(checktime_tests)

/*************************************************************************************
 * checktime_tests test case
 *************************************************************************************/
BOOST_AUTO_TEST_CASE_TEMPLATE(checktime_pass_tests, T, validating_testers)  { try {
   T chain;

	chain.produce_block();
	chain.create_account( "testapi"_n );
	chain.produce_block();
	chain.set_code( "testapi"_n, test_contracts::test_api_wasm() );
	chain.produce_block();

   // test checktime_pass
   CALL_TEST_FUNCTION( chain, "test_checktime", "checktime_pass", {});

   BOOST_REQUIRE_EQUAL( chain.validate(), true );
} FC_LOG_AND_RETHROW() }

template<class T, typename Tester>
void call_test(Tester& test, T ac, uint32_t billed_cpu_time_us , uint32_t max_cpu_usage_ms, uint32_t max_block_cpu_ms,
               std::vector<char> payload = {}, name account = "testapi"_n, transaction_metadata::trx_type trx_type = transaction_metadata::trx_type::input ) {
   push_trx(test, ac, billed_cpu_time_us, max_cpu_usage_ms, max_block_cpu_ms, billed_cpu_time_us > 0, payload, account, trx_type);
   test.produce_block();
}

BOOST_AUTO_TEST_CASE_TEMPLATE( checktime_fail_tests, T, validating_testers ) { try {
   T t;
   t.produce_block();

   ilog( "create account" );
   t.create_account( "testapi"_n );
   ilog( "set code" );
   t.set_code( "testapi"_n, test_contracts::test_api_wasm() );
   ilog( "produce block" );
   t.produce_block();

   int64_t x; int64_t net; int64_t cpu;
   t.control->get_resource_limits_manager().get_account_limits( "testapi"_n, x, net, cpu );
   wdump((net)(cpu));

   BOOST_CHECK_EXCEPTION( call_test( t, test_api_action<WASM_TEST_ACTION("test_checktime", "checktime_failure")>{},
                                     5000, 200, 200, fc::raw::pack(10000000000000000000ULL) ),
                          deadline_exception, is_deadline_exception );

   BOOST_CHECK_EXCEPTION( call_test( t, test_api_action<WASM_TEST_ACTION("test_checktime", "checktime_failure")>{},
                                     0, 200, 200, fc::raw::pack(10000000000000000000ULL) ),
                          tx_cpu_usage_exceeded, fc_exception_message_contains("reached on chain max_transaction_cpu_usage") );

   BOOST_CHECK_EXCEPTION( push_trx( t, test_api_action<WASM_TEST_ACTION("test_checktime", "checktime_failure")>{},
                                    5000, 10, 200, false, fc::raw::pack(10000000000000000000ULL) ),
                          tx_cpu_usage_exceeded, fc_exception_message_contains("reached speculative executed adjusted trx max time") );

   uint32_t time_left_in_block_us = config::default_max_block_cpu_usage - config::default_min_transaction_cpu_usage;
   std::string dummy_string = "nonce";
   uint32_t increment = config::default_max_transaction_cpu_usage / 3;
   for( auto i = 0; time_left_in_block_us > 2*increment; ++i ) {
      t.push_dummy( "testapi"_n, dummy_string + std::to_string(i), increment );
      time_left_in_block_us -= increment;
   }
   BOOST_CHECK_EXCEPTION( call_test( t, test_api_action<WASM_TEST_ACTION("test_checktime", "checktime_failure")>{},
                                    0, 200, 200, fc::raw::pack(10000000000000000000ULL) ),
                          block_cpu_usage_exceeded, is_block_cpu_usage_exceeded );

   BOOST_REQUIRE_EQUAL( t.validate(), true );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( checktime_interrupt_test) { try {
   savanna_tester t;
   savanna_tester other;
   auto block = t.produce_block();
   other.push_block(block);
   t.create_account( "testapi"_n );
   t.set_code( "testapi"_n, test_contracts::test_api_wasm() );
   block = t.produce_block();
   other.push_block(block);

   auto [trace, b] = CALL_TEST_FUNCTION_WITH_BLOCK( t, "test_checktime", "checktime_pass", {});
   BOOST_REQUIRE_EQUAL( b->transactions.size(), 1 );

   // Make a copy of the valid block and swicth the checktime_pass transaction with checktime_failure
   auto copy_b = b->clone();
   auto signed_tx = std::get<packed_transaction>(copy_b->transactions.back().trx).get_signed_transaction();
   auto& act = signed_tx.actions.back();
   constexpr chain::name checktime_fail_n{WASM_TEST_ACTION("test_checktime", "checktime_failure")};
   act.name = checktime_fail_n;
   act.data = fc::raw::pack(10000000000000000000ULL);
   // Re-sign the transaction
   signed_tx.signatures.clear();
   signed_tx.sign(t.get_private_key("testapi"_n, "active"), t.get_chain_id());
   // Replace the transaction
   auto new_packed_tx = packed_transaction(signed_tx);
   copy_b->transactions.back().trx = std::move(new_packed_tx);

   // Re-calculate the transaction merkle
   deque<digest_type> trx_digests;
   const auto& trxs = copy_b->transactions;
   for( const auto& a : trxs )
      trx_digests.emplace_back( a.digest() );
   copy_b->transaction_mroot = calculate_merkle( std::move(trx_digests) );
   // Re-sign the block
   copy_b->producer_signature = t.get_private_key(config::system_account_name, "active").sign(copy_b->calculate_id());

   std::promise<bool> block_start_promise;
   std::future<bool> block_start_future = block_start_promise.get_future();
   other.control->accepted_block_header().connect([&](const block_signal_params& t) {
      block_start_promise.set_value(true);
   });

   std::thread th( [&c=*other.control, &block_start_future]() {
      // wait for controller to accept block
      if (block_start_future.wait_for(std::chrono::seconds(1)) == std::future_status::timeout) {
         elog("Timed out waiting for block start");
         BOOST_FAIL("Timed out waiting for block start");
      }
      std::this_thread::sleep_for( std::chrono::milliseconds(100) );
      c.interrupt_transaction(controller::interrupt_t::apply_block_trx);
   } );

   // apply block, caught in an "infinite" loop
   BOOST_CHECK_EXCEPTION( other.push_block(signed_block::create_signed_block(std::move(copy_b))), fc::exception,
                          [](const fc::exception& e) { return e.code() == interrupt_exception::code_value; } );

   th.join();

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE( checktime_speculative_max_trx_test ) { try {
   fc::temp_directory tempdir;
   auto conf_genesis = tester::default_config( tempdir );
   auto& cfg = conf_genesis.second.initial_configuration;

   cfg.max_block_cpu_usage        = 350'000;
   cfg.max_transaction_cpu_usage  = 150'000;
   cfg.min_transaction_cpu_usage  = 1;

   savanna_tester t( conf_genesis.first, conf_genesis.second );
   t.execute_setup_policy( setup_policy::full );
   t.produce_block();
   t.create_account( "pause"_n );
   t.set_code( "pause"_n, test_contracts::test_api_wasm() );
   t.produce_block();

   BOOST_CHECK_EXCEPTION( push_trx( t, test_pause_action<WASM_TEST_ACTION("test_checktime", "checktime_failure")>{},
                                    0, 25, 500, false, fc::raw::pack(10000000000000000000ULL), "pause"_n ),
                          tx_cpu_usage_exceeded, fc_exception_message_contains("reached node configured max-transaction-time") );

   auto before = fc::time_point::now();
   // test case where max-transaction-time = -1, UINT32_MAX is converted to fc::microseconds::maximum() in push_trx
   // Verify restricted to 150ms (on-chain max_transaction_cpu_usage)
   BOOST_CHECK_EXCEPTION( push_trx( t, test_pause_action<WASM_TEST_ACTION("test_checktime", "checktime_failure")>{},
                                     100000, UINT32_MAX, 10000, false, fc::raw::pack(10000000000000000000ULL), "pause"_n ),
                          tx_cpu_usage_exceeded, fc_exception_message_contains("reached on chain max_transaction_cpu_usage 150000us") );
   auto after = fc::time_point::now();
   auto dur = (after - before).count();
   // verify within 20% of expected time
   BOOST_CHECK_MESSAGE( dur >= 150'000, "elapsed " << dur << "us" );
   BOOST_CHECK_MESSAGE( dur <= 180'000, "elapsed " << dur << "us" );

   // verify interrupt works for speculative trxs
   std::thread th( [&]() {
      std::this_thread::sleep_for( std::chrono::milliseconds(50) );
      t.control->interrupt_transaction(controller::interrupt_t::speculative_block_trx);
   } );

   before = fc::time_point::now();
   BOOST_CHECK_EXCEPTION( push_trx( t, test_pause_action<WASM_TEST_ACTION("test_checktime", "checktime_failure")>{},
                                     100000, UINT32_MAX, 10000, false, fc::raw::pack(10000000000000000000ULL), "pause"_n ),
                          interrupt_exception, fc_exception_message_contains("interrupt signaled") );
   after = fc::time_point::now();
   dur = (after - before).count();
   // verify within 20% of expected time
   BOOST_CHECK_MESSAGE( dur >= 50'000, "elapsed " << dur << "us" );
   BOOST_CHECK_MESSAGE( dur <= 60'000, "elapsed " << dur << "us" );

   th.join();

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( checktime_pause_max_trx_cpu_extended_test, T, testers ) { try {
   fc::temp_directory tempdir;
   auto conf_genesis = tester::default_config( tempdir );
   auto& cfg = conf_genesis.second.initial_configuration;

   cfg.max_block_cpu_usage        = 150'000;
   cfg.max_transaction_cpu_usage  = 24'999; // needs to be large enough for create_account and set_code
   cfg.min_transaction_cpu_usage  = 1;

   T t( conf_genesis.first, conf_genesis.second );
   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // eos_vm_oc wasm_runtime does not tier-up and completes compile before continuing execution.
      // A completely different test with different constraints would be needed to test with eos_vm_oc.
      // Since non-tier-up is not a normal valid nodeos runtime, just skip this test for eos_vm_oc.
      return;
   }
   t.execute_setup_policy( setup_policy::full );
   t.produce_block();
   t.create_account( "pause"_n );
   t.set_code( "pause"_n, test_contracts::test_api_wasm() );
   t.produce_block();

   int64_t ram_bytes; int64_t net; int64_t cpu;
   auto& rl = t.control->get_resource_limits_manager();
   rl.get_account_limits( "pause"_n, ram_bytes, net, cpu );
   BOOST_CHECK_EQUAL( cpu, -1 );
   auto cpu_limit = rl.get_block_cpu_limit();
   idump(("cpu_limit")(cpu_limit));
   BOOST_CHECK( cpu_limit <= 150'000 );

   // Test deadline is extended when max_transaction_cpu_time is the limiting factor

   BOOST_TEST( !t.is_code_cached("pause"_n) );

   // First call to contract which should cause the WASM to load and trx_context.pause_billing_timer() to be called.
   // Verify that the restriction on the transaction of 24'999 is honored even though there is wall clock time to
   // load the wasm. If this test fails it is possible that the wasm loaded faster or slower than expected.
   auto before = fc::time_point::now();
   BOOST_CHECK_EXCEPTION( call_test( t, test_pause_action<WASM_TEST_ACTION("test_checktime", "checktime_failure")>{},
                                     0, 9999, 500, fc::raw::pack(10000000000000000000ULL), "pause"_n ),
                          tx_cpu_usage_exceeded, fc_exception_message_contains("reached on chain max_transaction_cpu_usage") );
   auto after = fc::time_point::now();
   // Test that it runs longer than specified limit of 24'999 to allow for wasm load time.
   auto dur = (after - before).count();
   dlog("elapsed ${e}us", ("e", dur) );
   BOOST_CHECK( dur >= 24'999 ); // should never fail
   BOOST_TEST( t.is_code_cached("pause"_n) );
   // This assumes that loading the WASM takes at least 0.750 ms
   // If this check fails but duration is >= 24'999 (previous check did not fail), then the check here is likely
   // because WASM took less than 0.750 ms to load.
   BOOST_CHECK_MESSAGE( dur > 25'750, "elapsed " << dur << "us" );
   BOOST_CHECK_MESSAGE( dur < 150'000, "elapsed " << dur << "us" ); // Should not run to block_cpu_usage deadline

   // Test hitting max_transaction_time throws tx_cpu_usage_exceeded
   BOOST_CHECK_EXCEPTION( call_test( t, test_pause_action<WASM_TEST_ACTION("test_checktime", "checktime_failure")>{},
                                     0, 5, 50, fc::raw::pack(10000000000000000000ULL), "pause"_n ),
                          tx_cpu_usage_exceeded, fc_exception_message_contains("reached node configured max-transaction-time") );

   // Test hitting block deadline throws deadline_exception
   BOOST_CHECK_EXCEPTION( call_test( t, test_pause_action<WASM_TEST_ACTION("test_checktime", "checktime_failure")>{},
                                     0, 50, 5, fc::raw::pack(10000000000000000000ULL), "pause"_n ),
                          deadline_exception, is_deadline_exception );

   BOOST_REQUIRE_EQUAL( t.validate(), true );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( checktime_pause_max_trx_extended_test, T, testers ) { try {
   fc::temp_directory tempdir;
   auto conf_genesis = tester::default_config( tempdir );
   auto& cfg = conf_genesis.second.initial_configuration;

   cfg.max_block_cpu_usage        = 350'000;
   cfg.max_transaction_cpu_usage  = 250'000; // needs to be large enough for create_account and set_code
   cfg.min_transaction_cpu_usage  = 1;

   T t( conf_genesis.first, conf_genesis.second );
   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // eos_vm_oc wasm_runtime does not tier-up and completes compile before continuing execution.
      // A completely different test with different constraints would be needed to test with eos_vm_oc.
      // Since non-tier-up is not a normal valid nodeos runtime, just skip this test for eos_vm_oc.
      return;
   }
   t.execute_setup_policy( setup_policy::full );
   t.produce_block();
   t.create_account( "pause"_n );
   t.set_code( "pause"_n, test_contracts::test_api_wasm() );
   t.produce_block();

   // Test deadline is extended when max_transaction_time is the limiting factor

   BOOST_TEST( !t.is_code_cached("pause"_n) );

   // First call to contract which should cause the WASM to load and trx_context.pause_billing_timer() to be called.
   // Verify that the restriction on the max_transaction_time of 25ms is honored even though there is wall clock time to
   // load the wasm. If this test fails it is possible that the wasm loaded faster or slower than expected.
   auto before = fc::time_point::now();
   BOOST_CHECK_EXCEPTION( call_test( t, test_pause_action<WASM_TEST_ACTION("test_checktime", "checktime_failure")>{},
                                     0, 25, 500, fc::raw::pack(10000000000000000000ULL), "pause"_n ),
                          tx_cpu_usage_exceeded, fc_exception_message_contains("reached node configured max-transaction-time") );
   auto after = fc::time_point::now();
   // Test that it runs longer than specified limit of 24'999 to allow for wasm load time.
   auto dur = (after - before).count();
   dlog("elapsed ${e}us", ("e", dur) );
   BOOST_CHECK( dur >= 25'000 ); // should never fail
   BOOST_TEST( t.is_code_cached("pause"_n) );
   // This assumes that loading the WASM takes at least 0.750 ms
   // If this check fails but duration is >= 25'000 (previous check did not fail), then the check here is likely
   // because WASM took less than 0.750 ms to load.
   BOOST_CHECK_MESSAGE( dur > 25'750, "elapsed " << dur << "us" );
   BOOST_CHECK_MESSAGE( dur < 250'000, "elapsed " << dur << "us" ); // Should not run to max_transaction_cpu_usage deadline

   BOOST_REQUIRE_EQUAL( t.validate(), true );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE( checktime_pause_block_deadline_not_extended_test, T, testers ) { try {
   fc::temp_directory tempdir;
   auto conf_genesis = tester::default_config( tempdir );
   auto& cfg = conf_genesis.second.initial_configuration;

   cfg.max_block_cpu_usage        = 350'000;
   cfg.max_transaction_cpu_usage  = 250'000; // needs to be large enough for create_account and set_code
   cfg.min_transaction_cpu_usage  = 1;

   T t( conf_genesis.first, conf_genesis.second );
   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // eos_vm_oc wasm_runtime does not tier-up and completes compile before continuing execution.
      // A completely different test with different constraints would be needed to test with eos_vm_oc.
      // Since non-tier-up is not a normal valid nodeos runtime, just skip this test for eos_vm_oc.
      return;
   }
   t.execute_setup_policy( setup_policy::full );
   t.produce_block();
   t.create_account( "pause"_n );
   t.set_code( "pause"_n, test_contracts::test_api_wasm() );
   t.produce_block();

   // Test block deadline is not extended when it is the limiting factor
   // Specify large enough time so that WASM is completely loaded.

   BOOST_TEST( !t.is_code_cached("pause"_n) );

   // First call to contract which should cause the WASM to load and trx_context.pause_billing_timer() to be called.
   auto before = fc::time_point::now();
   BOOST_CHECK_EXCEPTION( call_test( t, test_pause_action<WASM_TEST_ACTION("test_checktime", "checktime_failure")>{},
                                     0, 150, 75, fc::raw::pack(10000000000000000000ULL), "pause"_n ),
                          deadline_exception, is_deadline_exception );
   auto after = fc::time_point::now();
   // WASM load times on my machine are around 35ms
   auto dur = (after - before).count();
   dlog("elapsed ${e}us", ("e", dur) );
   BOOST_CHECK( dur >= 75'000 ); // should never fail
   BOOST_TEST( t.is_code_cached("pause"_n) );

   // If this check fails but duration is >= 75'000 (previous check did not fail), then the check here is likely
   // because it took longer than 50 ms for checktime to trigger, trace to be created, and to get to the now() call.
   BOOST_CHECK_MESSAGE( dur < 125'000, "elapsed " << dur << "us" );

   BOOST_REQUIRE_EQUAL( t.validate(), true );
} FC_LOG_AND_RETHROW() }


BOOST_AUTO_TEST_CASE_TEMPLATE( checktime_pause_block_deadline_not_extended_while_loading_test, T, testers ) { try {
   fc::temp_directory tempdir;
   auto conf_genesis = tester::default_config( tempdir );
   auto& cfg = conf_genesis.second.initial_configuration;

   cfg.max_block_cpu_usage        = 350'000;
   cfg.max_transaction_cpu_usage  = 250'000; // needs to be large enough for create_account and set_code
   cfg.min_transaction_cpu_usage  = 1;

   T t( conf_genesis.first, conf_genesis.second );
   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // eos_vm_oc wasm_runtime does not tier-up and completes compile before continuing execution.
      // A completely different test with different constraints would be needed to test with eos_vm_oc.
      // Since non-tier-up is not a normal valid nodeos runtime, just skip this test for eos_vm_oc.
      return;
   }
   t.execute_setup_policy( setup_policy::full );
   t.produce_block();
   t.create_account( "pause"_n );
   t.set_code( "pause"_n, test_contracts::test_api_wasm() );
   t.produce_block();

   // Test block deadline is not extended when it is the limiting factor
   // This test is different from the previous in that not enough time is provided to load the WASM.
   // The block deadline will kick in once the timer is unpaused after loading the WASM.
   // This is difficult to determine as checktime is not checked until WASM has completed loading.
   // We want to test that blocktime is enforced immediately after timer is unpaused.

   BOOST_TEST( !t.is_code_cached("pause"_n) );

   // First call to contract which should cause the WASM to load and trx_context.pause_billing_timer() to be called.
   auto before = fc::time_point::now();
   BOOST_CHECK_EXCEPTION( call_test( t, test_pause_action<WASM_TEST_ACTION("test_checktime", "checktime_failure")>{},
                                     0, 150, 15, fc::raw::pack(10000000000000000000ULL), "pause"_n ),
                          deadline_exception, is_deadline_exception );
   auto after = fc::time_point::now();
   // Test that it runs longer than specified limit of 15ms to allow for wasm load time.
   // WASM load times on my machine are around 35ms
   auto dur = (after - before).count();
   dlog("elapsed ${e}us", ("e", dur) );
   BOOST_CHECK( dur >= 15'000 ); // should never fail
   BOOST_TEST( t.is_code_cached("pause"_n) );

   // WASM load times on my machine was 35ms.
   // Since checktime only kicks in after WASM is loaded this needs to be large enough to load the WASM, but should be
   // lower than the 150ms max_transaction_time
   BOOST_CHECK_MESSAGE( dur < 125'000, "elapsed " << dur << "us" );
   BOOST_REQUIRE_MESSAGE( dur < 150'000, "elapsed " << dur << "us" ); // should never fail

   BOOST_REQUIRE_EQUAL( t.validate(), true );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE(checktime_intrinsic, T, validating_testers) { try {
   T chain;

   chain.produce_block();
   chain.create_account("testapi"_n);
   chain.produce_block();

   std::stringstream ss;
   ss << R"CONTRACT(
(module
  (type $FUNCSIG$vij (func (param i32 i64)))
  (type $FUNCSIG$j (func  (result i64)))
  (type $FUNCSIG$vjj (func (param i64 i64)))
  (type $FUNCSIG$vii (func (param i32 i32)))
  (type $FUNCSIG$i (func  (result i32)))
  (type $FUNCSIG$iii (func (param i32 i32) (result i32)))
  (type $FUNCSIG$iiii (func (param i32 i32 i32) (result i32)))
  (type $FUNCSIG$vi (func (param i32)))
  (type $FUNCSIG$v (func ))
  (type $_1 (func (param i64 i64 i64)))
  (export "apply" (func $apply))
   (import "env" "memmove" (func $memmove (param i32 i32 i32) (result i32)))
   (import "env" "printui" (func $printui (param i64)))
  (memory $0 1)

  (func $apply (type $_1)
    (param $0 i64)
    (param $1 i64)
    (param $2 i64)
    (drop (grow_memory (i32.const 527)))

    (call $printui (i64.const 11))
)CONTRACT";

        for(unsigned int i = 0; i < 5000; ++i) {
           ss << R"CONTRACT(
(drop (call $memmove
    (i32.const 1)
    (i32.const 9)
    (i32.const 33554432)
    ))

)CONTRACT";
        }
        ss<< "))";
	chain.set_code( "testapi"_n, ss.str().c_str() );
	chain.produce_block();

        BOOST_TEST( !chain.is_code_cached("testapi"_n) );

        //initialize cache
        BOOST_CHECK_EXCEPTION( call_test( chain, test_api_action<WASM_TEST_ACTION("doesn't matter", "doesn't matter")>{},
                                          5000, 10, 10 ),
                               deadline_exception, is_deadline_exception );

        BOOST_TEST( chain.is_code_cached("testapi"_n) );

        //it will always call
        BOOST_CHECK_EXCEPTION( call_test( chain, test_api_action<WASM_TEST_ACTION("doesn't matter", "doesn't matter")>{},
                                          5000, 10, 10 ),
                               deadline_exception, is_deadline_exception );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE(checktime_grow_memory, T, validating_testers) { try {
   T chain;

   chain.produce_block();
   chain.create_account("testapi"_n);
   chain.produce_block();

   std::stringstream ss;
   ss << R"CONTRACT(
(module
  (memory 1)

  (func (export "apply") (param i64 i64 i64)
)CONTRACT";

        for(unsigned int i = 0; i < 5000; ++i) {
           ss << R"CONTRACT(
    (drop (grow_memory (i32.const 527)))
    (drop (grow_memory (i32.const -527)))

)CONTRACT";
        }
        ss<< "))";
	chain.set_code( "testapi"_n, ss.str().c_str() );
	chain.produce_block();

        BOOST_TEST( !chain.is_code_cached("testapi"_n) );

        //initialize cache
        BOOST_CHECK_EXCEPTION( call_test( chain, test_api_action<WASM_TEST_ACTION("doesn't matter", "doesn't matter")>{},
                                          5000, 10, 10 ),
                               deadline_exception, is_deadline_exception );

        BOOST_TEST( chain.is_code_cached("testapi"_n) );

        //it will always call
        BOOST_CHECK_EXCEPTION( call_test( chain, test_api_action<WASM_TEST_ACTION("doesn't matter", "doesn't matter")>{},
                                          5000, 10, 10 ),
                               deadline_exception, is_deadline_exception );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE_TEMPLATE(checktime_hashing_fail, T, validating_testers) { try {
   T chain;

	chain.produce_block();
	chain.create_account( "testapi"_n );
	chain.produce_block();
	chain.set_code( "testapi"_n, test_contracts::test_api_wasm() );
	chain.produce_block();

        BOOST_TEST( !chain.is_code_cached("testapi"_n) );
        //run a simple action to cache the contract
        CALL_TEST_FUNCTION(chain, "test_checktime", "checktime_pass", {});
        BOOST_TEST( chain.is_code_cached("testapi"_n) );

        //the contract should be cached, now we should get deadline_exception because of calls to checktime() from hashing function
        BOOST_CHECK_EXCEPTION( call_test( chain, test_api_action<WASM_TEST_ACTION("test_checktime", "checktime_sha1_failure")>{},
                                          5000, 3, 3 ),
                               deadline_exception, is_deadline_exception );

        BOOST_CHECK_EXCEPTION( call_test( chain, test_api_action<WASM_TEST_ACTION("test_checktime", "checktime_assert_sha1_failure")>{},
                                          5000, 3, 3 ),
                               deadline_exception, is_deadline_exception );

        BOOST_CHECK_EXCEPTION( call_test( chain, test_api_action<WASM_TEST_ACTION("test_checktime", "checktime_sha256_failure")>{},
                                          5000, 3, 3 ),
                               deadline_exception, is_deadline_exception );

        BOOST_CHECK_EXCEPTION( call_test( chain, test_api_action<WASM_TEST_ACTION("test_checktime", "checktime_assert_sha256_failure")>{},
                                          5000, 3, 3 ),
                               deadline_exception, is_deadline_exception );

        BOOST_CHECK_EXCEPTION( call_test( chain, test_api_action<WASM_TEST_ACTION("test_checktime", "checktime_sha512_failure")>{},
                                          5000, 3, 3 ),
                               deadline_exception, is_deadline_exception );

        BOOST_CHECK_EXCEPTION( call_test( chain, test_api_action<WASM_TEST_ACTION("test_checktime", "checktime_assert_sha512_failure")>{},
                                          5000, 3, 3 ),
                               deadline_exception, is_deadline_exception );

        BOOST_CHECK_EXCEPTION( call_test( chain, test_api_action<WASM_TEST_ACTION("test_checktime", "checktime_ripemd160_failure")>{},
                                          5000, 3, 3 ),
                               deadline_exception, is_deadline_exception );

        BOOST_CHECK_EXCEPTION( call_test( chain, test_api_action<WASM_TEST_ACTION("test_checktime", "checktime_assert_ripemd160_failure")>{},
                                          5000, 3, 3 ),
                               deadline_exception, is_deadline_exception );

   BOOST_REQUIRE_EQUAL( chain.validate(), true );
} FC_LOG_AND_RETHROW() }


BOOST_AUTO_TEST_CASE_TEMPLATE(checktime_start, T, validating_testers) try {
   T chain;

   const char checktime_start_wast[] = R"=====(
(module
 (func $start (loop (br 0)))
 (func (export "apply") (param i64 i64 i64))
 (start $start)
)
)=====";
   chain.produce_block();
   chain.create_account( "testapi"_n );
   chain.produce_block();
   chain.set_code( "testapi"_n, checktime_start_wast );
   chain.produce_block();

   BOOST_CHECK_EXCEPTION( call_test( chain, test_api_action<WASM_TEST_ACTION("doesn't matter", "doesn't matter")>{},
                                     5000, 3, 3 ),
                          deadline_exception, is_deadline_exception );
} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_SUITE_END()
