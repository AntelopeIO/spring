#pragma once

#include <cstddef>
#include <boost/interprocess/offset_ptr.hpp>

#include <chainbase/pinnable_mapped_file.hpp>

namespace chainbase {

   namespace bip = boost::interprocess;

   template<typename T, typename S>
   class chainbase_node_allocator {
    public:
      using value_type = T;
      using pointer = bip::offset_ptr<T>;

      chainbase_node_allocator(segment_manager* manager) : _manager{manager} {
         _ss_alloc = pinnable_mapped_file::get_small_size_allocator((std::byte*)manager);
      }

      chainbase_node_allocator(const chainbase_node_allocator& other) : chainbase_node_allocator(&*other._manager) {}

      template<typename U>
      chainbase_node_allocator(const chainbase_node_allocator<U, S>& other) : chainbase_node_allocator(&*other._manager) {}

      pointer allocate(std::size_t num) {
         if (num == 1) {
            if (_block_start == _block_end && _freelist == nullptr) {
               get_some(_allocation_batch_size);
            }
            if (_block_start < _block_end) {
               pointer result =  pointer{static_cast<T*>(static_cast<void*>(_block_start.get()))};
               _block_start += sizeof(T);
               return result;
            }
            assert(_freelist != nullptr);
            list_item* result = &*_freelist;
            _freelist = _freelist->_next;
            result->~list_item();
            --_freelist_size;
            return pointer{(T*)result};
         } else {
            return pointer{(T*)&*_ss_alloc->allocate(num*sizeof(T))};
         }
      }

      void deallocate(const pointer& p, std::size_t num) {
         if (num == 1) {
            _freelist = new (&*p) list_item{_freelist};
            ++_freelist_size;
         } else {
            _ss_alloc->deallocate(ss_allocator_t::pointer((char*)&*p), num*sizeof(T));
         }
      }

      void preallocate(std::size_t num) {
         if (num >= 2 * _allocation_batch_size)
            get_some((num + 7) & ~7);
      }

      bool operator==(const chainbase_node_allocator& other) const { return this == &other; }
      bool operator!=(const chainbase_node_allocator& other) const { return this != &other; }
      segment_manager* get_segment_manager() const { return _manager.get(); }
      size_t freelist_memory_usage() const { return _freelist_size * sizeof(T) + (_block_end - _block_start); }

    private:
      template<typename T2, typename S2>
      friend class chainbase_node_allocator;

      void get_some(size_t num_to_alloc) {
         static_assert(sizeof(T) >= sizeof(list_item), "Too small for free list");
         static_assert(sizeof(T) % alignof(list_item) == 0, "Bad alignment for free list");

         _block_start = static_cast<char*>(_manager->allocate(sizeof(T) * num_to_alloc));
         _block_end   = _block_start + sizeof(T) * num_to_alloc;

         if (_allocation_batch_size < max_allocation_batch_size)
            _allocation_batch_size *= 2;
      }

      struct list_item { bip::offset_ptr<list_item> _next; };

      static constexpr size_t max_allocation_batch_size = 512;
      
      bip::offset_ptr<char>            _block_start;
      bip::offset_ptr<char>            _block_end;
      bip::offset_ptr<list_item>       _freelist{};
      bip::offset_ptr<ss_allocator_t>  _ss_alloc;
      bip::offset_ptr<segment_manager> _manager;
      size_t                           _allocation_batch_size = 32;
      size_t                           _freelist_size = 0;
   };

}  // namepsace chainbase
