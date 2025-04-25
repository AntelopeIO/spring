#pragma once
#include <vector>
#include <memory>

namespace IR {
  struct Module;
}

namespace eosio { namespace chain {

class host_context;

enum class execution_status : int64_t;  // full definition is in wasm_interface.hpp

class wasm_instantiated_module_interface {
   public:
      //virtual void apply(apply_context& context) = 0;

      //virtual execution_status do_sync_call(sync_call_context& context) = 0;
      virtual execution_status execute(host_context& context) = 0;

      virtual ~wasm_instantiated_module_interface();
};

class wasm_runtime_interface {
   public:
      virtual std::unique_ptr<wasm_instantiated_module_interface> instantiate_module(const char* code_bytes, size_t code_size,
                                                                                     const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version) = 0;

      virtual ~wasm_runtime_interface();

      // eosvmoc_runtime needs this
      virtual void init_thread_local_data() {};

      virtual void set_num_threads_for_call_res_pools(uint32_t num_threads) {};
      virtual void set_max_call_depth_for_call_res_pools(uint32_t depth) {};
};

}}
