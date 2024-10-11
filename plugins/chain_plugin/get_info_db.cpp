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
         , server_version(fc::itoh(static_cast<uint32_t>(app().version())))
         , chain_id(controller.get_chain_id())
         , server_version_string(app().version_string())
         , server_full_version_string(app().full_version_string()) {}

      // Called on accepted_block signal.
      void on_accepted_block() {
         try {
            if (!get_info_enabled) {
               return;
            }

            store_info();
         } FC_LOG_AND_DROP(("get_info_db_impl on_accepted_block ERROR"));
      }

      // Called on irreversible_block signal.
      void on_irreversible_block(const chain::signed_block_ptr& block, const block_id_type& id) {
         try {
            if (!get_info_enabled) {
               return;
            }

            store_info(block, id);
         } FC_LOG_AND_DROP(("get_info_db_impl on_irreversible_block ERROR"));
      }

      // Returns cached get_info results
      get_info_db::get_info_results get_info() {
         // safely load the info_cache pointer
         std::shared_ptr<get_info_db::get_info_results> info = std::atomic_load(&info_cache);

         if (info && info->head_block_num > 0 && info->last_irreversible_block_num > 0 && info->fork_db_head_block_num > 0) {
            return *info;
         }

         // This only happens right after initialization when starts from
         // snapshot as no signals are emitted. We need to cache the current states.
         store_info();

         info = std::atomic_load(&info_cache);
         assert(info);
         return *info;
      }

   private:
      // A handle to the controller.
      const chain::controller& controller;

      // Indication whether get_info RPC is enabled.
      const bool get_info_enabled = false;

      // Cache to store the current get_info results.
      // Lock free by using std::atomic_load and std::atomic_store.
      std::shared_ptr<get_info_db::get_info_results> info_cache = nullptr;

      // Fixed data
      std::string           server_version;
      chain::chain_id_type  chain_id;
      std::string           server_version_string;
      std::string           server_full_version_string;

      void store_info_common(const std::shared_ptr<get_info_db::get_info_results>& info) {
         assert(info);

         // fixed part
         info->server_version             = server_version;
         info->chain_id                   = chain_id;
         info->server_version_string      = server_version_string;
         info->server_full_version_string = server_full_version_string;

         // chain head part
         const auto& head = controller.head();
         if (head.is_valid()) {
            info->head_block_id       = head.id();
            info->head_block_num      = block_header::num_from_id(info->head_block_id);
            info->head_block_time     = head.block_time();
            info->head_block_producer = head.producer();
         }

         // fork_db part
         if (controller.fork_db_has_root()) {
            info->fork_db_head_block_id        = controller.fork_db_head().id();
            info->fork_db_head_block_num       = block_header::num_from_id(*info->fork_db_head_block_id);
            info->earliest_available_block_num = controller.earliest_available_block_num();
         }

         // resource_limits
         const auto& rm = controller.get_resource_limits_manager();
         info->virtual_block_cpu_limit = rm.get_virtual_block_cpu_limit();
         info->virtual_block_net_limit = rm.get_virtual_block_net_limit();
         info->block_cpu_limit         = rm.get_block_cpu_limit();
         info->block_net_limit         = rm.get_block_net_limit();
         info->total_cpu_weight        = rm.get_total_cpu_weight();
         info->total_net_weight        = rm.get_total_net_weight();
      }

      void store_info() {
         std::shared_ptr<get_info_db::get_info_results> info = std::make_shared<get_info_db::get_info_results>();

         store_info_common(info);

         if (controller.fork_db_has_root()) {
            info->last_irreversible_block_id   = controller.fork_db_root().id();
            info->last_irreversible_block_num  = block_header::num_from_id(info->last_irreversible_block_id);
            info->last_irreversible_block_time = controller.fork_db_root().block_time();
         }

         std::atomic_store(&info_cache, info);  // replace current cache safely
      }

      void store_info(const chain::signed_block_ptr& block, const block_id_type& id) {
         std::shared_ptr<get_info_db::get_info_results> info = std::make_shared<get_info_db::get_info_results>();

         store_info_common(info);

         info->last_irreversible_block_id   = id;
         info->last_irreversible_block_num  = block_header::num_from_id(info->last_irreversible_block_id);
         info->last_irreversible_block_time = block->timestamp;

         std::atomic_store(&info_cache, info);  // replace current cache safely
      }
   }; // get_info_db_impl

   get_info_db::get_info_db( const chain::controller& controller, bool get_info_enabled )
      :_impl(std::make_unique<get_info_db_impl>(controller, get_info_enabled)) {}

   get_info_db::~get_info_db() = default;

   void get_info_db::on_accepted_block() {
      _impl->on_accepted_block();
   }

   void get_info_db::on_irreversible_block(const chain::signed_block_ptr& block, const block_id_type& id) {
      _impl->on_irreversible_block(block, id);
   }

   get_info_db::get_info_results get_info_db::get_info() const {
      return _impl->get_info();
   }
} // namespace eosio::chain_apis
