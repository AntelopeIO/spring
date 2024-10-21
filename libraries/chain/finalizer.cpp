#include <eosio/chain/finalizer.hpp>
#include <eosio/chain/exceptions.hpp>
#include <fc/log/logger_config.hpp>

namespace eosio::chain {

// ----------------------------------------------------------------------------------------
finalizer::vote_result finalizer::decide_vote(const block_state_ptr& bsp) {
   vote_result res;

   res.monotony_check = fsi.last_vote.empty() || bsp->timestamp() > fsi.last_vote.timestamp;
   // fsi.last_vote.empty() means we have never voted on a proposal, so the protocol feature
   // just activated and we can proceed

   if (!res.monotony_check) {
      assert(!fsi.last_vote.empty()); // otherwise `res.monotony_check` would be true.
      if (vote_logger.is_enabled(fc::log_level::debug)) {
         if (bsp->id() != fsi.last_vote.block_id) { // we may have already voted when we received the block
            fc_dlog(vote_logger, "monotony check failed, block ${bn} ${p}, cannot vote, ${t} <= ${lt}, fsi.last_vote ${lbn} ${lid}",
                    ("bn", bsp->block_num())("p", bsp->id())("t", bsp->timestamp())("lt", fsi.last_vote.timestamp)
                    ("lbn", fsi.last_vote.block_num())("lid", fsi.last_vote.block_id));
         }
      }
      return res;
   }

   if (!fsi.lock.empty()) {
      // Liveness check : check if the height of this proposal's justification is higher
      // than the height of the proposal I'm locked on.
      // Also check if lock_block_timestamp <= last_final_block_timestamp to allow finalizers that were active before
      // to participate in liveness when they come back into active finalizer policy.
      // This allows restoration of liveness if a replica is locked on a stale proposal
      // -------------------------------------------------------------------------------
      res.liveness_check = bsp->core.latest_qc_block_timestamp() > fsi.lock.timestamp;

      if (!res.liveness_check) {
         fc_dlog(vote_logger, "liveness check failed, block ${bn} ${id}: ${c} <= ${l}, fsi.lock ${lbn} ${lid}, latest_qc_claim: ${qc}",
                 ("bn", bsp->block_num())("id", bsp->id())("c", bsp->core.latest_qc_block_timestamp())("l", fsi.lock.timestamp)
                 ("lbn", fsi.lock.block_num())("lid", fsi.lock.block_id)
                 ("qc", bsp->core.latest_qc_claim()));

         // Safety check : check if this proposal extends the proposal we're locked on
         res.safety_check = bsp->core.extends(fsi.lock.block_id);
         if (!res.safety_check) {
            fc_dlog(vote_logger, "safety check failed, block ${bn} ${id} did not extend fsi.lock ${lbn} ${lid}",
                    ("bn", bsp->block_num())("id", bsp->id())("lbn", fsi.lock.block_num())("lid", fsi.lock.block_id));
         }
      }
   } else {
      // Safety and Liveness both fail if `fsi.lock` is empty. It should not happen.
      // `fsi.lock` is initially set to `lib` when switching to IF or starting from a snapshot.
      // -------------------------------------------------------------------------------------
      fc_wlog(vote_logger, "liveness check & safety check failed, block ${bn} ${id}, fsi.lock is empty",
              ("bn", bsp->block_num())("id", bsp->id()));
      res.liveness_check = false;
      res.safety_check   = false;
   }

   bool can_vote = res.liveness_check || res.safety_check;

   // Figure out if we can vote and whether our vote will be strong or weak
   // If we vote, update `fsi.last_vote` and also `fsi.lock` if we have a newer commit qc
   // -----------------------------------------------------------------------------------
   if (can_vote) {
      const auto latest_qc_block_timestamp = bsp->core.latest_qc_block_timestamp();

      // If `fsi.last_vote` is not set, it will be initialized with a timestamp slot of 0,
      // which means `fsi.last_vote.timestamp` would always be less than or equal
      // to `latest_qc_block_timestamp`.
      // So, we don't need to separately check for the case where `fsi.last_vote.empty()` is true.
      if (fsi.last_vote.timestamp <= latest_qc_block_timestamp) {
         res.decision = vote_decision::strong_vote;
      } else if (bsp->core.extends(fsi.last_vote.block_id)) {
         // If `fsi.other_branch_latest_time` is not present, it will have a timestamp slot of
         // 0, which means it will always be less than or equal to `latest_qc_block_timestamp`.
         // So, we don't need to separately check for the case where
         // `fsi.other_branch_latest_time` is not present.
         if (fsi.other_branch_latest_time <= latest_qc_block_timestamp) {
            res.decision = vote_decision::strong_vote;
         } else {
            res.decision = vote_decision::weak_vote;
         }
      } else {
         res.decision                 = vote_decision::weak_vote;
         fsi.other_branch_latest_time = fsi.last_vote.timestamp;
      }

      if (res.decision == vote_decision::strong_vote) {
         fsi.other_branch_latest_time = block_timestamp_type{};
         if (latest_qc_block_timestamp > fsi.lock.timestamp) {
            fsi.lock = bsp->core.get_block_reference(bsp->core.latest_qc_claim().block_num);
         }
      }

      fsi.last_vote = bsp->make_block_ref();
   }

   if (res.liveness_check) {
      fc_dlog(vote_logger, "block=${bn} ${id}, liveness=${l}, can vote=${cn}, voting=${v}, locked=${lbn} ${lid}",
              ("bn", bsp->block_num())("id", bsp->id())("l",res.liveness_check)
              ("cn",can_vote)("v", res.decision)("lbn", fsi.lock.block_num())("lid", fsi.lock.block_id));
   } else if (can_vote) {
      fc_dlog(vote_logger, "block=${bn} ${id}, liveness=${l}, safety=${s}, can vote=${cn}, voting=${v}, locked=${lbn} ${lid}",
              ("bn", bsp->block_num())("id", bsp->id())("l",res.liveness_check)("s",res.safety_check)
              ("cn",can_vote)("v", res.decision)("lbn", fsi.lock.block_num())("lid", fsi.lock.block_id));
   } else {
      fc_ilog(vote_logger, "block=${bn} ${id}, liveness=${l}, safety=${s}, can vote=${cn}, voting=${v}, "
                           "${ct} <= ${lt}, locked=${lbn} ${lid}, latest_qc_claim: ${qc}",
              ("bn", bsp->block_num())("id", bsp->id())("l",res.liveness_check)("s",res.safety_check)
              ("cn",can_vote)("v", res.decision)("ct", bsp->core.latest_qc_block_timestamp())("lt", fsi.lock.timestamp)
              ("lbn", fsi.lock.block_num())("lid", fsi.lock.block_id)("qc", bsp->core.latest_qc_claim()));
   }
   return res;
}

// ----------------------------------------------------------------------------------------
// finalizer has voted strong on bsp, update finalizer safety info if more recent than the current lock
bool finalizer::maybe_update_fsi(const block_state_ptr& bsp) {
   auto& latest_qc_claim__block_ref = bsp->core.get_block_reference(bsp->core.latest_qc_claim().block_num);
   if (latest_qc_claim__block_ref.timestamp > fsi.lock.timestamp && bsp->timestamp() > fsi.last_vote.timestamp) {
      fsi.lock                     = latest_qc_claim__block_ref;
      fsi.last_vote                = bsp->make_block_ref();
      fsi.other_branch_latest_time = block_timestamp_type{}; // always reset on strong vote
      return true;
   }
   return false;
}

// ----------------------------------------------------------------------------------------
vote_message_ptr finalizer::maybe_vote(const bls_public_key& pub_key,
                                       const block_state_ptr& bsp,
                                       const digest_type& digest) {
   finalizer::vote_decision decision = decide_vote(bsp).decision;
   if (decision == vote_decision::strong_vote || decision == vote_decision::weak_vote) {
      try {
         bls_signature sig;
         if (decision == vote_decision::weak_vote) {
            // if voting weak, the digest to sign should be a hash of the concatenation of the finalizer_digest
            // and the string "WEAK"
            sig =  priv_key.sign(create_weak_digest(digest));
         } else {
            sig =  priv_key.sign({(uint8_t*)digest.data(), (uint8_t*)digest.data() + digest.data_size()});
         }
         return std::make_shared<vote_message>(bsp->id(), decision == vote_decision::strong_vote, pub_key, sig);
      } FC_LOG_AND_DROP() // bls_signature can throw if invalid signature
   }
   return {};
}

// ----------------------------------------------------------------------------------------
inline bool has_voted_strong(const std::vector<finalizer_authority>& finalizers, const qc_sig_t& qc, const bls_public_key& key) {
   assert(qc.is_strong());
   auto it = std::ranges::find_if(finalizers, [&](const auto& fin) { return fin.public_key == key; });
   if (it != finalizers.end()) {
      auto index = std::distance(finalizers.begin(), it);
      assert(qc.strong_votes);
      return qc.strong_votes->test(index);
   }
   return false;
}

void my_finalizers_t::maybe_update_fsi(const block_state_ptr& bsp, const qc_t& received_qc) {
   if (finalizers.empty())
      return;

   // once we have voted, no reason to continue evaluating incoming QCs
   if (has_voted.load(std::memory_order::relaxed))
      return;

   assert(bsp->active_finalizer_policy);
   // qc should have already been verified via verify_qc, this EOS_ASSERT should never fire
   EOS_ASSERT(!bsp->pending_finalizer_policy || received_qc.pending_policy_sig, invalid_qc_claim,
              "qc ${bn} expected to have a pending policy signature", ("bn", received_qc.block_num));


   // see comment on possible optimization in maybe_vote
   std::lock_guard g(mtx);

   bool updated = false;
   for (auto& f : finalizers) {
      if (has_voted_strong(bsp->active_finalizer_policy->finalizers, received_qc.active_policy_sig, f.first)
          || (bsp->pending_finalizer_policy &&
              has_voted_strong(bsp->pending_finalizer_policy->second->finalizers, *received_qc.pending_policy_sig, f.first))) {
         updated |= f.second.maybe_update_fsi(bsp);
      }
   }

   if (updated) {
      save_finalizer_safety_info();
   }
}


// -------------------------------------------------------------------------------------------------
//                                 Finalizer Safety File i/o
// -------------------------------------------------------------------------------------------------

// pack/unpack block_ref (omitting generation numbers)
// ---------------------------------------------------
template<typename Stream>
void pack_v0(Stream& s, const block_ref& ref)  {
   fc::raw::pack(s, ref.block_id);
   fc::raw::pack(s, ref.timestamp);
   fc::raw::pack(s, ref.finality_digest);
}

template<typename Stream>
void unpack_v0(Stream& s, block_ref& ref) {
   fc::raw::unpack(s, ref.block_id);
   fc::raw::unpack(s, ref.timestamp);
   fc::raw::unpack(s, ref.finality_digest);
}

// pack/unpack v1 fsi (last_vote and lock omitting generation numbers)
// -------------------------------------------------------------------
template<typename Stream>
void pack_v1(Stream& s, const finalizer_safety_information& fsi) {
   pack_v0(s, fsi.last_vote);
   pack_v0(s, fsi.lock);
   fc::raw::pack(s, fsi.other_branch_latest_time);
}

template<typename Stream>
void unpack_v1(Stream& s, finalizer_safety_information& fsi) {
   unpack_v0(s, fsi.last_vote);
   unpack_v0(s, fsi.lock);
   fc::raw::unpack(s, fsi.other_branch_latest_time);
}

bool my_finalizers_t::save_finalizer_safety_info() const {
   try {
      if (!cfile_ds.is_open()) {
         EOS_ASSERT(!persist_file_path.empty(), finalizer_safety_exception,
                    "path for storing finalizer safety information file not specified");
         cfile_ds.set_file_path(persist_file_path);
         cfile_ds.open(fc::cfile::truncate_rw_mode);
      }
      // optimize by only calculating crc for inactive once
      if (inactive_safety_info_written_pos == 0) {
         persist_file.seekp(0);
         fc::raw::pack(persist_file, fsi_t::magic);
         fc::raw::pack(persist_file, current_safety_file_version);
         uint64_t size = finalizers.size() + inactive_safety_info.size();
         fc::raw::pack(persist_file, size);

         // save also the fsi that was originally present in the file, but which applied to
         // finalizers not configured anymore.
         for (const auto& [pub_key, fsi] : inactive_safety_info) {
            fc::raw::pack(persist_file, pub_key);
            pack_v1(persist_file, fsi);
         }
         inactive_safety_info_written_pos = persist_file.tellp();
         inactive_crc32 = persist_file.crc();
      } else {
         persist_file.seekp(inactive_safety_info_written_pos, inactive_crc32);
      }

      // active finalizers
      for (const auto& [pub_key, f] : finalizers) {
         fc::raw::pack(persist_file, pub_key);
         pack_v1(persist_file, f.fsi);
      }

      uint32_t cs = persist_file.checksum();
      fc::raw::pack(persist_file, cs);

      cfile_ds.flush();
      return true;
   } FC_LOG_AND_DROP()
   return false;
}


void my_finalizers_t::load_finalizer_safety_info_v0(fsi_map& res) {
   uint64_t num_finalizers {0};
   fc::raw::unpack(persist_file, num_finalizers);
   for (size_t i=0; i<num_finalizers; ++i) {
      bls_public_key pubkey;
      fc::raw::unpack(persist_file, pubkey);

      my_finalizers_t::fsi_t fsi;

      unpack_v0(persist_file, fsi.last_vote);
      unpack_v0(persist_file, fsi.lock);

      // special processing for v0's last member, which was this bool as last member instead of other_branch_latest_time
      bool votes_forked_since_latest_strong_vote;
      fc::raw::unpack(persist_file, votes_forked_since_latest_strong_vote);
      fsi.other_branch_latest_time =
         votes_forked_since_latest_strong_vote ? fsi.last_vote.timestamp : block_timestamp_type{};

      res.emplace(pubkey, fsi);
   }
}

void my_finalizers_t::load_finalizer_safety_info_v1(fsi_map& res) {
   uint64_t num_finalizers {0};
   fc::raw::unpack(persist_file, num_finalizers);
   for (size_t i=0; i<num_finalizers; ++i) {
      bls_public_key pubkey;
      my_finalizers_t::fsi_t fsi;
      fc::raw::unpack(persist_file, pubkey);
      unpack_v1(persist_file, fsi);
      res.emplace(pubkey, fsi);
   }
}

my_finalizers_t::fsi_map my_finalizers_t::load_finalizer_safety_info() {
   fsi_map res;

   EOS_ASSERT(!persist_file_path.empty(), finalizer_safety_exception,
              "path for storing finalizer safety persistence file not specified");
   EOS_ASSERT(!cfile_ds.is_open(), finalizer_safety_exception,
              "Trying to read an already open finalizer safety persistence file: ${p}",
              ("p", persist_file_path));

   if (!std::filesystem::exists(persist_file_path)) {
      if (!std::filesystem::exists(persist_file_path.parent_path()))
         std::filesystem::create_directories(persist_file_path.parent_path());
      fc_ilog(vote_logger, "finalizer safety persistence file ${p} does not exist (which is expected on the first use of a BLS finalizer key)",
              ("p", persist_file_path));
      return res;
   }

   cfile_ds.set_file_path(persist_file_path);

   try {
      // if we can't open the finalizer safety file on startup, throw exception so producer_plugin startup fails
      cfile_ds.open(fc::cfile::update_rw_mode);
   } FC_RETHROW_EXCEPTIONS(log_level::error, "unable to open finalizer safety persistence file ${p}", ("p", persist_file_path))

   try {
      persist_file.seekp(0);

      // read magic number. must be `fsi_t::magic`
      // -----------------------------------------
      uint64_t magic = 0;
      fc::raw::unpack(persist_file, magic);
      EOS_ASSERT(magic == fsi_t::magic, finalizer_safety_exception,
                 "bad magic number in finalizer safety persistence file: ${p}", ("p", persist_file_path));

      // We can load files with older, but not files with a version higher that the running nodeos understands.
      // -----------------------------------------------------------------------------------------------------
      uint64_t file_version = 0; // current file version
      fc::raw::unpack(persist_file, file_version);
      EOS_ASSERT(file_version <= current_safety_file_version, finalizer_safety_exception,
                 "Incorrect version number in finalizer safety persistence file: ${p}", ("p", persist_file_path));

      // finally read the `finalizer_safety_information` info
      // ----------------------------------------------------
      bool verify_checksum = true;
      switch (file_version) {
      case safety_file_version_0:
         load_finalizer_safety_info_v0(res);
         verify_checksum = false;
         break;
      case safety_file_version_1:
         load_finalizer_safety_info_v1(res);
         break;
      default:
         assert(0);
      }

      if (verify_checksum) {
         // verify checksum
         uint32_t calculated_checksum = persist_file.checksum();
         uint32_t cs = 0;
         fc::raw::unpack(persist_file, cs);
         EOS_ASSERT(cs == calculated_checksum, finalizer_safety_exception,
                    "bad checksum reading finalizer safety persistence file: ${p}", ("p", persist_file_path));
      }

      // close file after write
      cfile_ds.close();
   } FC_RETHROW_EXCEPTIONS(log_level::error, "corrupted finalizer safety persistence file ${p}", ("p", persist_file_path))
   // don't remove file we can't load
   return res;
}

// -------------------------------------------------------------------------------------------------
//                          End of Finalizer Safety File i/o
// -------------------------------------------------------------------------------------------------


// ----------------------------------------------------------------------------------------
void my_finalizers_t::set_keys(const std::map<std::string, std::string>& finalizer_keys) {
   if (finalizer_keys.empty())
      return;

   assert(finalizers.empty()); // set_keys should be called only once at startup
   fsi_map safety_info = load_finalizer_safety_info();
   for (const auto& [pub_key_str, priv_key_str] : finalizer_keys) {
      auto public_key {bls_public_key{pub_key_str}};
      auto it  = safety_info.find(public_key);
      const auto& fsi = it != safety_info.end() ? it->second : default_fsi;
      finalizers.emplace(public_key, finalizer{bls_private_key{priv_key_str}, fsi});
   }

   // Now that we have updated the  finalizer_safety_info of our local finalizers,
   // remove these from the in-memory map. Whenever we save the finalizer_safety_info, we will
   // write the info for the local finalizers, and the first time we'll write the information for
   // currently inactive finalizers (which might be configured again in the future).
   //
   // So for every vote but the first, we'll only have to write the safety_info for the configured
   // finalizers.
   // --------------------------------------------------------------------------------------------
   for (const auto& [pub_key_str, priv_key_str] : finalizer_keys)
      safety_info.erase(bls_public_key{pub_key_str});

   // now only inactive finalizers remain in safety_info => move it to inactive_safety_info
   inactive_safety_info = std::move(safety_info);
}


// --------------------------------------------------------------------------------------------
// Can be called either:
//   - when transitioning to IF (before any votes are to be sent)
//   - at spring startup, if we start at a block which is either within or past the IF transition.
// In either case, we are never updating existing finalizer safety information. This is only
// to ensure that the safety information will have defaults that ensure safety as much as
// possible, and allow for liveness which will allow the finalizers to eventually vote.
// --------------------------------------------------------------------------------------------
void my_finalizers_t::set_default_safety_information(const fsi_t& fsi) {
   std::lock_guard g(mtx);

   for (auto& [pub_key, f] : finalizers) {
      // update only finalizers which are uninitialized
      if (!f.fsi.last_vote.empty() || !f.fsi.lock.empty())
         continue;

      f.fsi = fsi;
   }

   // save it in case set_keys called afterwards.
   default_fsi = fsi;
}

} // namespace eosio::chain
