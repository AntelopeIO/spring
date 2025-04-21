#pragma once

#include <eosio/chain/thread_utils.hpp>
#include <eosio/chain/types.hpp>
#include <fc/io/raw.hpp>
#include <bit>
#include <array>
#include <future>

namespace eosio::chain {

namespace detail {

struct merkle_pool {
   merkle_pool() {
      thread_pool.start(4, thread_pool.make_on_except_abort());
   }
   named_thread_pool<struct merkle> thread_pool;
};

inline boost::asio::io_context::executor_type get_merkle_thread_pool() {
   static merkle_pool the_merkle_pool;
   return the_merkle_pool.thread_pool.get_executor().get_executor();
}

inline digest_type hash_combine(const digest_type& a, const digest_type& b) {
   return digest_type::hash(std::make_pair(std::cref(a), std::cref(b)));
}

template <class It, bool async = false>
requires std::is_same_v<std::decay_t<typename std::iterator_traits<It>::value_type>, digest_type>
inline digest_type calculate_merkle_pow2(const It& start, const It& end) {
   assert(end >= start + 2);
   auto size = static_cast<size_t>(end - start);
   assert(std::bit_floor(size) == size);

   if (size == 2)
      return hash_combine(start[0], start[1]);
   else {
      if (async && size >= 256) {
         auto async_calculate_merkle_pow2 = [&start, &size](auto fut) {
            size_t slice_size = size / fut.size();

            for (size_t i=0; i<fut.size(); ++i)
               fut[i] = post_async_task(get_merkle_thread_pool(), [s=start + slice_size * i, e=start + slice_size * (i+1)]() {
                  return calculate_merkle_pow2<It>(s, e);
               });

            std::array<digest_type, fut.size()> res;

            for (size_t i=0; i<fut.size(); ++i)
               res[i] = fut[i].get();

            return calculate_merkle_pow2(res.begin(), res.end());
         };

         if (size >= 2048) {
            // use 4 threads. Future array size dictates the number of threads (must be power of two)
            return async_calculate_merkle_pow2(std::array<std::future<digest_type>, 4>());
         }
         // use 2 threads. Future array size dictates the number of threads (must be power of two)
         return async_calculate_merkle_pow2(std::array<std::future<digest_type>, 2>());
      } else {
         auto mid = start + size / 2;
         return hash_combine(calculate_merkle_pow2(start, mid), calculate_merkle_pow2(mid, end));
      }
   }
}

} // namespace detail

// ************* public interface starts here ************************************************

// ------------------------------------------------------------------------
// calculate_merkle:
// -----------------
// takes two random access iterators delimiting a sequence of `digest_type`,
// returns the root digest for the provided sequence.
//
// does not overwrite passed sequence
//
// log2 recursion OK, uses less than 5KB stack space for 4 billion digests
// appended (or 0.25% of default 2MB thread stack size on Ubuntu).
// ------------------------------------------------------------------------
template <class It>
requires std::random_access_iterator<It> &&
         std::is_same_v<std::decay_t<typename std::iterator_traits<It>::value_type>, digest_type>
inline digest_type calculate_merkle(const It& start, const It& end) {
   assert(end >= start);
   auto size = static_cast<size_t>(end - start);
   if (size <= 1)
      return (size == 0) ? digest_type{} : *start;

   auto midpoint = std::bit_floor(size);
   if (size == midpoint)
      return detail::calculate_merkle_pow2<It, true>(start, end);

   auto mid = start + midpoint;
   return detail::hash_combine(detail::calculate_merkle_pow2<It, true>(start, mid),
                               calculate_merkle(mid, end));
}

// --------------------------------------------------------------------------
// calculate_merkle:
// -----------------
// takes a container or `std::span` of `digest_type`, returns the root digest
// for the sequence of digests in the container.
// --------------------------------------------------------------------------
template <class Cont>
requires std::random_access_iterator<decltype(Cont().begin())> &&
         std::is_same_v<std::decay_t<typename Cont::value_type>, digest_type>
inline digest_type calculate_merkle(const Cont& ids) {
   return calculate_merkle(ids.begin(), ids.end()); // cbegin not supported for std::span until C++23.
}


} /// eosio::chain
