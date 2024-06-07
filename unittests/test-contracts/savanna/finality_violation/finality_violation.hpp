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

      // attempt to record a finality violation. Action will return two bitsets in raw form.
      // The first one is the intersection of the two sets (guilty nodes), and the second one is the symmetric difference of the two sets (innocent nodes)
      [[eosio::action]]
      std::pair<std::string, std::string> addviolation(const finalizer_policy_input& finalizer_policy, const proof& proof1, const proof& proof2 );
      
      [[eosio::action]]
      void inclusion(const finalizer_policy_input& finalizer_policy, const proof& proof);


};