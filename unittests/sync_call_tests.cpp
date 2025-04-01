#include <eosio/chain/config.hpp>
#include <eosio/testing/tester.hpp>

using namespace eosio::chain;
using namespace eosio::testing;

using mvo = fc::mutable_variant_object;

BOOST_AUTO_TEST_SUITE(sync_call_tests)

// Generic ABI
static const char* doit_abi = R"=====(
{
   "version": "eosio::abi/1.2",
   "types": [],
   "structs": [ { "name": "doit", "base": "", "fields": [] },
                { "name": "doubleit", "base": "", "fields": [{"name": "input", "type": "uint32"}] },
                { "name": "callwithinpt", "base": "", "fields": [{"name": "input", "type": "uint32"}] },
                { "name": "callhostfunc", "base": "", "fields": [{"name": "index", "type": "uint32"}] }
              ],
   "actions": [ { "name": "doit", "type": "doit", "ricardian_contract": ""},
                { "name": "doit1", "type": "doit", "ricardian_contract": ""},
                { "name": "doubleit", "type": "doubleit", "ricardian_contract": ""},
                { "name": "callwithinpt", "type": "callwithinpt", "ricardian_contract": ""},
                { "name": "callhostfunc", "type": "callhostfunc", "ricardian_contract": ""}
              ],
   "tables": [],
   "ricardian_clauses": []
}
)=====";

// A common helper function
void create_accounts_and_set_code(const char* caller_wat, const char* callee_wat, validating_tester& t) {
   const auto& caller = account_name("caller");
   t.create_account(caller);
   t.set_code(caller, caller_wat);
   t.set_abi(caller, doit_abi);

   const auto& callee = account_name("callee");
   t.create_account(callee);
   t.set_code(callee, callee_wat);
}

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
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i32)
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
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i32)
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
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i32)
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
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i32)
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
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i32))

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
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i32)
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

// Make a large number of sync calls in a loop
static const char loop_caller_wast[] = R"=====(
(module
   (import "env" "call" (func $call (param i64 i64 i32 i32) (result i32))) ;; receiver, flags, data span
   (memory $0 1)
   (export "memory" (memory $0))
   (global $callee i64 (i64.const 4729647295212027904)) ;; "callee"_n uint64_t value

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64)
       (local $n i32)
       (i32.const 500)
       set_local $n      ;; n = 500;
       (loop $loop
          (drop (call $call (get_global $callee) (i64.const 0)(i32.const 0)(i32.const 8)))

          get_local $n
          i32.const 1
          i32.sub        ;; top_of_stack = n - 1;
          tee_local $n   ;; n = top_of_stack;
          br_if $loop    ;; if (n != 0) { goto loop; }
       )
   )
)
)=====";

// A dummy callee
static const char loop_callee_wast[] = R"=====(
(module
   (memory $0 1)
   (export "memory" (memory $0))

   (func $callee)

   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i32)
      (call $callee)
   )

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64))
)
)=====";

// Verify a large number of sequential calls can be made, to make sure resources
// are not exhausted (like wasm allocators)
BOOST_AUTO_TEST_CASE(large_number_of_sequential_test) { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }

   const auto& caller = account_name("caller");
   t.create_account(caller);
   t.set_code(caller, loop_caller_wast);
   t.set_abi(caller, doit_abi);

   const auto& callee = account_name("callee");
   t.create_account(callee);
   t.set_code(callee, loop_callee_wast);

   BOOST_REQUIRE_NO_THROW(t.push_action(caller, "doit"_n, caller, {}));
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
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i32)
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
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i32)
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
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i32)
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
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i32)
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
                         sync_call_validate_exception,
                         fc_exception_message_contains("does not exist"));
} FC_LOG_AND_RETHROW() }

// 1. reads input from action
// 1. makes a sync call to `double` in callee contract with the input as an argument
// 2. retrieves the result
// 3. saves the result in action trace for verification by test
static const char basic_params_return_value_caller_wast[] = R"=====(
(module
   (import "env" "call" (func $call (param i64 i64 i32 i32) (result i32))) ;; receiver, flags, data span
   (import "env" "get_call_return_value" (func $get_call_return_value (param i32 i32) (result i32))) ;; memory
   (import "env" "set_action_return_value" (func $set_action_return_value (param i32 i32)))
   (import "env" "read_action_data" (func $read_action_data (param i32 i32) (result i32)))
   (memory $0 1)
   (export "memory" (memory $0))
   (global $callee i64 (i64.const 4729647295212027904)) ;; "callee"_n uint64_t value

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64)
      (local $input_size i32)
      (local $return_value_size i32)
      (call $read_action_data(i32.const 0)(i32.const 4))       ;; read action input into address 0
      set_local $input_size
      (call $call (get_global $callee) (i64.const 0)(i32.const 0)(get_local $input_size)) ;; make a sync call with data starting at address 0
      set_local $return_value_size
      (drop (call $get_call_return_value (i32.const 8)(get_local $return_value_size))) ;; save return value at address 8
      (call $set_action_return_value (i32.const 8) (get_local $return_value_size))     ;; set the return value to action_return_value so test can check in action trace
   )
)
)=====";

// 1. retrieves the argument (1000)
// 2. passes the argument to `double` function
// 3. `double` doubles the input and returns the result (2000)
// 4. saves the result to host (to be retrieved by get_call_return_value in caller)
static const char basic_params_return_value_callee_wast[] = R"=====(
(module
   (import "env" "eosio_assert" (func $assert (param i32 i32)))
   (import "env" "get_call_data" (func $get_call_data (param i32 i32) (result i32))) ;; memory
   (import "env" "set_call_return_value" (func $set_call_return_value (param i32 i32))) ;; memory
   (memory $0 1)
   (export "memory" (memory $0))

   ;; multiple the input by 2 and return the result
   (func $callee (param $n i32) (result i32)
      get_local $n
      i32.const 2
      i32.mul      ;; Multiply $n by 2
   )

   ;; use get_call_data and set_call_return_value to get argument and store return value
   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i32)
      (drop (call $get_call_data (i32.const 0)(get_local $data_size)))

      i32.const 16      ;; address to store return value
      i32.const 0       ;; address of the argument read by get_call_data
      i32.load          ;; load the argument
      call $callee
      i32.store         ;; save the return value at address 16

      (call $set_call_return_value (i32.const 16)(i32.const 4)) ;; store the return value on host
   )

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64))
)
)=====";

// Verify basic parameters passing, set and retrieve return value
BOOST_AUTO_TEST_CASE(basic_params_return_value_passing) { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }

   const auto& caller = account_name("caller");
   t.create_account(caller);
   t.set_code(caller, basic_params_return_value_caller_wast);
   t.set_abi(caller, doit_abi);

   const auto& callee = account_name("callee");
   t.create_account(callee);
   t.set_code(callee, basic_params_return_value_callee_wast);

   // double 0
   auto trx_trace = t.push_action(caller, "doubleit"_n, caller, mvo() ("input", "0"));
   auto &atrace   = trx_trace->action_traces;
   BOOST_REQUIRE_EQUAL(fc::raw::unpack<uint32_t>(atrace[0].return_value), 0u);

   // double 1000
   auto trx_trace1 = t.push_action(caller, "doubleit"_n, caller, mvo() ("input", "1000"));
   auto &atrace1   = trx_trace1->action_traces;
   BOOST_REQUIRE_EQUAL(fc::raw::unpack<uint32_t>(atrace1[0].return_value), 2000u);

   // double 5000
   auto trx_trace2 = t.push_action(caller, "doubleit"_n, caller, mvo() ("input", "5000"));
   auto &atrace2   = trx_trace2->action_traces;
   BOOST_REQUIRE_EQUAL(fc::raw::unpack<uint32_t>(atrace2[0].return_value), 10000u);
} FC_LOG_AND_RETHROW() }

static const char get_call_data_less_memory_wast[] = R"=====(
(module
   (import "env" "eosio_assert" (func $assert (param i32 i32)))
   (import "env" "get_call_data" (func $get_call_data (param i32 i32) (result i32))) ;; memory
   (memory $0 1)
   (export "memory" (memory $0))

   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i32)
      (call $get_call_data (i32.const 0)(i32.const 0)) ;; destination memory size is 0
      (i32.const 8)  ;; caller passes in 8 bytes. get_call_data should always return 8
      i32.ne
      if             ;; assert if get_call_data did not return 8
         (call $assert (i32.const 0) (i32.const 0))
      end

      (call $get_call_data (i32.const 0)(i32.const 4)) ;; destination memory size is 4
      (i32.const 8)  ;; caller passes in 8 bytes. get_call_data should always return 8
      i32.ne
      if             ;; assert if get_call_data did not return 8
         (call $assert (i32.const 0) (i32.const 0))
      end
   )

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64))

   (data (i32.const 0) "get_call_data did not return actual data size when destination size is less than actual size")
)
)=====";

// Verify get_call_data always returns data size even if the destination memory size is 0 or less than the data size
BOOST_AUTO_TEST_CASE(get_call_data_less_memory_test) { try {
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
   t.set_code(callee, get_call_data_less_memory_wast);

   BOOST_REQUIRE_NO_THROW(t.push_action(caller, "doit"_n, caller, {}));
} FC_LOG_AND_RETHROW() }

// Make a sync call without parameters (data size being 0)
static const char no_parameters_caller_wast[] = R"=====(
(module
   (import "env" "call" (func $call (param i64 i64 i32 i32) (result i32))) ;; receiver, flags, data span
   (memory (export "memory") 1)
   (global $callee i64 (i64.const 4729647295212027904)) ;; "callee"_n uint64_t value

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64)
      (drop (call $call (get_global $callee) (i64.const 0)(i32.const 0)(i32.const 0))) ;; data size is 0
   )
)
)=====";

static const char no_parameters_callee_wast[] = R"=====(
(module
   (import "env" "eosio_assert" (func $assert (param i32 i32)))
   (import "env" "get_call_data" (func $get_call_data (param i32 i32) (result i32)))
   (memory (export "memory") 1)

   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i32)
      (call $get_call_data (i32.const 160)(i32.const 100)) ;; store call data in memory[160], with size 100
      (i32.const 0)  ;; caller did not pass in data. get_call_data should return 0
      i32.ne
      if             ;; assert if get_call_data did not return 0
         (call $assert (i32.const 0) (i32.const 0))
      end
   )

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64))

   (data (i32.const 0) "get_call_data did not return 0")
)
)=====";

// Verify get_call_data returns 0 if the function does not have any parameters
BOOST_AUTO_TEST_CASE(no_parameters_test) { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }

   create_accounts_and_set_code(no_parameters_caller_wast, no_parameters_callee_wast, t);

   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "doit"_n, "caller"_n, {})); // no_parameters_callee_wast will throw if get_call_data returns non-zero.
} FC_LOG_AND_RETHROW() }

static const char no_return_value_caller_wast[] = R"=====(
(module
   (import "env" "eosio_assert" (func $assert (param i32 i32)))
   (import "env" "call" (func $call (param i64 i64 i32 i32) (result i32))) ;; receiver, flags, data span
   (import "env" "get_call_return_value" (func $get_call_return_value (param i32 i32) (result i32)))
   (memory (export "memory") 1)
   (global $callee i64 (i64.const 4729647295212027904)) ;; "callee"_n uint64_t value

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64)
      (call $call (get_global $callee) (i64.const 0)(i32.const 0)(i32.const 0))
      (i32.const 0)  ;; callee did not call set_call_return_value, $call shoud return 0
      i32.ne
      if             ;; assert if $call did not return 0
         (call $assert (i32.const 0) (i32.const 16))
      end
   )

   (data (i32.const 16) "call did not return 0")
)
)=====";

// Do not return a value (set_call_return_value is not called in $sync_call)
static const char no_return_value_callee_wast[] = R"=====(
(module
   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i32))

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64))
)
)=====";

// Verify `call()` host function returns 0 if called function does not return a value, ie, `void func()`
BOOST_AUTO_TEST_CASE(no_return_value_test) { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }

   create_accounts_and_set_code(no_return_value_caller_wast, no_return_value_callee_wast, t);

   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "doit"_n, "caller"_n, {})); // no_return_value_caller_wast will throw if `call` returns a non-zero-length value.
} FC_LOG_AND_RETHROW() }

// Callee returns 0-length value 
static const char zero_return_value_size_callee_wast[] = R"=====(
(module
   (import "env" "set_call_return_value" (func $set_call_return_value (param i32 i32)))
   (memory (export "memory") 1)

   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i32))

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64)
      (call $set_call_return_value (i32.const 0)(i32.const 0))  ;; return value size is 0
   )
)
)=====";

// Verify `call()` host function returns 0 if called function returns a 0-length value,
// like `std::string get()` returning an empty string
BOOST_AUTO_TEST_CASE(zero_return_value_size_test) { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }

   // callee returns 0 sized value
   create_accounts_and_set_code(no_return_value_caller_wast, zero_return_value_size_callee_wast, t);

   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "doit"_n, "caller"_n, {})); // no_return_value_caller_wast will throw if `call` returns a non-zero-length value.
} FC_LOG_AND_RETHROW() }

static const char get_call_data_in_apply_wast[] = R"=====(
(module
   (import "env" "get_call_data" (func $get_call_data (param i32 i32) (result i32))) ;; memory
   (memory $0 1)
   (export "memory" (memory $0))

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64)
      (drop (call $get_call_data (i32.const 0)(i32.const 8)))
   )
)
)=====";

// Verify get_call_data can be only called in sync calls
BOOST_AUTO_TEST_CASE(get_call_data_in_apply_test) { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }

   const auto& caller = account_name("caller");
   t.create_account(caller);
   t.set_code(caller, get_call_data_in_apply_wast);
   t.set_abi(caller, doit_abi);

   BOOST_CHECK_EXCEPTION(t.push_action(caller, "doit"_n, caller, {}),
                         unaccessible_api,
                         fc_exception_message_contains("this API may only be called from sync call"));
} FC_LOG_AND_RETHROW() }


static const char set_call_return_value_invalid_size_wast[] = R"=====(
(module
   (import "env" "set_call_return_value" (func $set_call_return_value (param i32 i32)))
   (memory $0 10)  ;; 10 * 64KB, bigger than 512 KB needed below
   (export "memory" (memory $0))

   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i32)
      (call $set_call_return_value (i32.const 16)(i32.const 524289)) ;; max allowed return value size is 512 KB (524288)
   )

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64))
)
)=====";

// Verify exception is raised if return value is greater than max allowed size
BOOST_AUTO_TEST_CASE(set_call_return_value_invalid_size_test) { try {
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
   t.set_code(callee, set_call_return_value_invalid_size_wast);

   BOOST_CHECK_EXCEPTION(t.push_action(caller, "doit"_n, caller, {}),
                         sync_call_return_value_exception,
                         fc_exception_message_contains("sync call return value size must be less or equal to"));
} FC_LOG_AND_RETHROW() }

static const char set_call_return_value_not_in_sync_call_wast[] = R"=====(
(module
   (import "env" "set_call_return_value" (func $set_call_return_value (param i32 i32)))
   (memory $0 1)
   (export "memory" (memory $0))

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64)
      (call $set_call_return_value (i32.const 0)(i32.const 8))
   )
)
)=====";

// Verify set_call_return_value can be only called in sync calls
BOOST_AUTO_TEST_CASE(set_call_return_value_not_in_sync_call_test) { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }

   const auto& caller = account_name("caller");
   t.create_account(caller);
   t.set_code(caller, set_call_return_value_not_in_sync_call_wast);
   t.set_abi(caller, doit_abi);

   BOOST_CHECK_EXCEPTION(t.push_action(caller, "doit"_n, caller, {}),
                         unaccessible_api,
                         fc_exception_message_contains("this API may only be called from sync call"));
} FC_LOG_AND_RETHROW() }

static const char get_call_return_value_less_memory_wast[] = R"=====(
(module
   (import "env" "eosio_assert" (func $assert (param i32 i32)))
   (import "env" "call" (func $call (param i64 i64 i32 i32) (result i32))) ;; receiver, flags, data span
   (import "env" "get_call_return_value" (func $get_call_return_value (param i32 i32) (result i32))) ;; memory
   (memory $0 1)
   (export "memory" (memory $0))
   (global $callee i64 (i64.const 4729647295212027904)) ;; "callee"_n uint64_t value

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64)
      (drop (call $call (get_global $callee) (i64.const 0)(i32.const 0)(i32.const 4))) ;; make a sync call with data_size value as 4 (the last argument)
      (call $get_call_return_value (i32.const 1024)(i32.const 0)) ;; destination memory size is 0
      (i32.const 4)  ;; return_value should be 4
      i32.ne
      if             ;; not 4
          (call $assert (i32.const 0) (i32.const 8))
      end

      (call $get_call_return_value (i32.const 1024)(i32.const 3)) ;; destination memory size is 3
      (i32.const 4)  ;; return_value should be 4
      i32.ne
      if             ;; not 4
          (call $assert (i32.const 0) (i32.const 8))
      end
   )

   (data (i32.const 0) "\E8\03\00\00") ;; decimal 1000 in little endian format
   (data (i32.const 8) "get_call_return_value did not return actual data size when destination size is less than actual size")
)
)=====";

// Verify get_call_return_value always returns data size even if the destination memory size is 0 or less than the data size
BOOST_AUTO_TEST_CASE(get_call_return_value_less_memory_test) { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }

   const auto& caller = account_name("caller");
   t.create_account(caller);
   t.set_code(caller, get_call_return_value_less_memory_wast);
   t.set_abi(caller, doit_abi);

   const auto& callee = account_name("callee");
   t.create_account(callee);
   t.set_code(callee, basic_params_return_value_callee_wast);

   BOOST_REQUIRE_NO_THROW(t.push_action(caller, "doit"_n, caller, {}));
} FC_LOG_AND_RETHROW() }

static const char get_call_return_value_not_called_sync_call_wast[] = R"=====(
(module
   (import "env" "eosio_assert" (func $assert (param i32 i32)))
   (import "env" "get_call_return_value" (func $get_call_return_value (param i32 i32) (result i32))) ;; memory
   (memory $0 1)
   (export "memory" (memory $0))
   (global $callee i64 (i64.const 4729647295212027904)) ;; "callee"_n uint64_t value

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64)
      (call $get_call_return_value (i32.const 1024)(i32.const 16))
      (i32.const 0)  ;; return_value should be 0 as no sync call was made
      i32.ne
      if             ;; not 0
          (call $assert (i32.const 0) (i32.const 0))
      end
   )

   (data (i32.const 0) "get_call_return_value did not return actual data size when destination size is less than actual size")
)
)=====";

// Verify get_call_return_value returns 0 if no sync calls were made before
BOOST_AUTO_TEST_CASE(get_call_return_value_not_called_sync_call_test) { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }

   const auto& caller = account_name("caller");
   t.create_account(caller);
   t.set_code(caller, get_call_return_value_not_called_sync_call_wast);
   t.set_abi(caller, doit_abi);

   BOOST_REQUIRE_NO_THROW(t.push_action(caller, "doit"_n, caller, {}));
} FC_LOG_AND_RETHROW() }


static const char no_sync_call_entry_point_wast[] = R"=====(
(module
   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64))
)
)=====";

// Verify sync call is abort if sync call entry point does not exist and the no-op-if-not-exist
// is not set
BOOST_AUTO_TEST_CASE(no_sync_call_entry_point_no_op_flag_not_set_test)  { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }

   create_accounts_and_set_code(caller_wast, no_sync_call_entry_point_wast, t);

   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "doit"_n, "caller"_n, {}),
                         sync_call_not_supported_by_receiver_exception,
                         fc_exception_message_contains("receiver does not support sync calls"));
} FC_LOG_AND_RETHROW() }

static const char no_op_if_receiver_not_support_sync_call_caller_wast[] = R"=====(
(module
   (import "env" "call" (func $call (param i64 i64 i32 i32) (result i32))) ;; receiver, flags, data span
   (memory $0 1)
   (export "memory" (memory $0))
   (global $callee i64 (i64.const 4729647295212027904)) ;; "callee"_n uint64_t value

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64)
      (drop (call $call (get_global $callee) (i64.const 0x02)(i32.const 0)(i32.const 8)))  ;; flags' bit 1 is set to 1, for no_op_if_receiver_not_support_sync_call
   )
)
)=====";

// Verify sync call is no-op if sync call entry point does not exist and the no-op-if-not-exist
// is set
BOOST_AUTO_TEST_CASE(no_sync_call_entry_point_no_op_flag_set_test)  { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }

   create_accounts_and_set_code(no_op_if_receiver_not_support_sync_call_caller_wast, no_sync_call_entry_point_wast, t);

   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "doit"_n, "caller"_n, {}));
} FC_LOG_AND_RETHROW() }

// Wrong sync call signature (the type of data_size is wrong)
static const char bad_entry_point_wast[] = R"=====(
(module
   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i64)) ;; data_size type should be i32

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64))
)
)=====";

// Verify a contract with wrong sync call entry point cannot be deployed
BOOST_AUTO_TEST_CASE(bad_entry_point_test)  { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }

   const auto& callee = account_name("callee");
   t.create_account(callee);
   BOOST_CHECK_EXCEPTION(t.set_code(callee, bad_entry_point_wast),
                         wasm_serialization_error,
                         fc_exception_message_contains("sync call entry point has wrong function signature"));
} FC_LOG_AND_RETHROW() }

// 1. reads an i32 value as an input from action
// 2. makes a sync call to "callee"_n contract sync_call using the input as the argument
static const char one_input_caller_wast[] = R"=====(
(module
   (import "env" "call" (func $call (param i64 i64 i32 i32) (result i32))) ;; receiver, flags, data span
   (import "env" "read_action_data" (func $read_action_data (param i32 i32) (result i32)))
   (memory (export "memory") 1)
   (global $callee i64 (i64.const 4729647295212027904)) ;; "callee"_n uint64_t value

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64)
      (drop (call $read_action_data(i32.const 0)(i32.const 4)))  ;; read action input into address 0
      (drop (call $call (get_global $callee) (i64.const 0)(i32.const 0)(i32.const 4))) ;; make a sync call with data starting at address 0, size 4
   )
)
)=====";

// A direct recursive function calling itself `n - 1` times. Including the fisrt call
// by caller's `apply` entry point, total call depth is `n`.
static const char direct_recursive_wast[] = R"=====(
(module
   (import "env" "call" (func $call (param i64 i64 i32 i32) (result i32)))
   (import "env" "get_call_data" (func $get_call_data (param i32 i32) (result i32)))
   (memory (export "memory") 1)

   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i32)
      (local $n i32)

      (drop (call $get_call_data (i32.const 0)(get_local $data_size))) ;; read function parameter into address 0
      (set_local $n (i32.load (i32.const 0))) ;; set n

      (get_local $n)
      i32.const 1
      i32.ne
      if  ;; n != 1
         (i32.store
            (i32.const 4)
            (i32.sub (get_local $n) (i32.const 1))
         ) ;;  store `n - 1` to memory[4]
         (drop (call $call
                        (get_local $receiver)  ;; use the same receiver
                        ;;(i64.const 4729647295212027904)
                        (i64.const 0)          ;; flags
                        (i32.const 4)          ;; memory[4]
                        (i32.const 4)          ;; size
         )) ;; recursive call to itself with `n - 1 `
      end
   )

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64))
)
)=====";

BOOST_AUTO_TEST_CASE(direct_recursive_depth_enforcement_test)  { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }

   create_accounts_and_set_code(one_input_caller_wast, direct_recursive_wast, t);

   // Do a recursive call with n == 1
   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "callwithinpt"_n, "caller"_n, mvo()("input", "1")));

   // Do a recursive call with n == config::default_max_sync_call_depth
   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "callwithinpt"_n, "caller"_n, mvo()("input", std::to_string(config::default_max_sync_call_depth))));

   // Verify `config::default_max_sync_call_depth + 1` recursive calls will fail
   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "callwithinpt"_n, "caller"_n, mvo() ("input", std::to_string(config::default_max_sync_call_depth + 1))),
                         sync_call_depth_exception,
                         fc_exception_message_contains("reached sync call max call depth"));
} FC_LOG_AND_RETHROW() }

// 1. reads an i32 value `input` from action
// 2. in `apply` entry point, makes a sync call to "callee"_n contract using `input` as the argument
// 2. in `sync_call` entry point with `n` as the parameter, makes a further sync call to "callee"_n contract using `n` as the argument
static const char indirect_recursive_caller_wast[] = R"=====(
(module
   (import "env" "call" (func $call (param i64 i64 i32 i32) (result i32))) ;; receiver, flags, data span
   (import "env" "get_call_data" (func $get_call_data (param i32 i32) (result i32)))
   (import "env" "read_action_data" (func $read_action_data (param i32 i32) (result i32)))

   (memory (export "memory") 1)
   (global $callee i64 (i64.const 4729647295212027904)) ;; "callee"_n uint64_t value

   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i32)
      (local $n i32)

      (drop (call $get_call_data (i32.const 4)(get_local $data_size))) ;; read function parameter into memory[4]
      (set_local $n (i32.load (i32.const 4))) ;; set n

      (i32.store (i32.const 8) (get_local $n)) ;;  store `n` to memory[8]
      (drop
         (call
            $call
               (get_local $sender)    ;; call back to the sender
               (i64.const 0)          ;; flags
               (i32.const 8)          ;; memory[8]
               (i32.const 4)          ;; size
         )
      ) ;; recursive call to to the sender with `n`
   )

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64)
      (drop (call $read_action_data(i32.const 0)(i32.const 4)))  ;; read action input into address 0
      (drop (call $call (get_global $callee) (i64.const 0)(i32.const 0)(i32.const 4))) ;; make a sync call with data starting at address 0, size 4
   )
)
)=====";

// An indirect recursive function calling its sender and the sender calls back again.
static const char indirect_recursive_callee_wast[] = R"=====(
(module
   (import "env" "call" (func $call (param i64 i64 i32 i32) (result i32)))
   (import "env" "get_call_data" (func $get_call_data (param i32 i32) (result i32)))
   (memory (export "memory") 1)

   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i32)
      (local $n i32)

      (drop (call $get_call_data (i32.const 0)(get_local $data_size))) ;; read function parameter into address 0
      (set_local $n (i32.load (i32.const 0))) ;; set n

      (get_local $n)
      i32.const 1
      i32.ne
      if  ;; n != 1
         (i32.store
            (i32.const 4)
            (i32.sub (get_local $n) (i32.const 1))
         ) ;;  store `n - 1` to memory[4]
         (drop
            (call
               $call
                  (get_local $sender)    ;; call back to the sender
                  (i64.const 0)          ;; flags
                  (i32.const 4)          ;; memory[4]
                  (i32.const 4)          ;; size
         )) ;; recursive call to itself with `n - 1 `
      end
   )

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64))
)
)=====";

BOOST_AUTO_TEST_CASE(indirect_recursive_depth_enforcement_test)  { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }

   create_accounts_and_set_code(indirect_recursive_caller_wast, indirect_recursive_callee_wast, t);

   // Do a recursive call with n == 1 round. Each round consumes 2 call depths
   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "callwithinpt"_n, "caller"_n, mvo()("input", "1")));

   // Do a recursive call with n == config::default_max_sync_call_depth/2 rounds.
   BOOST_REQUIRE_NO_THROW(t.push_action("caller"_n, "callwithinpt"_n, "caller"_n, mvo()("input", std::to_string((config::default_max_sync_call_depth / 2)))));

   // The caller and callee call each other per round, that's why we can only do
   // max_sync_call_depth / 2 round
   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "callwithinpt"_n, "caller"_n, mvo() ("input", std::to_string((config::default_max_sync_call_depth / 2 + 1)))),
                         sync_call_depth_exception,
                         fc_exception_message_contains("reached sync call max call depth"));

} FC_LOG_AND_RETHROW() }

static const char constrains_enforcement_caller_wast[] = R"=====(
(module
   (import "env" "call" (func $call (param i64 i64 i32 i32) (result i32))) ;; receiver, flags, data span
   (import "env" "read_action_data" (func $read_action_data (param i32 i32) (result i32)))
   (memory $0 1)
   (export "memory" (memory $0))
   (global $callee i64 (i64.const 4729647295212027904)) ;; "callee"_n uint64_t value

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64)
      (drop (call $read_action_data(i32.const 0)(i32.const 4)))  ;; read action input (index) into address 0
      (drop (call $call (get_global $callee) (i64.const 0)(i32.const 0)(i32.const 4))) ;; make a sync call with data starting at address 0, size 4 (we know index is an i32)
   )
)
)=====";

// Call a host function based on index inside a sync call.
// Those host function are not allowed in sync calls and they should fail.
static const char constrains_enforcement_callee_wast[] = R"=====(
(module
   (import "env" "require_auth" (func $require_auth (param i64)))            ;; index 0
   (import "env" "require_auth2" (func $require_auth2 (param i64 i64)))      ;; index 1
   (import "env" "has_auth" (func $has_auth (param i64) (result i32)))       ;; index 2
   (import "env" "require_recipient" (func $require_recipient (param i64)))  ;; index 3
   (import "env" "get_action" (func $get_action (param i32 i32 i32 i32) (result i32))) ;; index 4
   (import "env" "read_action_data" (func $read_action_data (param i32 i32) (result i32))) ;; index 5
   (import "env" "action_data_size" (func $action_data_size (result i32))) ;; index 6
   (import "env" "set_action_return_value" (func $set_action_return_value (param i32 i32))) ;; index 7
   (import "env" "get_context_free_data" (func $get_context_free_data (param i32 i32 i32) (result i32)))  ;; index 8
   (import "env" "send_inline" (func $send_inline (param i32 i32)))          ;; index 9
   (import "env" "send_context_free_inline" (func $send_context_free_inline (param i32 i32)))   ;; index 10
   (import "env" "send_deferred" (func $send_deferred (param i32 i64 i32 i32 i32)))  ;; index 11
   (import "env" "cancel_deferred" (func $cancel_deferred (param i32) (result i32))) ;; index 12

   (import "env" "get_call_data" (func $get_call_data (param i32 i32) (result i32))) ;; memory
   (memory $0 1)
   (export "memory" (memory $0))

   (table 13 anyfunc)          ;; function table definition. update the number of entries below when a new function is added
   (elem (i32.const 0) $case_require_auth)               ;; index 0
   (elem (i32.const 1) $case_require_auth2)              ;; index 1
   (elem (i32.const 2) $case_has_auth)                   ;; index 2
   (elem (i32.const 3) $case_require_recipient)          ;; index 3
   (elem (i32.const 4) $case_get_action)                 ;; index 4
   (elem (i32.const 5) $case_read_action_data)           ;; index 5
   (elem (i32.const 6) $case_action_data_size)           ;; index 6
   (elem (i32.const 7) $case_set_action_return_value)    ;; index 7
   (elem (i32.const 8) $case_get_context_free_data)      ;; index 8
   (elem (i32.const 9) $case_send_inline)                ;; index 9
   (elem (i32.const 10) $case_send_context_free_inline)   ;; index 10
   (elem (i32.const 11) $case_send_deferred)             ;; index 11
   (elem (i32.const 12) $case_cancel_deferred)           ;; index 12

   (type $ftable (func))      ;; function table instantiation
   (func $case_require_auth
      i64.const 0             ;; argument of require_auth
      call $require_auth
   )
   (func $case_require_auth2
      i64.const 0             ;; 1st argument of require_auth2
      i64.const 0             ;; 2nd argument of require_auth2
      call $require_auth2
   )
   (func $case_has_auth
      i64.const 0             ;; argument of has_auth
      call $require_auth
   )
   (func $case_require_recipient
      i64.const 0             ;; argument of require_recipient
      call $require_recipient
   )
   (func $case_get_action
      i32.const 0
      i32.const 0
      i32.const 0
      i32.const 0
      (drop (call $get_action))
   )
   (func $case_read_action_data
      i32.const 0
      i32.const 0
      (drop (call $read_action_data))
   )
   (func $case_action_data_size
      (drop (call $action_data_size))
   )
   (func $case_set_action_return_value
      i32.const 0
      i32.const 0
      call $set_action_return_value
   )
   (func $case_get_context_free_data
      i32.const 0
      i32.const 0
      i32.const 0
      (drop (call $get_context_free_data))
   )
   (func $case_send_inline
      i32.const 0
      i32.const 0
      call $send_inline
   )
   (func $case_send_context_free_inline
      i32.const 0
      i32.const 0
      call $send_context_free_inline
   )
   (func $case_send_deferred
      i32.const 4  ;; create a pointer
      i64.const 0
      i32.const 0
      i32.const 0
      i32.const 0
      call $send_deferred
   )
   (func $case_cancel_deferred
      i32.const 4  ;; create a pointer
      (drop (call $cancel_deferred))
   )

   (func $callee (param $index i32)
      get_local $index
      call_indirect (type $ftable)  ;; switch on function table
   )

   (export "sync_call" (func $sync_call))
   (func $sync_call (param $sender i64) (param $receiver i64) (param $data_size i32)
      (drop (call $get_call_data (i32.const 0)(get_local $data_size)))  ;; read the argument: index
      i32.const 0       ;; address of index (stored by get_call_data)
      i32.load          ;; load index
      call $callee
   )

   (export "apply" (func $apply))
   (func $apply (param $receiver i64) (param $account i64) (param $action_name i64))
)
)=====";

BOOST_AUTO_TEST_CASE(constrains_enforcement_test)  { try {
   validating_tester t;

   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // skip eos_vm_oc for now.
      return;
   }

   create_accounts_and_set_code(constrains_enforcement_caller_wast, constrains_enforcement_callee_wast, t);

   // require_auth
   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "callhostfunc"_n, "caller"_n, mvo() ("index", "0")),
                         unaccessible_api,
                         fc_exception_message_contains("this API may only be called from action"));

   // require_auth2
   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "callhostfunc"_n, "caller"_n, mvo() ("index", "1")),
                         unaccessible_api,
                         fc_exception_message_contains("this API may only be called from action"));

   // has_auth
   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "callhostfunc"_n, "caller"_n, mvo() ("index", "2")),
                         unaccessible_api,
                         fc_exception_message_contains("this API may only be called from action"));

   // require_recipient
   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "callhostfunc"_n, "caller"_n, mvo() ("index", "3")),
                         unaccessible_api,
                         fc_exception_message_contains("this API may only be called from action"));

   // get_action
   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "callhostfunc"_n, "caller"_n, mvo() ("index", "4")),
                         unaccessible_api,
                         fc_exception_message_contains("this API may only be called from action"));

   // read_action_data
   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "callhostfunc"_n, "caller"_n, mvo() ("index", "5")),
                         unaccessible_api,
                         fc_exception_message_contains("this API may only be called from action"));

   // action_data_size
   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "callhostfunc"_n, "caller"_n, mvo() ("index", "6")),
                         unaccessible_api,
                         fc_exception_message_contains("this API may only be called from action"));

   // set_action_return_value
   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "callhostfunc"_n, "caller"_n, mvo() ("index", "7")),
                         unaccessible_api,
                         fc_exception_message_contains("this API may only be called from action"));

   // get_context_free_data. sync calls not allowed in context-free trxs
   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "callhostfunc"_n, "caller"_n, mvo() ("index", "8")),
                         unaccessible_api,
                         fc_exception_message_contains("this API may only be called from context_free apply"));

   // send_inline
   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "callhostfunc"_n, "caller"_n, mvo() ("index", "9")),
                         unaccessible_api,
                         fc_exception_message_contains("this API may only be called from action"));

   // send_context_free_inline
   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "callhostfunc"_n, "caller"_n, mvo() ("index", "10")),
                         unaccessible_api,
                         fc_exception_message_contains("this API may only be called from action"));

   // send_deferred
   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "callhostfunc"_n, "caller"_n, mvo() ("index", "11")),
                         unaccessible_api,
                         fc_exception_message_contains("this API may only be called from action"));

   // cancel_deferred
   BOOST_CHECK_EXCEPTION(t.push_action("caller"_n, "callhostfunc"_n, "caller"_n, mvo() ("index", "12")),
                         unaccessible_api,
                         fc_exception_message_contains("this API may only be called from action"));
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
