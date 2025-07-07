#include <eosio/chain/block_header_state.hpp>
#include <eosio/chain/block_header_state_utils.hpp>
#include <eosio/chain/finality_extension.hpp>
#include <eosio/chain/proposer_policy.hpp>
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
   core.pack_for_digest( enc );

   fc::raw::pack( enc, proposed_finalizer_policies );
   fc::raw::pack( enc, pending_finalizer_policy );

   assert(active_proposer_policy);
   fc::raw::pack( enc, *active_proposer_policy );

   // For things that are optionally present we should always pack the bool
   // indicating if they are there.
   fc::raw::pack( enc, latest_proposed_proposer_policy );
   fc::raw::pack( enc, latest_pending_proposer_policy );

   // Should be always present
   assert(activated_protocol_features);
   fc::raw::pack( enc, *activated_protocol_features );

   return enc.result();
}

digest_type block_header_state::compute_finality_digest() const {
   // compute commitments related to finality violation proofs
   auto latest_qc_claim_block_num = core.latest_qc_claim().block_num;
   auto blk_ref = core.is_genesis_core() // Savanna Genesis core does not have block_ref
                  ? block_ref{}
                  : core.get_block_reference(latest_qc_claim_block_num);

   level_3_commitments_t level_3_commitments {
         .reversible_blocks_mroot         = core.get_reversible_blocks_mroot(),
         .latest_qc_claim_block_num       = latest_qc_claim_block_num,
         .latest_qc_claim_finality_digest = blk_ref.finality_digest,
         .latest_qc_claim_timestamp       = blk_ref.timestamp,
         .timestamp                       = timestamp(),
         .base_digest                     = compute_base_digest()
   };

   // compute commitments related to finalizer policy transitions
   level_2_commitments_t level_2_commitments {
         .last_pending_fin_pol_digest           = last_pending_finalizer_policy_digest,
         .last_pending_fin_pol_start_timestamp  = last_pending_finalizer_policy_start_timestamp,
         .l3_commitments_digest                 = fc::sha256::hash(level_3_commitments)
   };

   assert(active_finalizer_policy);
   finality_digest_data_v1 finality_digest_data {
      .active_finalizer_policy_generation       = active_finalizer_policy->generation,
      .last_pending_finalizer_policy_generation = get_last_pending_finalizer_policy().generation,
      .finality_tree_digest                     = finality_mroot(),
      .l2_commitments_digest                    = fc::sha256::hash(level_2_commitments)
   };

   return fc::sha256::hash(finality_digest_data);
}

// returns scheduled active proposer policy for a given block at timestamp `t`
const proposer_policy_ptr& block_header_state::get_active_proposer_policy_for_block_at(block_timestamp_type next_block_timestamp) const {
   EOS_ASSERT(next_block_timestamp > timestamp(), block_too_old_exception,
              "next block timestamp ${n} must be greater than current timestamp ${c}", ("n", next_block_timestamp)("c", timestamp()));

   // if the block is in the same round of current block, use current active_proposer_policy
   if (detail::in_same_round(next_block_timestamp, timestamp())) {
      return active_proposer_policy;
   }

   // if there is no pending nor proposed proposer policy, use current active_proposer_policy
   if (!latest_proposed_proposer_policy && !latest_pending_proposer_policy) {
      return active_proposer_policy;
   }

   // at this point, the next block (with timestamp `next_block_timestamp`)
   // must be the first block in a round after the current round
   std::optional<uint32_t> prior_round_start_slot = detail::get_prior_round_start_slot(timestamp());
   if (latest_proposed_proposer_policy && prior_round_start_slot &&
         latest_proposed_proposer_policy->proposal_time.slot < *prior_round_start_slot &&
         latest_proposed_proposer_policy->proposal_time <= core.last_final_block_timestamp()) {
      return latest_proposed_proposer_policy;
   }

   if (latest_pending_proposer_policy &&
         latest_pending_proposer_policy->proposal_time <= core.last_final_block_timestamp()) {
      return latest_pending_proposer_policy;
   }

   return active_proposer_policy;
}

const producer_authority& block_header_state::get_scheduled_producer(block_timestamp_type t) const {
   return detail::get_scheduled_producer(active_proposer_policy->proposer_schedule.producers, t);
}

// returns producer using the proposer policy calculated by time `t`
const producer_authority& block_header_state::get_producer_for_block_at(block_timestamp_type next_block_timestamp) const {
   return detail::get_scheduled_producer(get_active_proposer_policy_for_block_at(next_block_timestamp)->proposer_schedule.producers, next_block_timestamp);
}

const producer_authority_schedule* block_header_state::pending_producers() const {
   if (latest_pending_proposer_policy) {
      return &latest_pending_proposer_policy->proposer_schedule;
   }
   return nullptr;
}

const vector<digest_type>& block_header_state::get_new_protocol_feature_activations()const {
   return detail::get_new_protocol_feature_activations(header_exts);
}

// The last proposed finalizer policy if none proposed or pending then the active finalizer policy
const finalizer_policy& block_header_state::get_last_proposed_finalizer_policy() const {
   if (!proposed_finalizer_policies.empty()) {
       return *proposed_finalizer_policies.back().second;
   } else if (pending_finalizer_policy.has_value()) {
      return *pending_finalizer_policy->second;
   }
   return *active_finalizer_policy;
}

// The last pending finalizer policy if none pending then the active finalizer policy
// Used to populate last_pending_finalizer_policy_digest
const finalizer_policy& block_header_state::get_last_pending_finalizer_policy() const {
   if (pending_finalizer_policy.has_value()) {
      return *pending_finalizer_policy->second;
   }
   return *active_finalizer_policy;
}

// Only defined for core.latest_qc_claim().block_num <= ref.block_num() <= core.current_block_num()
// Retrieves the finalizer policies applicable for the block referenced by `ref`.
// See full explanation in issue #694.
// ------------------------------------------------------------------------------------------------
finalizer_policies_t block_header_state::get_finalizer_policies(const block_ref& ref) const {
   assert(core.links.empty() ||   // called from a bogus block_state constructed in a test
          (core.latest_qc_claim().block_num <= ref.block_num() && ref.block_num() <= core.current_block_num()));
   finalizer_policies_t res;

   res.finality_digest = ref.finality_digest;

   auto active_gen = ref.active_policy_generation;
   assert(active_gen != 0);                                       // we should always have an active policy

   if (active_finalizer_policy->generation == active_gen)
      res.active_finalizer_policy = active_finalizer_policy;      // the one active at block_num is still active
   else {
      // cannot be the pending one as it never was active
      assert(!pending_finalizer_policy || pending_finalizer_policy->second->generation > active_gen);

      // has to be the one in latest_qc_claim_block_active_finalizer_policy
      assert(latest_qc_claim_block_active_finalizer_policy != nullptr);
      assert(latest_qc_claim_block_active_finalizer_policy->generation == active_gen);
      EOS_ASSERT(latest_qc_claim_block_active_finalizer_policy != nullptr &&
                    latest_qc_claim_block_active_finalizer_policy->generation == active_gen,
                 chain_exception,
                 "Logic error in finalizer policy retrieval"); // just in case
      res.active_finalizer_policy = latest_qc_claim_block_active_finalizer_policy;
   }

   auto pending_gen = ref.pending_policy_generation;
   if (pending_gen == 0)
      res.pending_finalizer_policy = nullptr;                    // no pending policy at block_num.
   else if (pending_gen == active_finalizer_policy->generation)
      res.pending_finalizer_policy = active_finalizer_policy;    // policy pending at block_num became active
   else {
      // cannot be the one in latest_qc_claim_block_active_finalizer_policy since it was active at
      // core.latest_qc_claim().block_num. So it must be the one still pending.
      assert(pending_finalizer_policy && pending_finalizer_policy->second->generation == pending_gen);
      EOS_ASSERT(pending_finalizer_policy && pending_finalizer_policy->second->generation == pending_gen, chain_exception,
                 "Logic error in finalizer policy retrieval");  // just in case
      res.pending_finalizer_policy = pending_finalizer_policy->second;
   }

   return res;
}

// Only defined for core.latest_qc_claim().block_num <= num <= core.current_block_num()
// Retrieves the active finalizer policy generation applicatble for the block `num`, which
// can be the current block or one of its ancestors up to core.latest_qc_claim().block_num (incl).
// -----------------------------------------------------------------------------------------------
uint32_t block_header_state::get_active_finalizer_policy_generation(block_num_type num) const {
   assert(core.links.empty() ||   // called from a bogus block_state constructed in a test
          (core.last_final_block_num() <= num && num <= core.current_block_num()));
   if (num == block_num())
      return active_finalizer_policy->generation;
   const block_ref& ref = core.get_block_reference(num);
   return ref.active_policy_generation;
}


// The last proposed proposer policy, if none proposed then the active proposer policy
const proposer_policy& block_header_state::get_last_proposed_proposer_policy() const {
   if (latest_proposed_proposer_policy) {
      return *latest_proposed_proposer_policy;
   }
   if (latest_pending_proposer_policy) {
      return *latest_pending_proposer_policy;
   }
   assert(active_proposer_policy);
   return *active_proposer_policy;
}

// This function evaluates possible promotions from pending to active
// and from proposed to pending (removing any proposed policies that are known at that
// time to never become pending)
//
// In particular,
// 1. If there is a pending policy, determine whether it should be promoted to active.
//    If the associated block number is less than or equal to the new LIB number,
//    the pending policy should be promoted to active. This also means the pending slot
//    is now open for a possible promotion of a proposed policy to pending.
//    This guarantees that there will be at most one pending policy at any given time.
// 2. If there is any proposed policy with an associated block number that is less than
//    or equal to the new LIB number:
//  i.   Find the proposed policy with the greatest associated block number that is still
//       less than or equal to the new LIB number (call this the target proposed policy).
//  ii.  Remove any proposed policies with an associated block number less than that of
//       the target proposed policy.
//  iii. If the pending slot is open, promote that target proposed policy to pending.
//       Otherwise, leave the target proposed policy (and any other proposed policies with
//       greater associated block numbers) alone in the proposed policy queue.
//
void evaluate_finalizer_policies_for_promotion(const block_header_state& prev,
                                               block_header_state& next_header_state) {
   // Common case: if no pending policy and no proposed_finalizer_policies to evaluate,
   //              just return
   if (!prev.pending_finalizer_policy.has_value() && prev.proposed_finalizer_policies.empty() ) {
      return;
   }

   auto lib = next_header_state.core.last_final_block_num();
   const auto& prev_pending = prev.pending_finalizer_policy;
   const auto& prev_proposed = prev.proposed_finalizer_policies;
   auto& next_pending = next_header_state.pending_finalizer_policy;
   auto& next_proposed = next_header_state.proposed_finalizer_policies;

   // Evaluate pending first.
   bool pending_slot_open = true;
   if (prev_pending.has_value()) {
      if (prev_pending->first <= lib) {
         // The block associated with the pending has become final, promote pending to active
         next_header_state.active_finalizer_policy = prev_pending->second;
      } else {
         // Pending not yet to become final, copy it to next_header_state
         next_pending = prev_pending;
         pending_slot_open = false;  // no slot openned up
      }
   }

   // Nothing more to do if existing proposed_finalizer_policies is empty
   if (prev_proposed.empty()) {
      return;
   }

   // Find the target proposed policy which is the proposed policy with the greatest
   // associated block number that is less than or equal to the new LIB number
   auto first_reversible = std::ranges::find_if(prev_proposed, [&](const auto& p) { return p.first > lib; });
   auto target = first_reversible > prev_proposed.begin() ? first_reversible - 1 : prev_proposed.end();

   // Promote target policy to pending if the pending slot is available, otherwise
   // copy it to next_proposed
   if (target != prev_proposed.end()) {
      if (pending_slot_open) {
         // promote the target to pending
         auto block_num = next_header_state.block_num();
         next_pending.emplace(block_num, target->second);
         next_header_state.last_pending_finalizer_policy_start_timestamp = next_header_state.timestamp();
      } else {
         // leave the target alone in the proposed policies
         next_proposed.emplace_back(*target);
      }

      if (first_reversible != prev_proposed.end()) {
         // Copy proposed policies with an associated block number greater than that of the target
         // proposed policy (implictly remove any proposed policies with an associated block
         // number less than that of the target proposed policy)
         next_proposed.insert( next_proposed.end(), first_reversible, prev_proposed.end());
      }
   } else {
      // No target proposed policy exists, just copy the previous proposed policies to next
      next_proposed = prev_proposed;
   }
}

void evaluate_proposer_policies_for_promotion(const block_header_state& prev,
                                              block_header_state& next) {
   // next block timestamp must be greater than next timestamp, validated in get_active_proposer_policy_for_block_at
   assert(next.timestamp() > prev.timestamp());

   auto& new_policy = prev.get_active_proposer_policy_for_block_at(next.timestamp());
   if (new_policy != next.active_proposer_policy) {
      next.active_proposer_policy = new_policy;
      if (next.latest_proposed_proposer_policy && new_policy == next.latest_proposed_proposer_policy) {
         next.latest_proposed_proposer_policy = nullptr;
         next.latest_pending_proposer_policy = nullptr;
      } else if (next.latest_pending_proposer_policy && new_policy == next.latest_pending_proposer_policy)
         next.latest_pending_proposer_policy = nullptr;
   }

   if (detail::first_block_of_round(next.timestamp(), prev.timestamp()) &&
      next.latest_proposed_proposer_policy && !next.latest_pending_proposer_policy) {
      next.latest_pending_proposer_policy = next.latest_proposed_proposer_policy;
      next.latest_proposed_proposer_policy = nullptr;
   }
}

// -------------------------------------------------------------------------------------------------
// `finish_next` updates the next `block_header_state` according to the contents of the
// header extensions (either new protocol_features or finality_extension) applicable to this
// next block .
//
// These extensions either result from the execution of the previous block (in case this node
// was the block producer) or were received from the network in a `signed_block`.
// -------------------------------------------------------------------------------------------------
void finish_next(const block_header_state& prev,
                 block_header_state& next_header_state,
                 vector<digest_type> new_protocol_feature_activations,
                 finality_extension f_ext,
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
   next_header_state.latest_proposed_proposer_policy = prev.latest_proposed_proposer_policy;
   next_header_state.latest_pending_proposer_policy = prev.latest_pending_proposer_policy;

   evaluate_proposer_policies_for_promotion(prev, next_header_state);

   if (f_ext.new_proposer_policy_diff) {
      // called when assembling the block
      next_header_state.latest_proposed_proposer_policy =
         std::make_shared<proposer_policy>(prev.get_last_proposed_proposer_policy().apply_diff(*f_ext.new_proposer_policy_diff));
   }

   // finality_core
   // -------------
   block_ref parent_block = prev.make_block_ref();
   next_header_state.core = prev.core.next(parent_block, f_ext.qc_claim);

   // finalizer policy
   // ----------------
   next_header_state.active_finalizer_policy = prev.active_finalizer_policy;

   // will be reset in evaluate_finalizer_policies_for_promotion if needed
   next_header_state.last_pending_finalizer_policy_start_timestamp = prev.last_pending_finalizer_policy_start_timestamp;

   evaluate_finalizer_policies_for_promotion(prev, next_header_state);

   next_header_state.last_pending_finalizer_policy_digest = fc::sha256::hash(next_header_state.get_last_pending_finalizer_policy());

   finalizer_policy new_finalizer_policy;

   if (f_ext.new_finalizer_policy_diff) {
      new_finalizer_policy = prev.get_last_proposed_finalizer_policy().apply_diff(*f_ext.new_finalizer_policy_diff);

      // a new `finalizer_policy` was proposed in this block, and is present in the finality_extension for
      // this new block.
      // Add this new proposal to the `proposed_finalizer_policies` which tracks the in-flight proposals.
      // ------------------------------------------------------------------------------------------------
      EOS_ASSERT(new_finalizer_policy.generation > prev.finalizer_policy_generation, invalid_block_header_extension,
                 "new finalizer policy generation ${n} not greater than previous ${p}", ("n", new_finalizer_policy.generation)("p", prev.finalizer_policy_generation));
      next_header_state.finalizer_policy_generation = new_finalizer_policy.generation;
      next_header_state.proposed_finalizer_policies.emplace_back(
         std::make_pair(next_header_state.block_num(), std::make_shared<finalizer_policy>(new_finalizer_policy)));
   } else {
      next_header_state.finalizer_policy_generation = prev.finalizer_policy_generation;
   }

   // now populate next_header_state.latest_qc_claim_block_active_finalizer_policy
   // this keeps track of the finalizer policy which was active @ latest_qc_claim().block_num, but which
   // can be overwritten by a previously pending policy (member `active_finalizer_policy`)
   // See full explanation in issue #694.
   // --------------------------------------------------------------------------------------------------
   const auto& next_core                 = next_header_state.core;
   auto        latest_qc_claim_block_num = next_core.latest_qc_claim().block_num;
   const auto  active_generation_num     = next_header_state.active_finalizer_policy->generation;
   if (prev.get_active_finalizer_policy_generation(latest_qc_claim_block_num) != active_generation_num) {
      const auto& latest_qc_claim_block_ref = next_header_state.core.get_block_reference(latest_qc_claim_block_num);
      next_header_state.latest_qc_claim_block_active_finalizer_policy =
         prev.get_finalizer_policies(latest_qc_claim_block_ref).active_finalizer_policy;
   } else {
      next_header_state.latest_qc_claim_block_active_finalizer_policy = nullptr;
   }

   // Finally update block id from header
   // -----------------------------------
   next_header_state.block_id = next_header_state.header.calculate_id();

   if (log) {
      auto& id = next_header_state.block_id;

      // Now that we have the block id of the new block, log what changed.
      // -----------------------------------------------------------------
      if (f_ext.new_finalizer_policy_diff) {
         dlog("New finalizer policy proposed in block ${n}:${id}: ${pol}",
               ("n",block_header::num_from_id(id))("id", id)("pol", new_finalizer_policy));
      }

      if (next_header_state.active_finalizer_policy != prev.active_finalizer_policy) {
         const auto& act = next_header_state.active_finalizer_policy;
         ilog("Finalizer policy generation change: ${old_gen} -> ${new_gen}",
              ("old_gen", prev.active_finalizer_policy->generation)("new_gen",act->generation));
         ilog("New finalizer policy becoming active in block ${n}:${id}: ${pol}",
              ("n",block_header::num_from_id(id))("id", id)("pol", *act));
      }

      if (next_header_state.active_proposer_policy->proposer_schedule.version != prev.active_proposer_policy->proposer_schedule.version) {
         const auto& act = next_header_state.active_proposer_policy;
         dlog("Proposer policy version change: ${old_ver} -> ${new_ver}",
              ("old_ver", prev.active_proposer_policy->proposer_schedule.version)("new_ver",act->proposer_schedule.version));
         dlog("New proposer policy becoming active in block ${n}:${id}: ${pol}",
              ("n",block_header::num_from_id(id))("id", id)("pol", *act));
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
   finality_extension new_f_ext { input.most_recent_ancestor_with_qc,
                                  std::move(new_finalizer_policy_diff),
                                  std::move(new_proposer_policy_diff) };

   uint16_t f_ext_id = finality_extension::extension_id();
   emplace_extension(next_header_state.header.header_extensions, f_ext_id, fc::raw::pack(new_f_ext));
   next_header_state.header_exts.emplace(f_ext_id, new_f_ext);

   // add protocol_feature_activation extension
   // -----------------------------------------
   if (!input.new_protocol_feature_activations.empty()) {
      uint16_t ext_id = protocol_feature_activation::extension_id();
      protocol_feature_activation pfa_ext{.protocol_features = input.new_protocol_feature_activations};

      emplace_extension(next_header_state.header.header_extensions, ext_id, fc::raw::pack(pfa_ext));
      next_header_state.header_exts.emplace(ext_id, std::move(pfa_ext));
   }

   finish_next(*this, next_header_state, std::move(input.new_protocol_feature_activations), std::move(new_f_ext), true);

   return next_header_state;
}

/**
 *  Transitions the current header state into the next header state given the supplied signed block header.
 *
 *  Given a signed block header, generate the expected template based upon the header time,
 *  then validate that the provided header matches the template.
 */
block_header_state block_header_state::next(const signed_block_header& h, validator_t& validator) const {
   auto producer = get_producer_for_block_at(h.timestamp).producer_name;
   
   EOS_ASSERT( h.previous == block_id, unlinkable_block_exception,
               "previous mismatch ${p} != ${id}", ("p", h.previous)("id", block_id) );
   EOS_ASSERT( h.producer == producer, wrong_producer, "wrong producer specified" );
   EOS_ASSERT( !h.new_producers, producer_schedule_exception,
               "Block header contains legacy producer schedule outdated by activation of WTMsig Block Signatures" );

   block_header_state next_header_state;
   next_header_state.header = static_cast<const block_header&>(h);
   next_header_state.header_exts = h.validate_and_extract_header_extensions();

   const auto& exts = next_header_state.header_exts;

   // retrieve protocol_feature_activation from incoming block header extension
   // -------------------------------------------------------------------------
   vector<digest_type> new_protocol_feature_activations;
   if (auto  pfa_entry = exts.find(protocol_feature_activation::extension_id()); pfa_entry != exts.end()) {
      auto& pfa_ext   = std::get<protocol_feature_activation>(pfa_entry->second);
      new_protocol_feature_activations = pfa_ext.protocol_features;
      validator( timestamp(), activated_protocol_features->protocol_features, new_protocol_feature_activations );
   }

   // retrieve finality_extension data from block header extension
   // --------------------------------------------------------------------
   auto  f_entry = exts.find(finality_extension::extension_id());
   EOS_ASSERT(f_entry != exts.end(), invalid_block_header_extension,
              "Instant Finality Extension is expected to be present in all block headers after switch to IF");
   const auto& f_ext = std::get<finality_extension>(f_entry->second);

   if (h.is_proper_svnn_block()) {
      // if there is no Finality Tree Root associated with the block,
      // then this needs to validate that h.action_mroot is the empty digest
      auto next_core_metadata = core.next_metadata(f_ext.qc_claim);
      bool no_finality_tree_associated = core.is_genesis_block_num(next_core_metadata.latest_qc_claim_block_num);

      EOS_ASSERT(no_finality_tree_associated == h.action_mroot.empty(), block_validate_exception,
                 "No Finality Tree Root associated with the block, does not match with empty action_mroot: "
                 "(${n}), action_mroot empty (${e}), latest_qc_claim_block_num (${f})",
                 ("n", no_finality_tree_associated)("e", h.action_mroot.empty())
                 ("f", next_core_metadata.latest_qc_claim_block_num));
   };

   finish_next(*this, next_header_state, std::move(new_protocol_feature_activations), f_ext, false);

   return next_header_state;
}

} // namespace eosio::chain

