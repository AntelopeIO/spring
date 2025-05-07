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

      void execute(host_context& context) override {
         eosio::chain::eosvmoc::code_cache_sync::mode m;
         m.whitelisted = context.is_eos_vm_oc_whitelisted();
         m.write_window = context.control.is_write_window();
         const code_descriptor* const cd = _eosvmoc_runtime.cc.get_descriptor_for_code_sync(m, _code_hash, _vm_version);
         EOS_ASSERT(cd, wasm_execution_error, "EOS VM OC instantiation failed");

         if (context.is_sync_call()) {  // sync call on either main thread or read only thread
            auto exec = _eosvmoc_runtime.acquire_call_exec();
            auto mem  = _eosvmoc_runtime.acquire_call_mem(context.sync_call_depth);
            auto cleanup = fc::make_scoped_exit([&](){
               _eosvmoc_runtime.release_call_exec(exec);
               _eosvmoc_runtime.release_call_mem(context.sync_call_depth, mem);
            });
            exec->execute(*cd, *mem, context);
         } else if ( is_main_thread() ) {  // action on main thread
            _eosvmoc_runtime.exec.execute(*cd, _eosvmoc_runtime.mem, context);
         }
         else {  // action on read only thread
            _eosvmoc_runtime.exec_thread_local->execute(*cd, *_eosvmoc_runtime.mem_thread_local, context);
         }
      }

      const digest_type              _code_hash;
      const uint8_t                  _vm_version;
      eosvmoc_runtime&               _eosvmoc_runtime;
      std::thread::id                _main_thread_id;
};

eosvmoc_runtime::eosvmoc_runtime(const std::filesystem::path data_dir, const eosvmoc::config& eosvmoc_config, const chainbase::database& db)
   : cc(data_dir, eosvmoc_config, db)
   , exec(cc)
   , mem(wasm_constraints::maximum_linear_memory/wasm_constraints::wasm_page_size)
   , exec_pool([&]() -> eosvmoc::executor* { return new eosvmoc::executor(cc); })
{
}

eosvmoc_runtime::~eosvmoc_runtime() {
}

std::unique_ptr<wasm_instantiated_module_interface> eosvmoc_runtime::instantiate_module(const char* code_bytes, size_t code_size,
                                                                                        const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version) {
   return std::make_unique<eosvmoc_instantiated_module>(code_hash, vm_type, *this);
}

eosvmoc::executor* eosvmoc_runtime::acquire_call_exec() {
   return exec_pool.acquire();
}

void eosvmoc_runtime::release_call_exec(eosvmoc::executor* e) {
   exec_pool.release(e);
}

eosvmoc::memory* eosvmoc_runtime::acquire_call_mem(uint32_t call_level) {
   return mem_pools.acquire_mem(call_level);
}

void eosvmoc_runtime::release_call_mem(uint32_t call_level, eosvmoc::memory* m) {
   mem_pools.release_mem(call_level, m);
}

void eosvmoc_runtime::set_num_threads_for_call_res_pools(uint32_t nthreads) {
   exec_pool.set_num_threads(nthreads, [&]() -> eosvmoc::executor* { return new eosvmoc::executor(cc); });
   mem_pools.set_num_threads(nthreads);
}

void eosvmoc_runtime::set_max_call_depth_for_call_res_pools(uint32_t depth) {
   exec_pool.set_max_call_depth(depth, [&]() -> eosvmoc::executor* { return new eosvmoc::executor(cc); });
   mem_pools.set_max_call_depth(depth);
}

void eosvmoc_runtime::init_thread_local_data() {
   exec_thread_local = std::make_unique<eosvmoc::executor>(cc);
   mem_thread_local  = std::make_unique<eosvmoc::memory>(eosvmoc::memory::sliced_pages_for_ro_thread);
}

thread_local std::unique_ptr<eosvmoc::executor> eosvmoc_runtime::exec_thread_local{};
thread_local std::unique_ptr<eosvmoc::memory> eosvmoc_runtime::mem_thread_local{};

}}}}
