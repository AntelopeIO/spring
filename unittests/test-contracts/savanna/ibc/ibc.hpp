#include <eosio/eosio.hpp>
#include <eosio/crypto.hpp>
#include <eosio/crypto_bls_ext.hpp>
#include <eosio/system.hpp>
#include <eosio/transaction.hpp>

#include "../common/savanna.hpp"

using namespace eosio;
using namespace savanna;

CONTRACT ibc : public contract {
   public:
      using contract::contract;

      const uint32_t POLICY_CACHE_EXPIRY = 5; //5 seconds for testing
      const uint32_t PROOF_CACHE_EXPIRY = 5; //5 seconds for testing

      //finalizer policy augmented with contextually-relevant data 
      TABLE storedpolicy : finalizer_policy_input {

         uint32_t       last_block_num = 0; //last block number where this policy is in force

         time_point     cache_expiry; //cache expiry

         uint64_t primary_key() const {return generation;}
         uint64_t by_cache_expiry()const { return cache_expiry.sec_since_epoch(); }

         EOSLIB_SERIALIZE( storedpolicy, (generation)(threshold)(finalizers)(last_block_num)(cache_expiry))

      };

      TABLE lastproof {

          uint32_t         block_num;

          checksum256      finality_mroot;

          time_point       cache_expiry;

          uint64_t primary_key()const { return (uint64_t)block_num; }
          uint64_t by_cache_expiry()const { return cache_expiry.sec_since_epoch(); }
          checksum256 by_merkle_root()const { return finality_mroot; }

      };

      typedef eosio::multi_index< "policies"_n, storedpolicy,
          indexed_by<"expiry"_n, const_mem_fun<storedpolicy, uint64_t, &storedpolicy::by_cache_expiry>>> policies_table;

      typedef eosio::multi_index< "lastproofs"_n, lastproof,
          indexed_by<"merkleroot"_n, const_mem_fun<lastproof, checksum256, &lastproof::by_merkle_root>>,
          indexed_by<"expiry"_n, const_mem_fun<lastproof, uint64_t, &lastproof::by_cache_expiry>>> proofs_table;

      void _maybe_set_finalizer_policy(const finalizer_policy_input& policy, const uint32_t from_block_num);
      void _maybe_add_proven_root(const uint32_t block_num, const checksum256& finality_mroot);

      template<typename Table>
      void _maybe_remove_from_cache();

      finalizer_policy_input _get_stored_finalizer_policy(const uint64_t finalizer_policy_generation);

      void _check_finality_proof(const finality_proof& finality_proof, const block_proof_of_inclusion& target_block_proof_of_inclusion);
      void _check_target_block_proof_of_inclusion(const block_proof_of_inclusion& proof, const std::optional<checksum256> reference_root);

      ACTION setfpolicy(const finalizer_policy_input& policy, const uint32_t from_block_num); //set finality policy
      ACTION checkproof(const proof& proof);

      ACTION testbitset(const std::string bitset_string, const std::vector<uint8_t> bitset_vector, const uint32_t finalizers_count);

};