#include <eosio/chain_plugin/tracked_votes.hpp>

#include <shared_mutex>

using namespace eosio;
using namespace eosio::chain::literals;

namespace eosio::chain_apis {
   /**
    * Implementation details of the last tracked cache
    */
   struct tracked_votes_impl {
      tracked_votes_impl(const chain::controller& controller)
      :controller(controller)
      {}

      // Cache to store last vote information for each known finalizer.
      // A map of finalizer public key --> vote info.
      std::unordered_map<std::string, tracked_votes::vote_info> last_votes;

      // A handle to the controller.
      const chain::controller& controller;

      // Called on accepted_block signal. Retrieve vote information from
      // QC in the block and store it in last_votes.
      void on_accepted_block( const chain::signed_block_ptr& block, const chain::block_id_type& id ) {
         if (!block->contains_extension(chain::quorum_certificate_extension::extension_id())) {
            return;
         }

         // Retrieve vote information from QC
         const auto& qc_ext = block->extract_extension<chain::quorum_certificate_extension>();
         chain::qc_vote_metrics_t vm = controller.vote_metrics(id, qc_ext.qc);

         auto track_votes = [&](const chain::qc_vote_metrics_t::fin_auth_set_t& finalizers, bool is_strong) {
            for (auto& f: finalizers) {
               assert(f);

               tracked_votes::vote_info v_info {
                  .public_key               = f->public_key.to_string(),
                  .description              = f->description,
                  .is_vote_strong           = true,
                  .voted_policy_generation  = vm.voted_policy_generation,
                  .voted_block_id           = vm.voted_block_id,
                  .voted_block_num          = chain::block_header::num_from_id(vm.voted_block_id),
                  .voted_block_timestamp    = vm.voted_block_timestamp
               };

               last_votes.emplace(f->public_key.to_string(), v_info); // track the voting information for the finalizer
            }
         };

         track_votes(vm.strong_votes, true);
         track_votes(vm.weak_votes, false);
      }

      // Returns last vote information by a given finalizer
      std::optional<tracked_votes::vote_info> get_last_vote_info(const std::string& finalizer_pub_key) const {
         auto it = last_votes.find(finalizer_pub_key);
         if (it != last_votes.end()) {
             return it->second;
         }

         return {};
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

   std::optional<tracked_votes::vote_info> tracked_votes::get_last_vote_info(const std::string& finalizer_pub_key) const {
      return _impl->get_last_vote_info(finalizer_pub_key);
   }
} // namespace eosio::chain_apis
