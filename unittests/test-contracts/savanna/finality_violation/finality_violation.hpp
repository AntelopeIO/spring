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

      ACTION rule1(const finalizer_policy_input finalizer_policy, const finality_proof proof_1, const finality_proof proof_2);
      ACTION rule2a(const finalizer_policy_input finalizer_policy, const finality_proof proof_1, const finality_proof proof_2);
      ACTION rule2b(const finalizer_policy_input finalizer_policy, 
                                    const finality_proof proof_1, 
                                    const finality_proof proof_2, 
                                    const block_proof_of_inclusion proof_of_inclusion);
      ACTION rule3();
      
};