#pragma once

#include <eosio/chain/block_timestamp.hpp>
#include <eosio/chain/producer_schedule.hpp>

namespace eosio::chain {

using producer_auth_differ = fc::ordered_diff<producer_authority, uint16_t>;
// Verify producer_auth_differ::size_type can represent all index values in the
// diff between two policies that could each hold up to max_proposers entries.
static_assert(std::numeric_limits<producer_auth_differ::size_type>::max() >= config::max_proposers - 1);
using producer_auth_diff_t = producer_auth_differ::diff_result;

struct proposer_policy_diff {
   uint32_t                    version = 0; ///< sequentially incrementing version number of producer_authority_schedule
   block_timestamp_type        proposal_time; // block when schedule was proposed
   producer_auth_diff_t        producer_auth_diff;
};

struct proposer_policy {
   // Useful for light clients, not necessary for nodeos
   block_timestamp_type        proposal_time; // block when schedule was proposed
   producer_authority_schedule proposer_schedule;

   proposer_policy_diff create_diff(const proposer_policy& target) const {
      return {.version = target.proposer_schedule.version,
              .proposal_time = target.proposal_time,
              .producer_auth_diff = producer_auth_differ::diff(proposer_schedule.producers, target.proposer_schedule.producers)};
   }

   template <typename X>
   requires std::same_as<std::decay_t<X>, proposer_policy_diff>
   [[nodiscard]] proposer_policy apply_diff(X&& diff) const {
      proposer_policy result;
      result.proposer_schedule.version = diff.version;
      result.proposal_time = diff.proposal_time;
      auto copy = proposer_schedule.producers;
      result.proposer_schedule.producers = producer_auth_differ::apply_diff(std::move(copy),
                                                                            std::forward<X>(diff).producer_auth_diff);
      return result;
   }
};

using proposer_policy_ptr = std::shared_ptr<proposer_policy>;

} /// eosio::chain

FC_REFLECT( eosio::chain::proposer_policy, (proposal_time)(proposer_schedule) )
FC_REFLECT( eosio::chain::producer_auth_diff_t, (remove_indexes)(insert_indexes) )
FC_REFLECT( eosio::chain::proposer_policy_diff, (version)(proposal_time)(producer_auth_diff) )
