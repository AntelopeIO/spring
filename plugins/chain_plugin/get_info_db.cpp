#include <eosio/chain_plugin/get_info_db.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/application.hpp>

#include <shared_mutex>

using namespace eosio;
using namespace appbase;

namespace eosio::chain_apis {
   /**
    * Implementation details of the get_info results
    */
   struct get_info_db_impl {
      get_info_db_impl(const chain::controller& controller, bool get_info_enabled)
         : controller(controller)
         , get_info_enabled(get_info_enabled) {}

      // A handle to the controller.
      const chain::controller& controller;

      // Indication whether get_info RPC is enabled.
      bool get_info_enabled = false;

      // Cache to store the current get_info results in respect to latest accepted block.
      get_info_db::get_info_results  cached_results;
      mutable std::shared_mutex      rw_mutex;

      // Called on accepted_block signal.
      void on_accepted_block() {
         try {
            if (!get_info_enabled) {
               return;
            }

            std::unique_lock write_lock(rw_mutex);

            const auto& rm       = controller.get_resource_limits_manager();
            auto        head_id  = controller.head().id();
            auto        lib_id   = controller.last_irreversible_block_id();
            auto        fhead_id = controller.fork_db_head().id();

            // cache get_info results
            cached_results = get_info_db::get_info_results {
               itoh(static_cast<uint32_t>(app().version())),
               controller.get_chain_id(),
               chain::block_header::num_from_id(head_id),
               chain::block_header::num_from_id(lib_id),
               lib_id,
               head_id,
               controller.head().block_time(),
               controller.head().producer(),
               rm.get_virtual_block_cpu_limit(),
               rm.get_virtual_block_net_limit(),
               rm.get_block_cpu_limit(),
               rm.get_block_net_limit(),
               app().version_string(),
               chain::block_header::num_from_id(fhead_id),
               fhead_id,
               app().full_version_string(),
               rm.get_total_cpu_weight(),
               rm.get_total_net_weight(),
               controller.earliest_available_block_num(),
               controller.last_irreversible_block_time()
            };
         } FC_LOG_AND_DROP(("get_info_db_impl on_accepted_block ERROR"));
      }

      // Returns cached get_info results
      get_info_db::get_info_results get_info() const {
         std::shared_lock read_lock(rw_mutex);

         return cached_results;
      }
   }; // get_info_db_impl

   get_info_db::get_info_db( const chain::controller& controller, bool get_info_enabled )
      :_impl(std::make_unique<get_info_db_impl>(controller, get_info_enabled)) {}

   get_info_db::~get_info_db() = default;

   void get_info_db::on_accepted_block() {
      _impl->on_accepted_block();
   }

   get_info_db::get_info_results get_info_db::get_info() const {
      return _impl->get_info();
   }
} // namespace eosio::chain_apis
