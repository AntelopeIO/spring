#include "../sync_callee/sync_callee.hpp"
#include "sync_callee1.hpp"
#include <eosio/system.hpp>

using namespace eosio;

[[eosio::call]]
uint32_t sync_callee1::div(uint32_t x, uint32_t y) {
   return x / y;
}

[[eosio::call]]
uint32_t sync_callee1::recursive(uint32_t n) {
   if (n == 0) {
      return 0;
   } else {
      sync_callee::recursive_func recur{ "callee"_n }; // recursively call back the caller which is the `recursive` functiom in "callee"_n account

      return n + recur(n - 1);;
   }
}

void sync_callee1::get_sender_test() {
   // This method is only called by "callee"_n
   check(get_sender() == "callee"_n, "get_sender() in sync_callee1::get_sender_test() got an incorrect value");
}
