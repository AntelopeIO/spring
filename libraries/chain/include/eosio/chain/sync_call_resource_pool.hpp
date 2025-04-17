#pragma once

#include <eosio/chain/config.hpp>
#include <boost/lockfree/stack.hpp>

/*
 *
 * Problem: A single sync call requires up to `max_sync_call_depth` of resources like wasm
 *          allocator, OC executor, and OC memory. The main thread and read only threads
 *          can have sync calls in parallel. `max_sync_call_depth` can be changed dynamically
 *          by BPs.
 * Solution: Use Boost lockfree stack as a resource pool.
 * Advantages:
 *    1. Lockfree for acquiring and releasing wasm resource.
 *    2. When `max_sync_call_depth` changes, the pool changes right away. No need
 *       to check if `max_sync_call_depth` has changed in every new sync call.
 *    3. Avoid the use of thread_locals.
 *    4. Simpler to reason.
 *
 */

namespace eosio::chain {

template <typename T>
class call_resource_pool {

public:
   using resource_creator = std::function<std::shared_ptr<T>()>;  // customized function to create a resource

   explicit call_resource_pool(resource_creator creator)
      : num_threads(1)
      , max_call_depth(1)
   {
      // create 1 resource for the main thread
      stack.push(creator());
   }

   // request a resource from the pool, called on any threads
   std::shared_ptr<T> acquire() {
      // Each thread can use at most `max_sync_call_depth` resources
      // The stack would never be empty for a new acquire request
      assert(!stack.empty());

      std::shared_ptr<T> res;
      stack.pop(res);

      assert(res);
      return res;
   }

   // release a resource back to the pool, called on any threads
   void release(std::shared_ptr<T> res) {
      stack.push(res);
   }

   // called on main thread from producer_plugin startup after number of read-only threads is determined
   void set_num_threads(uint32_t new_num_thread, resource_creator creator) {
      if (new_num_thread <= num_threads) {
         // For simplicity, we don't shrink the pool
         return;
      }

      resize(new_num_thread, max_call_depth, creator);
   }


   // called on main thread from sync_call protocol feature activation or set_packed_parameters
   void set_max_call_depth(uint32_t new_depth, resource_creator creator) {
      if (new_depth <= max_call_depth) {
         // For simplicity, we don't shrink the pool
         return;
      }

      resize(num_threads, new_depth, creator);
   }

private:
   void resize(uint32_t new_num_thread, uint32_t new_depth, resource_creator creator) {
      auto old_pool_size  = num_threads * max_call_depth;
      auto new_pool_size  = new_num_thread * new_depth;
      assert(new_pool_size > old_pool_size);  // Do not allow shrinking
      auto num_new_resources = new_pool_size - old_pool_size;

      // add new resources
      for (uint32_t i = 0u; i < num_new_resources ; ++i) {
         stack.push(creator());
      }

      num_threads    = new_num_thread;
      max_call_depth = new_depth;
   }

   uint32_t num_threads    = 1; // `1` for the main thread
   uint32_t max_call_depth = 1; // prior to sync call protocol feature activated

   boost::lockfree::stack<std::shared_ptr<T>> stack {config::default_max_sync_call_depth};
};

}  /// namespace eosio::chain
