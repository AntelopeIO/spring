#include "sync_callee.hpp"
#include "../sync_callee1/sync_callee1.hpp"
#include <eosio/print.hpp>

#include <vector>
#include <limits>

[[eosio::call]]
uint32_t sync_callee::basictest(uint32_t input) {
   eosio::print("I am basictest from sync_callee");

   return input;
}

[[eosio::call]]
sync_callee::two_fields_struct sync_callee::paramstest(two_fields_struct s1, int32_t m, four_fields_struct s2) {
   return { .a = s1.a * m + s2.c, .b = s1.b * m + s2.d };
}

[[eosio::call]]
uint32_t sync_callee::add(uint32_t x, uint32_t y) {
   return x + y;
}

[[eosio::call]]
uint32_t sync_callee::mul(uint32_t x, uint32_t y) {
   return x * y;
}

// calls sync_callee1's div and returns x / y
[[eosio::call]]
uint32_t sync_callee::nestedcalls(uint32_t x, uint32_t y) {
   sync_callee1::div_func div{ "callee1"_n };
   return div(x, y);
}

// calls self's add and returns the result
[[eosio::call]]
uint32_t sync_callee::selfcall(uint32_t x, uint32_t y) {
   sync_callee::add_func my_add{ "callee"_n };
   return my_add(x, y);
}

[[eosio::call]]
uint32_t sync_callee::recursive(uint32_t n) {
   if (n == 0) {
      return 0;
   } else {
      sync_callee1::recursive_func recur{ "callee1"_n }; // recursively call back the caller which is the `recursive` functiom in "callee"_n account

      return n + recur(n - 1);
   }
}

[[eosio::call]]
void sync_callee::voidreturn(uint32_t input) {
   eosio::print("I am the void return function");
}

[[eosio::call]]
uint32_t sync_callee::voidparam() {
   return 100;
}

[[eosio::call]]
void sync_callee::voidparamret() {
   eosio::print("I am the void return and void parameter function");
}

// Intentionally marked as an action. Used to test calling an unknown sync call function
[[eosio::action]]
void sync_callee::pureaction() {
   return;
}

// mark a function as both an action and a call
[[eosio::action, eosio::call]]
uint32_t sync_callee::actioncall(uint32_t x) {
   return x;
}

// an internal expensive function
static uint64_t fibonacci(uint32_t n) {
   if (n <= 1) return n;
   return fibonacci(n - 1) + fibonacci(n - 2);
}

[[eosio::call]]
void sync_callee::forever() {
   while (true) {
      fibonacci(20); // expensive
   }
}

[[eosio::call]]
void sync_callee::crash() {
   std::vector<uint32_t> v{};

   v[std::numeric_limits<uint64_t>::max()] = 10; // access error. vector is empty
}
