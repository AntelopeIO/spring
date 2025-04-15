#include <eosio/chain/webassembly/eos-vm-oc.hpp>
#include <eosio/chain/wasm_eosio_constraints.hpp>
#include <eosio/chain/wasm_eosio_injection.hpp>
#include <eosio/chain/host_context.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/global_property_object.hpp>

#include <vector>
#include <iterator>

namespace eosio { namespace chain { namespace webassembly { namespace eosvmoc {

class eosvmoc_instantiated_module : public wasm_instantiated_module_interface {
   public:
      eosvmoc_instantiated_module(const digest_type& code_hash, const uint8_t& vm_version, eosvmoc_runtime& wr) :
         _code_hash(code_hash),
         _vm_version(vm_version),
         _eosvmoc_runtime(wr),
         _main_thread_id(std::this_thread::get_id())
      {

      }

      ~eosvmoc_instantiated_module() {
         _eosvmoc_runtime.cc.free_code(_code_hash, _vm_version);
      }

      bool is_main_thread() { return _main_thread_id == std::this_thread::get_id(); };

      eosio::chain::execution_status execute(host_context& context) override {
         eosio::chain::eosvmoc::code_cache_sync::mode m;
         m.whitelisted = context.is_eos_vm_oc_whitelisted();
         m.write_window = context.control.is_write_window();
         const code_descriptor* const cd = _eosvmoc_runtime.cc.get_descriptor_for_code_sync(m, _code_hash, _vm_version);
         EOS_ASSERT(cd, wasm_execution_error, "EOS VM OC instantiation failed");

         if ( is_main_thread() ) {
            auto i = _eosvmoc_runtime.acquire_main_thread_exec_mem_index();
            _eosvmoc_runtime.exec[i]->execute(*cd, *(_eosvmoc_runtime.mem[i]), context);
            _eosvmoc_runtime.release_main_thread_exec_mem_index();
         }
         else {
            auto i = _eosvmoc_runtime.acquire_ro_thread_exec_mem_index();
            _eosvmoc_runtime.exec_thread_local[i]->execute(*cd, *(_eosvmoc_runtime.mem_thread_local[i]), context);
            _eosvmoc_runtime.release_ro_thread_exec_mem_index();
         }

         return eosio::chain::execution_status::executed;
      }

      const digest_type              _code_hash;
      const uint8_t                  _vm_version;
      eosvmoc_runtime&               _eosvmoc_runtime;
      std::thread::id                _main_thread_id;
};

eosvmoc_runtime::eosvmoc_runtime(const std::filesystem::path data_dir, const eosvmoc::config& eosvmoc_config, const chainbase::database& db)
   : cc(data_dir, eosvmoc_config, db) {
   for (auto i = 0; i < 14; ++i) {
      exec.emplace_back(std::make_unique<eosvmoc::executor>(cc));
      mem.emplace_back(std::make_unique<eosvmoc::memory>(wasm_constraints::maximum_linear_memory/wasm_constraints::wasm_page_size));
   }
}

eosvmoc_runtime::~eosvmoc_runtime() {
}

std::unique_ptr<wasm_instantiated_module_interface> eosvmoc_runtime::instantiate_module(const char* code_bytes, size_t code_size,
                                                                                        const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version, bool& ) {
   return std::make_unique<eosvmoc_instantiated_module>(code_hash, vm_type, *this);
}

uint32_t eosvmoc_runtime::acquire_main_thread_exec_mem_index() {
   return main_thread_index++;
}

void eosvmoc_runtime::release_main_thread_exec_mem_index() {
   --main_thread_index;
}

uint32_t eosvmoc_runtime::acquire_ro_thread_exec_mem_index() {
   return ro_thread_index++;
}

void eosvmoc_runtime::release_ro_thread_exec_mem_index() {
   --ro_thread_index;
}

void eosvmoc_runtime::init_thread_local_data() {
   for (auto i = 0; i < 14; ++i) {
      exec_thread_local.emplace_back(std::make_unique<eosvmoc::executor>(cc));
      mem_thread_local.emplace_back(std::make_unique<eosvmoc::memory>(eosvmoc::memory::sliced_pages_for_ro_thread));
   }
}

thread_local std::vector<std::unique_ptr<eosvmoc::executor>> eosvmoc_runtime::exec_thread_local{};
thread_local std::vector<std::unique_ptr<eosvmoc::memory>> eosvmoc_runtime::mem_thread_local{};
thread_local uint32_t eosvmoc_runtime::ro_thread_index = 0;

}}}}
