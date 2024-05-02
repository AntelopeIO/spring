#pragma   once

#include <eosio/chain/finality/finalizer_policy.hpp>
#include <eosio/chain/finality/proposer_policy.hpp>
#include <eosio/chain/finality/finality_core.hpp>

namespace eosio::chain {

struct instant_finality_extension : fc::reflect_init {
   static constexpr uint16_t extension_id()   { return 2; }
   static constexpr bool     enforce_unique() { return true; }

   instant_finality_extension() = default;
   instant_finality_extension(qc_claim_t qc_claim,
                              std::optional<finalizer_policy> new_finalizer_policy,
                              std::shared_ptr<proposer_policy> new_proposer_policy) :
      qc_claim(qc_claim),
      new_finalizer_policy(std::move(new_finalizer_policy)),
      new_proposer_policy(std::move(new_proposer_policy))
   {}

   void reflector_init() const {
      static_assert( fc::raw::has_feature_reflector_init_on_unpacked_reflected_types,
                     "instant_finality_extension expects FC to support reflector_init" );
      static_assert( extension_id() == 2, "instant_finality_extension extension id must be 2" );
   }

   qc_claim_t                         qc_claim;
   std::optional<finalizer_policy>    new_finalizer_policy;
   std::shared_ptr<proposer_policy>   new_proposer_policy;
};

} /// eosio::chain

FC_REFLECT( eosio::chain::instant_finality_extension, (qc_claim)(new_finalizer_policy)(new_proposer_policy) )
