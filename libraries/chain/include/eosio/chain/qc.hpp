#pragma once

#include <eosio/chain/finality_core.hpp>
#include <eosio/chain/finalizer_policy.hpp>
#include <eosio/chain/vote_message.hpp>
#include <eosio/chain/block_timestamp.hpp>
#include <eosio/chain/thread_utils.hpp>
#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/bls_signature.hpp>
#include <fc/bitutil.hpp>
#include <array>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

namespace eosio::chain {

   using bls_public_key          = fc::crypto::blslib::bls_public_key;
   using bls_signature           = fc::crypto::blslib::bls_signature;
   using bls_aggregate_signature = fc::crypto::blslib::bls_aggregate_signature;
   using bls_private_key         = fc::crypto::blslib::bls_private_key;

   using vote_bitset_t = fc::dynamic_bitset;
   using bls_key_map_t = std::map<bls_public_key, bls_private_key>;

   constexpr std::array weak_bls_sig_postfix = { 'W', 'E', 'A', 'K' };
   using weak_digest_t = std::array<uint8_t, sizeof(digest_type) + weak_bls_sig_postfix.size()>;

   inline weak_digest_t create_weak_digest(const digest_type& digest) {
      weak_digest_t res;
      std::memcpy(res.begin(), digest.data(), digest.data_size());
      std::memcpy(res.begin() + digest.data_size(), weak_bls_sig_postfix.data(), weak_bls_sig_postfix.size());
      return res;
   }

   struct finalizer_policies_t {
      digest_type          finality_digest;
      finalizer_policy_ptr active_finalizer_policy;  // Never null
      finalizer_policy_ptr pending_finalizer_policy; // Only null if the block has no pending finalizer policy
   };

   enum class vote_result_t {
      success,
      duplicate,             // duplicate vote, expected as votes arrive on multiple connections
      unknown_public_key,    // public key is invalid, indicates invalid vote
      invalid_signature,     // signature is invalid, indicates invalid vote
      unknown_block,         // block not available, possibly less than LIB, or too far in the future
      max_exceeded           // received too many votes for a connection
   };

   enum class vote_status_t {
      voted,
      not_voted,
      irrelevant_finalizer
   };

   struct aggregate_vote_result_t {
      vote_result_t           result{vote_result_t::unknown_public_key};
      finalizer_authority_ptr active_authority;
      finalizer_authority_ptr pending_authority;
   };

   struct qc_sig_t {
      std::optional<vote_bitset_t> strong_votes;
      std::optional<vote_bitset_t> weak_votes;
      bls_aggregate_signature      sig;

      bool is_weak()   const { return !!weak_votes; }
      bool is_strong() const { return !weak_votes; }

      // called from net threads
      void verify_signatures(const finalizer_policy_ptr& fin_policy, const digest_type& strong_digest, const weak_digest_t& weak_digest) const;

      // called from net threads
      void verify_weights(const finalizer_policy_ptr& fin_policy) const;

      void verify_vote_format(const finalizer_policy_ptr& fin_policy) const;

      // returns true if vote indicated by my_vote_index in strong_votes or
      // weak_votes are is the same as the one indicated by other_vote_index
      // in `other` signature's strong_votes or weak_votes
      bool vote_same_at(const qc_sig_t& other, uint32_t my_vote_index, uint32_t other_vote_index) const;
   };

   struct qc_t {
      uint32_t                 block_num{0};
      qc_sig_t                 active_policy_sig;  // signatures for the active finalizer policy
      std::optional<qc_sig_t>  pending_policy_sig; // signatures for the pending finalizer policy (if any)

      bool is_strong() const {
         return active_policy_sig.is_strong() && (!pending_policy_sig || pending_policy_sig->is_strong());
      }
      bool is_weak() const { return !is_strong(); }

      qc_claim_t to_qc_claim() const {
         return { .block_num = block_num, .is_strong_qc = is_strong() };
      }

      void verify_signatures(const finalizer_policies_t& policies) const; // validate qc signatures
      void verify_basic(const finalizer_policies_t& policies) const;      // do basic checks on provided qc, excluding signature verification
   };

   struct qc_data_t {
      std::optional<qc_t> qc;  // Comes either from traversing branch from parent and calling get_best_qc()
                               // or from an incoming block extension.
      qc_claim_t qc_claim;     // describes the above qc_t. In rare cases (bootstrap, starting from snapshot,
                               // disaster recovery), we may not have a qc_t so we use the `lib` block_num
                               // and specify `weak`.
   };

   /**
    * All public methods are thread-safe.
    * Used for incorporating votes into a qc signature.
    * "aggregating" in that it allows new votes to be added at any time.
    */
   class aggregating_qc_sig_t {
   public:
      enum class state_t {
         unrestricted,  // No quorum reached yet, still possible to achieve any state.
         restricted,    // Enough `weak` votes received to know it is impossible to reach the `strong` state.
         weak_achieved, // Enough `weak` + `strong` votes for a valid `weak` QC, still possible to reach the `strong` state.
         weak_final,    // Enough `weak` + `strong` votes for a valid `weak` QC, `strong` not possible anymore.
         strong         // Enough `strong` votes to have a valid `strong` QC
      };

      struct votes_t : fc::reflect_init {
      private:
         friend struct fc::reflector<votes_t>;
         friend struct fc::reflector_init_visitor<votes_t>;
         friend struct fc::has_reflector_init<votes_t>;
         friend class aggregating_qc_sig_t;
         struct bit_processed {
            alignas(hardware_destructive_interference_sz)
            std::atomic<bool> value;
         };

         vote_bitset_t                  bitset;
         bls_aggregate_signature        sig;
         std::vector<bit_processed>     processed; // avoid locking mutex for bitset duplicate check

         void reflector_init();
      public:
         explicit votes_t(size_t num_finalizers)
            : bitset(num_finalizers)
            , processed(num_finalizers) {}

         // thread safe
         bool has_voted(size_t index) const;

         void add_vote(size_t index, const bls_signature& sig);

         template<class CB>
         void visit_bitset(const CB& cb) const {
            for (size_t i = 0; i < bitset.size(); ++i) {
               if (bitset[i])
                  cb(i);
            }
         }
      };

      aggregating_qc_sig_t();
      aggregating_qc_sig_t(size_t num_finalizers, uint64_t quorum, uint64_t max_weak_sum_before_weak_final);
      explicit aggregating_qc_sig_t(const finalizer_policy_ptr& finalizer_policy);

      bool is_quorum_met() const;
      static bool is_quorum_met(state_t s) {
         return s == state_t::strong || s == state_t::weak_achieved || s == state_t::weak_final;
      }

      vote_result_t add_vote(uint32_t connection_id,
                             block_num_type block_num,
                             bool strong,
                             size_t index,
                             const bls_signature& sig,
                             uint64_t weight);

      bool has_voted(size_t index) const;

      // for debugging, thread safe
      template<class CB>
      void visit_votes(const CB& cb) const {
         std::lock_guard g(*_mtx);
         strong_votes.visit_bitset([&](size_t idx) { cb(idx, true); });
         weak_votes.visit_bitset([&](size_t idx)   { cb(idx, false); });
      }

      state_t state() const { std::lock_guard g(*_mtx); return aggregating_state; };

      std::optional<qc_sig_t> get_best_qc() const;
      // return true if better qc
      bool set_received_qc_sig(const qc_sig_t& qc);
      bool received_qc_sig_is_strong() const;
   private:
      friend struct fc::reflector<aggregating_qc_sig_t>;
      friend class qc_chain;
      std::unique_ptr<std::mutex> _mtx;
      std::optional<qc_sig_t>     received_qc_sig; // best qc_t received from the network inside block extension
      uint64_t                    quorum {0};
      uint64_t                    max_weak_sum_before_weak_final {0}; // max weak sum before becoming weak_final
      state_t                     aggregating_state { state_t::unrestricted };
      uint64_t                    strong_sum {0}; // accumulated sum of strong votes so far
      uint64_t                    weak_sum {0}; // accumulated sum of weak votes so far
      votes_t                     weak_votes {0};
      votes_t                     strong_votes {0};

      // called with mutex held
      vote_result_t check_duplicate(size_t index);
      // called by add_vote, already protected by mutex
      vote_result_t add_strong_vote(size_t index, const bls_signature& sig, uint64_t weight);
      // called by add_vote, already protected by mutex
      vote_result_t add_weak_vote(size_t index, const bls_signature& sig, uint64_t weight);

      bool is_quorum_met_no_lock() const;
      qc_sig_t extract_qc_sig_from_aggregating() const;
   };

   // finalizer authority of strong, weak, or missing votes
   struct qc_vote_metrics_t {
      struct fin_auth {
         finalizer_authority_ptr fin_auth;
         // If the finalizer votes in both active and pending policies,
         // use pending finalizer policy's generation.
         uint32_t                generation{0};
      };
      struct fin_auth_less {
         bool operator()(const fin_auth& lhs, const fin_auth& rhs) const {
            return lhs.fin_auth->public_key < rhs.fin_auth->public_key;
         };
      };
      using fin_auth_set_t = std::set<fin_auth, fin_auth_less>;

      fin_auth_set_t       strong_votes;
      fin_auth_set_t       weak_votes;
      fin_auth_set_t       missing_votes;
      block_timestamp_type voted_for_block_timestamp;
      block_id_type        voted_for_block_id;
   };

   /**
    * All public methods are thread-safe, pending_policy_sig optionally set at construction time.
    */
   class aggregating_qc_t {
   public:
      aggregating_qc_t(const finalizer_policy_ptr& active_finalizer_policy,
                       const finalizer_policy_ptr& pending_finalizer_policy)
         : active_finalizer_policy(active_finalizer_policy)
         , pending_finalizer_policy(pending_finalizer_policy)
         , active_policy_sig{active_finalizer_policy}
         , pending_policy_sig{!pending_finalizer_policy
                                 ? std::optional<aggregating_qc_sig_t>{}
                                 : std::optional<aggregating_qc_sig_t>{pending_finalizer_policy}}
      {}

      aggregating_qc_t() = default;

      std::optional<qc_t> get_best_qc(block_num_type block_num) const;

      qc_vote_metrics_t vote_metrics(const qc_t& qc) const;

      // return qc missing vote's finalizers
      qc_vote_metrics_t::fin_auth_set_t missing_votes(const qc_t& qc) const;

      // return true if better qc
      bool set_received_qc(const qc_t& qc);

      bool received_qc_is_strong() const;
      aggregate_vote_result_t aggregate_vote(uint32_t connection_id, const vote_message& vote,
                                             const block_id_type& block_id, std::span<const uint8_t> finalizer_digest);
      vote_status_t has_voted(const bls_public_key& key) const;
      bool is_quorum_met() const;

   private:
      friend struct fc::reflector<aggregating_qc_t>;
      finalizer_policy_ptr                active_finalizer_policy;  // not modified after construction
      finalizer_policy_ptr                pending_finalizer_policy; // not modified after construction
      aggregating_qc_sig_t                active_policy_sig;
      std::optional<aggregating_qc_sig_t> pending_policy_sig;
   };

} //eosio::chain


FC_REFLECT_ENUM(eosio::chain::vote_result_t, (success)(duplicate)(unknown_public_key)(invalid_signature)(unknown_block)(max_exceeded))
FC_REFLECT(eosio::chain::qc_sig_t, (strong_votes)(weak_votes)(sig));
FC_REFLECT(eosio::chain::aggregating_qc_sig_t, (received_qc_sig)(quorum)(max_weak_sum_before_weak_final)(aggregating_state)(strong_sum)(weak_sum)(weak_votes)(strong_votes));
FC_REFLECT(eosio::chain::aggregating_qc_t, (active_finalizer_policy)(pending_finalizer_policy)(active_policy_sig)(pending_policy_sig));
FC_REFLECT_ENUM(eosio::chain::aggregating_qc_sig_t::state_t, (unrestricted)(restricted)(weak_achieved)(weak_final)(strong));
FC_REFLECT(eosio::chain::aggregating_qc_sig_t::votes_t, (bitset)(sig));
FC_REFLECT(eosio::chain::qc_t, (block_num)(active_policy_sig)(pending_policy_sig));
