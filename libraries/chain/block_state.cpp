#include <eosio/chain/block_state.hpp>
#include <eosio/chain/block_header_state_utils.hpp>
#include <eosio/chain/block_state_legacy.hpp>
#include <eosio/chain/finalizer.hpp>
#include <eosio/chain/snapshot_detail.hpp>
#include <eosio/chain/exceptions.hpp>

#include <fc/crypto/bls_utils.hpp>

namespace eosio::chain {

namespace detail {

   inline void verify_signee(const signature_type& producer_signature, const block_id_type& block_id,
                             const std::vector<signature_type>& additional_signatures,
                             const block_signing_authority& valid_block_signing_authority)
   {
      auto num_keys_in_authority = std::visit([](const auto& a) { return a.keys.size(); },
                                              valid_block_signing_authority);
      EOS_ASSERT(1 + additional_signatures.size() <= num_keys_in_authority, wrong_signing_key,
                 "number of block signatures (${num_block_signatures}) exceeds number of keys (${num_keys}) in block"
                 " signing authority: ${authority}",
                 ("num_block_signatures", 1 + additional_signatures.size())("num_keys", num_keys_in_authority)
                 ("authority", valid_block_signing_authority));

      std::set<public_key_type> keys;
      keys.emplace(fc::crypto::public_key(producer_signature, block_id, true));

      for (const auto& s : additional_signatures) {
         auto res = keys.emplace(s, block_id, true);
         EOS_ASSERT(res.second, wrong_signing_key, "block signed by same key twice: ${key}", ("key", *res.first));
      }

      bool is_satisfied = false;
      size_t relevant_sig_count = 0;

      std::tie(is_satisfied, relevant_sig_count) = producer_authority::keys_satisfy_and_relevant(
         keys, valid_block_signing_authority);

      EOS_ASSERT(relevant_sig_count == keys.size(), wrong_signing_key,
                 "block signed by unexpected key: ${signing_keys}, expected: ${authority}. ${c} != ${s}",
                 ("signing_keys", keys)("authority", valid_block_signing_authority)("c", relevant_sig_count)("s", keys. size()));

      EOS_ASSERT(is_satisfied, wrong_signing_key,
                 "block signatures ${signing_keys} do not satisfy the block signing authority: ${authority}",
                 ("signing_keys", keys)("authority", valid_block_signing_authority));
   }

   // EOS_ASSERTs if signature does not validate
   inline bool verify_block_sig(const block_header_state& prev, const signed_block_ptr& block, bool skip_validate_signee) {
      if (!skip_validate_signee) {
         auto sigs = detail::extract_additional_signatures(block);
         const auto& valid_block_signing_authority = prev.get_producer_for_block_at(block->timestamp).authority;
         verify_signee(block->producer_signature, block->calculate_id(), sigs, valid_block_signing_authority);
      }
      return true;
   };

   void inject_additional_signatures( signed_block& b, const std::vector<signature_type>& additional_signatures) {
      if (!additional_signatures.empty()) {
         // as an optimization we don't copy this out into the legitimate extension structure as it serializes
         // the same way as the vector of signatures
         static_assert(fc::reflector<additional_block_signatures_extension>::total_member_count == 1);
         static_assert(std::is_same_v<decltype(additional_block_signatures_extension::signatures), std::vector<signature_type>>);

         emplace_extension(b.block_extensions, additional_block_signatures_extension::extension_id(),
                           fc::raw::pack(additional_signatures));
      }
   }

   void sign(signed_block& block, const block_id_type& block_id,
             const signer_callback_type& signer, const block_signing_authority& valid_block_signing_authority) {
      auto sigs = signer(block_id);

      EOS_ASSERT(!sigs.empty(), no_block_signatures, "Signer returned no signatures");
      block.producer_signature = sigs.back();
      // last is producer signature, rest are additional signatures to inject in the block extension
      sigs.pop_back();

      verify_signee(block.producer_signature, block_id, sigs, valid_block_signing_authority);
      inject_additional_signatures(block, sigs);
   }

} // namespace detail

using namespace eosio::chain::detail;

// ASSUMPTION FROM controller_impl::apply_block = all untrusted blocks will have their signatures pre-validated here
block_state::block_state(const block_header_state& prev, signed_block_ptr b, const protocol_feature_set& pfs,
                         const validator_t& validator, bool skip_validate_signee)
   : block_header_state((verify_block_sig(prev, b, skip_validate_signee), prev.next(*b, validator)))
   , block(std::move(b))
   , strong_digest(compute_finality_digest())
   , weak_digest(create_weak_digest(strong_digest))
   , aggregating_qc(active_finalizer_policy, pending_finalizer_policy ? pending_finalizer_policy->second : finalizer_policy_ptr{})
{
}

block_state::block_state(const block_header_state&                bhs,
                         deque<transaction_metadata_ptr>&&        trx_metas,
                         deque<transaction_receipt>&&             trx_receipts,
                         const std::optional<valid_t>&            valid,
                         const std::optional<qc_t>&               qc,
                         const signer_callback_type&              signer,
                         const block_signing_authority&           valid_block_signing_authority,
                         const digest_type&                       action_mroot)
   : block_header_state(bhs)
   , block()
   , strong_digest(compute_finality_digest())
   , weak_digest(create_weak_digest(strong_digest))
   , aggregating_qc(active_finalizer_policy, pending_finalizer_policy ? pending_finalizer_policy->second : finalizer_policy_ptr{})
   , valid(valid)
   , pub_keys_recovered(true) // called by produce_block so signature recovery of trxs must have been done
   , cached_trxs(std::move(trx_metas))
   , action_mroot(action_mroot)
{
   mutable_block_ptr new_block = signed_block::create_mutable_block(signed_block_header{bhs.header});
   new_block->transactions = std::move(trx_receipts);

   if( qc ) {
      fc_dlog(vote_logger, "integrate qc ${qc} into block ${bn} ${id}", ("qc", qc->to_qc_claim())("bn", block_num())("id", id()));
      emplace_extension(new_block->block_extensions,
                        quorum_certificate_extension::extension_id(), fc::raw::pack( *qc ));
   }

   sign(*new_block, block_id, signer, valid_block_signing_authority);

   block = signed_block::create_signed_block(std::move(new_block));
}

// Used for transition from dpos to Savanna.
block_state_ptr block_state::create_if_genesis_block(const block_state_legacy& bsp) {
   dlog("Create if genesis block ${bn}", ("bn", bsp.block_num()));
   assert(bsp.action_mroot_savanna);

   auto result_ptr = std::make_shared<block_state>();
   auto &result = *result_ptr;

   // set block_header_state data ----
   result.block_id = bsp.id();
   result.header = bsp.header;
   result.activated_protocol_features = bsp.activated_protocol_features;

   assert(bsp.header.contains_header_extension(finality_extension::extension_id())); // required by transition mechanism
   finality_extension f_ext = bsp.header.extract_header_extension<finality_extension>();
   assert(f_ext.new_finalizer_policy_diff); // required by transition mechanism
   result.active_finalizer_policy = std::make_shared<finalizer_policy>(finalizer_policy{}.apply_diff(std::move(*f_ext.new_finalizer_policy_diff)));

   result.core = finality_core::create_core_for_genesis_block(bsp.id(), bsp.timestamp());

   result.last_pending_finalizer_policy_digest = fc::sha256::hash(*result.active_finalizer_policy);
   result.last_pending_finalizer_policy_start_timestamp = bsp.timestamp();
   result.active_proposer_policy = std::make_shared<proposer_policy>();
   result.active_proposer_policy->proposer_schedule = bsp.active_schedule;
   result.latest_proposed_proposer_policy = {}; // none pending at IF genesis block
   result.latest_pending_proposer_policy = {}; // none pending at IF genesis block
   result.proposed_finalizer_policies = {}; // none proposed at IF genesis block
   result.pending_finalizer_policy = std::nullopt; // none pending at IF genesis block
   result.finalizer_policy_generation = 1;
   result.header_exts = bsp.header_exts;

   // set block_state data ----
   result.block = bsp.block;
   result.strong_digest = result.compute_finality_digest(); // all block_header_state data populated in result at this point
   result.weak_digest = create_weak_digest(result.strong_digest);

   // aggregating_qc will not be used in the genesis block as finalizers will not vote on it, but still create it for consistency.
   result.aggregating_qc = aggregating_qc_t{result.active_finalizer_policy, finalizer_policy_ptr{}};

   // build leaf_node and validation_tree
   valid_t::finality_leaf_node_t leaf_node {
      .block_num        = bsp.block_num(),
      .timestamp        = bsp.timestamp(),
      .parent_timestamp = block_timestamp_type(), // for the genesis block, the parent_timestamp is the the earliest representable timestamp.
      .finality_digest  = result.strong_digest,
      .action_mroot     = *bsp.action_mroot_savanna
   };

   // construct valid structure
   incremental_merkle_tree validation_tree;
   validation_tree.append(fc::sha256::hash(leaf_node));
   result.valid = valid_t {
      .validation_tree   = validation_tree,
      .validation_mroots = { validation_tree.get_root() }
   };

   result.validated.store(bsp.is_valid());
   result.pub_keys_recovered = bsp._pub_keys_recovered;
   result.cached_trxs = bsp._cached_trxs;
   result.action_mroot = *bsp.action_mroot_savanna;
   result.base_digest = {}; // calculated on demand in get_finality_data()

   return result_ptr;
}

block_state_ptr block_state::create_transition_block(
                   const block_state&                prev,
                   signed_block_ptr                  b,
                   const protocol_feature_set&       pfs,
                   const validator_t&                validator,
                   bool                              skip_validate_signee,
                   const std::optional<digest_type>& action_mroot_savanna) {
   dlog("Create transition block ${bn}", ("bn", prev.block_num()+1));
   auto result_ptr = std::make_shared<block_state>(prev, b, pfs, validator, skip_validate_signee);

   result_ptr->action_mroot = action_mroot_savanna.has_value() ? *action_mroot_savanna : digest_type();
   // action_mroot_savanna can be empty in IRREVERSIBLE mode. Do not create valid structure
   // if action_mroot is empty.
   if( !result_ptr->action_mroot.empty() ) {
     result_ptr->valid = prev.new_valid(*result_ptr, result_ptr->action_mroot, result_ptr->strong_digest);
   }

   return result_ptr;
}

// Spring 1.0.1 to ? snapshot v8 format. Updated `finality_core` to include finalizer policies
// generation numbers. Also new member `block_state::latest_qc_claim_block_active_finalizer_policy`
// ------------------------------------------------------------------------------------------------
block_state::block_state(snapshot_detail::snapshot_block_state_v8&& sbs)
   : block_header_state {
         .block_id                        = sbs.block_id,
         .header                          = std::move(sbs.header),
         .activated_protocol_features     = std::move(sbs.activated_protocol_features),
         .core                            = std::move(sbs.core),
         .active_finalizer_policy         = std::move(sbs.active_finalizer_policy),
         .active_proposer_policy          = std::move(sbs.active_proposer_policy),
         .latest_proposed_proposer_policy = std::move(sbs.latest_proposed_proposer_policy),
         .latest_pending_proposer_policy  = std::move(sbs.latest_pending_proposer_policy),
         .proposed_finalizer_policies     = std::move(sbs.proposed_finalizer_policies),
         .pending_finalizer_policy        = std::move(sbs.pending_finalizer_policy),
         .latest_qc_claim_block_active_finalizer_policy = std::move(sbs.latest_qc_claim_block_active_finalizer_policy),
         .finalizer_policy_generation     = sbs.finalizer_policy_generation,
         .last_pending_finalizer_policy_digest = sbs.last_pending_finalizer_policy_digest,
         .last_pending_finalizer_policy_start_timestamp = sbs.last_pending_finalizer_policy_start_timestamp
      }
   , strong_digest(compute_finality_digest())
   , weak_digest(create_weak_digest(strong_digest))
   , aggregating_qc(active_finalizer_policy,
                    pending_finalizer_policy ? pending_finalizer_policy->second : finalizer_policy_ptr{}) // just in case we receive votes
   , valid(std::move(sbs.valid))
{
   header_exts = header.validate_and_extract_header_extensions();
}

deque<transaction_metadata_ptr> block_state::extract_trxs_metas() {
   pub_keys_recovered = false;
   auto result = std::move(cached_trxs);
   cached_trxs.clear();
   return result;
}

void block_state::set_trxs_metas( deque<transaction_metadata_ptr>&& trxs_metas, bool keys_recovered ) {
   pub_keys_recovered = keys_recovered;
   cached_trxs = std::move( trxs_metas );
}

// Called from vote threads
aggregate_vote_result_t block_state::aggregate_vote(uint32_t connection_id, const vote_message& vote) {
   auto finalizer_digest = vote.strong ? strong_digest.to_uint8_span() : std::span<const uint8_t>(weak_digest);
   return aggregating_qc.aggregate_vote(connection_id, vote, block_id, finalizer_digest);
}

// Only used for testing
vote_status_t block_state::has_voted(const bls_public_key& key) const {
   return aggregating_qc.has_voted(key);
}

// Called from net threads
void block_state::verify_qc_signatures(const qc_t& qc) const {
   finalizer_policies_t policies = get_finalizer_policies(qc.block_num); // get policies active at claimed block number
   qc.verify_signatures(policies);
}

// Called from net threads
void block_state::verify_qc_basic(const qc_t& qc) const {
   finalizer_policies_t policies = get_finalizer_policies(qc.block_num); // get policies active at claimed block number
   qc.verify_basic(policies);
}

void block_state::verify_qc(const qc_t& qc) const {
   finalizer_policies_t policies = get_finalizer_policies(qc.block_num); // get policies active at claimed block number
   qc.verify_basic(policies);
   qc.verify_signatures(policies);
}

qc_claim_t block_state::extract_qc_claim() const {
   if (auto itr = header_exts.find(finality_extension::extension_id()); itr != header_exts.end())
      return std::get<finality_extension>(itr->second).qc_claim;
   return {};
}

valid_t block_state::new_valid(const block_header_state& next_bhs, const digest_type& action_mroot, const digest_type& strong_digest) const {
   assert(valid);
   assert(next_bhs.core.last_final_block_num() >= core.last_final_block_num());
   assert(!strong_digest.empty());

   // Copy parent's validation_tree and validation_mroots.
   auto start = next_bhs.core.last_final_block_num() - core.last_final_block_num();
   valid_t next_valid {
      .validation_tree = valid->validation_tree,
      // Trim roots from the front end, up to block number `next_bhs.core.last_final_block_num()`
      .validation_mroots = { valid->validation_mroots.cbegin() + start, valid->validation_mroots.cend() }
   };

   // construct block's finality leaf node.
   valid_t::finality_leaf_node_t leaf_node{
      .block_num        = next_bhs.block_num(),
      .timestamp        = next_bhs.timestamp(),
      .parent_timestamp = timestamp(),
      .finality_digest  = strong_digest,
      .action_mroot     = action_mroot
   };

   auto leaf_node_digest = fc::sha256::hash(leaf_node);

   // append new finality leaf node digest to validation_tree
   next_valid.validation_tree.append(leaf_node_digest);

   // append the root of the new Validation Tree to validation_mroots.
   next_valid.validation_mroots.emplace_back(next_valid.validation_tree.get_root());

   // post condition of validation_mroots
   assert(next_valid.validation_mroots.size() == (next_bhs.block_num() - next_bhs.core.last_final_block_num() + 1));

   return next_valid;
}

digest_type block_state::get_validation_mroot(block_num_type target_block_num) const {
   if (!valid) {
      return digest_type{};
   }

   assert(valid->validation_mroots.size() > 0);
   auto low  = core.last_final_block_num();
   auto high = low + valid->validation_mroots.size();
   EOS_ASSERT(low <= target_block_num && target_block_num < high, block_validate_exception,
              "target_block_num ${b} is outside of range of ${low} and ${high}",
              ("b", target_block_num)("low", low)("high", high));

   return valid->validation_mroots[target_block_num - low];
}

digest_type block_state::get_finality_mroot_claim(const qc_claim_t& qc_claim) const {
   auto next_core_metadata = core.next_metadata(qc_claim);

   // For proper IF blocks that do not have an associated Finality Tree defined
   if (core.is_genesis_block_num(next_core_metadata.latest_qc_claim_block_num)) {
      return digest_type{};
   }

   return get_validation_mroot(next_core_metadata.latest_qc_claim_block_num);
}

finality_data_t block_state::get_finality_data() {
   if (!base_digest) {
      base_digest = compute_base_digest(); // cache it
   }

   auto latest_qc_claim_block_num = core.latest_qc_claim().block_num;
   block_ref blk_ref{};  // Savanna Genesis does not have block_ref
   std::optional<finalizer_policy_with_string_key> pending_fin_pol;

   if (is_savanna_genesis_block()) {
      // For Genesis Block, use the active finalizer policy which went through
      // proposed to pending to active in the single block.
      pending_fin_pol = finalizer_policy_with_string_key(*active_finalizer_policy);
   } else {
      // Check if there is a finalizer policy promoted to pending in the block
      if (pending_finalizer_policy.has_value() && pending_finalizer_policy->first == block_num()) {
         // The `first` element of `pending_finalizer_policy` pair is the block number
         // when the policy becomes pending
         pending_fin_pol = finalizer_policy_with_string_key(*pending_finalizer_policy->second);
      }

      blk_ref = core.get_block_reference(latest_qc_claim_block_num);
   }

   return {
      // major_version and minor_version take the default values set by finality_data_t definition
      .active_finalizer_policy_generation       = active_finalizer_policy->generation,
      .action_mroot                             = action_mroot,
      .reversible_blocks_mroot                  = core.get_reversible_blocks_mroot(),
      .latest_qc_claim_block_num                = latest_qc_claim_block_num,
      .latest_qc_claim_finality_digest          = blk_ref.finality_digest,
      .latest_qc_claim_timestamp                = blk_ref.timestamp,
      .base_digest                              = *base_digest,
      .pending_finalizer_policy                 = std::move(pending_fin_pol),
      .last_pending_finalizer_policy_generation = get_last_pending_finalizer_policy().generation
   };
}

} /// eosio::chain
