#include <eosio/chain_plugin/tracked_votes.hpp>

#include <shared_mutex>

using namespace eosio;
using namespace eosio::chain::literals;

namespace eosio::chain_apis {
   /**
    * Implementation details of the last tracked cache
    */
   struct tracked_votes_impl {
      explicit tracked_votes_impl(const chain::controller& controller)
      : controller(controller)
      {}

      bool tracking_enabled = false;

      // Cache to store last vote information for each known finalizer.
      // A map of finalizer public key --> vote info.
      std::map<fc::crypto::blslib::bls_public_key, tracked_votes::vote_info> last_votes;

      // A handle to the controller.
      const chain::controller& controller;

      // Called on accepted_block signal. Retrieve vote information from
      // QC in the block and store it in last_votes.
      void on_accepted_block( const chain::signed_block_ptr& block, const chain::block_id_type& id ) {
         try {
            if (!block->is_proper_svnn_block())
               return;
            if (!tracking_enabled && !chain::vote_logger.is_enabled(fc::log_level::info))
               return;

            // do not bother tracking/logging when syncing or replaying
            auto now = fc::time_point::now();
            if (now - block->timestamp > fc::minutes(5) && (block->block_num() % 1000 != 0))
               return;

            if (!block->contains_extension(chain::quorum_certificate_extension::extension_id())) {
               if (chain::vote_logger.is_enabled(fc::log_level::info)) {
                  std::optional<chain::block_header_extension> fin_ext = block->extract_header_extension(chain::finality_extension::extension_id());
                  chain::qc_claim_t claim = fin_ext ? std::get<chain::finality_extension>(*fin_ext).qc_claim : chain::qc_claim_t{};
                  fc_ilog(chain::vote_logger, "Block ${id}... #${n} @ ${t} produced by ${p}, latency: ${l}ms has no qc, claim: ${c}",
                          ("id", id.str().substr(8, 16))("n", block->block_num())("t", block->timestamp)("p", block->producer)
                          ("l", (now - block->timestamp).count() / 1000)("c", claim));
               }
               return;
            }

            if (tracking_enabled) {
               // Retrieve vote information from QC
               const auto& qc_ext = block->extract_extension<chain::quorum_certificate_extension>();
               chain::qc_vote_metrics_t vm = controller.vote_metrics(id, qc_ext.qc);

               if (tracking_enabled) {
                  auto track_votes = [&](const chain::qc_vote_metrics_t::fin_auth_set_t& finalizers, bool is_strong) {
                     for (auto& f: finalizers) {
                        assert(f.fin_auth);

                        tracked_votes::vote_info v_info {
                           .description                  = f.fin_auth->description,
                           .public_key                   = f.fin_auth->public_key.to_string(),
                           .is_vote_strong               = is_strong,
                           .finalizer_policy_generation  = f.generation,
                           .voted_for_block_id           = vm.voted_for_block_id,
                           .voted_for_block_num          = chain::block_header::num_from_id(vm.voted_for_block_id),
                           .voted_for_block_timestamp    = vm.voted_for_block_timestamp
                        };

                        last_votes[f.fin_auth->public_key] = std::move(v_info); // track the voting information for the finalizer
                     }
                  };

                  track_votes(vm.strong_votes, true);
                  track_votes(vm.weak_votes, false);
               }
               log_missing_votes(block, id, vm.missing_votes, qc_ext.qc.block_num);
            } else if (chain::vote_logger.is_enabled(fc::log_level::info)) {
               const auto& qc_ext = block->extract_extension<chain::quorum_certificate_extension>();
               auto missing = controller.missing_votes(id, qc_ext.qc);
               log_missing_votes(block, id, missing, qc_ext.qc.block_num);
            }

         } FC_LOG_AND_DROP(("tracked_votes_impl on_accepted_block ERROR"));
      }

      // Returns last vote information by a given finalizer
      std::optional<tracked_votes::vote_info> get_last_vote_info(const fc::crypto::blslib::bls_public_key& finalizer_pub_key) const {
         auto it = last_votes.find(finalizer_pub_key);
         if (it != last_votes.end()) {
             return it->second;
         }

         return {};
      }

      void set_tracking_enabled(bool enabled) {
         tracking_enabled = enabled;
      }

      void log_missing_votes(const chain::signed_block_ptr& block, const chain::block_id_type& id,
                             const chain::qc_vote_metrics_t::fin_auth_set_t& missing_votes,
                             uint32_t missed_block_num) const {
         if (chain::vote_logger.is_enabled(fc::log_level::info)) {
            std::string not_voted;
            for (const auto& f : missing_votes) {
               if (controller.is_node_finalizer_key(f.fin_auth->public_key)) {
                  fc_wlog(chain::vote_logger, "Local finalizer ${f} did not vote in block ${n} : ${id} for block ${m_n}",
                          ("f", f.fin_auth->description)("n", block->block_num())("id", id.str().substr(8,16))("m_n", missed_block_num));
               }
               not_voted += f.fin_auth->description;
               not_voted += ',';
            }
            if (!not_voted.empty()) {
               not_voted.resize(not_voted.size() - 1); // remove ','
               fc_ilog(chain::vote_logger, "Block ${id}... #${n} @ ${t} produced by ${p}, latency: ${l}ms has no votes for block #${m_n} from finalizers: ${v}",
                    ("id", id.str().substr(8, 16))("n", block->block_num())("t", block->timestamp)("p", block->producer)
                    ("l", (fc::time_point::now() - block->timestamp).count() / 1000)("v", not_voted)("m_n", missed_block_num));
            }
         }
      }

   }; // tracked_votes_impl

   tracked_votes::tracked_votes( const chain::controller& controller )
   :_impl(std::make_unique<tracked_votes_impl>(controller))
   {
   }

   tracked_votes::~tracked_votes() = default;

   void tracked_votes::on_accepted_block( const chain::signed_block_ptr& block, const chain::block_id_type& id ) {
      _impl->on_accepted_block(block, id);
   }

   std::optional<tracked_votes::vote_info> tracked_votes::get_last_vote_info(const fc::crypto::blslib::bls_public_key& finalizer_pub_key) const {
      return _impl->get_last_vote_info(finalizer_pub_key);
   }

   void tracked_votes::set_tracking_enabled(bool enabled) {
      _impl->set_tracking_enabled(enabled);
   }

} // namespace eosio::chain_apis
