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

      [[eosio::action]]

      std::pair<std::string, std::string> addviolation(const finalizer_policy_input& finalizer_policy, const proof& proof1, const proof& proof2 ); //set finality policy

      //ACTION logverdict(const std::string guilty, const std::string not_guilty);

};