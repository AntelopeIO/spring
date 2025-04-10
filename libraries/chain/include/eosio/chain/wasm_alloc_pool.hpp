#pragma once

#include <eosio/vm/allocator.hpp>
#include <boost/lockfree/stack.hpp>

/*
 *
 * Problem: Dynamically creating wasm allocators is expensive. To support sync
 *          calls, each of the main and read only threads requires a set of
 *          pre-constructed `max_sync_call_depth` of wasm allocators.
 *          But `max_sync_call_depth` can be changed by BPs, which causes resizing
 *          of the allocator sets.
 * Solution: Use a wasm allocator pool with a Boost lockfree stack.
 * Advantages:
 *    1. Lockfree for acquiring and releasing wasm allocators.
 *    2. When `max_sync_call_depth` changes, the pool changes right away. No need
 *       to check if `max_sync_call_depth` has changed every time.
 *    3. Avoid the use of thread_locals.
 *    4. Simpler to reason.
 *
 */

namespace eosio::chain {

class wasm_alloc_pool {

public:
   wasm_alloc_pool();
   ~wasm_alloc_pool();

   // request a wasm allocator from the pool, called on any threads
   vm::wasm_allocator* acquire();

   // release a wasm allocator back to the pool, called on any threads
   void release(vm::wasm_allocator* alloc);

   // called on main thread from producer_plugin startup after number of read-only threads is determined
   void set_num_threads(uint32_t num_threads);

   // called on main thread from sync_call protocol feature activation or set_packed_parameters
   void set_max_call_depth(uint32_t depth);

private:
   void resize(uint32_t new_num_thread, uint32_t new_depth);

   uint32_t num_threads    = 1; // `1` for the main thread
   uint32_t max_call_depth = 1; // prior to sync call protocol feature activated

   std::unique_ptr<boost::lockfree::stack<vm::wasm_allocator*>> stack;
};

}  /// namespace eosio::chain
