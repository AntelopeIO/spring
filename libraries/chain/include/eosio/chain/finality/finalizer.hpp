#pragma once
#include <eosio/chain/block_state.hpp>
#include <eosio/chain/finality/vote_message.hpp>
#include <fc/crypto/bls_utils.hpp>
#include <fc/io/cfile.hpp>
#include <compare>
#include <mutex>
#include <ranges>

// -------------------------------------------------------------------------------------------
// this file defines the classes:
//
// finalizer:
// ---------
//     - holds the bls12 private key which allows the finalizer to sign proposals (the
//       proposal is assumed to have been previously validated for correctness). These
//       signatures will be aggregated by block proposers into quorum certificates, which
//       are an essential part of the Savanna consensus algorithm.
//     - every time a finalizer votes, it may update its own safety info in memory
//     - finalizer safety info is appropriately initialized (iff not already present
//       in the persistent file) at Spring startup.
//
//  my_finalizers_t:
//  ---------------
//     - stores the set of finalizers currently active on this node.
//     - manages a `finalizer safety` file (`safety.dat`) which tracks the active finalizers
//       safety info (file is updated after each vote), and also the safety information for
//       every finalizer which has been active on this node (using the same `finalizer-dir`)
// -------------------------------------------------------------------------------------------

namespace eosio::chain {

   // ----------------------------------------------------------------------------------------
   struct finalizer_safety_information {
      block_timestamp_type last_vote_range_start;
      block_ref            last_vote;
      block_ref            lock;

      static constexpr uint64_t magic_unversioned = 0x5AFE11115AFE1111ull;
      static constexpr uint64_t magic             = 0x5AFE11115AFE1112ull;

      static finalizer_safety_information unset_fsi() { return {}; }

      auto operator==(const finalizer_safety_information& o) const {
         return last_vote_range_start == o.last_vote_range_start &&
            last_vote == o.last_vote &&
            lock == o.lock;
      }
   };

   // ----------------------------------------------------------------------------------------
   // Access is protected by my_finalizers_t mutex
   struct finalizer {
      enum class vote_decision { no_vote, strong_vote, weak_vote };
      struct vote_result {
         vote_decision decision       {vote_decision::no_vote};
         bool          safety_check   {false};
         bool          liveness_check {false};
         bool          monotony_check {false};
      };

      bls_private_key               priv_key;
      finalizer_safety_information  fsi;

      vote_result  decide_vote(const block_state_ptr& bsp);
      vote_message_ptr maybe_vote(const bls_public_key& pub_key, const block_state_ptr& bsp, const digest_type& digest);
      // finalizer has voted strong, update fsi if it does not already contain vote or better
      bool maybe_update_fsi(const block_state_ptr& bsp);
   };

   // ----------------------------------------------------------------------------------------
   struct my_finalizers_t {
   public:
      static constexpr uint64_t current_safety_file_version = 1;

      using fsi_t   = finalizer_safety_information;
      using fsi_map = std::map<bls_public_key, fsi_t>;
      using vote_t  = std::tuple<vote_message_ptr, finalizer_authority_ptr, finalizer_authority_ptr>;

   private:
      const std::filesystem::path       persist_file_path;     // where we save the safety data
      std::atomic<bool>                 has_voted{false};      // true if this node has voted and updated safety info
      mutable std::mutex                mtx;
      mutable fc::datastream<fc::cfile> persist_file;          // we want to keep the file open for speed
      std::map<bls_public_key, finalizer>  finalizers;         // the active finalizers for this node, loaded at startup, not mutated afterwards
      fsi_map                           inactive_safety_info;  // loaded at startup, not mutated afterwards
      fsi_t                             default_fsi = fsi_t::unset_fsi(); // default provided at spring startup
      mutable bool                      inactive_safety_info_written{false};

   public:
      explicit my_finalizers_t(const std::filesystem::path& persist_file_path)
         : persist_file_path(persist_file_path)
      {}

      template<class F> // thread safe, F(vote_t)
      void maybe_vote(const block_state_ptr& bsp, F&& process_vote) {
         if (finalizers.empty())
            return;

         assert(bsp->active_finalizer_policy);

         std::vector<vote_t> votes;
         votes.reserve(finalizers.size());

         auto in_policy = [](finalizer_authority_ptr& auth, const finalizer_policy_ptr& finalizer_policy, const bls_public_key& key) {
            return std::ranges::any_of(finalizer_policy->finalizers, [&](const finalizer_authority& fin_auth) {
               if (fin_auth.public_key == key) {
                  auth = finalizer_authority_ptr{finalizer_policy, &fin_auth}; // use aliasing shared_ptr constructor
                  return true;
               }
               return false;
            });
         };

         // Possible improvement in the future, look at locking only individual finalizers and releasing the lock for writing the file.
         // Would require making sure that only the latest is ever written to the file and that the file access was protected separately.
         std::unique_lock g(mtx);

         // first accumulate all the votes
         // optimized for finalizers of size one which should be the normal configuration outside of tests
         for (auto& f : finalizers) {
            finalizer_authority_ptr active_auth;
            finalizer_authority_ptr pending_auth;
            bool in_active  = in_policy(active_auth, bsp->active_finalizer_policy, f.first);
            // don't shortcut in_pending as we want to signal both active_auth & pending_auth if appropriate
            bool in_pending = bsp->pending_finalizer_policy && in_policy(pending_auth, bsp->pending_finalizer_policy->second, f.first);
            if (in_active || in_pending) {
               vote_message_ptr vote_msg = f.second.maybe_vote(f.first, bsp, bsp->strong_digest);
               if (vote_msg)
                  votes.push_back(vote_t{std::move(vote_msg), std::move(active_auth), std::move(pending_auth)});
            }
         }

         // then save the safety info and, if successful, gossip the votes
         if (!votes.empty()) {
            save_finalizer_safety_info();
            g.unlock();
            has_voted.store(true, std::memory_order::relaxed);
            for (const auto& vote : votes)
               std::forward<F>(process_vote)(vote);
         }
      }

      void maybe_update_fsi(const block_state_ptr& bsp, const qc_t& received_qc);

      size_t  size() const { return finalizers.size(); }   // doesn't change, thread safe
      bool    empty() const { return finalizers.empty(); } // doesn't change, thread safe
      bool    contains(const bls_public_key& pub_key) const { return finalizers.contains(pub_key); } // doesn't change, thread safe

      template<typename F>
      bool all_of_public_keys(F&& f) const { // only access keys which do not change, thread safe
         return std::ranges::all_of(std::views::keys(finalizers), std::forward<F>(f));
      }

      template<typename F>
      bool any_of_public_keys(F&& f) const { // only access keys which do not change, thread safe
         return std::ranges::any_of(std::views::keys(finalizers), std::forward<F>(f));
      }

      /// only call on startup
      void    set_keys(const std::map<std::string, std::string>& finalizer_keys);
      void    set_default_safety_information(const fsi_t& fsi);

      // following two member functions could be private, but are used in testing, not thread safe
      void    save_finalizer_safety_info() const;
      fsi_map load_finalizer_safety_info();

      // for testing purposes only, not thread safe
      const fsi_t& get_fsi(const bls_public_key& k) { return finalizers[k].fsi; }
      void         set_fsi(const bls_public_key& k, const fsi_t& fsi) { finalizers[k].fsi = fsi; }
   };

}

namespace std {
   inline std::ostream& operator<<(std::ostream& os, const eosio::chain::finalizer_safety_information& fsi) {
      os << "fsi(" << fsi.last_vote_range_start.slot << ", " << fsi.last_vote << ", " << fsi.lock << ")";
      return os;
   }

   inline std::ostream& operator<<(std::ostream& os, const eosio::chain::finalizer::vote_result& vr) {
      os << "vote_result(\"";
      using vote_decision = eosio::chain::finalizer::vote_decision;
      switch(vr.decision) {
      case vote_decision::strong_vote: os << "strong_vote"; break;
      case vote_decision::weak_vote:   os << "weak_vote";   break;
      case vote_decision::no_vote:     os << "no_vote";     break;
      }
      os << "\", monotony_check(" << vr.monotony_check << "), liveness_check(" << vr.liveness_check <<
         "), safety_check(" << vr.safety_check<<  "))";
      return os;
   }
}

FC_REFLECT(eosio::chain::finalizer_safety_information, (last_vote_range_start)(last_vote)(lock))
FC_REFLECT_ENUM(eosio::chain::finalizer::vote_decision, (strong_vote)(weak_vote)(no_vote))
