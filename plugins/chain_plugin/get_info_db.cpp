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
         , chain_id(controller.get_chain_id())
         , version_hex(fc::itoh(static_cast<uint32_t>(app().version())))
         , version_string(app().version_string())
         , full_version_string(app().full_version_string()) {}

      // A handle to the controller.
      const chain::controller& controller;

      // Indication whether get_info RPC is enabled.
      const bool get_info_enabled = false;

      // Cache to store the current get_info results in respect to latest accepted block.
      mutable std::shared_mutex      rw_mutex;
      get_info_db::get_info_results  cached_results;

      // Values are never changed
      const chain_id_type chain_id;
      const std::string   version_hex;
      const std::string   version_string;
      const std::string   full_version_string;

      // Called on accepted_block signal.
      void on_accepted_block() {
         try {
            if (!get_info_enabled) {
               return;
            }

            std::unique_lock write_lock(rw_mutex);

            const auto& rm        = controller.get_resource_limits_manager();

            // chain head and forkdb root do not have values during replay
            const auto& head      = controller.head();
            auto head_is_valid    = head.is_valid();
            auto head_id          = head_is_valid ? head.id() : block_id_type{};
            auto block_time       = head_is_valid ? head.block_time() : time_point{};
            auto producer         = head_is_valid ? head.producer() : account_name{};

            auto fork_db_has_root = controller.fork_db_has_root();
            auto lib_id           = fork_db_has_root ? controller.last_irreversible_block_id() : block_id_type{};
            auto lib_time         = fork_db_has_root ? controller.last_irreversible_block_time() : time_point{};
            auto fhead_id         = fork_db_has_root ? controller.fork_db_head().id() : block_id_type{};

            // cache get_info results
            cached_results = get_info_db::get_info_results {
               version_hex,
               chain_id,
               chain::block_header::num_from_id(head_id),
               chain::block_header::num_from_id(lib_id),
               lib_id,
               head_id,
               block_time,
               producer,
               rm.get_virtual_block_cpu_limit(),
               rm.get_virtual_block_net_limit(),
               rm.get_block_cpu_limit(),
               rm.get_block_net_limit(),
               version_string,
               chain::block_header::num_from_id(fhead_id),
               fhead_id,
               full_version_string,
               rm.get_total_cpu_weight(),
               rm.get_total_net_weight(),
               controller.earliest_available_block_num(),
               lib_time
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
