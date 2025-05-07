#pragma once

#include <eosio/chain/webassembly/eos-vm-oc/memory.hpp>
#include <eosio/chain/sync_call_resource_pool.hpp>

namespace eosio::chain::eosvmoc {

/*
 * Manages a set of memory pools for sync calls.
 * Shallow call depths use dedicated memory pools with decreasing number
 * of pre-allocated slices. Deep call depths share one pool with one
 * pre-allocated * memory slice.
 *
 * Depth 1 is allocated highest slices, then each depth decrements by one,
 * until reaching one slice for all remaining depths.
 *
 * For example, if max call depth is 10 and we allocate 5 slices for the first
 * depth, we will have 5 memory pools:
 *
 * pool 1: 5 slices, for depth 1
 * pool 2: 4 slices, for depth 2
 * pool 3: 3 slices, for depth 3
 * pool 4: 2 slices, for depth 4
 * pool 5: 1 slice,  for depths 5 to 10
 *
 * */
class memory_pools {
   public:
      memory_pools();

      // Acquire memory from the pool of `call_depth`
      eosvmoc::memory* acquire_mem(uint32_t call_depth);

      // Release memory `m` back to the pool of `call_depth`
      void release_mem(uint32_t call_depth, eosvmoc::memory* m);

      // Update memory pools based on new number of threads
      void set_num_threads(uint32_t nthreads);

      // Update memory pools based on new max call depth
      void set_max_call_depth(uint32_t depth);

      // Slices for the first call depth.
      static constexpr uint32_t first_call_depth_slices = 5;

   private:
      uint32_t num_threads = 1; // main thread + read-only threads. default 1 for main thread
      std::vector<std::unique_ptr<call_resource_pool<eosvmoc::memory>>> pools;
};

}
