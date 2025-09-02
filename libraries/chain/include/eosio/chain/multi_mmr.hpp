#pragma once
#include <fc/crypto/sha256.hpp>

#include <chainbase/cow_ptr.hpp>

#include <numeric>
#include <ranges>

namespace eosio::chain {

struct multi_mmr_default_memory {};
struct multi_mmr_chaindb_memory {};

template<typename MemoryModel>
struct multi_mmr_memory_traits;

template<>
struct multi_mmr_memory_traits<multi_mmr_default_memory> {
   template<typename T> using allocator = std::allocator<T>;
};

template<>
struct multi_mmr_memory_traits<multi_mmr_chaindb_memory> {
   template<typename T> using allocator = chainbase::allocator<T>;
};

template<typename T>
concept MultiMMRMemoryModel = requires {
   typename multi_mmr_memory_traits<T>::template allocator<int>;
};

template<typename F>
concept SHA256Generator = std::invocable<F, size_t> &&
                          std::same_as<std::invoke_result_t<F, size_t>, fc::sha256>;

template<typename F>
concept SHA256Callback = std::invocable<F, size_t, fc::sha256> &&
                         std::same_as<std::invoke_result_t<F, size_t, fc::sha256>, void>;

template<size_t N, MultiMMRMemoryModel MM = multi_mmr_default_memory, size_t Max=24>
struct multi_mmr {
   struct peaks {
      uint32_t ref_count = 0;
      std::array<fc::sha256, N> p;
   };

   using model_type = MM;
   using allocator_type = typename multi_mmr_memory_traits<MM>::template allocator<peaks>;

   multi_mmr() = default;

   //only bother copying active peaks
   multi_mmr(const multi_mmr& other) : size(other.size) {
      if(!size)
         return;
      for_each_active_peak([&](unsigned peakidx) {
         data[peakidx] = other.data[peakidx];
      });
   }

   template<SHA256Generator F>
   void append(F&& f) {
      const unsigned new_peak_idx = std::countr_one(size++);

      for(unsigned i = 0; i < N; ++i) {
         data[new_peak_idx].write().p[i] = std::accumulate(data.begin(), data.begin()+new_peak_idx, f(i), [i](const fc::sha256& a, const chainbase::cow_ptr<peaks, allocator_type>& b) {
            return fc::sha256::packhash(b.read()->p[i], a);
         });
      }
   }

   void append(const fc::sha256& hash) requires (N == 1) {
      append([&](size_t) {
         return hash;
      });
   }

   //ends up much like constructing a new MMR; but do it in place on the stack to avoid any new allocations
   fc::sha256 root() {
      if(size == 0)
         return fc::sha256();

      std::array<fc::sha256, std::bit_width(N)> work;

      roots([&](const size_t mmr_idx, const fc::sha256& mmr_idx_root) {
         const unsigned new_peak_idx = std::countr_one(mmr_idx);
         work[new_peak_idx] = std::accumulate(work.begin(), work.begin()+new_peak_idx, mmr_idx_root, [](const fc::sha256&& a, const fc::sha256& b) {
            return fc::sha256::packhash(b, a);
         });
      });

      const int highest_combine_peak = std::bit_width(N) - 1;

      fc::sha256 result = work[highest_combine_peak];
      if constexpr (!std::has_single_bit(N)) {
         for(const int pos : std::views::iota(0, highest_combine_peak) | std::views::reverse)
            if(N & (1<<pos))
               result = fc::sha256::packhash(result, work[pos]);
      }

      return result;
   }

   //get the root of each MMR
   template <SHA256Callback F>
   void roots(F&& f) {
      for(size_t i = 0; i < N; ++i) {
         if(size == 0)
            return f(i, fc::sha256());

         fc::sha256 result = data[highest_peak_idx()].read()->p[i];
         for(const int pos : std::views::iota(0, highest_peak_idx()) | std::views::reverse)
            if(size & (1<<pos))
               result = fc::sha256::packhash(result, data[pos].read()->p[i]);

         f(i, result);
      }
   }

   int highest_peak_idx() const {
      assert(size);
      return std::bit_width(size) - 1;
   }

   template <typename F>
   void for_each_active_peak(F&& f) {
      for(unsigned i = 0; i <= highest_peak_idx(); ++i)
         if(size & (1<<i))
            f(i);
   }

   size_t size = 0;
   std::array<chainbase::cow_ptr<peaks, allocator_type>, Max> data = {};
};

template<size_t N, size_t Max = 24>
using shared_multi_mmr = multi_mmr<N, multi_mmr_chaindb_memory, Max>;

template<size_t Max = 24>
using shared_mmr = multi_mmr<1, multi_mmr_chaindb_memory, Max>;

}