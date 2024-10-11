#include <eosio/chain_plugin/get_info_db.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/application.hpp>

#include <shared_mutex>

using namespace eosio;
using namespace eosio::chain;
using namespace appbase;

namespace eosio::chain_apis {
   /**
    * Implementation details of the get_info results
    */
   struct get_info_db_impl {
      get_info_db_impl(const chain::controller& controller, bool get_info_enabled)
         : controller(controller)
         , get_info_enabled(get_info_enabled)
      {
         if (!get_info_enabled) {
            return;
         }

         // fixed part
         cached_results.server_version             = fc::itoh(static_cast<uint32_t>(app().version()));
         cached_results.chain_id                   = controller.get_chain_id();
         cached_results.server_version_string      = app().version_string();
         cached_results.server_full_version_string = app().full_version_string();

         // chain head part
         store_chain_head_part();

         // fork_db part
         store_fork_db_part();

         // resource limits part is not applicable at initializtion
      }

      // Called on accepted_block signal.
      void on_accepted_block() {
         try {
            if (!get_info_enabled) {
               return;
            }

            std::unique_lock write_lock(rw_mutex);
            store_all_parts();
         } FC_LOG_AND_DROP(("get_info_db_impl on_accepted_block ERROR"));
      }

      // Called on irreversible_block signal.
      void on_irreversible_block() {
         try {
            if (!get_info_enabled) {
               return;
            }

            std::unique_lock write_lock(rw_mutex);
            store_fork_db_part();
            store_resource_limits_part();
         } FC_LOG_AND_DROP(("get_info_db_impl on_irreversible_block ERROR"));
      }

      // Returns cached get_info results
      get_info_db::get_info_results get_info() {
         {
            std::shared_lock read_lock(rw_mutex);
            if (cached_results.head_block_num != 0 &&
                cached_results.last_irreversible_block_num != 0) {
               // Most common cases
               return cached_results;
            }
         }

         // This only happens right after initialization when starts from
         // snapshot as no signals are emitted. We need to cache the current states.
         std::unique_lock write_lock(rw_mutex);
         store_all_parts();

         return cached_results;
      }

   private:
      // A handle to the controller.
      const chain::controller& controller;

      // Indication whether get_info RPC is enabled.
      const bool get_info_enabled = false;

      // Cache to store the current get_info results in respect to latest accepted block.
      mutable std::shared_mutex      rw_mutex;
      get_info_db::get_info_results  cached_results;

      // caller holds a mutex
      void store_chain_head_part() {
         // chain head part
         const auto& head = controller.head();
         if (head.is_valid()) {
            cached_results.head_block_id       = head.id();
            cached_results.head_block_num      = block_header::num_from_id(cached_results.head_block_id);
            cached_results.head_block_time     = head.block_time();
            cached_results.head_block_producer = head.producer();
         }
      }

      // caller holds a mutex
      void store_fork_db_part() {
         // fork_db part
         if (controller.fork_db_has_root()) {
            cached_results.last_irreversible_block_id   = controller.last_irreversible_block_id();
            cached_results.last_irreversible_block_num  = block_header::num_from_id(cached_results.last_irreversible_block_id);
            cached_results.last_irreversible_block_time = controller.last_irreversible_block_time();
            cached_results.fork_db_head_block_id        = controller.fork_db_head().id();
            cached_results.fork_db_head_block_num       = block_header::num_from_id(*cached_results.fork_db_head_block_id);
            cached_results.earliest_available_block_num = controller.earliest_available_block_num();
         }
      }

      // caller holds a mutex
      void store_resource_limits_part() {
         const auto& rm = controller.get_resource_limits_manager();

         cached_results.virtual_block_cpu_limit = rm.get_virtual_block_cpu_limit();
         cached_results.virtual_block_net_limit = rm.get_virtual_block_net_limit();
         cached_results.block_cpu_limit         = rm.get_block_cpu_limit();
         cached_results.block_net_limit         = rm.get_block_net_limit();
         cached_results.total_cpu_weight        = rm.get_total_cpu_weight();
         cached_results.total_net_weight        = rm.get_total_net_weight();
      }

      // caller holds a mutex
      void store_all_parts() {
         store_chain_head_part();
         store_fork_db_part();
         store_resource_limits_part();
      }
   }; // get_info_db_impl

   get_info_db::get_info_db( const chain::controller& controller, bool get_info_enabled )
      :_impl(std::make_unique<get_info_db_impl>(controller, get_info_enabled)) {}

   get_info_db::~get_info_db() = default;

   void get_info_db::on_accepted_block() {
      _impl->on_accepted_block();
   }

   void get_info_db::on_irreversible_block() {
      _impl->on_irreversible_block();
   }

   get_info_db::get_info_results get_info_db::get_info() const {
      return _impl->get_info();
   }
} // namespace eosio::chain_apis
