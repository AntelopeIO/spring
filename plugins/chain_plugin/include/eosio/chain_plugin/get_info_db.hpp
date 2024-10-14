#pragma once

#include <eosio/chain/controller.hpp>

namespace eosio::chain_apis {
   /**
    * This class manages the ephemeral data that is needed by `get_info` RPC call.
    * There is no persistence and the data is recreated when the class is instantiated
    * based on the current state of the chain.
    */

   class get_info_db {
   public:

      /**
       * Instantiate a get_info results cache from the given chain controller.
       * The caller is expected to manage lifetimes such that this controller
       * reference does not go stale for the life of the cache.
       * The cache is updated whenever accepted_block signal is received.
       *
       * @param chain            - controller to read data from
       * @param get_info_enabled - true if get_info RPC endpoint is enabled
       */
      get_info_db( const class eosio::chain::controller& chain, bool get_info_enabled );
      ~get_info_db();

      /**
       * get_info results
       */
      struct get_info_results {
         std::string                          server_version;
         chain::chain_id_type                 chain_id;
         uint32_t                             head_block_num = 0;
         uint32_t                             last_irreversible_block_num = 0;
         chain::block_id_type                 last_irreversible_block_id;
         chain::block_id_type                 head_block_id;
         fc::time_point                       head_block_time;
         chain::account_name                  head_block_producer;

         uint64_t                             virtual_block_cpu_limit = 0;
         uint64_t                             virtual_block_net_limit = 0;

         uint64_t                             block_cpu_limit = 0;
         uint64_t                             block_net_limit = 0;
         std::optional<std::string>           server_version_string;
         std::optional<uint32_t>              fork_db_head_block_num;
         std::optional<chain::block_id_type>  fork_db_head_block_id;
         std::optional<std::string>           server_full_version_string;
         std::optional<uint64_t>              total_cpu_weight;
         std::optional<uint64_t>              total_net_weight;
         std::optional<uint32_t>              earliest_available_block_num;
         std::optional<fc::time_point>        last_irreversible_block_time;

         // Returns true if the struct contains full data for using.
         bool contains_full_data() const {
            return (head_block_num > 0) && (last_irreversible_block_num > 0) && (fork_db_head_block_num > 0);
         }
      };

      // Called on accepted_block signal
      void on_accepted_block();

      // Called on irreversible_block signal
      void on_irreversible_block(const chain::signed_block_ptr& block, const chain::block_id_type& lib);

      // Returns the cached get_info results
      get_info_results get_info() const;

   private:
      std::unique_ptr<struct get_info_db_impl> _impl;
   }; // get_info_db
} // namespace eosio::chain_apis

FC_REFLECT(eosio::chain_apis::get_info_db::get_info_results,
           (server_version)(chain_id)(head_block_num)(last_irreversible_block_num)(last_irreversible_block_id)
           (head_block_id)(head_block_time)(head_block_producer)
           (virtual_block_cpu_limit)(virtual_block_net_limit)(block_cpu_limit)(block_net_limit)
           (server_version_string)(fork_db_head_block_num)(fork_db_head_block_id)(server_full_version_string)
           (total_cpu_weight)(total_net_weight)(earliest_available_block_num)(last_irreversible_block_time))
