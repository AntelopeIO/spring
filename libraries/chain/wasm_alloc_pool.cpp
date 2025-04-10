#include <eosio/chain/wasm_alloc_pool.hpp>
#include <fc/log/logger.hpp>

namespace eosio::chain {

// only called on main thread
wasm_alloc_pool::wasm_alloc_pool()
   : num_threads(1)
   , max_call_depth(1)
{
   // create 1 wasm allocator for the main thread
   stack = std::make_unique<boost::lockfree::stack<vm::wasm_allocator*>>(1);
   stack->push(new vm::wasm_allocator);
}

wasm_alloc_pool::~wasm_alloc_pool() {
   vm::wasm_allocator* alloc;
   while (stack->pop(alloc)) {
      delete alloc;
   }
}

// called on any threads
vm::wasm_allocator* wasm_alloc_pool::acquire() {
   // Each thread can use at most `max_sync_call_depth` wasm allocators
   // The stack would never be empty for a new acquire request
   assert(!stack->empty());

   vm::wasm_allocator* alloc;
   stack->pop(alloc);

   assert(alloc);
   return alloc;
}

// called on any threads
void wasm_alloc_pool::release(vm::wasm_allocator* alloc) {
   stack->push(alloc);
}

// called on main thread from producer_plugin startup number of read-only threads is determined
void wasm_alloc_pool::set_num_threads(uint32_t new_num_thread) {
   if (new_num_thread <= num_threads) {
      // For simplicity, we don't shrink the pool
      return;
   }

   resize(new_num_thread, max_call_depth);
}

// called on main thread from sync_call protocol feature activation or set_packed_parameters
void wasm_alloc_pool::set_max_call_depth(uint32_t new_depth) {
   if (new_depth <= max_call_depth) {
      // For simplicity, we don't shrink the pool
      return;
   }

   resize(num_threads, new_depth);
}

// called on main thread (it is called by set_num_threads or set_max_call_depth).
void wasm_alloc_pool::resize(uint32_t new_num_thread, uint32_t new_depth) {
   auto old_pool_size  = num_threads * max_call_depth;
   auto new_pool_size  = new_num_thread * new_depth;
   assert(new_pool_size > old_pool_size);  // Do not allow shrinking
   auto num_new_allocs = new_pool_size - old_pool_size;

   // add new allocators
   for (uint32_t i = 0u; i < num_new_allocs ; ++i) {
      stack->push(new vm::wasm_allocator);
   }

   num_threads    = new_num_thread;
   max_call_depth = new_depth;
} 

} /// eosio::chain
