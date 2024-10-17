#pragma   once

#include <eosio/chain/finalizer_policy.hpp>
#include <eosio/chain/proposer_policy.hpp>
#include <eosio/chain/finality_core.hpp>

namespace eosio::chain {

struct finality_extension : fc::reflect_init {
   static constexpr uint16_t extension_id()   { return 2; }
   static constexpr bool     enforce_unique() { return true; }

   finality_extension() = default;
   finality_extension(qc_claim_t qc_claim,
                              std::optional<finalizer_policy_diff>&& new_finalizer_policy_diff,
                              std::optional<proposer_policy_diff>&& new_proposer_policy_diff) :
      qc_claim(qc_claim),
      new_finalizer_policy_diff(std::move(new_finalizer_policy_diff)),
      new_proposer_policy_diff(std::move(new_proposer_policy_diff))
   {}

   void reflector_init() const {
      static_assert( fc::raw::has_feature_reflector_init_on_unpacked_reflected_types,
                     "finality_extension expects FC to support reflector_init" );
      static_assert( extension_id() == 2, "finality_extension extension id must be 2" );
   }

   qc_claim_t                              qc_claim;
   std::optional<finalizer_policy_diff>    new_finalizer_policy_diff;
   std::optional<proposer_policy_diff>     new_proposer_policy_diff;
};

} /// eosio::chain

FC_REFLECT( eosio::chain::finality_extension, (qc_claim)(new_finalizer_policy_diff)(new_proposer_policy_diff) )
