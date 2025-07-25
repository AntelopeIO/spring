#include <eosio/call.hpp>
#include <eosio/eosio.hpp>

class [[eosio::contract]] sync_callee1 : public eosio::contract{
public:
   using contract::contract;

   // returns x / y
   [[eosio::call]]
   uint32_t div(uint32_t x, uint32_t y);
   using div_func = eosio::call_wrapper<"div"_i, &sync_callee1::div>;

   // recursively calls back the caller
   [[eosio::call]]
   uint32_t recursive(uint32_t n);
   using recursive_func = eosio::call_wrapper<"recursive"_i, &sync_callee1::recursive>;

   [[eosio::call]]
   void get_sender_test();
   using get_sender_test_func = eosio::call_wrapper<"get_sender_test"_i, &sync_callee1::get_sender_test>;
};
