#include <eosio/chain/webassembly/interface.hpp>
#include <eosio/chain/webassembly/eos-vm.hpp>
#include <eosio/chain/wasm_interface.hpp>
#include <eosio/chain/apply_context.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/transaction_context.hpp>
#include <eosio/chain/producer_schedule.hpp>
#include <eosio/chain/exceptions.hpp>
#include <boost/core/ignore_unused.hpp>
#include <eosio/chain/authorization_manager.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/wasm_interface_private.hpp>
#include <eosio/chain/wasm_eosio_validation.hpp>
#include <eosio/chain/wasm_eosio_injection.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/protocol_state_object.hpp>
#include <eosio/chain/account_object.hpp>
#include <fc/exception/exception.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/crypto/sha1.hpp>
#include <fc/io/raw.hpp>

#include <softfloat.hpp>
#include <compiler_builtins.hpp>
#include <boost/asio.hpp>
#include <fstream>
#include <string.h>

#if defined(EOSIO_EOS_VM_RUNTIME_ENABLED) || defined(EOSIO_EOS_VM_JIT_RUNTIME_ENABLED)
#include <eosio/vm/allocator.hpp>
#endif

namespace eosio { namespace chain {

   wasm_interface::wasm_interface(vm_type vm, vm_oc_enable eosvmoc_tierup, const chainbase::database& d,
                                  platform_timer& main_thread_timer, const std::filesystem::path data_dir, const eosvmoc::config& eosvmoc_config, bool profile)
     : my( new wasm_interface_impl(vm, eosvmoc_tierup, d, main_thread_timer, data_dir, eosvmoc_config, profile) ) {}

   wasm_interface::~wasm_interface() {}

#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
   void wasm_interface::init_thread_local_data() {
      // OC tierup and OC runtime are mutually exclusive
      if (my->eosvmoc) {
         my->eosvmoc->init_thread_local_data();
      } else if (my->wasm_runtime_time == wasm_interface::vm_type::eos_vm_oc && my->runtime_interface) {
         my->runtime_interface->init_thread_local_data();
      }
   }
#endif

   void wasm_interface::validate(const controller& control, const bytes& code) {
      const auto& pso = control.db().get<protocol_state_object>();

      if (control.is_builtin_activated(builtin_protocol_feature_t::configurable_wasm_limits)) {
         const auto& gpo = control.get_global_properties();
         webassembly::eos_vm_runtime::validate( code, gpo.wasm_configuration, pso.whitelisted_intrinsics );
         return;
      }
      Module module;
      try {
         Serialization::MemoryInputStream stream((U8*)code.data(), code.size());
         WASM::serialize(stream, module);
      } catch(const Serialization::FatalSerializationException& e) {
         EOS_ASSERT(false, wasm_serialization_error, e.message.c_str());
      } catch(const IR::ValidationException& e) {
         EOS_ASSERT(false, wasm_serialization_error, e.message.c_str());
      }

      wasm_validations::wasm_binary_validation validator(control, module);
      validator.validate();

      webassembly::eos_vm_runtime::validate( code, pso.whitelisted_intrinsics );

      //there are a couple opportunties for improvement here--
      //Easy: Cache the Module created here so it can be reused for instantiaion
      //Hard: Kick off instantiation in a separate thread at this location
   }

   void wasm_interface::code_block_num_last_used(const digest_type& code_hash, uint8_t vm_type, uint8_t vm_version,
                                                 block_num_type first_used_block_num, block_num_type block_num_last_used)
   {
      my->code_block_num_last_used(code_hash, vm_type, vm_version, first_used_block_num, block_num_last_used);
   }

   void wasm_interface::current_lib(const uint32_t lib) {
      my->current_lib(lib);
   }

   void wasm_interface::apply( const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version, apply_context& context ) {
      if (substitute_apply && substitute_apply(code_hash, vm_type, vm_version, context))
         return;
      my->apply( code_hash, vm_type, vm_version, context );
   }

   bool wasm_interface::is_code_cached(const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version) const {
      return my->is_code_cached(code_hash, vm_type, vm_version);
   }

#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
   bool wasm_interface::is_eos_vm_oc_enabled() const {
      return my->is_eos_vm_oc_enabled();
   }

   uint64_t wasm_interface::get_eos_vm_oc_compile_interrupt_count() const {
      return my->get_eos_vm_oc_compile_interrupt_count();
   }
#endif

   wasm_instantiated_module_interface::~wasm_instantiated_module_interface() = default;
   wasm_runtime_interface::~wasm_runtime_interface() = default;

#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
   thread_local std::unique_ptr<eosvmoc::executor> wasm_interface_impl::eosvmoc_tier::exec{};
   thread_local std::unique_ptr<eosvmoc::memory>   wasm_interface_impl::eosvmoc_tier::mem{};
#endif

std::istream& operator>>(std::istream& in, wasm_interface::vm_type& runtime) {
   std::string s;
   in >> s;
   if (s == "eos-vm")
      runtime = eosio::chain::wasm_interface::vm_type::eos_vm;
   else if (s == "eos-vm-jit")
      runtime = eosio::chain::wasm_interface::vm_type::eos_vm_jit;
   else if (s == "eos-vm-oc-forced")
      runtime = eosio::chain::wasm_interface::vm_type::eos_vm_oc;
   else
      in.setstate(std::ios_base::failbit);
   return in;
}

} } /// eosio::chain
