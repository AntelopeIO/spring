#include <sync_call_tester.hpp>
#include <eosio/chain/config.hpp>
#include <boost/test/unit_test.hpp>

// Test sync calls which are initiated by contracts written in C++
// and converted to WASM by CDT.

using namespace eosio::testing;
using mvo = fc::mutable_variant_object;

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
} FC_LOG_AND_RETHROW() }

// Verify complex parameter passing works
BOOST_AUTO_TEST_CASE(params_test) { try {
   call_tester_cpp t;

   // "params_test"_n action in caller contract makes a sync call with
   // complex parameters and validates return value from the sync call
   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "paramstest"_n, "caller"_n, {}));
} FC_LOG_AND_RETHROW() }

// Verify sequential sync calls works 
BOOST_AUTO_TEST_CASE(sequential_call_test) { try {
   call_tester_cpp t;

   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "seqcalls"_n, "caller"_n, {}));
} FC_LOG_AND_RETHROW() }

// Verify nested sync calls works 
BOOST_AUTO_TEST_CASE(nested_call_test) { try {
   call_tester_cpp t;

   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "nestedcalls"_n, "caller"_n, {}));
} FC_LOG_AND_RETHROW() }

// Verify sync call to the same receiver works 
BOOST_AUTO_TEST_CASE(self_call_test) { try {
   call_tester_cpp t;

   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "selfcall"_n, "caller"_n, {}));
} FC_LOG_AND_RETHROW() }

// Verify recursive calls
BOOST_AUTO_TEST_CASE(recursive_call_test) { try {
   call_tester_cpp t;

   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "recursvcall"_n, "caller"_n, mvo() ("n", config::default_max_sync_call_depth - 1)));  // why
} FC_LOG_AND_RETHROW() }

// Verify sync_call_depth_exception throws for too deep recursive calls
BOOST_AUTO_TEST_CASE(deep_recursive_call_test) { try {
   call_tester_cpp t;

   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "recursvcall"_n, "caller"_n, mvo() ("n", config::default_max_sync_call_depth)),
                         sync_call_depth_exception,
                         fc_exception_message_contains("reached sync call max call depth"));
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(void_return_test) { try {
   call_tester_cpp t;

   auto  trx_trace    = t.push_action("caller"_n, "voidreturn"_n, "caller"_n, {});
   auto& action_trace = trx_trace->action_traces[0];
   auto& call_trace   = action_trace.call_traces[0];

   // Verify it is indeed called
   BOOST_REQUIRE_EQUAL(call_trace.console, "I am the void return function");
} FC_LOG_AND_RETHROW() }

// Verify no parameters
BOOST_AUTO_TEST_CASE(void_parameters_test) { try {
   call_tester_cpp t;

   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "voidparam"_n, "caller"_n, {}));
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(void_return_parameters_test) { try {
   call_tester_cpp t;

   auto  trx_trace    = t.push_action("caller"_n, "voidparamret"_n, "caller"_n, {});
   auto& action_trace = trx_trace->action_traces[0];
   auto& call_trace   = action_trace.call_traces[0];

   // Verify it is indeed called
   BOOST_REQUIRE_EQUAL(call_trace.console, "I am the void return and void parameter function");
} FC_LOG_AND_RETHROW() }

//
BOOST_AUTO_TEST_CASE(unknown_receiver_test) { try {
   call_tester_cpp t;

   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "unkwnrcvrtst"_n, "caller"_n, {}),
                         eosio_assert_message_exception,
                         fc_exception_message_contains("receiver does not support sync call but support_mode is set to abort"));
} FC_LOG_AND_RETHROW() }

// The called function was not declared as an action, not a sync call
// in the contract
BOOST_AUTO_TEST_CASE(unknown_function_test) { try {
   call_tester_cpp t;

   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "unkwnfunctst"_n, "caller"_n, {}),
                         eosio_assert_message_exception,
                         fc_exception_message_contains("receiver does not support sync call but support_mode is set to abort"));
} FC_LOG_AND_RETHROW() }

// Verify a function can be tagged as both an action and a call
BOOST_AUTO_TEST_CASE(act_call_both_tagged_test) { try {
   call_tester_cpp t;

   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "actcalltst"_n, "caller"_n, {}));
} FC_LOG_AND_RETHROW() }

// Verify when the called function is stuck in an infinite loop
BOOST_AUTO_TEST_CASE(forever_loop_test) { try {
   call_tester_cpp t;

   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "forevertest"_n, "caller"_n, {}),
                         wasm_execution_error,
                         fc_exception_message_contains("eos-vm system failure"));
} FC_LOG_AND_RETHROW() }

// Verify when the called function crashes
BOOST_AUTO_TEST_CASE(crash_test) { try {
   call_tester_cpp t;

   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "crashtest"_n, "caller"_n, {}),
                         wasm_execution_error,
                         fc_exception_message_contains("wasm memory out-of-bounds"));
} FC_LOG_AND_RETHROW() }

// Verify when the call wrapper does not exist in the receiver
BOOST_AUTO_TEST_CASE(unknown_call_wrapper_test) { try {
   call_tester_cpp t;

   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "unkwnwraptst"_n, "caller"_n, {}),
                         eosio_assert_message_exception,
                         fc_exception_message_contains("receiver does not support sync call but support_mode is set to abort"));
} FC_LOG_AND_RETHROW() }

// Verify a sync call can can insert a record into a table
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

BOOST_AUTO_TEST_SUITE_END()
