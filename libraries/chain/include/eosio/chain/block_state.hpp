#pragma once

#include <eosio/chain/block_header_state.hpp>
#include <eosio/chain/block.hpp>
#include <eosio/chain/transaction_metadata.hpp>
#include <eosio/chain/action_receipt.hpp>
#include <eosio/chain/incremental_merkle.hpp>
#include <eosio/chain/thread_utils.hpp>

namespace eosio::chain {

struct vote_message;

using signer_callback_type = std::function<std::vector<signature_type>(const digest_type&)>;

struct block_state_legacy;
struct block_state_accessor;

/*
 * Important concepts:
 * 1. A Finality Merkle Tree is a Merkle tree over a sequence of Finality Leaf Nodes,
 *    one for each block starting from the IF Genesis Block and ending at some
 *    specified descendant block.
 * 2. The Validation Tree associated with a target block is the Finality Merkle
 *    Tree over Finality Leaf Nodes starting with the one for the IF Genesis Block
 *    and ending with the one for the target Block.
 * 3. The Finality Tree associated with a target block is the Validation Tree of the
 *    block referenced by the target block's latest_qc_claim__block_num.
 *    That is, validation_tree(core.latest_qc_claim().block_num))
 * */
struct valid_t {
   struct finality_leaf_node_t {
      uint32_t             major_version{light_header_protocol_version_major};
      uint32_t             minor_version{light_header_protocol_version_minor};
      block_num_type       block_num{0};     // the block number
      block_timestamp_type timestamp;
      block_timestamp_type parent_timestamp;
      digest_type          finality_digest;  // finality digest for the block
      digest_type          action_mroot;     // digest of the root of the action Merkle tree of the block
   };

   // The Finality Merkle Tree, containing leaf nodes from IF genesis block to current block
   incremental_merkle_tree validation_tree;

   // The sequence of root digests of the validation trees associated
   // with an unbroken sequence of blocks consisting of the blocks
   // starting with the one that has a block number equal
   // to core.last_final_block_num, and ending with the current block
   std::vector<digest_type> validation_mroots;
};

// This is mostly used by SHiP & deep-mind to stream finality_data
struct finality_data_t {
   uint32_t     major_version{light_header_protocol_version_major};
   uint32_t     minor_version{light_header_protocol_version_minor};
   uint32_t     active_finalizer_policy_generation{0};
   digest_type  action_mroot{};
   digest_type  reversible_blocks_mroot{};
   block_num_type       latest_qc_claim_block_num{0};
   digest_type          latest_qc_claim_finality_digest;
   block_timestamp_type latest_qc_claim_timestamp;
   digest_type          base_digest{};

   // Finalizer policy if one is promoted to pending in the block.
   // Use string format for public key in the policy for easier uses.
   std::optional<finalizer_policy_with_string_key> pending_finalizer_policy;

   uint32_t     last_pending_finalizer_policy_generation{0}; // // use active_finalizer_policy_generation if pending_finalizer_policy does not exist
};

struct block_state : public block_header_state {     // block_header_state provides parent link
   // ------ data members -------------------------------------------------------------
   signed_block_ptr           block;
   digest_type                strong_digest;         // finalizer_digest (strong, cached so we can quickly validate votes)
   weak_digest_t              weak_digest;           // finalizer_digest (weak, cached so we can quickly validate votes)
   aggregating_qc_t           aggregating_qc;        // where we accumulate votes we receive
   std::optional<valid_t>     valid;

   // ------ updated for votes, used for fork_db ordering ------------------------------
private:
   copyable_atomic<bool>      validated{false};     // We have executed the block's trxs and verified that action merkle root (block id) matches.

   // ------ data members caching information available elsewhere ----------------------
   bool                       pub_keys_recovered = false;
   deque<transaction_metadata_ptr> cached_trxs;
   digest_type                action_mroot; // For finality_data sent to SHiP
   std::optional<digest_type> base_digest;  // For finality_data sent to SHiP, computed on demand in get_finality_data()

   // ------ private methods -----------------------------------------------------------
   bool                                is_pub_keys_recovered() const { return pub_keys_recovered; }
   deque<transaction_metadata_ptr>     extract_trxs_metas();
   void                                set_trxs_metas(deque<transaction_metadata_ptr>&& trxs_metas, bool keys_recovered);
   const deque<transaction_metadata_ptr>& trxs_metas()  const { return cached_trxs; }

   friend struct test_block_state_accessor;
   friend struct fc::reflector<block_state>;
   friend struct controller_impl;
   friend struct completed_block;
   friend struct building_block;

public:
   // ------ functions -----------------------------------------------------------------
   const block_id_type&   id()                const { return block_header_state::id(); }
   const block_id_type&   previous()          const { return block_header_state::previous(); }
   uint32_t               block_num()         const { return block_header_state::block_num(); }
   block_timestamp_type   timestamp()         const { return block_header_state::timestamp(); }
   const extensions_type& header_extensions() const { return block_header_state::header.header_extensions; }
   uint32_t               irreversible_blocknum() const { return core.last_final_block_num(); }

   uint32_t               last_final_block_num() const         { return core.last_final_block_num(); }
   block_timestamp_type   last_final_block_timestamp() const   { return core.last_final_block_timestamp(); }

   uint32_t               latest_qc_block_num() const          { return core.latest_qc_claim().block_num; }
   block_timestamp_type   latest_qc_block_timestamp() const    { return core.latest_qc_block_timestamp(); }

   void                   set_valid(bool v) { validated.store(v); }
   bool                   is_valid() const  { return validated.load(); }

   std::optional<qc_t> get_best_qc() const { return aggregating_qc.get_best_qc(block_num()); } // thread safe
   bool received_qc_is_strong() const { return aggregating_qc.received_qc_is_strong(); } // thread safe
   // return true if better qc, thread safe
   bool set_received_qc(const qc_t& qc) { return aggregating_qc.set_received_qc(qc); }
   // extract the qc_claim from block header finality_extension
   qc_claim_t extract_qc_claim() const;

   // heuristic for determination if we are syncing or replaying for optimizations
   bool is_recent() const {
      return timestamp() > fc::time_point::now() - fc::seconds(30);
   }

   // use the cached `finality_digest`
   block_ref make_block_ref() const {
      return block_ref{block_id, timestamp(), strong_digest, active_finalizer_policy->generation,
                       pending_finalizer_policy ? pending_finalizer_policy->second->generation : 0};
   }

   protocol_feature_activation_set_ptr get_activated_protocol_features() const { return block_header_state::activated_protocol_features; }
   // build next valid structure from current one with input of next
   valid_t new_valid(const block_header_state& bhs, const digest_type& action_mroot, const digest_type& strong_digest) const;

   // Returns the root digest of the finality tree associated with the target_block_num
   // [core.last_final_block_num, block_num]
   digest_type get_validation_mroot( block_num_type target_block_num ) const;

   // Returns finality_mroot_claim of the current block
   digest_type get_finality_mroot_claim(const qc_claim_t& qc_claim) const;

   // Returns finality_data of the current block
   finality_data_t get_finality_data();

   // connection_id only for logging
   aggregate_vote_result_t aggregate_vote(uint32_t connection_id, const vote_message& vote); // aggregate vote into aggregating_qc
   vote_status_t has_voted(const bls_public_key& key) const;

   void verify_qc_signatures(const qc_t& qc) const; // validate qc signatures (slow)
   void verify_qc_basic(const qc_t& qc) const;      // do basic checks on provided qc, excluding signature verification
   void verify_qc(const qc_t& qc) const;            // full qc verification incl. signatures

   using bhs_t  = block_header_state;
   using bhsp_t = block_header_state_ptr;

   block_state() = default;
   block_state(const block_state&) = delete;
   block_state(block_state&&) = default;

   block_state(const block_header_state& prev, signed_block_ptr b, const protocol_feature_set& pfs,
               const validator_t& validator, bool skip_validate_signee);

   block_state(const block_header_state&                bhs,
               deque<transaction_metadata_ptr>&&        trx_metas,
               deque<transaction_receipt>&&             trx_receipts,
               const std::optional<valid_t>&            valid,
               const std::optional<qc_t>&               qc,
               const signer_callback_type&              signer,
               const block_signing_authority&           valid_block_signing_authority,
               const digest_type&                       action_mroot);

   static std::shared_ptr<block_state> create_if_genesis_block(const block_state_legacy& bsp);

   // Constructs a Transition Savanna block state from a Legacy block state.
   static std::shared_ptr<block_state> create_transition_block(
         const block_state&                prev,
         signed_block_ptr                  b,
         const protocol_feature_set&       pfs,
         const validator_t&                validator,
         bool                              skip_validate_signee,
         const std::optional<digest_type>& action_mroot_savanna);

   explicit block_state(snapshot_detail::snapshot_block_state_v8&& sbs);

   // Only defined for latest_qc_block_num() <= num <= block_num()
   finalizer_policies_t get_finalizer_policies(block_num_type num) const {
      if (num == block_num())
         return block_header_state::get_finalizer_policies(make_block_ref());
      return block_header_state::get_finalizer_policies(core.get_block_reference(num));
   }
};

using block_state_ptr       = std::shared_ptr<block_state>;
using block_state_pair      = std::pair<std::shared_ptr<block_state_legacy>, block_state_ptr>;

} // namespace eosio::chain

// not exporting pending_qc or valid_qc
FC_REFLECT( eosio::chain::valid_t::finality_leaf_node_t, (major_version)(minor_version)(block_num)(timestamp)(parent_timestamp)(finality_digest)(action_mroot) )
FC_REFLECT( eosio::chain::valid_t, (validation_tree)(validation_mroots))
FC_REFLECT( eosio::chain::finality_data_t, (major_version)(minor_version)(active_finalizer_policy_generation)(action_mroot)(reversible_blocks_mroot)(latest_qc_claim_block_num)(latest_qc_claim_finality_digest)(latest_qc_claim_timestamp)(base_digest)(pending_finalizer_policy)(last_pending_finalizer_policy_generation) )
FC_REFLECT_DERIVED( eosio::chain::block_state, (eosio::chain::block_header_state), (block)(strong_digest)(weak_digest)(aggregating_qc)(valid)(validated) )
