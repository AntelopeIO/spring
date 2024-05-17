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

      };

      typedef eosio::multi_index< "violations"_n, violation> violations_table;

      ACTION addviolation(const finalizer_policy_input& policy, const proof proof1, const proof proof2 ); //set finality policy

};