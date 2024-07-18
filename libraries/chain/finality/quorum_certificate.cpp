#include <eosio/chain/finality/quorum_certificate.hpp>
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

bool pending_quorum_certificate::has_voted(size_t index) const {
   return strong_votes.has_voted(index) || weak_votes.has_voted(index);
}

bool pending_quorum_certificate::has_voted_no_lock(bool strong, size_t index) const {
   if (strong) {
      return strong_votes.has_voted(index);
   }
   return weak_votes.has_voted(index);
}

void pending_quorum_certificate::votes_t::reflector_init() {
   processed = std::vector<std::atomic<bool>>(bitset.size());
   for (size_t i = 0; i < bitset.size(); ++i) {
      if (bitset[i]) {
         processed[i].store(true, std::memory_order_relaxed);
      }
   }
}

bool pending_quorum_certificate::votes_t::has_voted(size_t index) const {
   assert(index < processed.size());
   return processed[index].load(std::memory_order_relaxed);
}


vote_status pending_quorum_certificate::votes_t::add_vote(size_t index, const bls_signature& signature) {
   if (bitset[index]) { // check here as could have come in while unlocked
      return vote_status::duplicate; // shouldn't be already present
   }
   processed[index].store(true, std::memory_order_relaxed);
   bitset.set(index);
   sig.aggregate(signature); // works even if _sig is default initialized (fp2::zero())
   return vote_status::success;
}

pending_quorum_certificate::pending_quorum_certificate()
   : _mtx(std::make_unique<std::mutex>()) {
}

pending_quorum_certificate::pending_quorum_certificate(size_t num_finalizers, uint64_t quorum, uint64_t max_weak_sum_before_weak_final)
   : _mtx(std::make_unique<std::mutex>())
   , quorum(quorum)
   , max_weak_sum_before_weak_final(max_weak_sum_before_weak_final)
   , weak_votes(num_finalizers)
   , strong_votes(num_finalizers) {
}

bool pending_quorum_certificate::is_quorum_met() const {
   std::lock_guard g(*_mtx);
   return is_quorum_met_no_lock();
}

// called by add_vote, already protected by mutex
vote_status pending_quorum_certificate::add_strong_vote(size_t index, const bls_signature& sig, uint64_t weight) {
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
vote_status pending_quorum_certificate::add_weak_vote(size_t index, const bls_signature& sig, uint64_t weight) {
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
vote_status pending_quorum_certificate::add_vote(uint32_t connection_id, block_num_type block_num,
                                                 bool strong, std::span<const uint8_t> finalizer_digest, size_t index,
                                                 const bls_public_key& pubkey, const bls_signature& sig, uint64_t weight) {
   if (has_voted_no_lock(strong, index)) {
      fc_dlog(vote_logger, "connection - ${c} block_num: ${bn}, duplicate", ("c", connection_id)("bn", block_num));
      return vote_status::duplicate;
   }

   if (!fc::crypto::blslib::verify(pubkey, finalizer_digest, sig)) {
      fc_wlog(vote_logger, "connection - ${c} signature from finalizer ${k}.. cannot be verified", ("k", pubkey.to_string().substr(8,16)));
      return vote_status::invalid_signature;
   }

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
quorum_certificate_sig pending_quorum_certificate::to_valid_quorum_certificate() const {
   quorum_certificate_sig valid_qc_sig;

   if( pending_state == state_t::strong ) {
      valid_qc_sig.strong_votes = strong_votes.bitset;
      valid_qc_sig.sig          = strong_votes.sig;
   } else if (is_quorum_met_no_lock()) {
      valid_qc_sig.strong_votes = strong_votes.bitset;
      valid_qc_sig.weak_votes   = weak_votes.bitset;
      valid_qc_sig.sig          = strong_votes.sig;
      valid_qc_sig.sig.aggregate(weak_votes.sig);
   } else
      assert(0); // this should be called only when we have a valid qc.

   return valid_qc_sig;
}

std::optional<quorum_certificate> pending_quorum_certificate::get_best_qc(block_num_type block_num) const {
   std::lock_guard g(*_mtx);
   // if pending_qc does not have a valid QC, consider valid_qc only
   if( !is_quorum_met_no_lock() ) {
      if( valid_qc ) {
         return std::optional{quorum_certificate{ block_num, *valid_qc }};
      } else {
         return std::nullopt;
      }
   }

   // extract valid QC from pending_qc
   quorum_certificate_sig valid_qc_from_pending = to_valid_quorum_certificate();

   // if valid_qc does not have value, consider valid_qc_from_pending only
   if( !valid_qc ) {
      return std::optional{quorum_certificate{ block_num, valid_qc_from_pending }};
   }

   // Both valid_qc and valid_qc_from_pending have value. Compare them and select a better one.
   // Strong beats weak. Tie break by valid_qc.
   const auto& best_qc =
      valid_qc->is_strong() == valid_qc_from_pending.is_strong() ?
      *valid_qc : // tie broken by valid_qc
      valid_qc->is_strong() ? *valid_qc : valid_qc_from_pending; // strong beats weak
   return std::optional{quorum_certificate{ block_num, best_qc }};
}

void pending_quorum_certificate::set_valid_qc(const quorum_certificate_sig& qc) {
   std::lock_guard g(*_mtx);
   valid_qc = qc;
}

bool pending_quorum_certificate::valid_qc_is_strong() const {
   std::lock_guard g(*_mtx);
   return valid_qc && valid_qc->is_strong();
}

bool pending_quorum_certificate::is_quorum_met_no_lock() const {
   return is_quorum_met(pending_state);
}

} // namespace eosio::chain
