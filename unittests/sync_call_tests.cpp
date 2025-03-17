#include <eosio/testing/tester.hpp>

using namespace eosio::chain;
using namespace eosio::testing;

BOOST_AUTO_TEST_SUITE(sync_call_tests)

// Generic ABI
static const char* doit_abi = R"=====(
{
   "version": "eosio::abi/1.2",
   "types": [],
   "structs": [{ "name": "doit", "base": "", "fields": [] }],
   "actions": [{ "name": "doit", "type": "doit", "ricardian_contract": ""},
               { "name": "doit1", "type": "doit", "ricardian_contract": ""}],
   "tables": [],
   "ricardian_clauses": []
}
)=====";

// Make a sync call to a function in the same account
static const char sync_call_in_same_account_wast[] = R"=====(
(module
   (import "env" "eosio_assert" (func $assert (param i32 i32)))
   (import "env" "call" (func $call (param i64 i64 i32 i32) (result i32))) ;; receiver, flags, data span
   (memory $0 1)
   (export "memory" (memory $0))

   (func $callee
      (call $assert (i32.const 0) (i32.const 0)) ;; the test checks this assert to make sure this function was called
   )

   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i64)
      (call $callee) 
   )

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64)
      (drop (call $call (get_local $receiver) (i64.const 0)(i32.const 0)(i32.const 8))) ;; using the same receiver
   )

   (data (i32.const 0) "sync_call in same contract called")
)
)=====";

// Verify sync call to a function in the same account works
BOOST_AUTO_TEST_CASE(same_account) { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }

   const auto& acct = account_name("synccall");
   t.create_account(acct);
   t.set_code(acct, sync_call_in_same_account_wast);
   t.set_abi(acct, doit_abi);

   BOOST_CHECK_EXCEPTION(t.push_action(acct, "doit"_n, acct, {}),
                         eosio_assert_message_exception,
                         fc_exception_message_contains("sync_call in same contract called"));
                         
} FC_LOG_AND_RETHROW() }

// Make a sync call to a function in "callee"_n account
static const char caller_wast[] = R"=====(
(module
   (import "env" "call" (func $call (param i64 i64 i32 i32) (result i32))) ;; receiver, flags, data span
   (memory $0 1)
   (export "memory" (memory $0))
   (global $callee i64 (i64.const 4729647295212027904)) ;; "callee"_n uint64_t value

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64)
      (drop (call $call (get_global $callee) (i64.const 0)(i32.const 0)(i32.const 8)))
   )
)
)=====";

// Provide the called function via "sync_call" entry point calling the function
static const char callee_wast[] = R"=====(
(module
   (import "env" "eosio_assert" (func $assert (param i32 i32)))
   (memory $0 1)
   (export "memory" (memory $0))

   (func $callee
      (call $assert (i32.const 0) (i32.const 0)) ;; the test checks this assert to make sure this function  was called
   )

   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i64)
      (call $callee) 
   )

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64))

   (data (i32.const 0) "sync_call in different contract called")
)
)=====";

// Verify sync call works for called function in a different account
BOOST_AUTO_TEST_CASE(different_account) { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }

   const auto& caller = account_name("caller");
   t.create_account(caller);
   t.set_code(caller, caller_wast);
   t.set_abi(caller, doit_abi);

   const auto& callee = account_name("callee");
   t.create_account(callee);
   t.set_code(callee, callee_wast);

   BOOST_CHECK_EXCEPTION(t.push_action(caller, "doit"_n, caller, {}),
                         eosio_assert_message_exception,
                         fc_exception_message_contains("sync_call in different contract called"));
} FC_LOG_AND_RETHROW() }

// Calls "callee1"_n
static const char call_depth_wast[] = R"=====(
(module
   (import "env" "call" (func $call (param i64 i64 i32 i32) (result i32))) ;; receiver, flags, data span
   (memory $0 1)
   (export "memory" (memory $0))
   (global $callee1 i64 (i64.const 4729647295748898816)) ;; "calllee1"_n uint64 value

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64)
      (drop (call $call (get_global $callee1) (i64.const 0)(i32.const 0)(i32.const 8)))
   )
)
)=====";

// Calls "callee2"_n
static const char callee1_wast[] = R"=====(
(module
   (import "env" "call" (func $call (param i64 i64 i32 i32) (result i32))) ;; receiver, flags, data span
   (memory $0 1)
   (export "memory" (memory $0))
   (global $callee2 i64 (i64.const 4729647296285769728)) ;; "calllee2"_n uint64 value

   ;; callee intentionally asserts such that the test can check it was called
   (func $callee
      (drop (call $call (get_global $callee2) (i64.const 0)(i32.const 0)(i32.const 8)))
   )

   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i64)
      (call $callee) 
   )

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64))
)
)=====";

// The final function to be called
static const char callee2_wast[] = R"=====(
(module
   (import "env" "eosio_assert" (func $assert (param i32 i32)))
   (memory $0 1)
   (export "memory" (memory $0))

   ;; callee intentionally asserts such that the test can check it was called
   (func $callee
      (call $assert (i32.const 0) (i32.const 0))
   )

   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i64)
      (call $callee) 
   )

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64))

   (data (i32.const 0) "multiple level call")
)
)=====";

// Verify multiple levels sync calls work
BOOST_AUTO_TEST_CASE(multi_level_call_depth) { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }

   const auto& caller = account_name("caller");
   t.create_account(caller);
   t.set_code(caller, call_depth_wast);
   t.set_abi(caller, doit_abi);

   const auto& callee1 = account_name("callee1");
   t.create_account(callee1);
   t.set_code(callee1, callee1_wast);

   const auto& callee2 = account_name("callee2");
   t.create_account(callee2);
   t.set_code(callee2, callee2_wast);

   BOOST_CHECK_EXCEPTION(t.push_action(caller, "doit"_n, caller, {}),
                         eosio_assert_message_exception,
                         fc_exception_message_contains("multiple level call"));
} FC_LOG_AND_RETHROW() }

// Call "callee1"_n and "callee2"_n in sequence
static const char seq_caller_wast[] = R"=====(
(module
   (import "env" "call" (func $call (param i64 i64 i32 i32) (result i32))) ;; receiver, flags, data span
   (memory $0 1)
   (export "memory" (memory $0))
   (global $callee1 i64 (i64.const 4729647295748898816))
   (global $callee2 i64 (i64.const 4729647296285769728))

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64)
      (drop (call $call (get_global $callee1) (i64.const 0)(i32.const 0)(i32.const 8)))
      (drop (call $call (get_global $callee2) (i64.const 0)(i32.const 0)(i32.const 8)))
   )
)
)=====";

static const char seq_callee1_wast[] = R"=====(
(module
   (memory $0 1)
   (export "memory" (memory $0))

   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i64))

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64))
)
)=====";

// The final function to be called
static const char seq_callee2_wast[] = R"=====(
(module
   (import "env" "eosio_assert" (func $assert (param i32 i32)))
   (memory $0 1)
   (export "memory" (memory $0))

   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i64)
      (call $assert (i32.const 0) (i32.const 0))
   )

   ;; not used but needed for set_code validation
   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64))

   (data (i32.const 0) "calls in sequence")
)
)=====";

// Verify sequential sync calls work
BOOST_AUTO_TEST_CASE(seq_sync_calls) { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }

   const auto& caller = account_name("caller");
   t.create_account(caller);
   t.set_code(caller, seq_caller_wast);
   t.set_abi(caller, doit_abi);

   const auto& callee1 = account_name("callee1");
   t.create_account(callee1);
   t.set_code(callee1, seq_callee1_wast);

   const auto& callee2 = account_name("callee2");
   t.create_account(callee2);
   t.set_code(callee2, seq_callee2_wast);

   t.produce_block();

   BOOST_CHECK_EXCEPTION(t.push_action(caller, "doit"_n, caller, {}),
                         eosio_assert_message_exception,
                         fc_exception_message_contains("calls in sequence"));
} FC_LOG_AND_RETHROW() }

// Make sync calls from different actions
static const char different_actions_caller_wast[] = R"=====(
(module
   (import "env" "call" (func $call (param i64 i64 i32 i32) (result i32))) ;; receiver, flags, data span
   (memory $0 1)
   (export "memory" (memory $0))
   (global $doit_value i64 (i64.const 5556755844919459840))
   (global $callee1    i64 (i64.const 4729647295748898816))
   (global $callee2    i64 (i64.const 4729647296285769728))

   ;; sync call a function in "callee1"_n
   (func $doit
      (drop (call $call (get_global $callee1) (i64.const 0)(i32.const 0)(i32.const 8)))
   )

   ;; sync call a function in "callee2"_n
   (func $doit1
      (drop (call $call (get_global $callee2) (i64.const 0)(i32.const 0)(i32.const 8)))
   )

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64)
      (get_global $doit_value)
      (get_local  $action_name)
      i64.eq
      if
         call $doit
      else
         call $doit1
      end
   )
)
)=====";

// called from `doit` action
static const char different_actions_callee1_wast[] = R"=====(
(module
   (import "env" "eosio_assert" (func $assert (param i32 i32)))
   (memory $0 1)
   (export "memory" (memory $0))

   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i64)
      (call $assert (i32.const 0) (i32.const 0))
   )

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64))

   (data (i32.const 0) "call from doit")
)
)=====";

// called from `doit1` action
static const char different_actions_callee2_wast[] = R"=====(
(module
   (import "env" "eosio_assert" (func $assert (param i32 i32)))
   (memory $0 1)
   (export "memory" (memory $0))

   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i64)
      (call $assert (i32.const 0) (i32.const 0))
   )

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64))

   (data (i32.const 0) "call from doit1")
)
)=====";

// Verify calls from different actions work
BOOST_AUTO_TEST_CASE(calls_from_different_actions) { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }

   const auto& caller = account_name("caller");
   t.create_account(caller);
   t.set_code(caller, different_actions_caller_wast);
   t.set_abi(caller, doit_abi);

   const auto& callee1 = account_name("callee1");
   t.create_account(callee1);
   t.set_code(callee1, different_actions_callee1_wast);

   const auto& callee2 = account_name("callee2");
   t.create_account(callee2);
   t.set_code(callee2, different_actions_callee2_wast);

   t.produce_block();

   // Do a sync call from action "doit"_n
   BOOST_CHECK_EXCEPTION(t.push_action(caller, "doit"_n, caller, {}),
                         eosio_assert_message_exception,
                         fc_exception_message_contains("call from doit"));

   // Do another sync call from action "doit1"_n
   BOOST_CHECK_EXCEPTION(t.push_action(caller, "doit1"_n, caller, {}),
                         eosio_assert_message_exception,
                         fc_exception_message_contains("call from doit1"));
} FC_LOG_AND_RETHROW() }

// Make recursive sync calls
static const char recursive_caller_wast[] = R"=====(
(module
   (import "env" "call" (func $call (param i64 i64 i32 i32) (result i32))) ;; receiver, flags, data span
   (import "env" "eosio_assert" (func $assert (param i32 i32)))
   (memory $0 1)
   (export "memory" (memory $0))
   (global $callee i64 (i64.const 4729647295212027904))

   ;; sync call a function in "callee"_n
   (func $doit (param $first_time i32)
      (i32.const 1)
      (get_local $first_time)
      i32.eq  ;; if $first_time is 1, call callee, otherwise exit
      if
         (drop (call $call (get_global $callee) (i64.const 0)(i32.const 0)(i32.const 8)))
      else
         (call $assert (i32.const 0) (i32.const 0))  ;; called recursive from sync_call
      end
   )

   ;; called recursively from callee
   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i64)
      (call $doit (i32.const 0)) ;; argument 0 to request doit to exit
   )

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64)
      (call $doit (i32.const 1)) ;; argument 1 to request doit to call callee
   )

   (data (i32.const 0) "recursively called")
)
)=====";

static const char recursive_callee_wast[] = R"=====(
(module
   (import "env" "call" (func $call (param i64 i64 i32 i32) (result i32))) ;; receiver, flags, data span
   (memory $0 1)
   (export "memory" (memory $0))
   (global $caller i64 (i64.const 4729647518550327296))

   ;; called from caller and calls caller again
   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i64)
      (drop (call $call (get_global $caller) (i64.const 0)(i32.const 0)(i32.const 8)))
   )

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64))
)
)=====";

// Verify recursive calls abort
BOOST_AUTO_TEST_CASE(recursive_calls) { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }
   ilog("!!! caller: ${c}", ("c", "caller"_n.to_uint64_t()));

   const auto& caller = account_name("caller");
   t.create_account(caller);
   t.set_code(caller, recursive_caller_wast);
   t.set_abi(caller, doit_abi);

   const auto& callee = account_name("callee");
   t.create_account(callee);
   t.set_code(callee, recursive_callee_wast);

   t.produce_block();

   // Do a sync call from action "doit"_n
   BOOST_CHECK_EXCEPTION(t.push_action(caller, "doit"_n, caller, {}),
                         eosio_assert_message_exception,
                         fc_exception_message_contains("recursively called"));
} FC_LOG_AND_RETHROW() }

// Verify sync call fails if receiver account does not exist
BOOST_AUTO_TEST_CASE(receiver_account_not_existent) { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }

   const auto& caller = account_name("caller");
   t.create_account(caller);
   t.set_code(caller, caller_wast);
   t.set_abi(caller, doit_abi);

   // The caller intends to call a function in "callee"_n account, which is not created. 
   BOOST_CHECK_EXCEPTION(t.push_action(caller, "doit"_n, caller, {}),
                         action_validate_exception,
                         fc_exception_message_contains("does not exist"));
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
