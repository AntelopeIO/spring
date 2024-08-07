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
      //ACTION rule2a(const finalizer_policy_input finalizer_policy, const finality_proof proof_1, const finality_proof proof_2);
      ACTION rule2(const finalizer_policy_input finalizer_policy, 
                     const finality_proof high_proof,
                     const finality_proof low_proof,
                     const std::vector<checksum256> reversible_blocks_digests);
      ACTION rule3();
      
      ACTION testmroot(const checksum256 root, const std::vector<checksum256> reversible_blocks_digests);
      
};