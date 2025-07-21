#include "../sync_callee/sync_callee.hpp"
#include "sync_callee1.hpp"

[[eosio::call]]
uint32_t sync_callee1::div(uint32_t x, uint32_t y) {
   check(get_sender() == "callee"_n, "get_sender() returned an incorrect value"); // this is only called from sync_callee
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
