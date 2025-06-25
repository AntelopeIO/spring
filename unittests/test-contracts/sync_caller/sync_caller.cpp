#include "../sync_callee/sync_callee.hpp"
#include "../sync_callee1/sync_callee1.hpp"

#include <eosio/eosio.hpp>
#include <eosio/call.hpp>

using namespace eosio;

class [[eosio::contract]] sync_caller : public eosio::contract{
public:
   using contract::contract;

   // basic sync calls
   [[eosio::action]]
   void basictest(int32_t input) {
      sync_callee::basictest_func sync_call{ "callee"_n };

      eosio::check(sync_call(input) == input, "return value not the same as input"); // basic_test_func just returns the same value as input and does a print
   }

   // pass complex parameters
   [[eosio::action]]
   void paramstest() {
      sync_callee::paramstest_func    sync_call{ "callee"_n };
      sync_callee::two_fields_struct  input1{ 10, 20 };
      sync_callee::four_fields_struct input2{ 'a', true, 50, 100 };
      int32_t m = 2;

      // paramstest_func multiplies each field of input1 by m, adds last two
      // fields of input2, and returns a two_fields_struct
      auto output = sync_call(input1, m, input2);
      eosio::check(output.a == m * input1.a + input2.c, "field a of output is not correct");
      eosio::check(output.b == m * input1.b + input2.d, "field b of output is not correct");
   }

   // sequential sync calls to the same and different receivers
   [[eosio::action]]
   void seqcalls() {
      // to `add` on receiver "callee"_n
      sync_callee::add_func add{ "callee"_n };
      eosio::check(add(10, 20) == 30, "10 + 20 was not 30");

      // to `mul` on receiver "callee"_n
      sync_callee::mul_func mul{ "callee"_n };
      eosio::check(mul(2, 50) == 100, "2 * 50 was not 100");

      // to `div` on receiver "callee1"_n
      sync_callee1::div_func div{ "callee1"_n };
      eosio::check(div(1000, 4) == 250, "1000 / 4 was not 250");
   }

   // nested sync calls
   [[eosio::action]]
   void nestedcalls() {
      // call `nesteddiv` on receiver "callee"_n, which in turn calls `div` on receiver "callee1"_n
      sync_callee::nestedcalls_func nested_div{ "callee"_n };
      eosio::check(nested_div(32, 4) == 8, "32 / 4 was not 8");
   }

   // a sync call to the same receiver
   [[eosio::action]]
   void selfcall() {
      // call `selfcall` on receiver "callee"_n, which in turn calls `add` on the same "callee"_n
      sync_callee::selfcall_func selfcall{ "callee"_n };
      eosio::check(selfcall(7, 8) == 15, "7 + 8 was not 15");
   }

   // recursive call
   [[eosio::action]]
   void recursvcall(uint32_t n) {
      // call `recur` on "callee"_n, which calls `recur1` on "callee1"_n, which
      // calls `recur` again on "callee"_n, ...
      uint32_t expected = 0;
      for (uint32_t i = 1; i <= n; ++i) {
         expected += i;
      }

      sync_callee::recursive_func recur{ "callee"_n };
      eosio::check(recur(n) == expected, "result from recurvise call not expected");
   }

   [[eosio::action]]
   void voidreturn() {
      sync_callee::voidreturn_func void_return{ "callee"_n };

      void_return(10);
   }

   [[eosio::action]]
   void voidparam() {
      sync_callee::voidparam_func void_param{ "callee"_n };

      eosio::check(void_param() == 100, "void_param() did not return 100");
   }

   [[eosio::action]]
   void voidparamret() {
      sync_callee::voidparamret_func void_paramret{ "callee"_n };

      void_paramret();
   }

   // receiver does not exist
   [[eosio::action]]
   void unkwnrcvrtst() {
      sync_callee::basictest_func basictest{ "unknown"_n }; // link basictest_func to receiver `unknown`_n

      uint32_t input = 10;
      basictest(input);
   }

   // sync_callee forgets to mark pureaction() as a sync call
   [[eosio::action]]
   void unkwnfunctst() {
      sync_callee::pure_action_func pure_action{ "callee"_n };

      pure_action();
   }

   // sync_callee1::div_func does not exist in the receiver "callee"_n
   [[eosio::action]]
   void unkwnwraptst() {
      sync_callee1::div_func div{ "callee"_n };

      div(10, 2);
   }

   // actioncall() is tagged by both eosio::action and eosio::call
   [[eosio::action]]
   void actcalltst() {
      sync_callee::action_and_call_func action_and_call{ "callee"_n };

      // action_and_call just returns the value of the argument
      eosio::check(action_and_call(50) == 50, "action_and_call() did not return the argument");
   }

   // forever() is an infinite loop `while(true)`
   [[eosio::action]]
   void forevertest() {
      sync_callee::forever_func forever{ "callee"_n };

      forever();
   }

   // forever() is an infinite loop `while(true)`
   [[eosio::action]]
   void crashtest() {
      sync_callee::crash_func crash{ "callee"_n };

      crash();
   }

   // Insert an entry using read_only sync call, which will fail because
   // it tries to modify states
   [[eosio::action]]
   void insertrdonly() {
      sync_callee::insert_person_read_only_func{"callee"_n}("alice"_n, "alice", "123 Main St.");
   }

   // Insert an entry using regular sync call
   [[eosio::action]]
   void insertperson() {
      sync_callee::insert_person_func{"callee"_n}("alice"_n, "alice", "123 Main St.");
   }

   // Read an entry
   [[eosio::action]]
   void getperson() {
      auto user_info = sync_callee::get_person_func{"callee"_n}("alice"_n);

      eosio::check(user_info.first_name == "alice", "first name not alice");
      eosio::check(user_info.street == "123 Main St.", "street not 123 Main St.");
   }
};
