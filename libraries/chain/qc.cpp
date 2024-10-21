#include <eosio/chain/qc.hpp>
#include <eosio/chain/vote_message.hpp>
#include <eosio/chain/block_header_state.hpp>
#include <fc/crypto/bls_utils.hpp>

namespace eosio::chain {

inline std::string bitset_to_string(const vote_bitset_t& bs) {
   std::string r;
   boost::to_string(bs, r);
   return r;
}

inline vote_bitset_t vector_to_bitset(const std::vector<uint32_t>& v) {
   return {v.cbegin(), v.cend()};
}

inline std::vector<uint32_t> bitset_to_vector(const vote_bitset_t& bs) {
   std::vector<uint32_t> r;
   r.resize(bs.num_blocks());
   boost::to_block_range(bs, r.begin());
   return r;
}

// A dual finalizer votes on both active and pending finalizer policies.
inline void verify_dual_finalizers_votes(const finalizer_policies_t& policies,
                                         const qc_sig_t& active_policy_sig,
                                         const qc_sig_t& pending_policy_sig,
                                         uint32_t block_num) {
   // Find dual finalizers (which vote on both active and pending policies)
   // and verify each dual finalizer votes in the same way.
   // As the number of finalizers is small, to avoid copying bls_public_keys
   // all over the places,
   // we choose to use nested loops instead of sorting public keys and doing
   // a binary search.
   uint32_t active_vote_index = 0;
   for (const auto& active_fin: policies.active_finalizer_policy->finalizers) {
      uint32_t pending_vote_index = 0;
      for (const auto& pending_fin: policies.pending_finalizer_policy->finalizers) {
         if (active_fin.public_key == pending_fin.public_key) {
            EOS_ASSERT(active_policy_sig.vote_same_at(pending_policy_sig, active_vote_index, pending_vote_index),
                       invalid_qc,
                       "qc ${bn} contains a dual finalizer ${k} which does not vote the same on active and pending policies",
                       ("bn", block_num)("k", active_fin.public_key));
            break;
         }
         ++pending_vote_index;
      }
      ++active_vote_index;
   }
}

void qc_t::verify_signatures(const finalizer_policies_t& policies) const {
   const auto& strong_digest = policies.finality_digest;
   auto        weak_digest   = create_weak_digest(strong_digest);

   active_policy_sig.verify_signatures(policies.active_finalizer_policy, strong_digest, weak_digest);

   if (pending_policy_sig) {
      EOS_ASSERT(policies.pending_finalizer_policy, invalid_qc,
                 "qc ${bn} contains pending policy signature for nonexistent pending finalizer policy", ("bn", block_num));
      pending_policy_sig->verify_signatures(policies.pending_finalizer_policy, strong_digest, weak_digest);
   }
}

void qc_t::verify_basic(const finalizer_policies_t& policies) const {
   active_policy_sig.verify_vote_format(policies.active_finalizer_policy);
   active_policy_sig.verify_weights(policies.active_finalizer_policy);

   if (pending_policy_sig) {
      EOS_ASSERT(policies.pending_finalizer_policy, invalid_qc,
                 "qc ${bn} contains pending policy signature for nonexistent pending finalizer policy", ("bn", block_num));

      // verify that every finalizer included in both policies voted the same
      verify_dual_finalizers_votes(policies, active_policy_sig, *pending_policy_sig, block_num);

      pending_policy_sig->verify_vote_format(policies.pending_finalizer_policy);
      pending_policy_sig->verify_weights(policies.pending_finalizer_policy);
   } else {
      EOS_ASSERT(!policies.pending_finalizer_policy, invalid_qc,
                 "qc ${bn} does not contain pending policy signature for pending finalizer policy", ("bn", block_num));
   }
}

// returns true iff the other and I voted in the same way.
bool qc_sig_t::vote_same_at(const qc_sig_t& other, uint32_t my_vote_index, uint32_t other_vote_index) const {
   assert(!strong_votes || my_vote_index < strong_votes->size());
   assert(!weak_votes || my_vote_index < weak_votes->size());

   // We have already verified the same index has not voted both strong
   // and weak for a given qc_sig_t (I or other).
   bool same_strong = ((strong_votes && (*strong_votes)[my_vote_index]) ==
                       (other.strong_votes && (*other.strong_votes)[other_vote_index]));
   bool same_weak = ((weak_votes && (*weak_votes)[my_vote_index]) ==
                     (other.weak_votes && (*other.weak_votes)[other_vote_index]));

   return (same_strong && same_weak);
}

void qc_sig_t::verify_vote_format(const finalizer_policy_ptr& fin_policy) const {
   assert(fin_policy);

   const auto& finalizers = fin_policy->finalizers;
   auto num_finalizers = finalizers.size();

   EOS_ASSERT( strong_votes || weak_votes, invalid_qc,
               "Neither strong_votes nor weak_votes present for finalizer policy, generation ${n}",
               ("n", fin_policy->generation) );

   // verify number of finalizers matches with vote bitset size
   if (strong_votes) {
      EOS_ASSERT( num_finalizers == strong_votes->size(), invalid_qc,
                  "vote bitset size is not the same as the number of finalizers for the policy it refers to, "
                  "vote bitset size: ${s}, num of finalizers for the policy: ${n}",
                  ("s", strong_votes->size())("n", num_finalizers) );
   }
   if (weak_votes) {
      EOS_ASSERT( num_finalizers == weak_votes->size(), invalid_qc,
                  "vote bitset size is not the same as the number of finalizers for the policy it refers to, "
                  "vote bitset size: ${s}, num of finalizers for the policy: ${n}",
                  ("s", weak_votes->size())("n", num_finalizers) );
   }

   // verify a finalizer cannot vote both strong and weak
   if (strong_votes && weak_votes) {
      for (size_t i=0; i<strong_votes->size(); ++i) {
         // at most one is true
         EOS_ASSERT( !((*strong_votes)[i] && (*weak_votes)[i]), invalid_qc,
                     "finalizer (bit index ${i}) voted both strong and weak",
                     ("i", i) );

      }
   }
}

void qc_sig_t::verify_weights(const finalizer_policy_ptr& fin_policy) const {

   const auto& finalizers = fin_policy->finalizers;
   auto num_finalizers = finalizers.size();

   // utility to accumulate voted weights
   auto weights = [&] ( const vote_bitset_t& votes_bitset ) -> uint64_t {
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
      EOS_ASSERT( strong_weights >= fin_policy->threshold, invalid_qc,
                  "strong quorum is not met, strong_weights: ${s}, threshold: ${t}",
                  ("s", strong_weights)("t", fin_policy->threshold) );
   } else {
      EOS_ASSERT( strong_weights + weak_weights >= fin_policy->threshold, invalid_qc,
                  "weak quorum is not met, strong_weights: ${s}, weak_weights: ${w}, threshold: ${t}",
                  ("s", strong_weights)("w", weak_weights)("t", fin_policy->threshold) );
   }
}

void qc_sig_t::verify_signatures(const finalizer_policy_ptr& fin_policy,
                                 const digest_type& strong_digest,
                                 const weak_digest_t& weak_digest) const {
   const auto& finalizers = fin_policy->finalizers;
   auto num_finalizers = finalizers.size();

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
               invalid_qc_signature, "qc signature validation failed" );

}

bool aggregating_qc_sig_t::has_voted(size_t index) const {
   return strong_votes.has_voted(index) || weak_votes.has_voted(index);
}

void aggregating_qc_sig_t::votes_t::reflector_init() {
   processed = std::vector<bit_processed>(bitset.size());
   for (size_t i = 0; i < bitset.size(); ++i) {
      if (bitset[i]) {
         processed[i].value.store(true, std::memory_order_relaxed);
      }
   }
}

bool aggregating_qc_sig_t::votes_t::has_voted(size_t index) const {
   assert(index < processed.size());
   return processed[index].value.load(std::memory_order_relaxed);
}


void aggregating_qc_sig_t::votes_t::add_vote(size_t index, const bls_signature& signature) {
   processed[index].value.store(true, std::memory_order_relaxed);
   bitset.set(index);
   sig.aggregate(signature); // works even if _sig is default initialized (fp2::zero())
}

aggregating_qc_sig_t::aggregating_qc_sig_t()
   : _mtx(std::make_unique<std::mutex>()) {
}

aggregating_qc_sig_t::aggregating_qc_sig_t(size_t num_finalizers, uint64_t quorum, uint64_t max_weak_sum_before_weak_final)
   : _mtx(std::make_unique<std::mutex>())
   , quorum(quorum)
   , max_weak_sum_before_weak_final(max_weak_sum_before_weak_final)
   , weak_votes(num_finalizers)
   , strong_votes(num_finalizers) {
}

aggregating_qc_sig_t::aggregating_qc_sig_t(const finalizer_policy_ptr& finalizer_policy)
   : aggregating_qc_sig_t(finalizer_policy->finalizers.size(),
                          finalizer_policy->threshold,
                          finalizer_policy->max_weak_sum_before_weak_final()) {
}

bool aggregating_qc_sig_t::is_quorum_met() const {
   std::lock_guard g(*_mtx);
   return is_quorum_met_no_lock();
}

// called with held mutex
vote_result_t aggregating_qc_sig_t::check_duplicate(size_t index) {
   if (strong_votes.bitset[index] || weak_votes.bitset[index])
      return vote_result_t::duplicate;
   return vote_result_t::success;
}

// called by add_vote, already protected by mutex
vote_result_t aggregating_qc_sig_t::add_strong_vote(size_t index, const bls_signature& sig, uint64_t weight) {
   strong_votes.add_vote(index, sig);
   strong_sum += weight;

   switch (aggregating_state) {
   case state_t::unrestricted:
   case state_t::restricted:
      if (strong_sum >= quorum) {
         assert(aggregating_state != state_t::restricted);
         aggregating_state = state_t::strong;
      } else if (weak_sum + strong_sum >= quorum)
         aggregating_state = (aggregating_state == state_t::restricted) ? state_t::weak_final : state_t::weak_achieved;
      break;

   case state_t::weak_achieved:
      if (strong_sum >= quorum)
         aggregating_state = state_t::strong;
      break;

   case state_t::weak_final:
   case state_t::strong:
      // getting another strong vote...nothing to do
      break;
   }
   return vote_result_t::success;
}

// called by add_vote, already protected by mutex
vote_result_t aggregating_qc_sig_t::add_weak_vote(size_t index, const bls_signature& sig, uint64_t weight) {
   weak_votes.add_vote(index, sig);
   weak_sum += weight;

   switch (aggregating_state) {
   case state_t::unrestricted:
   case state_t::restricted:
      if (weak_sum + strong_sum >= quorum)
         aggregating_state = state_t::weak_achieved;

      if (weak_sum > max_weak_sum_before_weak_final) {
         if (aggregating_state == state_t::weak_achieved)
            aggregating_state = state_t::weak_final;
         else if (aggregating_state == state_t::unrestricted)
            aggregating_state = state_t::restricted;
      }
      break;

   case state_t::weak_achieved:
      if (weak_sum >= max_weak_sum_before_weak_final)
         aggregating_state = state_t::weak_final;
      break;

   case state_t::weak_final:
   case state_t::strong:
      // getting another weak vote... nothing to do
      break;
   }
   return vote_result_t::success;
}

// thread safe
vote_result_t aggregating_qc_sig_t::add_vote(uint32_t connection_id, block_num_type block_num,
                                             bool strong, size_t index,
                                             const bls_signature& sig, uint64_t weight) {
   std::unique_lock g(*_mtx);
   state_t pre_state = aggregating_state;
   vote_result_t s = check_duplicate(index);
   if (s == vote_result_t::success) {
      if (strong)
         s = add_strong_vote(index, sig, weight);
      else
         s = add_weak_vote(index, sig, weight);
   }
   state_t post_state = aggregating_state;
   g.unlock();

   fc_dlog(vote_logger, "connection - ${c} block_num: ${bn}, index: ${i}, vote strong: ${sv}, status: ${s}, pre-state: ${pre}, post-state: ${state}, quorum_met: ${q}",
           ("c", connection_id)("bn", block_num)("i", index)("sv", strong)("s", s)("pre", pre_state)("state", post_state)("q", is_quorum_met(post_state)));
   return s;
}

// called by get_best_qc which acquires a mutex
qc_sig_t aggregating_qc_sig_t::extract_qc_sig_from_aggregating() const {
   qc_sig_t qc_sig;

   if( aggregating_state == state_t::strong ) {
      qc_sig.strong_votes = strong_votes.bitset;
      qc_sig.sig          = strong_votes.sig;
   } else if (is_quorum_met_no_lock()) {
      qc_sig.strong_votes = strong_votes.bitset;
      qc_sig.weak_votes   = weak_votes.bitset;
      qc_sig.sig          = strong_votes.sig;
      qc_sig.sig.aggregate(weak_votes.sig);
   } else
      assert(0); // this should be called only when we have an aggregating_qc_sig_t with a quorum

   return qc_sig;
}

std::optional<qc_sig_t> aggregating_qc_sig_t::get_best_qc() const {
   std::lock_guard g(*_mtx);
   // if this does not have a valid QC, consider received_qc_sig only
   if( !is_quorum_met_no_lock() ) {
      if( received_qc_sig ) {
         return std::optional{*received_qc_sig};
      }
      return {};
   }

   qc_sig_t qc_sig_from_agg = extract_qc_sig_from_aggregating();

   // if received_qc_sig does not have value, consider qc_sig_from_agg only
   if( !received_qc_sig ) {
      return std::optional{std::move(qc_sig_from_agg)};
   }

   // Both received_qc_sig and qc_sig_from_agg have value. Compare them and select a better one.
   // Strong beats weak. Tie-break by received_qc_sig, strong beats weak
   if (received_qc_sig->is_strong() || qc_sig_from_agg.is_weak()) {
      return std::optional{qc_sig_t{ *received_qc_sig }};
   }
   return std::optional{qc_sig_t{ std::move(qc_sig_from_agg) }};
}

bool aggregating_qc_sig_t::set_received_qc_sig(const qc_sig_t& qc) {
   std::lock_guard g(*_mtx);
   if (!received_qc_sig || (received_qc_sig->is_weak() && qc.is_strong())) {
      received_qc_sig = qc;
      return true;
   }
   return false;
}

bool aggregating_qc_sig_t::received_qc_sig_is_strong() const {
   std::lock_guard g(*_mtx);
   return received_qc_sig && received_qc_sig->is_strong();
}

bool aggregating_qc_sig_t::is_quorum_met_no_lock() const {
   return is_quorum_met(aggregating_state);
}

std::optional<qc_t> aggregating_qc_t::get_best_qc(block_num_type block_num) const {
   std::optional<qc_sig_t> active_best_qc = active_policy_sig.get_best_qc();
   if (!active_best_qc) // active is always required
      return {};

   if (pending_policy_sig) {
      std::optional<qc_sig_t> pending_best_qc = pending_policy_sig->get_best_qc();
      if (pending_best_qc)
         return std::optional<qc_t>{qc_t{block_num, std::move(*active_best_qc), std::move(pending_best_qc)}};
      return {}; // no quorum on pending_policy_sig so no qc for this block
   }

   // no pending_policy_sig so only need active
   return std::optional<qc_t>{qc_t{block_num, std::move(*active_best_qc), {}}};
}

bool aggregating_qc_t::set_received_qc(const qc_t& qc) {
   // qc should have already been verified via verify_qc, this EOS_ASSERT should never fire
   EOS_ASSERT(!pending_policy_sig || qc.pending_policy_sig, invalid_qc,
              "qc ${bn} expected to have a pending policy signature", ("bn", qc.block_num));
   bool active_better = active_policy_sig.set_received_qc_sig(qc.active_policy_sig);
   bool pending_better = false;
   if (pending_policy_sig) {
      pending_better = pending_policy_sig->set_received_qc_sig(*qc.pending_policy_sig);
   }
   return active_better || pending_better;
}

bool aggregating_qc_t::received_qc_is_strong() const {
   if (!pending_policy_sig) { // consider only active
      return active_policy_sig.received_qc_sig_is_strong();
   }
   return active_policy_sig.received_qc_sig_is_strong() && pending_policy_sig->received_qc_sig_is_strong();
}

aggregate_vote_result_t aggregating_qc_t::aggregate_vote(uint32_t connection_id, const vote_message& vote,
                                                         const block_id_type& block_id, std::span<const uint8_t> finalizer_digest)
{
   aggregate_vote_result_t r;
   block_num_type block_num = block_header::num_from_id(block_id);

   bool verified_sig = false;
   auto verify_sig = [&]() -> vote_result_t {
      if (!verified_sig && !fc::crypto::blslib::verify(vote.finalizer_key, finalizer_digest, vote.sig)) {
         fc_wlog(vote_logger, "connection - ${c} block_num: ${bn} block_id: ${id}, signature from finalizer ${k}.. cannot be verified, vote strong: ${sv}",
                 ("c", connection_id)("bn", block_num)("id", block_id)("k", vote.finalizer_key.to_string().substr(8,16))("sv", vote.strong));
         return vote_result_t::invalid_signature;
      }
      verified_sig = true;
      return vote_result_t::success;
   };

   auto add_vote = [&](finalizer_authority_ptr& auth, const finalizer_policy_ptr& finalizer_policy, aggregating_qc_sig_t& agg_qc_sig) -> vote_result_t {
      const auto& finalizers = finalizer_policy->finalizers;
      auto itr = std::ranges::find_if(finalizers, [&](const auto& finalizer) { return finalizer.public_key == vote.finalizer_key; });
      vote_result_t s = vote_result_t::unknown_public_key;
      if (itr != finalizers.end()) {
         auth = finalizer_authority_ptr{finalizer_policy, &(*itr)}; // use aliasing shared_ptr constructor
         auto index = std::distance(finalizers.begin(), itr);
         if (agg_qc_sig.has_voted(index)) {
            fc_tlog(vote_logger, "connection - ${c} block_num: ${bn} block_id: ${id}, duplicate finalizer ${k}..",
                    ("c", connection_id)("bn", block_num)("id", block_id)("k", vote.finalizer_key.to_string().substr(8,16)));
            return vote_result_t::duplicate;
         }
         if (vote_result_t vs = verify_sig(); vs != vote_result_t::success)
            return vs;
         s = agg_qc_sig.add_vote(connection_id, block_num,
                                 vote.strong,
                                 index,
                                 vote.sig,
                                 finalizers[index].weight);

      }
      return s;
   };

   r.result = add_vote(r.active_authority, active_finalizer_policy, active_policy_sig);
   if (r.result != vote_result_t::success && r.result != vote_result_t::unknown_public_key)
      return r;

   if (pending_finalizer_policy) {
      assert(pending_policy_sig);
      vote_result_t ps = add_vote(r.pending_authority, pending_finalizer_policy, *pending_policy_sig);
      if (ps != vote_result_t::unknown_public_key)
         r.result = ps;
   }

   if (r.result == vote_result_t::unknown_public_key) {
      fc_wlog(vote_logger, "connection - ${c} finalizer_key ${k} in vote is not in finalizer policies",
              ("c", connection_id)("k", vote.finalizer_key.to_string().substr(8,16)));
   }
   return r;
}

vote_status_t aggregating_qc_t::has_voted(const bls_public_key& key) const {
   auto finalizer_has_voted = [](const finalizer_policy_ptr& policy,
                                 const aggregating_qc_sig_t& agg_qc_sig,
                                 const bls_public_key& key) -> vote_status_t {
      const auto& finalizers = policy->finalizers;
      auto it = std::ranges::find_if(finalizers, [&](const auto& finalizer) { return finalizer.public_key == key; });
      if (it != finalizers.end()) {
         auto index = std::distance(finalizers.begin(), it);
         return agg_qc_sig.has_voted(index) ? vote_status_t::voted : vote_status_t::not_voted;
      }
      return vote_status_t::irrelevant_finalizer;
   };

   vote_status_t active_status = finalizer_has_voted(active_finalizer_policy, active_policy_sig, key);
   if (!pending_finalizer_policy || active_status == vote_status_t::not_voted) {
      return active_status;
   }

   EOS_ASSERT(pending_policy_sig, invalid_qc,
              "qc does not contain pending policy signature for pending finalizer policy");
   vote_status_t pending_status = finalizer_has_voted(pending_finalizer_policy, *pending_policy_sig, key);

   if (pending_status == vote_status_t::irrelevant_finalizer) {
      return active_status;
   }
   return pending_status;
}

bool aggregating_qc_t::is_quorum_met() const {
   return active_policy_sig.is_quorum_met() && (!pending_policy_sig || pending_policy_sig->is_quorum_met());
}

qc_vote_metrics_t aggregating_qc_t::vote_metrics(const qc_t& qc) const {
   qc_vote_metrics_t result;

   auto add_votes = [&](const finalizer_policy_ptr& finalizer_policy, const auto& votes, qc_vote_metrics_t::fin_auth_set_t& results) {
      assert(votes.size() == finalizer_policy->finalizers.size());
      size_t added = 0;
      for (size_t i = 0; i < votes.size(); ++i) {
         if (votes[i]) {
            results.insert(qc_vote_metrics_t::fin_auth{
                  .fin_auth   = finalizer_authority_ptr{finalizer_policy, &finalizer_policy->finalizers[i]}, // use aliasing shared_ptr constructor

                  // add_policy_votes and add_votes in turn is called on
                  // pending_finalizer_policy after on active_finalizer_policy.
                  // Therefore pending_finalizer_policy generation will be used
                  // for generation if the finalizer votes on both active and
                  // pending finalizer policies.
                  .generation = finalizer_policy->generation});
            ++added;
         }
      }
      return added;
   };
   auto add_policy_votes = [&](const finalizer_policy_ptr& finalizer_policy, const qc_sig_t& qc_sig) {
      size_t added = 0;
      if (qc_sig.strong_votes) {
         added = add_votes(finalizer_policy, *qc_sig.strong_votes, result.strong_votes);
      }
      if (qc_sig.weak_votes) {
         added = add_votes(finalizer_policy, *qc_sig.weak_votes, result.weak_votes);
      }
      if (added != finalizer_policy->finalizers.size()) {
         vote_bitset_t not_voted(finalizer_policy->finalizers.size());
         if (qc_sig.strong_votes) {
            not_voted = *qc_sig.strong_votes;
         }
         if (qc_sig.weak_votes) {
            assert(not_voted.size() == qc_sig.weak_votes->size());
            not_voted |= *qc_sig.weak_votes;
         }
         not_voted.flip();
         add_votes(finalizer_policy, not_voted, result.missing_votes);
      }
   };

   add_policy_votes(active_finalizer_policy, qc.active_policy_sig);
   if (pending_finalizer_policy) {
      assert(pending_policy_sig);
      add_policy_votes(pending_finalizer_policy, *qc.pending_policy_sig);
   }

   return result;
}

qc_vote_metrics_t::fin_auth_set_t aggregating_qc_t::missing_votes(const qc_t& qc) const {
   // all asserts are verified by verify_qc()
   qc_vote_metrics_t::fin_auth_set_t not_voted;

   auto check_other = [](const auto& other_votes, size_t i) {
      return other_votes && (*other_votes)[i];
   };
   auto add_not_voted = [&](const finalizer_policy_ptr& finalizer_policy, const qc_sig_t& qc_sig) {
      assert(qc_sig.strong_votes || qc_sig.weak_votes);
      const vote_bitset_t& votes                      = qc_sig.strong_votes ? *qc_sig.strong_votes : *qc_sig.weak_votes;
      const std::optional<vote_bitset_t>& other_votes = qc_sig.strong_votes ? qc_sig.weak_votes : qc_sig.strong_votes;
      const auto& finalizers = finalizer_policy->finalizers;
      assert(votes.size() == finalizers.size());
      assert(!other_votes || other_votes->size() == finalizers.size());
      for (size_t i = 0; i < votes.size(); ++i) {
         if (!votes[i] && !check_other(other_votes, i)) {
            not_voted.insert(qc_vote_metrics_t::fin_auth{
                  .fin_auth   = finalizer_authority_ptr{finalizer_policy, &finalizers[i]}, // use aliasing shared_ptr constructor
                  .generation = finalizer_policy->generation});
         }
      }
   };

   add_not_voted(active_finalizer_policy, qc.active_policy_sig);
   if (pending_finalizer_policy) {
      assert(pending_policy_sig);
      add_not_voted(pending_finalizer_policy, *qc.pending_policy_sig);
   }

   return not_voted;
}

} // namespace eosio::chain
