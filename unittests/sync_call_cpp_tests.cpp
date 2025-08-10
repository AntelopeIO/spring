#include <sync_call_tester.hpp>
#include <test_contracts.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/chain/config.hpp>
#include <eosio/chain/trace.hpp>
#include <boost/test/unit_test.hpp>

// Tests for sync calls initiated by contracts in C++

using namespace eosio::testing;
using mvo = fc::mutable_variant_object;

// Convenient tester for cpp tests
struct call_tester_cpp : call_tester {
   call_tester_cpp()
   : call_tester(std::vector<account_and_wasm_code>{
         {"caller"_n,  test_contracts::sync_caller_wasm(),  test_contracts::sync_caller_abi()},
         {"callee"_n,  test_contracts::sync_callee_wasm(),  test_contracts::sync_callee_abi()},
         {"callee1"_n, test_contracts::sync_callee1_wasm(), test_contracts::sync_callee1_abi()}
     })
   {}
};

BOOST_AUTO_TEST_SUITE(sync_call_cpp_tests)

// Verify a basic sync call works
BOOST_AUTO_TEST_CASE(basic_test) { try {
   call_tester_cpp t;

   // Action `basictest` in "caller"_n  takes the `input` parameter, passes it to
   // sync call in "callee"_n,, the sync call returns it back to the action, and
   // the action returns it to the user
   int   input        = 15;
   auto  trx_trace    = t.push_action("caller"_n, "basictest"_n, "caller"_n, mvo()("input", std::to_string(input)));
   auto& action_trace = trx_trace->action_traces[0];
   auto& call_trace   = action_trace.call_traces[0];

   // Verify the callee does a console log
   BOOST_REQUIRE_EQUAL(call_trace.console, "I am basictest from sync_callee");

   // Verify the callee returns back the input parameter
   int32_t output;
   auto& return_value = call_trace.return_value;
   std::memcpy(&output, return_value.data(), sizeof(output));
   BOOST_REQUIRE_EQUAL(output, input);

   // Verify pretty printing of console output
   std::string header  = "Test BEGIN ==================";
   std::string trailer = "\nTest END   ==================";

   fc::unsigned_int sender_ordinal = 0;
   std::string actual = eosio::chain::expand_console(header, trailer, action_trace.call_traces, 0, sender_ordinal, "caller", action_trace.console, action_trace.console_markers);

   static const std::string expected = R"=====(Test BEGIN ==================
Before calling sync call basictest
[caller->(callee,249882312350186951)]: CALL BEGIN ======
I am basictest from sync_callee
[caller->(callee,249882312350186951)]: CALL END   ======
After returned from basictest
Test END   ==================)=====";

   BOOST_REQUIRE_EQUAL(actual, expected);
} FC_LOG_AND_RETHROW() }

// Verify complex parameter passing works
BOOST_AUTO_TEST_CASE(params_test) { try {
   call_tester_cpp t;

   // "params_test"_n action in caller contract makes a sync call with
   // complex parameters and validates return value from the sync call
   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "paramstest"_n, "caller"_n, {}));
} FC_LOG_AND_RETHROW() }

// Verify a sequence of sync calls work
BOOST_AUTO_TEST_CASE(sequential_call_test) { try {
   call_tester_cpp t;

   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "seqcalls"_n, "caller"_n, {}));
} FC_LOG_AND_RETHROW() }

// Verify nested sync calls work
BOOST_AUTO_TEST_CASE(nested_call_test) { try {
   call_tester_cpp t;

   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "nestedcalls"_n, "caller"_n, {}));
} FC_LOG_AND_RETHROW() }

// Verify sync call to the same receiver works 
BOOST_AUTO_TEST_CASE(self_call_test) { try {
   call_tester_cpp t;

   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "selfcall"_n, "caller"_n, {}));
} FC_LOG_AND_RETHROW() }

// Verify recursive sync calls (calling back the same accounts) under
// config::default_max_sync_call_depth work
BOOST_AUTO_TEST_CASE(recursive_call_test) { try {
   call_tester_cpp t;

   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "recursvcall"_n, "caller"_n, mvo() ("n", config::default_max_sync_call_depth - 1)));
} FC_LOG_AND_RETHROW() }

// Verify sync_call_depth_exception throws for too deep recursive calls
BOOST_AUTO_TEST_CASE(deep_recursive_call_test) { try {
   call_tester_cpp t;

   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "recursvcall"_n, "caller"_n, mvo() ("n", config::default_max_sync_call_depth)),
                         sync_call_depth_exception,
                         fc_exception_message_contains("reached sync call max call depth"));
} FC_LOG_AND_RETHROW() }

// Verify void return sync call work
BOOST_AUTO_TEST_CASE(void_return_test) { try {
   call_tester_cpp t;

   auto  trx_trace    = t.push_action("caller"_n, "voidreturn"_n, "caller"_n, {});
   auto& action_trace = trx_trace->action_traces[0];
   auto& call_trace   = action_trace.call_traces[0];

   // Verify it is indeed called
   BOOST_REQUIRE_EQUAL(call_trace.console, "I am the void return function");
} FC_LOG_AND_RETHROW() }

// Verify sync calls without parameters work
BOOST_AUTO_TEST_CASE(void_parameters_test) { try {
   call_tester_cpp t;

   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "voidparam"_n, "caller"_n, {}));
} FC_LOG_AND_RETHROW() }

// Verify sync calls without return value work
BOOST_AUTO_TEST_CASE(void_return_parameters_test) { try {
   call_tester_cpp t;

   auto  trx_trace    = t.push_action("caller"_n, "voidparamret"_n, "caller"_n, {});
   auto& action_trace = trx_trace->action_traces[0];
   auto& call_trace   = action_trace.call_traces[0];

   // Verify it is indeed called
   BOOST_REQUIRE_EQUAL(call_trace.console, "I am the void return and void parameter function");
} FC_LOG_AND_RETHROW() }

// Verify exception throws if a sync call is made on an unknown receiver
BOOST_AUTO_TEST_CASE(unknown_receiver_test) { try {
   call_tester_cpp t;

   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "unkwnrcvrtst"_n, "caller"_n, {}),
                         eosio_assert_message_exception,
                         fc_exception_message_contains("receiver does not support sync call but support_mode is set to abort"));
} FC_LOG_AND_RETHROW() }

// Verify exception throws if the called function was not tagged as
// a call in the contract
BOOST_AUTO_TEST_CASE(unknown_function_test) { try {
   call_tester_cpp t;

   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "unkwnfunctst"_n, "caller"_n, {}),
                         eosio_assert_message_exception,
                         fc_exception_message_contains("receiver does not support sync call but support_mode is set to abort"));
} FC_LOG_AND_RETHROW() }

// Verify exception throws when the called function is stuck in an infinite loop
BOOST_AUTO_TEST_CASE(forever_loop_test) { try {
   call_tester_cpp t;
   signed_transaction trx;
   trx.actions.emplace_back(vector<permission_level>{{"caller"_n, config::active_name}}, "caller"_n,   "forevertest"_n, bytes{});
   t.set_transaction_headers(trx);
   t.sign(trx, "caller"_n);

   BOOST_CHECK_EXCEPTION(t.push_transaction(trx, fc::time_point::now() + fc::microseconds(config::default_max_block_cpu_usage)),
                         deadline_exception,
                         fc_exception_message_contains("deadline exceeded"));
} FC_LOG_AND_RETHROW() }

// Verify exception throws when the called function crashes
BOOST_AUTO_TEST_CASE(crash_test) { try {
   call_tester_cpp t;

   // Currently EOS-VM-OC and other VMs return different exception messages.
   std::string expected_msg = (t.get_config().wasm_runtime == eosio::chain::wasm_interface::vm_type::eos_vm_oc)
      ? "access violation"
      : "wasm memory out-of-bounds";

   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "crashtest"_n, "caller"_n, {}),
                         wasm_execution_error,
                         fc_exception_message_contains(expected_msg));
} FC_LOG_AND_RETHROW() }

// Verify exception throws when the call wrapper does not exist in the receiver
BOOST_AUTO_TEST_CASE(unknown_call_wrapper_test) { try {
   call_tester_cpp t;

   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "unkwnwraptst"_n, "caller"_n, {}),
                         eosio_assert_message_exception,
                         fc_exception_message_contains("receiver does not support sync call but support_mode is set to abort"));
} FC_LOG_AND_RETHROW() }

// Verify a sync call can insert a record into a table
BOOST_AUTO_TEST_CASE(insert_into_table_test) { try {
   call_tester_cpp t;

   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "insertperson"_n, "caller"_n, {}));
} FC_LOG_AND_RETHROW() }

// Verify a sync call can read a record from a table
BOOST_AUTO_TEST_CASE(read_from_table_test) { try {
   call_tester_cpp t;

   t.push_action("caller"_n, "insertperson"_n, "caller"_n, {});
   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "getperson"_n, "caller"_n, {}));
} FC_LOG_AND_RETHROW() }

// Verify a read only sync call cannot modify a table (states)
BOOST_AUTO_TEST_CASE(insert_into_table_read_only_test) { try {
   call_tester_cpp t;

   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "insertrdonly"_n, "caller"_n, {}),
                         unaccessible_api,
                         fc_exception_message_contains("this API is not allowed in read only action/call"));
} FC_LOG_AND_RETHROW() }

// Test erase after erase using the same iterator
BOOST_AUTO_TEST_CASE(erase_erase_test) { try {
   call_tester_cpp t;
   BOOST_CHECK_EXCEPTION(t.push_action("callee"_n, "eraseerase"_n, "callee"_n, {}),
                         table_operation_not_permitted,
                         fc_exception_message_contains("dereference of deleted object"));
} FC_LOG_AND_RETHROW() }

// Test erasures are broadcast along the calling path
BOOST_AUTO_TEST_CASE(indirectly_erase_erase_test) { try {
   call_tester_cpp t;
   BOOST_CHECK_EXCEPTION(t.push_action("callee"_n, "eraseerase1"_n, "callee"_n, {}),
                         table_operation_not_permitted,
                         fc_exception_message_contains("dereference of deleted object"));
} FC_LOG_AND_RETHROW() }

// Test new rows can be added into a table after the table is emptyed.
BOOST_AUTO_TEST_CASE(erase_table_test) { try {
   call_tester_cpp t;
   BOOST_REQUIRE_NO_THROW(t.push_action("callee"_n, "erasetable"_n, "callee"_n, {}));
} FC_LOG_AND_RETHROW() }

// Test iterator looping after the first iterator is erased.
BOOST_AUTO_TEST_CASE(erase_first_itearor_loop_test) { try {
   call_tester_cpp t;
   BOOST_CHECK_EXCEPTION(t.push_action("callee"_n, "eraitrloop1"_n, "callee"_n, {}),
                         table_operation_not_permitted,
                         fc_exception_message_contains("dereference of deleted object"));
} FC_LOG_AND_RETHROW() }

// Test iterator looping after the second iterator is erased.
BOOST_AUTO_TEST_CASE(erase_second_itearor_loop_test) { try {
   call_tester_cpp t;
   BOOST_CHECK_EXCEPTION(t.push_action("callee"_n, "eraitrloop2"_n, "callee"_n, {}),
                         table_operation_not_permitted,
                         fc_exception_message_contains("dereference of deleted object"));
} FC_LOG_AND_RETHROW() }

// Test iterator looping after the last iterator is erased.
BOOST_AUTO_TEST_CASE(erase_last_itearor_loop_test) { try {
   call_tester_cpp t;
   BOOST_CHECK_EXCEPTION(t.push_action("callee"_n, "eraitrloop3"_n, "callee"_n, {}),
                         table_operation_not_permitted,
                         fc_exception_message_contains("dereference of deleted object"));
} FC_LOG_AND_RETHROW() }

// Test modify after erase using the same iterator
BOOST_AUTO_TEST_CASE(erase_modify_test) { try {
   call_tester_cpp t;
   BOOST_CHECK_EXCEPTION(t.push_action("callee"_n, "erasemodify"_n, "callee"_n, {}),
                         table_operation_not_permitted,
                         fc_exception_message_contains("dereference of deleted object"));
} FC_LOG_AND_RETHROW() }

// Test erasures are broadcast along the calling path
BOOST_AUTO_TEST_CASE(indirectly_erase_modify_test) { try {
   call_tester_cpp t;
   BOOST_CHECK_EXCEPTION(t.push_action("callee"_n, "erasemodify1"_n, "callee"_n, {}),
                         table_operation_not_permitted,
                         fc_exception_message_contains("dereference of deleted object"));
} FC_LOG_AND_RETHROW() }

// Verify initiating action does not have console but its children sync calls have console
BOOST_AUTO_TEST_CASE(caller_has_no_console_test) { try {
   call_tester_cpp t;

   auto  trx_trace    = t.push_action("caller"_n, "callernocnsl"_n, "caller"_n, {});
   auto& action_trace = trx_trace->action_traces[0];
   std::string header  = "Test BEGIN ==================";
   std::string trailer = "\nTest END   ==================";
   fc::unsigned_int sender_ordinal = 0;
   std::string actual = eosio::chain::expand_console(header, trailer, action_trace.call_traces, 0, sender_ordinal, "caller", action_trace.console, action_trace.console_markers);

   static const std::string expected = R"=====(Test BEGIN ==================
[caller->(callee,249882312350186951)]: CALL BEGIN ======
I am basictest from sync_callee
[caller->(callee,249882312350186951)]: CALL END   ======
Test END   ==================)=====";

   BOOST_REQUIRE_EQUAL(actual, expected);
} FC_LOG_AND_RETHROW() }

// Verify initiating action has console but its children sync calls do not have console
BOOST_AUTO_TEST_CASE(callee_has_no_console_test) { try {
   call_tester_cpp t;

   auto  trx_trace    = t.push_action("caller"_n, "calleenocnsl"_n, "caller"_n, {});
   auto& action_trace = trx_trace->action_traces[0];
   std::string header  = "Test BEGIN ==================";
   std::string trailer = "\nTest END   ==================";
   fc::unsigned_int sender_ordinal = 0;
   std::string actual = eosio::chain::expand_console(header, trailer, action_trace.call_traces, 0, sender_ordinal, "caller", action_trace.console, action_trace.console_markers);

   static const std::string expected = R"=====(Test BEGIN ==================
Before making sync call. After returned from sync call.
Test END   ==================)=====";

   BOOST_REQUIRE_EQUAL(actual, expected);
} FC_LOG_AND_RETHROW() }

// Verify get_sender() works
BOOST_AUTO_TEST_CASE(get_sender_test) { try {
   call_tester_cpp t;
   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "getsendertst"_n, "caller"_n, {}));
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
