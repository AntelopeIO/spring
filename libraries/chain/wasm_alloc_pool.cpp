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
void wasm_alloc_pool::set_threads(uint32_t new_num_thread) {
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

void wasm_alloc_pool::resize(uint32_t new_num_thread, uint32_t new_depth) {
   auto old_pool_size  = num_threads * max_call_depth; // take a note of old size

   // save allocators not being in use into a temporary place
   std::vector<vm::wasm_allocator*> temp;
   vm::wasm_allocator* alloc;
   while (stack->pop(alloc)) {
      temp.push_back(alloc);
   }

   // update with new values
   num_threads         = new_num_thread;
   max_call_depth      = new_depth;
   auto new_pool_size  = num_threads * max_call_depth;

   // recreate the internal stack with the new capacity
   stack = std::make_unique<boost::lockfree::stack<vm::wasm_allocator*>>(new_pool_size);

   // put back existing allocators not in use to the pool.
   // allocators in use by the main thread will be released back to the pool eventually
   for (const auto& alloc: temp) {
      stack->push(alloc);
   }

   // add new ones
   auto num_new_allocs = new_pool_size - old_pool_size;
   for (uint32_t i = 0u; i < num_new_allocs ; ++i) {
      stack->push(new vm::wasm_allocator);
   }
} 

} /// eosio::chain
