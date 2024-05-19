#include <eosio/eosio.hpp>
#include <eosio/crypto.hpp>
#include <eosio/crypto_bls_ext.hpp>
#include <eosio/system.hpp>
#include <eosio/transaction.hpp>

#include "../common/savanna.hpp"

using namespace eosio;
using namespace savanna;

CONTRACT finality_violation : public contract {
   public:
      using contract::contract;

      TABLE violation {

         uint64_t id;

         finalizer_policy_input finalizer_policy;

         proof proof1;
         proof proof2;

         uint64_t primary_key()const { return id; }
          
      };

      typedef eosio::multi_index< "violations"_n, violation> violations_table;

      ACTION addviolation(const finalizer_policy_input& finalizer_policy, const proof& proof1, const proof& proof2 ); //set finality policy

};