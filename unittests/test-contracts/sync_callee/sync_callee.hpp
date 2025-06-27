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

   struct person_info {
      std::string first_name;
      std::string street;
   };

   [[eosio::call]]
   uint32_t basictest(uint32_t input);
   using basictest_func = eosio::call_wrapper<"basictest"_n, &sync_callee::basictest>;

   // pass in two structs and an integer, multiply each field in the struct by
   // the integer, add last two fields of the second struct, and return the result
   [[eosio::call]]
   two_fields_struct paramstest(two_fields_struct s1, int32_t m, four_fields_struct s2);
   using paramstest_func = eosio::call_wrapper<"paramstest"_n, &sync_callee::paramstest>;

   // returns x + y
   [[eosio::call]]
   uint32_t add(uint32_t x, uint32_t y);
   using add_func = eosio::call_wrapper<"add"_n, &sync_callee::add>;

   // returns x * y
   [[eosio::call]]
   uint32_t mul(uint32_t x, uint32_t y);
   using mul_func = eosio::call_wrapper<"mul"_n, &sync_callee::mul>;

    // calls sync_callee1's div and returns x / y
   [[eosio::call]]
   uint32_t nestedcalls(uint32_t x, uint32_t y);
   using nestedcalls_func = eosio::call_wrapper<"nestedcalls"_n, &sync_callee::nestedcalls>;

    // calls self's add and returns the result
   [[eosio::call]]
   uint32_t selfcall(uint32_t x, uint32_t y);
   using selfcall_func = eosio::call_wrapper<"selfcall"_n, &sync_callee::selfcall>;

   [[eosio::call]]
   uint32_t recursive(uint32_t n);
   using recursive_func = eosio::call_wrapper<"recursive"_n, &sync_callee::recursive>;

   [[eosio::call]]
   void voidreturn(uint32_t n);
   using voidreturn_func = eosio::call_wrapper<"voidreturn"_n, &sync_callee::voidreturn>;

   [[eosio::call]]
   uint32_t voidparam();
   using voidparam_func = eosio::call_wrapper<"voidparam"_n, &sync_callee::voidparam>;

   [[eosio::call]]
   void voidparamret();
   using voidparamret_func = eosio::call_wrapper<"voidparamret"_n, &sync_callee::voidparamret>;

   // Intentionally mark this as an action, not a call
   [[eosio::action]]
   void pureaction();
   using pure_action_func = eosio::call_wrapper<"pureaction"_n, &sync_callee::pureaction>;

   // mark a function as both an action and a call
   [[eosio::action, eosio::call]]
   uint32_t actioncall(uint32_t x);  // return x
   using action_and_call_func = eosio::call_wrapper<"actioncall"_n, &sync_callee::actioncall>;

   // mark a function as both an action and a call
   [[eosio::call]]
   void forever();
   using forever_func = eosio::call_wrapper<"forever"_n, &sync_callee::forever>;

   // make the called function crash (by accessing invalid memory)
   [[eosio::call]]
   void crash();
   using crash_func = eosio::call_wrapper<"crash"_n, &sync_callee::crash>;

   [[eosio::call]]
   void insertperson(eosio::name user, std::string first_name, std::string street);
   using insert_person_read_only_func = eosio::call_wrapper<"insertperson"_n, &sync_callee::insertperson, eosio::access_mode::read_only>;
   using insert_person_func = eosio::call_wrapper<"insertperson"_n, &sync_callee::insertperson>;

   [[eosio::call]]
   person_info getperson(eosio::name user);
   using get_person_func = eosio::call_wrapper<"getperson"_n, &sync_callee::getperson>;

private:
   struct [[eosio::table]] person {
      eosio::name key;
      std::string first_name;
      std::string street;

      uint64_t primary_key() const { return key.value; }
   };

   using address_index = eosio::multi_index<"people"_n, person>;
};
