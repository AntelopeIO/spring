#pragma once
#include <eosio/chain/code_object.hpp>
#include <eosio/chain/types.hpp>
#include <eosio/chain/whitelisted_intrinsics.hpp>
#include <eosio/chain/exceptions.hpp>
#include <functional>

namespace eosio { namespace chain {

   struct platform_timer;
   class apply_context;
   class host_context;
   class wasm_runtime_interface;
   class controller;
   namespace eosvmoc { struct config; }
   namespace webassembly { namespace eos_vm_runtime { struct validate_result; }}

   struct wasm_exit {
      int32_t code = 0;
   };

   /**
    * @class wasm_interface
    *
    */
   class wasm_interface {
      public:
         enum class vm_type {
            eos_vm,
            eos_vm_jit,
            eos_vm_oc
         };

         //return string description of vm_type
         static std::string vm_type_string(vm_type vmtype) {
             switch (vmtype) {
             case vm_type::eos_vm:
                return "eos-vm";
             case vm_type::eos_vm_oc:
                return "eos-vm-oc";
             default:
                 return "eos-vm-jit";
             }
         }

         enum class vm_oc_enable {
            oc_auto,
            oc_all,
            oc_none
         };

         wasm_interface(vm_type vm, vm_oc_enable eosvmoc_tierup, const chainbase::database& d, platform_timer& main_thread_timer, const std::filesystem::path data_dir, const eosvmoc::config& eosvmoc_config, bool profile);
         ~wasm_interface();

#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
         // initialize exec per thread
         void init_thread_local_data();

         void set_num_threads_for_call_res_pools(uint32_t num_threads);
         void set_max_call_depth_for_call_res_pools(uint32_t depth);

         // returns true if EOS VM OC is enabled
         bool is_eos_vm_oc_enabled() const;

         // return number of wasm execution interrupted by eos vm oc compile completing, used for testing
         uint64_t get_eos_vm_oc_compile_interrupt_count() const;
#endif

         //call before dtor to skip what can be minutes of dtor overhead with some runtimes; can cause leaks
         void indicate_shutting_down();

         //validates code -- does a WASM validation pass and checks the wasm against EOSIO specific constraints
         static webassembly::eos_vm_runtime::validate_result validate(const controller& control, const bytes& code);

         //returns true if the code contains a valid sync_call entry point
         static bool is_sync_call_supported(const char* code_bytes, size_t code_size);

         //indicate that a particular code probably won't be used after given block_num
         void code_block_num_last_used(const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version, const uint32_t& block_num);

         //indicate the current LIB. evicts old cache entries
         void current_lib(const uint32_t lib);

         //Calls apply/sync_call
         void execute(const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version, host_context& context);

         //Returns true if the code is cached
         bool is_code_cached(const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version) const;

         // If substitute_apply is set, then apply calls it before doing anything else. If substitute_apply returns true,
         // then apply returns immediately. Provided function must be multi-thread safe.
         std::function<bool(const digest_type& code_hash, uint8_t vm_type, uint8_t vm_version, apply_context& context)> substitute_apply;

      private:
         unique_ptr<struct wasm_interface_impl> my;
   };

} } // eosio::chain

namespace eosio{ namespace chain {
   std::istream& operator>>(std::istream& in, wasm_interface::vm_type& runtime);
   inline std::ostream& operator<<(std::ostream& os, wasm_interface::vm_oc_enable t) {
      if (t == wasm_interface::vm_oc_enable::oc_auto) {
         os << "auto";
      } else if (t == wasm_interface::vm_oc_enable::oc_all) {
         os << "all";
      } else if (t == wasm_interface::vm_oc_enable::oc_none) {
         os << "none";
      }
      return os;
   }
}}

FC_REFLECT_ENUM( eosio::chain::wasm_interface::vm_type, (eos_vm)(eos_vm_jit)(eos_vm_oc) )
