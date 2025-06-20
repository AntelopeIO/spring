#include "../sync_callee/sync_callee.hpp"

#include <eosio/eosio.hpp>
#include <eosio/call.hpp>
#include <eosio/print.hpp>

using namespace eosio;

class [[eosio::contract]] sync_caller : public eosio::contract{
public:
   using contract::contract;

   // Verify basic sync calls
   [[eosio::action]]
   void basictest(int32_t input) {
      sync_callee::basictest_func sync_call{ "callee"_n };

      eosio::print("Before calling sync call basictest");
      eosio::check(sync_call(input) == input, "return value not the same as input"); // basic_test_func just returns the same value as input and does a print
      eosio::print("After returned from basictest");
   }

   // Verify passing complex parameters
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
};
