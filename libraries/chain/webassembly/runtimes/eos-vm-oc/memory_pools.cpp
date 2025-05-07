#include <eosio/chain/webassembly/eos-vm-oc/memory_pools.hpp>

namespace eosio::chain::eosvmoc {

memory_pools::memory_pools()
   : num_threads(1)  // for main thread
{
   // create a single pool for the main thread and first depth of sync calls
   pools.emplace_back(std::make_unique<call_resource_pool<eosvmoc::memory>>(
      [&]() -> eosvmoc::memory* {
         return new eosvmoc::memory(first_call_depth_slices);
   }));
};

// Acquire memory from the pool of `call_depth`
eosvmoc::memory* memory_pools::acquire_mem(uint32_t call_depth) {
   assert(call_depth > 0 && pools.size() > 0);
   // a call whose depth in the range of 1 to pools.size() - 1 uses
   // its own pool, the rest use the last pool
   auto i = std::min(static_cast<size_t>(call_depth), pools.size()) - 1; 
   return pools[i]->acquire();
}

// Release memory `m` back to the pool of `call_depth`
void memory_pools::release_mem(uint32_t call_depth, eosvmoc::memory* m) {
   assert(call_depth > 0 && pools.size() > 0);
   // a call whose depth in the range of 1 to pools.size() - 1 uses
   // its own pool, the rest use the last pool
   auto i = std::min(static_cast<size_t>(call_depth),  pools.size()) - 1; 
   pools[i]->release(m);
}

// Update memory pools based on new number of threads
void memory_pools::set_num_threads(uint32_t nthreads) {
   num_threads = nthreads;

   auto slices = first_call_depth_slices;
   for (auto i = 0u; i < pools.size(); ++i) {
      pools[i]->set_num_threads(num_threads, [&]() -> eosvmoc::memory* { return new eosvmoc::memory(slices); });
      --slices;
   }
}

// Update memory pools based on new max call depth
void memory_pools::set_max_call_depth(uint32_t max_depth) {
   if (max_depth <= pools.size()) {  // Shrinking not allowed
      return;
   }

   const auto num_curr_pools = pools.size();
   const auto max_num_pools = first_call_depth_slices; // max number of pools allowed

   // Determine if new pools are needed.
   uint32_t num_new_pools = 0;
   if (max_depth > max_num_pools) {
      if (num_curr_pools < max_num_pools) {
         num_new_pools = max_num_pools - num_curr_pools;
      }
   } else {
      num_new_pools = max_depth - num_curr_pools;
   }

   // Create new pools
   if (num_new_pools > 0) {
      auto slices = first_call_depth_slices - num_curr_pools; // starting number of slices
      pools.reserve(num_curr_pools + num_new_pools);

      for (auto i = 0u; i < num_new_pools; ++i) {
         pools.emplace_back(std::make_unique<call_resource_pool<eosvmoc::memory>>([&]() -> eosvmoc::memory* { return new eosvmoc::memory(slices); }));
         pools[i]->set_num_threads(num_threads, [&]() -> eosvmoc::memory* { return new eosvmoc::memory(slices); });
         --slices;
      }
   }

   // Update the depth of the pool whose number of slices is 1.
   // All other pools are for a particular depth only; their depths are fixed at 1;
   // no need to update.
   if (pools.size() == first_call_depth_slices && max_depth > first_call_depth_slices) {
      const auto updated_depth = max_depth - first_call_depth_slices + 1;
      const auto idx = pools.size() - 1;
      pools[idx]->set_max_call_depth(updated_depth, []() -> eosvmoc::memory* { return new eosvmoc::memory(1); });
   }
}

}
