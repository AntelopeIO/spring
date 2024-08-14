#pragma once
#include "eosio/chain/protocol_feature_manager.hpp"
#include <eosio/chain/block.hpp>
#include <eosio/chain/block_header.hpp>


namespace eosio::chain::detail {

   inline bool is_builtin_activated(const protocol_feature_activation_set_ptr& pfa,
                                    const protocol_feature_set& pfs,
                                    builtin_protocol_feature_t feature_codename) {
      auto digest = pfs.get_builtin_digest(feature_codename);
      const auto& protocol_features = pfa->protocol_features;
      return digest && protocol_features.find(*digest) != protocol_features.end();
   }

   inline uint32_t get_current_round_start_slot( const block_timestamp_type& t) {
      auto index = t.slot % config::producer_repetitions; // current index in current round
      return t.slot - index;
   }

   // returns true if next and curr are in the same round
   inline bool in_same_round(const block_timestamp_type& next, const block_timestamp_type& curr) {
      return (next.slot < detail::get_current_round_start_slot(curr) + config::producer_repetitions);
   }

   inline std::optional<uint32_t> get_prior_round_start_slot( const block_timestamp_type& t) {
      if (t.slot < config::producer_repetitions) { // No prior round
         return {};
      }
      return get_current_round_start_slot(t) - config::producer_repetitions;
   }

   inline bool first_block_of_round(const block_timestamp_type& curr_block_time,
                                    const block_timestamp_type& parent_block_time) {
      assert(parent_block_time.slot < curr_block_time.slot);
      // if parent's time slot is in prior round, it means block is the
      // first block in current round
      return parent_block_time.slot < get_current_round_start_slot(curr_block_time);
   }

   inline const producer_authority& get_scheduled_producer(const vector<producer_authority>& producers, block_timestamp_type t) {
      auto index = t.slot % (producers.size() * config::producer_repetitions);
      index /= config::producer_repetitions;
      return producers[index];
   }

   constexpr auto additional_sigs_eid = additional_block_signatures_extension::extension_id();

   /**
    * Given a complete signed block, extract the validated additional signatures if present;
    *
    * @param b complete signed block
    * @return the list of additional signatures
    */
   inline vector<signature_type> extract_additional_signatures(const signed_block_ptr& b) {
      auto exts = b->validate_and_extract_extensions();

      if (auto it = exts.find(additional_sigs_eid); it != exts.end()) {
         auto& additional_sigs = std::get<additional_block_signatures_extension>(it->second);
         return std::move(additional_sigs.signatures);
      }

      return {};
   }

   /**
    *  Reference cannot outlive header_exts. Assumes header_exts is not mutated after instantiation.
    */
   inline const vector<digest_type>& get_new_protocol_feature_activations(const header_extension_multimap& header_exts) {
      static const vector<digest_type> no_activations{};

      if (auto it = header_exts.find(protocol_feature_activation::extension_id()); it != header_exts.end())
         return std::get<protocol_feature_activation>(it->second).protocol_features;

      return no_activations;
   }

} /// namespace eosio::chain
