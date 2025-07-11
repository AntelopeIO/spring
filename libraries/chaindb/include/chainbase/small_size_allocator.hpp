#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <functional>
#include <mutex>
#include <boost/interprocess/offset_ptr.hpp>

namespace bip = boost::interprocess;

namespace chainbase {

namespace detail {

// ---------------------------------------------------------------------------------------
//         One of the allocators from `small_size_allocator` below
//         -------------------------------------------------------
//
// - allocates buffers of `sz` bytes.
// - allocates in batch from `backing_allocator` (see `allocation_batch_size`)
// - freed buffers are linked into a free list for fast further allocations
// - allocated buffers are never returned to the `backing_allocator`
// - thread-safe
// ---------------------------------------------------------------------------------------
template <class backing_allocator>
class allocator {
public:
   using pointer = backing_allocator::pointer;

   allocator(backing_allocator back_alloc, std::size_t sz)
      : _sz(sz)
      , _back_alloc(back_alloc) {}

   pointer allocate() {
      std::lock_guard g(_m);
      if (_block_start == _block_end && _freelist == nullptr) {
         get_some();
      }
      if (_block_start < _block_end) {
         pointer result =  pointer{_block_start.get()};
         _block_start += _sz;
         return result;
      }
      assert(_freelist != nullptr);
      list_item* result = &*_freelist;
      _freelist         = _freelist->_next;
      result->~list_item();
      --_freelist_size;
      return pointer{(typename backing_allocator::value_type*)result};
   }

   void deallocate(const pointer& p) {
      std::lock_guard g(_m);
      _freelist = new (&*p) list_item{_freelist};
      ++_freelist_size;
   }

   size_t freelist_memory_usage() const {
      std::lock_guard g(_m);
      return _freelist_size * _sz + (_block_end - _block_start);
   }

   size_t num_blocks_allocated() const {
      std::lock_guard g(_m);
      return _num_blocks_allocated;
   }

private:
   struct list_item { bip::offset_ptr<list_item> _next; };
   static constexpr size_t max_allocation_batch_size = 512;

   void get_some() {
      assert(_sz >= sizeof(list_item));
      assert(_sz % alignof(list_item) == 0);

      _block_start = _back_alloc.allocate(_sz * _allocation_batch_size);
      _block_end   = _block_start + _sz * _allocation_batch_size;
      ++_num_blocks_allocated;
      if (_allocation_batch_size < max_allocation_batch_size)
         _allocation_batch_size *= 2;
   }

   std::size_t                _sz;
   bip::offset_ptr<list_item> _freelist;
   bip::offset_ptr<char>      _block_start;
   bip::offset_ptr<char>      _block_end;
   backing_allocator          _back_alloc;
   size_t                     _allocation_batch_size = 32;
   size_t                     _freelist_size         = 0;
   size_t                     _num_blocks_allocated  = 0; // number of blocks allocated from boost segment allocator
   mutable std::mutex         _m;
};

} // namespace detail


// ---------------------------------------------------------------------------------------
//          An array of 128 allocators for sizes from 8 to 1024 bytes
//          ---------------------------------------------------------
//
//  - All pointers used are of type `backing_allocator::pointer`
//  - allocate/deallocate specify size in bytes.
//  - Any requested size greater than `num_allocators * size_increment` will be routed
//    to the backing_allocator
// ---------------------------------------------------------------------------------------
template <class backing_allocator, size_t num_allocators = 128, size_t size_increment = 8>
requires ((size_increment & (size_increment - 1)) == 0) // power of two
class small_size_allocator {
public:
   using pointer       = backing_allocator::pointer;
   using alloc_ptr     = bip::offset_ptr<detail::allocator<backing_allocator>>;
   using alloc_array_t = std::array<alloc_ptr, num_allocators>;
private:
   backing_allocator _back_alloc;
   alloc_array_t     _allocators;

   static constexpr size_t max_size = num_allocators * size_increment;

   static constexpr size_t allocator_index(size_t sz_in_bytes) {
      assert(sz_in_bytes > 0);
      return (sz_in_bytes - 1) / size_increment;
   }

   template <std::size_t... I>
   auto make_allocators(backing_allocator back_alloc, std::index_sequence<I...>) {
      return alloc_array_t{new (&*_back_alloc.allocate(sizeof(detail::allocator<backing_allocator>)))
                              detail::allocator<backing_allocator>(back_alloc, (I + 1) * size_increment)...};
   }

public:
   explicit small_size_allocator(backing_allocator back_alloc)
      : _back_alloc(std::move(back_alloc))
      , _allocators(make_allocators(back_alloc, std::make_index_sequence<num_allocators>{})) {}

   pointer allocate(std::size_t sz_in_bytes) {
      if (sz_in_bytes <= max_size) {
         return _allocators[allocator_index(sz_in_bytes)]->allocate();
      }
      return _back_alloc.allocate(sz_in_bytes);
   }

   void deallocate(const pointer& p, std::size_t sz_in_bytes) {
      if (sz_in_bytes <= max_size) {
         _allocators[allocator_index(sz_in_bytes)]->deallocate(p);
      } else
         _back_alloc.deallocate(p, sz_in_bytes);
   }

   size_t freelist_memory_usage() const {
      size_t sz = 0;
      for (auto& alloc : _allocators)
         sz += alloc->freelist_memory_usage();
      return sz;
   }

   size_t num_blocks_allocated() const {
      size_t sz = 0;
      for (auto& alloc : _allocators)
         sz += alloc->num_blocks_allocated();
      return sz;
   }
};

// ---------------------------------------------------------------------------------------
//          Object allocator
//          ----------------
//
//  emulates the API of `bip::allocator<T, segment_manager>`
//  backing_allocator is normally the `small_size_allocator`, in which case:
// - If the allocation size (num_objects * sizeof(T)) is less than 1024 bytes, it will be routed
//   through the small size allocator which allocates in batch from the `segment_manager`.
// - If the allocation size (num_objects * sizeof(T)) is greater than 1024 bytes, the allocator
//   will allocate directly from the segment manager.
// - the 1024 bytes limit is derived from the template parameters of `small_size_allocator`
//   (size_t num_allocators = 128, size_t size_increment = 8)
// ---------------------------------------------------------------------------------------
template<typename T, class backing_allocator>
class object_allocator {
public:
   using char_pointer = backing_allocator::pointer;
   using pointer      = char_pointer::template rebind<T>;
   using value_type   = T;

   explicit object_allocator(backing_allocator* back_alloc) :_back_alloc(back_alloc) {
   }

   pointer allocate(std::size_t num_objects) {
      return pointer(static_cast<T*>(static_cast<void*>(_back_alloc->allocate(num_objects * sizeof(T)).get())));
   }

   void deallocate(const pointer& p, std::size_t num_objects) {
      assert(p != nullptr);
      return _back_alloc->deallocate(char_pointer(static_cast<char*>(static_cast<void*>(p.get()))), num_objects * sizeof(T));
   }

   bool operator==(const object_allocator&) const = default;
   
private:
   bip::offset_ptr<backing_allocator> _back_alloc; // allocates by size in bytes
};

} // namespace chainbase
