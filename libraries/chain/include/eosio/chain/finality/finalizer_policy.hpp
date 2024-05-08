#pragma once

#include <eosio/chain/types.hpp>
#include <eosio/chain/finality/finalizer_authority.hpp>
#include <fc/container/ordered_diff.hpp>

namespace eosio::chain {

   static_assert(std::numeric_limits<uint16_t>::max() <= config::max_finalizers);
   using finalizers_differ = fc::ordered_diff<finalizer_authority, uint16_t>;
   using finalizers_diff_t = finalizers_differ::diff_result;

   struct finalizer_policy_diff {
      uint32_t                         generation = 0; ///< sequentially incrementing version number
      uint64_t                         threshold = 0;  ///< vote weight threshold to finalize blocks
      finalizers_diff_t                finalizers_diff;
   };

   struct finalizer_policy {
      uint32_t                         generation = 0; ///< sequentially incrementing version number
      uint64_t                         threshold = 0;  ///< vote weight threshold to finalize blocks
      std::vector<finalizer_authority> finalizers;     ///< Instant Finality voter set

      finalizer_policy_diff create_diff(const finalizer_policy& target) const {
         return {.generation = target.generation,
                 .threshold = target.threshold,
                 .finalizers_diff = finalizers_differ::diff(finalizers, target.finalizers)};
      }

      template <typename X>
      requires std::same_as<std::decay_t<X>, finalizer_policy_diff>
      void apply_diff(X&& diff) {
         generation = diff.generation;
         threshold = diff.threshold;
         finalizers = finalizers_differ::apply_diff(std::move(finalizers), std::forward<X>(diff).finalizers_diff);
      }

      // max accumulated weak weight before becoming weak_final
      uint64_t max_weak_sum_before_weak_final() const {
         uint64_t sum = std::accumulate( finalizers.begin(), finalizers.end(), 0,
            [](uint64_t acc, const finalizer_authority& f) {
               return acc + f.weight;
            }
         );

         return (sum - threshold);
      }
   };

   using finalizer_policy_ptr = std::shared_ptr<finalizer_policy>;
   using finalizer_policy_diff_ptr = std::shared_ptr<finalizer_policy_diff>;

} /// eosio::chain

FC_REFLECT( eosio::chain::finalizer_policy, (generation)(threshold)(finalizers) )
FC_REFLECT( eosio::chain::finalizers_diff_t, (remove_indexes)(insert_indexes) )
FC_REFLECT( eosio::chain::finalizer_policy_diff, (generation)(threshold)(finalizers_diff) )
