#include <eosio/chain_plugin/get_info_db.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/application.hpp>

//libstdc++ flags atomic_*<shared_ptr> as deprecated in c++20. while libstdc++ 12 adds atomic<shared_ptr>, it is
// still missing in libc++ 19
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

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
            // In IRREVERSIBLE mode, it is expected get_info to return the same
            // head_block_num and last_irreversible_block_num.
            // But a get_info request can come between accepted_block signal and
            // irreversible_block signal, which results in inconsistencies.
            // On accepted_block, only store get_info results if the mode is not IRREVERSIBLE.
            // (for IRREVERSIBLE mode, get_info results are stored on rreversible_block signal)
            if (get_info_enabled && controller.get_read_mode() != db_read_mode::IRREVERSIBLE) {
               store_info();
            }
         } FC_LOG_AND_DROP(("get_info_db_impl on_accepted_block ERROR"));
      }

      // Called on irreversible_block signal.
      void on_irreversible_block(const chain::signed_block_ptr& block, const block_id_type& id) {
         try {
            if (get_info_enabled) {
               store_info(block, id);
            }
         } FC_LOG_AND_DROP(("get_info_db_impl on_irreversible_block ERROR"));
      }

      // Returns cached get_info results
      get_info_db::get_info_results get_info() {
         // safely load the info_cache pointer
         std::shared_ptr<get_info_db::get_info_results> info = std::atomic_load(&info_cache);

         if (info && info->contains_full_data()) {
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
      // Using std::atomic_load and std::atomic_store to switch pointers.
      std::shared_ptr<get_info_db::get_info_results> info_cache = nullptr;

      // Fixed data
      std::string           server_version;
      chain::chain_id_type  chain_id;
      std::string           server_version_string;
      std::string           server_full_version_string;

      // Stores common data, and returns fork_db_has_root for future uses to avoid
      // multiple mutexes in fork db.
      bool store_info_common(const std::shared_ptr<get_info_db::get_info_results>& info) {
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
         const auto& fork_db_head = controller.fork_db_head();
         bool fork_db_has_root = fork_db_head.is_valid(); // a valid head implies fork_db has root
         if (fork_db_has_root) {
            info->fork_db_head_block_id        = fork_db_head.id();
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

         return fork_db_has_root;
      }

      void store_info() {
         std::shared_ptr<get_info_db::get_info_results> info = std::make_shared<get_info_db::get_info_results>();

         bool fork_db_has_root = store_info_common(info); // store_info_common returns fork_db_has_root to avoid mutex in fork db in call to controller.fork_db_has_root()

         if (fork_db_has_root) {
            const auto& root = controller.fork_db_root(); // avoid multiple mutexes in fork db
            info->last_irreversible_block_id   = root.id();
            info->last_irreversible_block_num  = block_header::num_from_id(info->last_irreversible_block_id);
            info->last_irreversible_block_time = root.block_time();
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

#pragma GCC diagnostic pop
