#pragma once
#include <eosio/chain/block_header.hpp>
#include <eosio/chain/finality_core.hpp>
#include <eosio/chain/protocol_feature_manager.hpp>
#include <eosio/chain/qc.hpp>
#include <eosio/chain/finalizer_policy.hpp>
#include <eosio/chain/finality_extension.hpp>
#include <eosio/chain/chain_snapshot.hpp>
#include <future>

namespace eosio::chain {

namespace snapshot_detail {
  struct snapshot_block_state_v7;
  struct snapshot_block_state_v8;
}

namespace detail { struct schedule_info; };

// Light header protocol version, separate from protocol feature version
constexpr uint32_t light_header_protocol_version_major = 1;
constexpr uint32_t light_header_protocol_version_minor = 0;

// commitments used in the context of finality violation proofs
struct level_3_commitments_t {
   digest_type          reversible_blocks_mroot;
   block_num_type       latest_qc_claim_block_num{0};
   digest_type          latest_qc_claim_finality_digest;
   block_timestamp_type latest_qc_claim_timestamp;
   block_timestamp_type timestamp; // This is the timestamp of the current block.
   digest_type          base_digest;
};

// commitments used in the context of finalizer policy transitions
struct level_2_commitments_t {
   digest_type           last_pending_fin_pol_digest;
   block_timestamp_type  last_pending_fin_pol_start_timestamp;
   digest_type           l3_commitments_digest;
};

// finality digest
struct finality_digest_data_v1 {
   uint32_t    major_version{light_header_protocol_version_major};
   uint32_t    minor_version{light_header_protocol_version_minor};
   uint32_t    active_finalizer_policy_generation{0};
   uint32_t    last_pending_finalizer_policy_generation{0}; // use active_finalizer_policy_generation if pending_finalizer_policy does not exist
   digest_type finality_tree_digest;
   digest_type l2_commitments_digest;
};

// ------------------------------------------------------------------------------------------
// this is used for tracking in-flight `finalizer_policy` changes, which have been requested,
// but are not activated yet. This struct is associated to a block_number in the
// `finalizer_policies` flat_multimap: `block_num => state, finalizer_policy`
//
// When state == proposed, the block_num identifies the block in which the new policy was
// proposed via set_finalizers.
//
// When that block becomes final, according to the block_header_state's finality_core,
// 1. the policy becomes pending
// 2. its key `block_num,` in the proposer_policies multimap, is the current block
//
// When this current block itself becomes final, the policy becomes active.
// ------------------------------------------------------------------------------------------
struct building_block_input {
   block_id_type                     parent_id;
   block_timestamp_type              parent_timestamp;
   block_timestamp_type              timestamp;
   account_name                      producer;
   vector<digest_type>               new_protocol_feature_activations;
};

// this struct can be extracted from a building block
struct block_header_state_input : public building_block_input {
   digest_type                       transaction_mroot;    // Comes from std::get<checksum256_type>(building_block::trx_mroot_or_receipt_digests)
   std::optional<proposer_policy>    new_proposer_policy;  // Comes from building_block::new_proposer_policy
   std::optional<finalizer_policy>   new_finalizer_policy; // Comes from building_block::new_finalizer_policy
   qc_claim_t                        most_recent_ancestor_with_qc; // Comes from traversing branch from parent and calling get_best_qc()
   digest_type                       finality_mroot_claim;
};

struct block_header_state : fc::reflect_init {
   // ------ data members ------------------------------------------------------------
   block_id_type                       block_id;
   block_header                        header;
   protocol_feature_activation_set_ptr activated_protocol_features;

   finality_core                       core;                    // thread safe, not modified after creation

   finalizer_policy_ptr                active_finalizer_policy; // finalizer set + threshold + generation, supports `digest()`
   proposer_policy_ptr                 active_proposer_policy;  // producer authority schedule, supports `digest()`

   /*
    Tracking proposer policy transition is based on https://github.com/AntelopeIO/spring/issues/454

    1. If latest_proposed_proposer_policy is not nullopt and its proposal_time
       indicates that it was proposed in a round before the prior round,
       then maybe promote latest_proposed_proposer_policy to active_proposer_policy
       (maybe because other conditions may also need to apply, to be discussed in a bit).
       If it is promoted, then latest_pending_proposer_policy and latest_proposed_proposer_policy
       should be set to nullopt at this point in time (latest_proposed_proposer_policy
       may still be changed in later steps).
    2. Otherwise, if latest_pending_proposer_policy is not nullopt, maybe promote
       latest_pending_proposer_policy to active_proposer_policy.
       If it is promoted, then latest_pending_proposer_policy should be set to nullopt
       at this point in time (may still be changed in later steps).
    3. If latest_proposed_proposer_policy is not nullopt and latest_pending_proposer_policy
       is now nullopt, latest_proposed_proposer_policy is moved into
       latest_pending_proposer_policy. If it is moved, then latest_proposed_proposer_policy
       should be set to nullopt at this point in time (may still be changed in later steps).
    4. If this block proposes a new proposer policy, that policy should be set
       in latest_proposed_proposer_policy (replacing whatever happened to be there).
    5. An extra condition on steps 1 and 2 above when deciding whether to promote
       latest_pending_proposer_policy or latest_proposed_proposer_policy to
       active_proposer_policy is we should not promote the policy if the proposal_time
       of the policy is greater than the last_final_block_timestamp of the previous block.
    */
   proposer_policy_ptr latest_proposed_proposer_policy;
   proposer_policy_ptr latest_pending_proposer_policy;

   // Track in-flight proposed finalizer policies.
   // When the block associated with a proposed finalizer policy becomes final,
   // it becomes pending.
   std::vector<std::pair<block_num_type, finalizer_policy_ptr>> proposed_finalizer_policies;

   // Track in-flight pending finalizer policy. At most one pending
   // finalizer policy at any moment.
   // When the block associated with the pending finalizer policy becomes final,
   // it becomes active.
   std::optional<std::pair<block_num_type, finalizer_policy_ptr>> pending_finalizer_policy;

   // It may be that the `finality_core` references a finalizer policy generation which is neither the active
   // or pending one. This can happen when a pending policy became active, replacing the previously active
   // policy which would be lost if not tracked in the below member variable.
   // When starting from a snapshot, it is critical that all finalizer policies referenced by the finality core
   // can still be accessed, since they are needed for validating QCs for blocks as far back as core.latest_qc_claim().
   // This pointer can (and will often) be nullptr, which means that a pending finalizer policy did not
   // become active between `core.latest_qc_claim().block_num` and `core.current_block_num()` (inclusive).
   //
   // note: It is also possible for the latest final block (which also is tracked in the finality_core) to have
   // an active finalizer policy that is still not being tracked, but we don't care about that as it is not needed
   // for QC verification.
   finalizer_policy_ptr                latest_qc_claim_block_active_finalizer_policy;

   // generation increases by one each time a new finalizer_policy is proposed in a block
   // It matches the finalizer policy generation most recently included in this block's `finality_extension` or its ancestors
   uint32_t                            finalizer_policy_generation{1};

   // digest of the finalizer policy (which includes the generation number in it) with the greatest generation number
   // in the history of the blockchain so far that is not in proposed state (so either pending or active state)
   digest_type                         last_pending_finalizer_policy_digest;

   // Timestamp of the block at which the last pending finalizer policy first was promoted to pending.
   // If the last pending finalizer policy is the current active finalizer policy, then it is the timestamp of the block at which
   // that active finalizer policy first was promoted to pending. Savanna genesis block it is the genesis block number.
   block_timestamp_type                last_pending_finalizer_policy_start_timestamp;

   // ------ data members caching information available elsewhere ----------------------
   header_extension_multimap           header_exts;     // redundant with the data stored in header


   // ------ functions -----------------------------------------------------------------
   const block_id_type&  id()             const { return block_id; }
   const digest_type     finality_mroot() const { return header.is_proper_svnn_block() ? header.action_mroot : digest_type{}; }
   block_timestamp_type  timestamp()      const { return header.timestamp; }
   account_name          producer()       const { return header.producer; }
   const block_id_type&  previous()       const { return header.previous; }
   uint32_t              block_num()      const { return block_header::num_from_id(previous()) + 1; }
   block_timestamp_type  last_qc_block_timestamp() const {
      auto last_qc_block_num  = core.latest_qc_claim().block_num;
      return core.get_block_reference(last_qc_block_num).timestamp;
   }
   const producer_authority_schedule& active_schedule_auth()  const { return active_proposer_policy->proposer_schedule; }
   const protocol_feature_activation_set_ptr& get_activated_protocol_features() const { return activated_protocol_features; }

   block_header_state next(block_header_state_input& data) const;
   block_header_state next(const signed_block_header& h, validator_t& validator) const;

   digest_type compute_base_digest() const;
   digest_type compute_finality_digest() const;

   block_ref make_block_ref() const {
      return block_ref{block_id, timestamp(), compute_finality_digest(), active_finalizer_policy->generation,
                       pending_finalizer_policy ? pending_finalizer_policy->second->generation : 0};
   }

   // Returns true if the block is a Savanna Genesis Block.
   // This method is applicable to any transition block which is re-classified as a Savanna block.
   bool is_savanna_genesis_block() const { return core.is_genesis_block_num(block_num()); }

   // Returns true if the block is a Proper Savanna Block
   bool is_proper_svnn_block() const { return header.is_proper_svnn_block(); }

   // block descending from this need the provided qc in the block extension
   bool is_needed(const qc_claim_t& qc_claim) const {
      return qc_claim > core.latest_qc_claim();
   }

   const vector<digest_type>& get_new_protocol_feature_activations() const;
   // returns active_proposer_policy used by child block built at `next_block_timestamp`
   const proposer_policy_ptr& get_active_proposer_policy_for_block_at(block_timestamp_type next_block_timestamp) const;
   // returns producer using current active proposer policy
   const producer_authority& get_scheduled_producer(block_timestamp_type t) const;
   // returns producer of child block built at `next_block_timestamp`
   const producer_authority& get_producer_for_block_at(block_timestamp_type next_block_timestamp) const;
   // returns current pending policy, nullptr if not existing
   const producer_authority_schedule* pending_producers() const;

   const finalizer_policy& get_last_proposed_finalizer_policy() const;
   const finalizer_policy& get_last_pending_finalizer_policy() const;
   const proposer_policy& get_last_proposed_proposer_policy() const;

   // Only defined for core.latest_qc_claim().block_num <= num <= core.current_block_num()
   finalizer_policies_t get_finalizer_policies(const block_ref& ref) const;

   // Defined for core.last_final_block_num().block_num <= num <= core.current_block_num()
   uint32_t get_active_finalizer_policy_generation(block_num_type block_num) const;

   template<typename Ext> const Ext* header_extension() const {
      if (auto itr = header_exts.find(Ext::extension_id()); itr != header_exts.end()) {
         return &std::get<Ext>(itr->second);
      }
      return nullptr;
   }

   void reflector_init() {
      header_exts = header.validate_and_extract_header_extensions();
   }
};

using block_header_state_ptr = std::shared_ptr<block_header_state>;

}

FC_REFLECT( eosio::chain::block_header_state, (block_id)(header)
            (activated_protocol_features)(core)(active_finalizer_policy)
            (active_proposer_policy)(latest_proposed_proposer_policy)(latest_pending_proposer_policy)(proposed_finalizer_policies)
            (pending_finalizer_policy)(latest_qc_claim_block_active_finalizer_policy)(finalizer_policy_generation)
            (last_pending_finalizer_policy_digest)(last_pending_finalizer_policy_start_timestamp))

FC_REFLECT( eosio::chain::level_3_commitments_t, (reversible_blocks_mroot)(latest_qc_claim_block_num )(latest_qc_claim_finality_digest)(latest_qc_claim_timestamp)(timestamp)(base_digest))
FC_REFLECT( eosio::chain::level_2_commitments_t, (last_pending_fin_pol_digest)(last_pending_fin_pol_start_timestamp)(l3_commitments_digest) )
FC_REFLECT( eosio::chain::finality_digest_data_v1, (major_version)(minor_version)(active_finalizer_policy_generation)(last_pending_finalizer_policy_generation)(finality_tree_digest)(l2_commitments_digest) )
