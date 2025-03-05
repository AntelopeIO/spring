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

      //Rule #1 : Do not vote on different blocks with the same timestamp
      [[eosio::action]]
      std::pair<std::string, std::string> rule1(  const finalizer_policy_input& finalizer_policy, const finality_proof& proof_1, const finality_proof& proof_2);

      //Rule #2 : Do not vote on a block that conflicts with the time interval of a strong vote
      [[eosio::action]]
      std::pair<std::string, std::string> rule2(  const finalizer_policy_input& finalizer_policy, 
                     const finality_proof& high_proof,
                     const finality_proof& low_proof,
                     const reversible_proof_of_inclusion& reversible_proof_of_inclusion);

      //Rule #3 : Do not vote on a block that conflicts with another block on which you are locked
      [[eosio::action]]
      std::pair<std::string, std::string> rule3(  const finalizer_policy_input& finalizer_policy, 
                     const finality_proof& high_proof,
                     const finality_proof& low_proof,
                     const reversible_proof_of_inclusion& reversible_proof_of_inclusion);
      

      
      // compare two bitsets
      static std::pair<savanna::bitset, savanna::bitset> compare_qc(const quorum_certificate_input& qc1, const quorum_certificate_input& qc2);

};