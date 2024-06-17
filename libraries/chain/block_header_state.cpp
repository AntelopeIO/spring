#include <eosio/chain/block_header_state.hpp>
#include <eosio/chain/block_header_state_utils.hpp>
#include <eosio/chain/finality/instant_finality_extension.hpp>
#include <eosio/chain/finality/proposer_policy.hpp>
#include <eosio/chain/exceptions.hpp>
#include <limits>

namespace eosio::chain {

// this is a versioning scheme that is separate from protocol features that only
// gets updated if a protocol feature causes a breaking change to light block
// header validation

// compute base_digest explicitly because of pointers involved.
digest_type block_header_state::compute_base_digest() const {
   digest_type::encoder enc;

   fc::raw::pack( enc, header );
   fc::raw::pack( enc, core );

   for (const auto& fp_pair : finalizer_policies) {
      fc::raw::pack( enc, fp_pair.first );
      const finalizer_policy_tracker& tracker = fp_pair.second;
      fc::raw::pack( enc, tracker.state );
      assert(tracker.policy);
      fc::raw::pack( enc, *tracker.policy );
   }

   assert(active_proposer_policy);
   fc::raw::pack( enc, *active_proposer_policy );

   for (const auto& pp_pair : proposer_policies) {
      fc::raw::pack( enc, pp_pair.first );
      assert(pp_pair.second);
      fc::raw::pack( enc, *pp_pair.second );
   }

   if (activated_protocol_features) {
      fc::raw::pack( enc, *activated_protocol_features );
   }

   return enc.result();
}

digest_type block_header_state::compute_finality_digest() const {
   auto base_digest = compute_base_digest();
   std::pair<const digest_type&, const digest_type&> last_pending_and_base{ last_pending_finalizer_policy_digest, base_digest };
   auto lpfp_base_digest = fc::sha256::hash(last_pending_and_base);

   assert(active_finalizer_policy);
   finality_digest_data_v1 finality_digest_data {
      .active_finalizer_policy_generation      = active_finalizer_policy->generation,
      .final_on_strong_qc_block_num            = core.final_on_strong_qc_block_num,
      .finality_tree_digest                    = finality_mroot(),
      .last_pending_finalizer_policy_and_base_digest = lpfp_base_digest
   };

   return fc::sha256::hash(finality_digest_data);
}

const producer_authority& block_header_state::get_scheduled_producer(block_timestamp_type t) const {
   return detail::get_scheduled_producer(active_proposer_policy->proposer_schedule.producers, t);
}

const vector<digest_type>& block_header_state::get_new_protocol_feature_activations()const {
   return detail::get_new_protocol_feature_activations(header_exts);
}

// The last proposed finalizer policy if none proposed or pending then the active finalizer policy
const finalizer_policy& block_header_state::get_last_proposed_finalizer_policy() const {
   if (!finalizer_policies.empty()) {
      for (auto ritr = finalizer_policies.rbegin(); ritr != finalizer_policies.rend(); ++ritr) {
         if (ritr->second.state == finalizer_policy_tracker::state_t::proposed)
            return *ritr->second.policy;
      }
      return *finalizer_policies.rbegin()->second.policy;
   }
   return *active_finalizer_policy;
}

// The last pending finalizer policy if none pending then the active finalizer policy
// Used to populate last_pending_finalizer_policy_digest which is expected to be the highest generation pending
const finalizer_policy& block_header_state::get_last_pending_finalizer_policy() const {
   if (!finalizer_policies.empty()) {
      // lambda only used when asserts enabled
      [[maybe_unused]] auto highest_pending_generation = [this]() {
         finalizer_policy_ptr highest;
         for (auto ritr = finalizer_policies.rbegin(); ritr != finalizer_policies.rend(); ++ritr) {
            if (ritr->second.state == finalizer_policy_tracker::state_t::pending) {
               if (!highest || highest->generation < ritr->second.policy->generation)
                  highest = ritr->second.policy;
            }
         }
         return highest;
      };
      for (auto ritr = finalizer_policies.rbegin(); ritr != finalizer_policies.rend(); ++ritr) {
         if (ritr->second.state == finalizer_policy_tracker::state_t::pending) {
            assert(highest_pending_generation() == ritr->second.policy);
            return *ritr->second.policy;
         }
      }
   }
   return *active_finalizer_policy;
}

// The last proposed proposer policy, if none proposed then the active proposer policy
const proposer_policy& block_header_state::get_last_proposed_proposer_policy() const {
   if (proposer_policies.empty()) {
      assert(active_proposer_policy);
      return *active_proposer_policy;
   }
   auto it = proposer_policies.rbegin();
   assert(it != proposer_policies.rend());
   return *it->second;
}

bool is_finalizer_policies_valid(const flat_multimap<block_num_type, finalizer_policy_tracker>& fin_policies) {
   // check that we have at most *one* proposed and *one* pending `finalizer_policy`
   // for at most one block number,
   // and at most *one* pending `finalizer_policy` for `fin_policies`.
   // -----------------------------------------------------------------------------
   block_num_type block_num(0);
   block_num_type prev_block_num(0);
   bool pending{false}, proposed{false};
   uint32_t pending_total{0};

   for (auto it = fin_policies.begin(); it != fin_policies.end(); ++it) {
      if (prev_block_num > it->first) // in ascending order
         return false;
      else
         prev_block_num = it->first;
      if (block_num != it->first) {
         pending = proposed = false;
         block_num = it->first;
      }
      const auto& tracker = it->second;
      if (tracker.state == finalizer_policy_tracker::state_t::proposed) {
         if (proposed)
            return false;
         else
            proposed = true;
      }
      if (tracker.state == finalizer_policy_tracker::state_t::pending) {
         ++pending_total;
         if (pending_total == 2)
            return false;

         if (pending)
            return false;
         else
            pending = true;
      }
   }
   return true;
}

void build_next_finalizer_policies(const block_header_state& prev,
                                   block_header_state& next_header_state) {
   assert(!prev.finalizer_policies.empty());
   assert(is_finalizer_policies_valid(prev.finalizer_policies));

   auto lib = next_header_state.core.last_final_block_num();

   if (prev.finalizer_policies.begin()->first > lib) {
      // we have at least one `finalizer_policy` in our map, but none of these is
      // due to become active of this block because lib has not advanced enough, so
      // we just copy the multimap and keep using the same `active_finalizer_policy`
      // ---------------------------------------------------------------------------
      next_header_state.finalizer_policies = prev.finalizer_policies;
      return;
   }

   auto itr = prev.finalizer_policies.begin();
   auto pending_itr = prev.finalizer_policies.end(); // existing pending policy
   auto first_policy_after_lib_itr = prev.finalizer_policies.end(); // first policy whose associated block number is greater than LIB

   while (itr != prev.finalizer_policies.end()) {
      if (itr->second.state == finalizer_policy_tracker::state_t::pending) {
         // found the existing pending policy
         assert(pending_itr == prev.finalizer_policies.end()); // Can have only one pending
         pending_itr = itr;
      }

      if (itr->first > lib && first_policy_after_lib_itr == prev.finalizer_policies.end()) {
         // found the first policy whose associated block number is greater than LIB
         first_policy_after_lib_itr = itr;
      }

      if (pending_itr != prev.finalizer_policies.end() && first_policy_after_lib_itr != prev.finalizer_policies.end() ) {
         // both pending_itr and first_policy_after_lib_itr are found. no need to continue
         break;
      }

      ++itr;
   }

   // A pending spot is available if no current pending policy exists or
   // current pending policy becomes active
   bool pending_spot_open = true;
   if (pending_itr != prev.finalizer_policies.end()) { // pending policy exists
      assert(pending_itr->second.state == finalizer_policy_tracker::state_t::pending);

      if (pending_itr->first <= lib) {
         // it is time to become active, pending spot becomes available
         next_header_state.active_finalizer_policy.reset(new finalizer_policy(*pending_itr->second.policy));
      } else {
         // it is not due to become active. no spot available
         pending_spot_open = false;
      }
   }

   // find the target proposed policy whose associated block number  is the largest block number is
   // less than or eqaul to LIB. start from the end of finalizer_policies
   // as it is sorted by ascending order of assoiciated block numbers
   auto ritr = prev.finalizer_policies.rbegin();
   while (ritr != prev.finalizer_policies.rend()) {
      if (ritr->first <= lib && ritr->second.state == finalizer_policy_tracker::state_t::proposed) {
         if (pending_spot_open) {
            finalizer_policy_tracker t { finalizer_policy_tracker::state_t::pending,
                                         ritr->second.policy };
            auto next_block_num = next_header_state.block_num();
            next_header_state.finalizer_policies.emplace(next_block_num, std::move(t));
         } else {
            next_header_state.finalizer_policies.emplace(*ritr);
         }

         break;
      }

      ++ritr;
   }

   if (first_policy_after_lib_itr != prev.finalizer_policies.end()) {
      // copy remainder of proposed policies
      next_header_state.finalizer_policies.insert(boost::container::ordered_unique_range_t(),
                                                  first_policy_after_lib_itr,
                                                  prev.finalizer_policies.end());
   }

   assert(is_finalizer_policies_valid(next_header_state.finalizer_policies));
}

// -------------------------------------------------------------------------------------------------
// `finish_next` updates the next `block_header_state` according to the contents of the
// header extensions (either new protocol_features or instant_finality_extension) applicable to this
// next block .
//
// These extensions either result from the execution of the previous block (in case this node
// was the block producer) or were received from the network in a `signed_block`.
// -------------------------------------------------------------------------------------------------
void finish_next(const block_header_state& prev,
                 block_header_state& next_header_state,
                 vector<digest_type> new_protocol_feature_activations,
                 instant_finality_extension if_ext,
                 bool log) { // only log on assembled blocks, to avoid double logging
   // activated protocol features
   // ---------------------------
   if (!new_protocol_feature_activations.empty()) {
      next_header_state.activated_protocol_features = std::make_shared<protocol_feature_activation_set>(
         *prev.activated_protocol_features, std::move(new_protocol_feature_activations));
   } else {
      next_header_state.activated_protocol_features = prev.activated_protocol_features;
   }

   // proposer policy
   // ---------------
   next_header_state.active_proposer_policy = prev.active_proposer_policy;

   if (!prev.proposer_policies.empty()) {
      auto it = prev.proposer_policies.begin();
      // +1 since this is called after the block is built, this will be the active schedule for the next block
      if (it->first.slot <= next_header_state.header.timestamp.slot + 1) {
         next_header_state.active_proposer_policy = it->second;
         next_header_state.proposer_policies = { ++it, prev.proposer_policies.end() };
      } else {
         next_header_state.proposer_policies = prev.proposer_policies;
      }
   }

   std::optional<proposer_policy> new_proposer_policy;
   if (if_ext.new_proposer_policy_diff) {
      new_proposer_policy = prev.get_last_proposed_proposer_policy().apply_diff(*if_ext.new_proposer_policy_diff);
   }
   if (new_proposer_policy) {
      // called when assembling the block
      next_header_state.proposer_policies[new_proposer_policy->active_time] =
         std::make_shared<proposer_policy>(std::move(*new_proposer_policy));
   }

   // finality_core
   // -------------
   block_ref parent_block {
      .block_id  = prev.block_id,
      .timestamp = prev.timestamp()
   };
   next_header_state.core = prev.core.next(parent_block, if_ext.qc_claim);

   // finalizer policy
   // ----------------
   next_header_state.active_finalizer_policy = prev.active_finalizer_policy;

   if (!prev.finalizer_policies.empty()) {
      build_next_finalizer_policies(prev, next_header_state);
   }

   next_header_state.last_pending_finalizer_policy_digest = fc::sha256::hash(next_header_state.get_last_pending_finalizer_policy());

   if (if_ext.new_finalizer_policy_diff) {
      finalizer_policy new_finalizer_policy = prev.get_last_proposed_finalizer_policy().apply_diff(*if_ext.new_finalizer_policy_diff);

      // a new `finalizer_policy` was proposed in the previous block, and is present in the previous
      // block's header extensions.
      // Add this new proposal to the `finalizer_policies` multimap which tracks the in-flight proposals,
      // increment the generation number, and log that proposal (debug level).
      // ------------------------------------------------------------------------------------------------
      if (log)
         dlog("New finalizer policy proposed in block ${id}..: ${pol}",
              ("id", prev.block_id.str().substr(8,16))("pol", new_finalizer_policy));
      next_header_state.finalizer_policy_generation = new_finalizer_policy.generation;
      next_header_state.finalizer_policies.emplace(
         next_header_state.block_num(),
         finalizer_policy_tracker{finalizer_policy_tracker::state_t::proposed,
                                  std::make_shared<finalizer_policy>(std::move(new_finalizer_policy))});
   } else {
      next_header_state.finalizer_policy_generation = prev.finalizer_policy_generation;
   }

   // Finally update block id from header
   // -----------------------------------
   next_header_state.block_id = next_header_state.header.calculate_id();

   // Now that we have the block id of the new block, log what changed.
   // -----------------------------------------------------------------
   if (next_header_state.active_finalizer_policy != prev.active_finalizer_policy) {
      const auto& act = next_header_state.active_finalizer_policy;
      if (log) {
         ilog("Finalizer policy generation change: ${old_gen} -> ${new_gen}",
              ("old_gen", prev.active_finalizer_policy->generation)("new_gen",act->generation));
         ilog("New finalizer policy becoming active in block ${id}: ${pol}",("id", next_header_state.block_id)("pol", *act));
      }
   }
}
   
block_header_state block_header_state::next(block_header_state_input& input) const {
   block_header_state next_header_state;

   // header
   // ------
   next_header_state.header = {
      .timestamp         = input.timestamp,
      .producer          = input.producer,
      .confirmed         = 0,
      .previous          = input.parent_id,
      .transaction_mroot = input.transaction_mroot,
      .action_mroot      = input.finality_mroot_claim,
      .schedule_version  = block_header::proper_svnn_schedule_version
   };

   // finality extension
   // ------------------
   std::optional<finalizer_policy_diff> new_finalizer_policy_diff;
   if (input.new_finalizer_policy) {
      new_finalizer_policy_diff = get_last_proposed_finalizer_policy().create_diff(*input.new_finalizer_policy);
   }
   std::optional<proposer_policy_diff> new_proposer_policy_diff;
   if (input.new_proposer_policy) {
      new_proposer_policy_diff = get_last_proposed_proposer_policy().create_diff(*input.new_proposer_policy);
   }
   instant_finality_extension new_if_ext { input.most_recent_ancestor_with_qc,
                                           std::move(new_finalizer_policy_diff),
                                           std::move(new_proposer_policy_diff) };

   uint16_t if_ext_id = instant_finality_extension::extension_id();
   emplace_extension(next_header_state.header.header_extensions, if_ext_id, fc::raw::pack(new_if_ext));
   next_header_state.header_exts.emplace(if_ext_id, new_if_ext);

   // add protocol_feature_activation extension
   // -----------------------------------------
   if (!input.new_protocol_feature_activations.empty()) {
      uint16_t ext_id = protocol_feature_activation::extension_id();
      protocol_feature_activation pfa_ext{.protocol_features = input.new_protocol_feature_activations};

      emplace_extension(next_header_state.header.header_extensions, ext_id, fc::raw::pack(pfa_ext));
      next_header_state.header_exts.emplace(ext_id, std::move(pfa_ext));
   }

   finish_next(*this, next_header_state, std::move(input.new_protocol_feature_activations), std::move(new_if_ext), true);

   return next_header_state;
}

/**
 *  Transitions the current header state into the next header state given the supplied signed block header.
 *
 *  Given a signed block header, generate the expected template based upon the header time,
 *  then validate that the provided header matches the template.
 */
block_header_state block_header_state::next(const signed_block_header& h, validator_t& validator) const {
   auto producer = detail::get_scheduled_producer(active_proposer_policy->proposer_schedule.producers, h.timestamp).producer_name;
   
   EOS_ASSERT( h.previous == block_id, unlinkable_block_exception,
               "previous mismatch ${p} != ${id}", ("p", h.previous)("id", block_id) );
   EOS_ASSERT( h.producer == producer, wrong_producer, "wrong producer specified" );
   EOS_ASSERT( !h.new_producers, producer_schedule_exception,
               "Block header contains legacy producer schedule outdated by activation of WTMsig Block Signatures" );

   block_header_state next_header_state;
   next_header_state.header = static_cast<const block_header&>(h);
   next_header_state.header_exts = h.validate_and_extract_header_extensions();
   next_header_state.finalizer_policy_generation = finalizer_policy_generation;

   const auto& exts = next_header_state.header_exts;

   // retrieve protocol_feature_activation from incoming block header extension
   // -------------------------------------------------------------------------
   vector<digest_type> new_protocol_feature_activations;
   if( exts.count(protocol_feature_activation::extension_id() > 0) ) {
      auto  pfa_entry = exts.lower_bound(protocol_feature_activation::extension_id());
      auto& pfa_ext   = std::get<protocol_feature_activation>(pfa_entry->second);
      new_protocol_feature_activations = pfa_ext.protocol_features;
      validator( timestamp(), activated_protocol_features->protocol_features, new_protocol_feature_activations );
   }

   // retrieve instant_finality_extension data from block header extension
   // --------------------------------------------------------------------
   EOS_ASSERT(exts.count(instant_finality_extension::extension_id()) > 0, invalid_block_header_extension,
              "Instant Finality Extension is expected to be present in all block headers after switch to IF");
   auto  if_entry     = exts.lower_bound(instant_finality_extension::extension_id());
   const auto& if_ext = std::get<instant_finality_extension>(if_entry->second);

   if (h.is_proper_svnn_block()) {
      // if there is no Finality Tree Root associated with the block,
      // then this needs to validate that h.action_mroot is the empty digest
      auto next_core_metadata = core.next_metadata(if_ext.qc_claim);
      bool no_finality_tree_associated = core.is_genesis_block_num(next_core_metadata.final_on_strong_qc_block_num);

      EOS_ASSERT(no_finality_tree_associated == h.action_mroot.empty(), block_validate_exception,
                 "No Finality Tree Root associated with the block, does not match with empty action_mroot: "
                 "(${n}), action_mroot empty (${e}), final_on_strong_qc_block_num (${f})",
                 ("n", no_finality_tree_associated)("e", h.action_mroot.empty())
                 ("f", next_core_metadata.final_on_strong_qc_block_num));
   };

   finish_next(*this, next_header_state, std::move(new_protocol_feature_activations), if_ext, false);

   return next_header_state;
}

// -------------------------------------------------------------------------------
// do some sanity checks on block_header_state
// -------------------------------------------------------------------------------
bool block_header_state::sanity_check() const {
   return is_finalizer_policies_valid(finalizer_policies);
}

} // namespace eosio::chain

