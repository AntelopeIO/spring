#pragma once

#include <eosio/chain/wasm_interface.hpp>
#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
#include <eosio/chain/webassembly/eos-vm-oc.hpp>
#else
#define _REGISTER_EOSVMOC_INTRINSIC(CLS, MOD, METHOD, WASM_SIG, NAME, SIG)
#endif
#include <eosio/chain/webassembly/runtime_interface.hpp>
#include <eosio/chain/wasm_eosio_injection.hpp>
#include <eosio/chain/transaction_context.hpp>
#include <eosio/chain/code_object.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/thread_utils.hpp>
#include <fc/scoped_exit.hpp>

#include "IR/Module.h"
#include "Platform/Platform.h"
#include "WAST/WAST.h"
#include "IR/Validate.h"

#include <eosio/chain/webassembly/eos-vm.hpp>
#include <eosio/vm/allocator.hpp>

#include <mutex>

using namespace fc;
using namespace eosio::chain::webassembly;
using namespace IR;

using boost::multi_index_container;

namespace eosio { namespace chain {

   namespace eosvmoc { struct config; }

   struct wasm_interface_impl {
      struct wasm_cache_entry {
         digest_type                                          code_hash;
         uint32_t                                             last_block_num_used;
         std::unique_ptr<wasm_instantiated_module_interface>  module;
         uint8_t                                              vm_type = 0;
         uint8_t                                              vm_version = 0;
      };
      struct by_hash;
      struct by_last_block_num;

#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
struct eosvmoc_tier {
   // Called from main thread
   eosvmoc_tier(const std::filesystem::path& d, const eosvmoc::config& c, const chainbase::database& db,
                eosvmoc::code_cache_async::compile_complete_callback cb)
      : cc(d, c, db, std::move(cb)) {
      // Construct exec and mem for the main thread
      exec = std::make_unique<eosvmoc::executor>(cc);
      mem  = std::make_unique<eosvmoc::memory>(wasm_constraints::maximum_linear_memory/wasm_constraints::wasm_page_size);
   }

   // Called from read-only threads
   void init_thread_local_data() {
      exec = std::make_unique<eosvmoc::executor>(cc);
      mem  = std::make_unique<eosvmoc::memory>(eosvmoc::memory::sliced_pages_for_ro_thread);
   }

   eosvmoc::code_cache_async cc;

   // Each thread requires its own exec and mem. Defined in wasm_interface.cpp
   thread_local static std::unique_ptr<eosvmoc::executor> exec;
   thread_local static std::unique_ptr<eosvmoc::memory>   mem;
};
#endif

      wasm_interface_impl(wasm_interface::vm_type vm, wasm_interface::vm_oc_enable eosvmoc_tierup, const chainbase::database& d,
                          platform_timer& main_thread_timer, const std::filesystem::path data_dir,
                          const eosvmoc::config& eosvmoc_config, bool profile)
         : db(d)
         , main_thread_timer(main_thread_timer)
         , wasm_runtime_time(vm)
         , eosvmoc_tierup(eosvmoc_tierup)
      {
#ifdef EOSIO_EOS_VM_RUNTIME_ENABLED
         if(vm == wasm_interface::vm_type::eos_vm)
            runtime_interface = std::make_unique<webassembly::eos_vm_runtime::eos_vm_runtime<eosio::vm::interpreter>>();
#endif
#ifdef EOSIO_EOS_VM_JIT_RUNTIME_ENABLED
         if(vm == wasm_interface::vm_type::eos_vm_jit && profile) {
            eosio::vm::set_profile_interval_us(200);
            runtime_interface = std::make_unique<webassembly::eos_vm_runtime::eos_vm_profile_runtime>();
         }
         if(vm == wasm_interface::vm_type::eos_vm_jit && !profile)
            runtime_interface = std::make_unique<webassembly::eos_vm_runtime::eos_vm_runtime<eosio::vm::jit>>();
#endif
#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
         if(vm == wasm_interface::vm_type::eos_vm_oc)
            runtime_interface = std::make_unique<webassembly::eosvmoc::eosvmoc_runtime>(data_dir, eosvmoc_config, d);
#endif
         if(!runtime_interface)
            EOS_THROW(wasm_exception, "${r} wasm runtime not supported on this platform and/or configuration", ("r", vm));

#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
         if(eosvmoc_tierup != wasm_interface::vm_oc_enable::oc_none) {
            EOS_ASSERT(vm != wasm_interface::vm_type::eos_vm_oc, wasm_exception, "You can't use EOS VM OC as the base runtime when tier up is activated");
            eosvmoc = std::make_unique<eosvmoc_tier>(data_dir, eosvmoc_config, d, [this](boost::asio::io_context& ctx, const digest_type& code_id, fc::time_point queued_time) {
               async_compile_complete(ctx, code_id, queued_time);
            });
         }
#endif
      }

      ~wasm_interface_impl() = default;

#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
      // called from async thread
      void async_compile_complete(boost::asio::io_context& ctx, const digest_type& code_id, fc::time_point queued_time) {
         if (executing_code_hash.load() == code_id) { // is action still executing?
            auto elapsed = fc::time_point::now() - queued_time;
            auto expire_in = std::max(fc::microseconds(0), fc::milliseconds(500) - elapsed);
            std::shared_ptr<boost::asio::steady_timer> timer = std::make_shared<boost::asio::steady_timer>(ctx);
            timer->expires_from_now(std::chrono::microseconds(expire_in.count()));
            timer->async_wait([timer, this, code_id](const boost::system::error_code& ec) {
               if (ec)
                  return;
               if (executing_code_hash.load() == code_id) {
                  ilog("EOS VM OC tier up interrupting ${id}", ("id", code_id));
                  eos_vm_oc_compile_interrupt = true;
                  main_thread_timer.interrupt_timer();
               }
            });
         }
      }
#endif

      void apply( const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version, apply_context& context ) {
         bool attempt_tierup = false;
#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
         attempt_tierup = eosvmoc && (eosvmoc_tierup == wasm_interface::vm_oc_enable::oc_all || context.should_use_eos_vm_oc());
         if (attempt_tierup) {
            const chain::eosvmoc::code_descriptor* cd = nullptr;
            chain::eosvmoc::code_cache_base::get_cd_failure failure = chain::eosvmoc::code_cache_base::get_cd_failure::temporary;
            try {
               // Ideally all validator nodes would switch to using oc before block producer nodes so that validators
               // are never overwhelmed. Compile whitelisted account contracts first on non-produced blocks. This makes
               // it more likely that validators will switch to the oc compiled contract before the block producer runs
               // an action for the contract with oc.
               chain::eosvmoc::code_cache_async::mode m;
               m.whitelisted = context.is_eos_vm_oc_whitelisted();
               m.high_priority = m.whitelisted && context.is_applying_block();
               m.write_window = context.control.is_write_window();
               cd = eosvmoc->cc.get_descriptor_for_code(m, context.get_receiver(), code_hash, vm_version, failure);
            } catch (...) {
               // swallow errors here, if EOS VM OC has gone in to the weeds we shouldn't bail: continue to try and run baseline
               // In the future, consider moving bits of EOS VM that can fire exceptions and such out of this call path
               static bool once_is_enough;
               if (!once_is_enough)
                  elog("EOS VM OC has encountered an unexpected failure");
               once_is_enough = true;
            }
            if (cd) {
               if (!context.is_applying_block()) // read_only_trx_test.py looks for this log statement
                  tlog("${a} speculatively executing ${h} with eos vm oc", ("a", context.get_receiver())("h", code_hash));
               eosvmoc->exec->execute(*cd, *eosvmoc->mem, context);
               return;
            }
         }
#endif
         // Do not allow oc interrupt if no undo as the transaction needs to be undone to restart it.
         // Do not allow oc interrupt if implicit or scheduled. There are two implicit trxs: onblock and onerror.
         //   The onerror trx of deferred trxs is implicit. Interrupt needs to be disabled for deferred trxs because
         //   they capture all exceptions, explicitly handle undo stack, and directly call trx_context.execute_action.
         //   Not allowing interrupt for onblock seems rather harmless, so instead of distinguishing between onerror and
         //   onblock, just disallow for all implicit.
         const bool allow_oc_interrupt = attempt_tierup && context.is_applying_block() &&
                                         context.trx_context.has_undo() && !context.trx_context.is_implicit() && !context.trx_context.is_scheduled();
         auto ex = fc::make_scoped_exit([&]() {
            if (allow_oc_interrupt) {
               eos_vm_oc_compile_interrupt = false;
               executing_code_hash.store({}); // indicate no longer executing
            }
         });
         if (allow_oc_interrupt)
            executing_code_hash.store(code_hash);
         try {
            get_instantiated_module(code_hash, vm_type, vm_version, context.trx_context)->apply(context);
         } catch (const interrupt_exception& e) {
            if (allow_oc_interrupt && eos_vm_oc_compile_interrupt && main_thread_timer.timer_state() == platform_timer::state_t::interrupted) {
               ++eos_vm_oc_compile_interrupt_count;
               dlog("EOS VM OC compile complete interrupt of ${r} <= ${a}::${act} code ${h}, interrupt #${c}",
                    ("r", context.get_receiver())("a", context.get_action().account)
                    ("act", context.get_action().name)("h", code_hash)("c", eos_vm_oc_compile_interrupt_count));
               EOS_THROW(interrupt_oc_exception, "EOS VM OC compile complete interrupt of ${r} <= ${a}::${act} code ${h}, interrupt #${c}",
                    ("r", context.get_receiver())("a", context.get_action().account)
                    ("act", context.get_action().name)("h", code_hash)("c", eos_vm_oc_compile_interrupt_count));
            }
            throw;
         }
      }

      // used for testing
      uint64_t get_eos_vm_oc_compile_interrupt_count() const { return eos_vm_oc_compile_interrupt_count; }

      bool is_code_cached(const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version) const {
         // This method is only called from tests; performance is not critical.
         // No need for an additional check if we should lock or not.
         std::lock_guard g(instantiation_cache_mutex);
         wasm_cache_index::iterator it = wasm_instantiation_cache.find( boost::make_tuple(code_hash, vm_type, vm_version) );
         return it != wasm_instantiation_cache.end();
      }

      void code_block_num_last_used(const digest_type& code_hash, uint8_t vm_type, uint8_t vm_version,
                                    block_num_type first_used_block_num, block_num_type block_num_last_used)
      {
         // The caller of this method apply_eosio_setcode has asserted that
         // the transaction is not read-only, implying we are
         // in write window. Read-only threads are not running.
         // Safe to update the cache without locking.
         wasm_cache_index::iterator it = wasm_instantiation_cache.find(boost::make_tuple(code_hash, vm_type, vm_version));
         if(it != wasm_instantiation_cache.end()) {
            if (first_used_block_num == block_num_last_used) {
               // First used and no longer needed in the same block, erase immediately, do not wait for LIB.
               // Since created and destroyed in the same block, likely will not be needed in a forked block.
               // Prevents many setcodes in the same block using up space in the cache.
               wasm_instantiation_cache.erase(it);
            } else {
               wasm_instantiation_cache.modify(it, [block_num_last_used](wasm_cache_entry& e) {
                  e.last_block_num_used = block_num_last_used;
               });
            }
         }

#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
         // see comment above
         if (first_used_block_num == block_num_last_used && eosvmoc)
            eosvmoc->cc.free_code(code_hash, vm_version);
#endif
      }

      // reports each code_hash and vm_version that will be erased to callback
      void current_lib(uint32_t lib) {
         // producer_plugin has asserted irreversible_block signal is called
         // in write window. Read-only threads are not running.
         // Safe to update the cache without locking.
         // Anything last used before or on the LIB can be evicted.
         const auto first_it = wasm_instantiation_cache.get<by_last_block_num>().begin();
         const auto last_it  = wasm_instantiation_cache.get<by_last_block_num>().upper_bound(lib);
#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
         if(eosvmoc) for(auto it = first_it; it != last_it; it++)
            eosvmoc->cc.free_code(it->code_hash, it->vm_version);
#endif
         wasm_instantiation_cache.get<by_last_block_num>().erase(first_it, last_it);
      }

#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
      bool is_eos_vm_oc_enabled() const {
         return (eosvmoc || wasm_runtime_time == wasm_interface::vm_type::eos_vm_oc);
      }
#endif

      const std::unique_ptr<wasm_instantiated_module_interface>& get_instantiated_module(
         const digest_type&   code_hash,
         const uint8_t&       vm_type,
         const uint8_t&       vm_version,
         transaction_context& trx_context)
      {
         if (trx_context.control.is_write_window()) {
            // When in write window (either read only threads are not enabled or
            // they are not schedued to run), only main thread is processing
            // transactions. No need to lock.
            return get_or_build_instantiated_module(code_hash, vm_type, vm_version, trx_context);
         } else {
            std::lock_guard g(instantiation_cache_mutex);
            return get_or_build_instantiated_module(code_hash, vm_type, vm_version, trx_context);
         }
      }

      // Locked by the caller if required.
      const std::unique_ptr<wasm_instantiated_module_interface>& get_or_build_instantiated_module(
         const digest_type&   code_hash,
         const uint8_t&       vm_type,
         const uint8_t&       vm_version,
         transaction_context& trx_context )
      {
         wasm_cache_index::iterator it = wasm_instantiation_cache.find( boost::make_tuple(code_hash, vm_type, vm_version) );
         if (it != wasm_instantiation_cache.end()) {
            // An instantiated module's module should never be null.
            assert(it->module);
            return it->module;
         }

         const code_object* codeobject = &db.get<code_object,by_code_hash>(boost::make_tuple(code_hash, vm_type, vm_version));
         it = wasm_instantiation_cache.emplace( wasm_interface_impl::wasm_cache_entry {
            .code_hash = code_hash,
            .last_block_num_used = UINT32_MAX,
            .module = nullptr,
            .vm_type = vm_type,
            .vm_version = vm_version
         } ).first;
         auto timer_pause = fc::make_scoped_exit([&](){
            trx_context.resume_billing_timer();
         });
         trx_context.pause_billing_timer();
         wasm_instantiation_cache.modify(it, [&](auto& c) {
            c.module = runtime_interface->instantiate_module(codeobject->code.data(), codeobject->code.size(), code_hash, vm_type, vm_version);
         });
         return it->module;
      }

      std::unique_ptr<wasm_runtime_interface> runtime_interface;

      typedef boost::multi_index_container<
         wasm_cache_entry,
         indexed_by<
            ordered_unique<tag<by_hash>,
               composite_key< wasm_cache_entry,
                  member<wasm_cache_entry, digest_type, &wasm_cache_entry::code_hash>,
                  member<wasm_cache_entry, uint8_t,     &wasm_cache_entry::vm_type>,
                  member<wasm_cache_entry, uint8_t,     &wasm_cache_entry::vm_version>
               >
            >,
            ordered_non_unique<tag<by_last_block_num>, member<wasm_cache_entry, uint32_t, &wasm_cache_entry::last_block_num_used>>
         >
      > wasm_cache_index;
      mutable std::mutex instantiation_cache_mutex;
      wasm_cache_index wasm_instantiation_cache;

      const chainbase::database& db;
      platform_timer& main_thread_timer;
      const wasm_interface::vm_type wasm_runtime_time;
      const wasm_interface::vm_oc_enable eosvmoc_tierup;
      large_atomic<digest_type> executing_code_hash{};
      std::atomic<bool> eos_vm_oc_compile_interrupt{false};
      uint32_t eos_vm_oc_compile_interrupt_count{0}; // for testing

#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
      std::unique_ptr<struct eosvmoc_tier> eosvmoc{nullptr}; // used by all threads
#endif
   };

} } // eosio::chain
