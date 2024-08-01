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
       * @param chain - controller to read data from
       */
      explicit tracked_votes( const class eosio::chain::controller& chain );
      ~tracked_votes();

      /**
       * vote information for a given finalizer.
       */
      struct vote_info {
         std::string          public_key;
         std::string          description;
         bool                 is_vote_strong{false};
         uint32_t             voted_policy_generation{0};
         chain::block_id_type voted_block_id;
         uint32_t             voted_block_num{0};
         fc::time_point       voted_block_timestamp;
      };

      // Called on accepted_block signal. Retrieve vote information from
      // QC in the block and store it in last_votes.
      void on_accepted_block(const chain::signed_block_ptr& block, const chain::block_id_type& id);

      // Returns last vote information by a given finalizer
      std::optional<vote_info> get_last_vote_info(const std::string& finalizer_pub_key) const;

   private:
      std::unique_ptr<struct tracked_votes_impl> _impl;
   };

}

FC_REFLECT( eosio::chain_apis::tracked_votes::vote_info, (public_key)(description)(is_vote_strong)(voted_policy_generation)(voted_block_id)(voted_block_num)(voted_block_timestamp))
