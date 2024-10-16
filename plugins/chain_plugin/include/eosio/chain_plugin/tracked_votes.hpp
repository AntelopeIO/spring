#pragma once
#include <eosio/chain/types.hpp>
#include <eosio/chain/block.hpp>
#include <eosio/chain/controller.hpp>

namespace eosio::chain_apis {
   /**
    * This class manages the ephemeral data that is needed by `get_finalizers_info` RPC call.
    * There is no persistence and the cache is recreated when the class is instantiated
    * based on the current state of the chain.
    */
   class tracked_votes {
   public:

      /**
       * Instantiate a new tracked votes cache from the given chain controller
       * The caller is expected to manage lifetimes such that this controller reference does not go stale
       * for the life of the tracked votes cache
       * @param tracking_enabled true if get_last_vote_info() vote tracking enabled
       * @param chain - controller to read data from
       */
      explicit tracked_votes(const class eosio::chain::controller& chain);
      ~tracked_votes();

      /**
       * vote information for a given finalizer.
       */
      struct vote_info {
         std::string          description; // voting finalizer's description
         std::string          public_key;  // voting finalizer's public key
         bool                 is_vote_strong{false}; // indicating the vote is strong or not
         uint32_t             finalizer_policy_generation{0}; // the generation of finalizer policy being used to vote
         chain::block_id_type voted_for_block_id;  // block id of the block being voted
         uint32_t             voted_for_block_num{0}; // block number of the block being voted
         fc::time_point       voted_for_block_timestamp; // block timestamp of the block being voted
      };

      // Called on accepted_block signal. Retrieve vote information from
      // QC in the block and store it in last_votes.
      void on_accepted_block(const chain::signed_block_ptr& block, const chain::block_id_type& id);

      // Returns last vote information by a given finalizer
      std::optional<vote_info> get_last_vote_info(const fc::crypto::blslib::bls_public_key& finalizer_pub_key) const;

      // Sets tracking_enabled
      void set_tracking_enabled(bool enabled);

   private:
      std::unique_ptr<struct tracked_votes_impl> _impl;
   };

}

FC_REFLECT( eosio::chain_apis::tracked_votes::vote_info, (description)(public_key)(is_vote_strong)(finalizer_policy_generation)(voted_for_block_id)(voted_for_block_num)(voted_for_block_timestamp))
