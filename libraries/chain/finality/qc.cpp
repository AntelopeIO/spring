#include <eosio/chain/finality/qc.hpp>
#include <eosio/chain/finality/vote_message.hpp>
#include <fc/crypto/bls_utils.hpp>

namespace eosio::chain {

inline std::string bitset_to_string(const vote_bitset& bs) {
   std::string r;
   boost::to_string(bs, r);
   return r;
}

inline vote_bitset vector_to_bitset(const std::vector<uint32_t>& v) {
   return {v.cbegin(), v.cend()};
}

inline std::vector<uint32_t> bitset_to_vector(const vote_bitset& bs) {
   std::vector<uint32_t> r;
   r.resize(bs.num_blocks());
   boost::to_block_range(bs, r.begin());
   return r;
}

void qc_sig_t::verify(const finalizer_policy_ptr& fin_policy,
                                    const digest_type& strong_digest,
                                    const weak_digest_t& weak_digest) const {

   const auto& finalizers = fin_policy->finalizers;
   auto num_finalizers = finalizers.size();

   // utility to accumulate voted weights
   auto weights = [&] ( const vote_bitset& votes_bitset ) -> uint64_t {
      EOS_ASSERT( num_finalizers == votes_bitset.size(), invalid_qc_claim,
                  "vote bitset size is not the same as the number of finalizers for the policy it refers to, "
                  "vote bitset size: ${s}, num of finalizers for the policy: ${n}",
                  ("s", votes_bitset.size())("n", num_finalizers) );

      uint64_t sum = 0;
      for (auto i = 0u; i < num_finalizers; ++i) {
         if( votes_bitset[i] ) { // ith finalizer voted
            sum += finalizers[i].weight;
         }
      }
      return sum;
   };

   // compute strong and weak accumulated weights
   auto strong_weights = strong_votes ? weights( *strong_votes ) : 0;
   auto weak_weights = weak_votes ? weights( *weak_votes ) : 0;

   // verfify quorum is met
   if( is_strong() ) {
      EOS_ASSERT( strong_weights >= fin_policy->threshold, invalid_qc_claim,
                  "strong quorum is not met, strong_weights: ${s}, threshold: ${t}",
                  ("s", strong_weights)("t", fin_policy->threshold) );
   } else {
      EOS_ASSERT( strong_weights + weak_weights >= fin_policy->threshold, invalid_qc_claim,
                  "weak quorum is not met, strong_weights: ${s}, weak_weights: ${w}, threshold: ${t}",
                  ("s", strong_weights)("w", weak_weights)("t", fin_policy->threshold) );
   }

   // no reason to use bls_public_key wrapper
   std::vector<bls12_381::g1> pubkeys;
   pubkeys.reserve(2);
   std::vector<std::vector<uint8_t>> digests;
   digests.reserve(2);

   // utility to aggregate public keys for verification
   auto aggregate_pubkeys = [&](const auto& votes_bitset) -> bls12_381::g1 {
      const auto n = std::min(num_finalizers, votes_bitset.size());
      std::vector<bls12_381::g1> pubkeys_to_aggregate;
      pubkeys_to_aggregate.reserve(n);
      for(auto i = 0u; i < n; ++i) {
         if (votes_bitset[i]) { // ith finalizer voted
            pubkeys_to_aggregate.emplace_back(finalizers[i].public_key.jacobian_montgomery_le());
         }
      }

      return bls12_381::aggregate_public_keys(pubkeys_to_aggregate);
   };

   // aggregate public keys and digests for strong and weak votes
   if( strong_votes ) {
      pubkeys.emplace_back(aggregate_pubkeys(*strong_votes));
      digests.emplace_back(std::vector<uint8_t>{strong_digest.data(), strong_digest.data() + strong_digest.data_size()});
   }

   if( weak_votes ) {
      pubkeys.emplace_back(aggregate_pubkeys(*weak_votes));
      digests.emplace_back(std::vector<uint8_t>{weak_digest.begin(), weak_digest.end()});
   }

   // validate aggregated signature
   EOS_ASSERT( bls12_381::aggregate_verify(pubkeys, digests, sig.jacobian_montgomery_le()),
               invalid_qc_claim, "qc signature validation failed" );

}

bool open_qc_sig_t::has_voted(size_t index) const {
   return strong_votes.has_voted(index) || weak_votes.has_voted(index);
}

bool open_qc_sig_t::has_voted(bool strong, size_t index) const {
   if (strong) {
      return strong_votes.has_voted(index);
   }
   return weak_votes.has_voted(index);
}

void open_qc_sig_t::votes_t::reflector_init() {
   processed = std::vector<std::atomic<bool>>(bitset.size());
   for (size_t i = 0; i < bitset.size(); ++i) {
      if (bitset[i]) {
         processed[i].store(true, std::memory_order_relaxed);
      }
   }
}

bool open_qc_sig_t::votes_t::has_voted(size_t index) const {
   assert(index < processed.size());
   return processed[index].load(std::memory_order_relaxed);
}


vote_status open_qc_sig_t::votes_t::add_vote(size_t index, const bls_signature& signature) {
   if (bitset[index]) { // check here as could have come in while unlocked
      return vote_status::duplicate; // shouldn't be already present
   }
   processed[index].store(true, std::memory_order_relaxed);
   bitset.set(index);
   sig.aggregate(signature); // works even if _sig is default initialized (fp2::zero())
   return vote_status::success;
}

open_qc_sig_t::open_qc_sig_t()
   : _mtx(std::make_unique<std::mutex>()) {
}

open_qc_sig_t::open_qc_sig_t(size_t num_finalizers, uint64_t quorum, uint64_t max_weak_sum_before_weak_final)
   : _mtx(std::make_unique<std::mutex>())
   , quorum(quorum)
   , max_weak_sum_before_weak_final(max_weak_sum_before_weak_final)
   , weak_votes(num_finalizers)
   , strong_votes(num_finalizers) {
}

bool open_qc_sig_t::is_quorum_met() const {
   std::lock_guard g(*_mtx);
   return is_quorum_met_no_lock();
}

// called by add_vote, already protected by mutex
vote_status open_qc_sig_t::add_strong_vote(size_t index, const bls_signature& sig, uint64_t weight) {
   if (auto s = strong_votes.add_vote(index, sig); s != vote_status::success) {
      return s;
   }
   strong_sum += weight;

   switch (pending_state) {
   case state_t::unrestricted:
   case state_t::restricted:
      if (strong_sum >= quorum) {
         assert(pending_state != state_t::restricted);
         pending_state = state_t::strong;
      } else if (weak_sum + strong_sum >= quorum)
         pending_state = (pending_state == state_t::restricted) ? state_t::weak_final : state_t::weak_achieved;
      break;

   case state_t::weak_achieved:
      if (strong_sum >= quorum)
         pending_state = state_t::strong;
      break;

   case state_t::weak_final:
   case state_t::strong:
      // getting another strong vote...nothing to do
      break;
   }
   return vote_status::success;
}

// called by add_vote, already protected by mutex
vote_status open_qc_sig_t::add_weak_vote(size_t index, const bls_signature& sig, uint64_t weight) {
   if (auto s = weak_votes.add_vote(index, sig); s != vote_status::success)
      return s;
   weak_sum += weight;

   switch (pending_state) {
   case state_t::unrestricted:
   case state_t::restricted:
      if (weak_sum + strong_sum >= quorum)
         pending_state = state_t::weak_achieved;

      if (weak_sum > max_weak_sum_before_weak_final) {
         if (pending_state == state_t::weak_achieved)
            pending_state = state_t::weak_final;
         else if (pending_state == state_t::unrestricted)
            pending_state = state_t::restricted;
      }
      break;

   case state_t::weak_achieved:
      if (weak_sum >= max_weak_sum_before_weak_final)
         pending_state = state_t::weak_final;
      break;

   case state_t::weak_final:
   case state_t::strong:
      // getting another weak vote... nothing to do
      break;
   }
   return vote_status::success;
}

// thread safe
vote_status open_qc_sig_t::add_vote(uint32_t connection_id, block_num_type block_num,
                                    bool strong, size_t index,
                                    const bls_signature& sig, uint64_t weight) {
   std::unique_lock g(*_mtx);
   state_t pre_state = pending_state;
   vote_status s = strong ? add_strong_vote(index, sig, weight)
                          : add_weak_vote(index, sig, weight);
   state_t post_state = pending_state;
   g.unlock();

   fc_dlog(vote_logger, "connection - ${c} block_num: ${bn}, vote strong: ${sv}, status: ${s}, pre-state: ${pre}, post-state: ${state}, quorum_met: ${q}",
        ("c", connection_id)("bn", block_num)("sv", strong)("s", s)("pre", pre_state)("state", post_state)("q", is_quorum_met(post_state)));
   return s;
}

// called by get_best_qc which acquires a mutex
qc_sig_t open_qc_sig_t::to_valid_qc_sig() const {
   qc_sig_t valid_qc_sig;

   if( pending_state == state_t::strong ) {
      valid_qc_sig.strong_votes = strong_votes.bitset;
      valid_qc_sig.sig          = strong_votes.sig;
   } else if (is_quorum_met_no_lock()) {
      valid_qc_sig.strong_votes = strong_votes.bitset;
      valid_qc_sig.weak_votes   = weak_votes.bitset;
      valid_qc_sig.sig          = strong_votes.sig;
      valid_qc_sig.sig.aggregate(weak_votes.sig);
   } else
      assert(0); // this should be called only when we have a valid qc_t.

   return valid_qc_sig;
}

std::optional<qc_sig_t> open_qc_sig_t::get_best_qc() const {
   std::lock_guard g(*_mtx);
   // if this does not have a valid QC, consider qc_sig only
   if( !is_quorum_met_no_lock() ) {
      if( qc_sig ) {
         return std::optional{*qc_sig};
      }
      return {};
   }

   // extract valid QC sig from open qc sig
   qc_sig_t valid_qc_sig_from_open = to_valid_qc_sig();

   // if qc_sig does not have value, consider valid_qc_sig_from_open only
   if( !qc_sig ) {
      return std::optional{std::move(valid_qc_sig_from_open)};
   }

   // Both qc_sig and valid_qc_sig_from_open have value. Compare them and select a better one.
   // Strong beats weak. Tie-break by qc_sig, strong beats weak
   const bool use_qc_sig = qc_sig->is_strong() == valid_qc_sig_from_open.is_strong() ? qc_sig->is_strong() : false;
   if (use_qc_sig) {
      return std::optional{qc_sig_t{ *qc_sig }};
   }
   return std::optional{qc_sig_t{ std::move(valid_qc_sig_from_open) }};
}

void open_qc_sig_t::set_valid_qc_sig(const qc_sig_t& qc) {
   std::lock_guard g(*_mtx);
   qc_sig = qc;
}

bool open_qc_sig_t::valid_qc_sig_is_strong() const {
   std::lock_guard g(*_mtx);
   return qc_sig && qc_sig->is_strong();
}

bool open_qc_sig_t::is_quorum_met_no_lock() const {
   return is_quorum_met(pending_state);
}

std::optional<qc_t> open_qc_t::get_best_qc(block_num_type block_num) const {
   std::optional<qc_sig_t> active_best_qc = active_policy_sig.get_best_qc();

   if (!pending_policy_sig) { // consider only active
      if (active_best_qc) {
         return std::optional{qc_t{ block_num, std::move(*active_best_qc), {} }};
      }
      return {};
   }
   // consider both
   std::optional<qc_sig_t> pending_best_qc = pending_policy_sig->get_best_qc();
   if (pending_best_qc) {
      return std::optional{qc_t{ block_num, std::move(*active_best_qc), std::move(*pending_best_qc) }};
   }
   return {};
}

void open_qc_t::set_valid_qc(const qc_t& qc) {
   active_policy_sig.set_valid_qc_sig(qc.active_policy_sig);
   if (qc.pending_policy_sig) {
      assert(pending_policy_sig);
      pending_policy_sig->set_valid_qc_sig(*qc.pending_policy_sig);
   }
}

bool open_qc_t::valid_qc_is_strong() const {
   if (!pending_policy_sig) { // consider only active
      return active_policy_sig.valid_qc_sig_is_strong();
   }
   return active_policy_sig.valid_qc_sig_is_strong() && pending_policy_sig->valid_qc_sig_is_strong();
}

} // namespace eosio::chain
