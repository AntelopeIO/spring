#include "sync_callee.hpp"
#include <eosio/print.hpp>

[[eosio::call]]
uint32_t sync_callee::basictest(uint32_t input) {
   eosio::print("I am basictest from sync_callee");

   return input;
}

[[eosio::call]]
sync_callee::two_fields_struct sync_callee::paramstest(two_fields_struct s1, int32_t m, four_fields_struct s2) {
   return { .a = s1.a * m + s2.c, .b = s1.b * m + s2.d };
}
