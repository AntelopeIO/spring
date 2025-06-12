#include <eosio/call.hpp>
#include <eosio/eosio.hpp>

class [[eosio::contract]] sync_callee : public eosio::contract{
public:
   using contract::contract;

   struct two_fields_struct {
      int64_t a;
      uint64_t b;
   };
   
   struct four_fields_struct {
      char a;
      bool b;
      int64_t c;
      uint64_t d;
   };

   [[eosio::call]]
   uint32_t basictest(uint32_t input);

   // pass in two structs and an integer, multiply each field in the struct by
   // the integer, add last two fields of the second struct, and return the result
   [[eosio::call]]
   two_fields_struct paramstest(two_fields_struct s1, int32_t m, four_fields_struct s2);

   using basictest_func = eosio::call_wrapper<"basictest"_n, &sync_callee::basictest>;
   using paramstest_func = eosio::call_wrapper<"paramstest"_n, &sync_callee::paramstest>;
};
