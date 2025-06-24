#include <test_contracts.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/chain/trace.hpp>
#include <boost/test/unit_test.hpp>

// Test sync calls which are initiated by contracts written in C++
// and converted to WASM by CDT.

using namespace eosio::testing;
using mvo = fc::mutable_variant_object;

BOOST_AUTO_TEST_SUITE(sync_call_cpp_tests)

// Verify a basic sync call works
BOOST_AUTO_TEST_CASE(basic_test) { try {
   call_tester t(std::vector<account_and_wasm_code>{
      {"caller"_n, test_contracts::sync_caller_wasm(), test_contracts::sync_caller_abi()},
      {"callee"_n, test_contracts::sync_callee_wasm(), test_contracts::sync_callee_abi()}
   });

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
[caller->(callee,basictest)]: CALL BEGIN ======
I am basictest from sync_callee
[caller->(callee,basictest)]: CALL END   ======
After returned from basictest
Test END   ==================)=====";

   BOOST_REQUIRE_EQUAL(actual, expected);
} FC_LOG_AND_RETHROW() }

// Verify complex parameter passing works
BOOST_AUTO_TEST_CASE(params_test) { try {
   call_tester t(std::vector<account_and_wasm_code>{
      {"caller"_n, test_contracts::sync_caller_wasm(), test_contracts::sync_caller_abi()},
      {"callee"_n, test_contracts::sync_callee_wasm(), test_contracts::sync_callee_abi()}
   });

   // "params_test"_n action in caller contract makes a sync call with
   // complex parameters and validates return value from the sync call
   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "paramstest"_n, "caller"_n, {}));
} FC_LOG_AND_RETHROW() }

// Verify initiating action does not have console but its children sync calls have console
BOOST_AUTO_TEST_CASE(caller_has_no_console_test) { try {
   call_tester t({
      {"caller"_n, test_contracts::sync_caller_wasm(), test_contracts::sync_caller_abi()},
      {"callee"_n, test_contracts::sync_callee_wasm(), test_contracts::sync_callee_abi()}
   });

   auto  trx_trace    = t.push_action("caller"_n, "callernocnsl"_n, "caller"_n, {});
   auto& action_trace = trx_trace->action_traces[0];
   std::string header  = "Test BEGIN ==================";
   std::string trailer = "\nTest END   ==================";
   fc::unsigned_int sender_ordinal = 0;
   std::string actual = eosio::chain::expand_console(header, trailer, action_trace.call_traces, 0, sender_ordinal, "caller", action_trace.console, action_trace.console_markers);

   static const std::string expected = R"=====(Test BEGIN ==================
[caller->(callee,basictest)]: CALL BEGIN ======
I am basictest from sync_callee
[caller->(callee,basictest)]: CALL END   ======
Test END   ==================)=====";

   BOOST_REQUIRE_EQUAL(actual, expected);
} FC_LOG_AND_RETHROW() }

// Verify initiating action has console but its children sync calls do not have console
BOOST_AUTO_TEST_CASE(callee_has_no_console_test) { try {
   call_tester t({
      {"caller"_n, test_contracts::sync_caller_wasm(), test_contracts::sync_caller_abi()},
      {"callee"_n, test_contracts::sync_callee_wasm(), test_contracts::sync_callee_abi()}
   });

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

BOOST_AUTO_TEST_SUITE_END()
