#pragma once
#include <boost/interprocess/offset_ptr.hpp>
#include <boost/interprocess/allocators/allocator.hpp>

#include <cstddef>
#include <cstring>
#include <algorithm>
#include <string>
#include <optional>

#include <chainbase/pinnable_mapped_file.hpp>

namespace chainbase {
   namespace bip = boost::interprocess;

   template<typename T>
   class shared_cow_vector {
      struct impl {
         uint32_t reference_count;
         uint32_t size;
         T data[0];
      };

   public:
      using allocator_type = bip::allocator<char, segment_manager>;
      using iterator       = const T*;
      using const_iterator = const T*;

      template<typename Alloc>
      explicit shared_cow_vector(Alloc&& ) {}

      template<typename Iter>
      explicit shared_cow_vector(Iter begin, Iter end, const allocator_type& alloc) {
         std::size_t size = std::distance(begin, end);
         _alloc<true>(alloc, &*begin, size, size);
      }

      explicit shared_cow_vector(const T* ptr, std::size_t size, const allocator_type& alloc) {
         _alloc<true>(alloc, ptr, size, size);
      }

      shared_cow_vector(const shared_cow_vector& other) : _data(other._data) {
         if (_data != nullptr) {
            ++_data->reference_count;
         }
      }

      shared_cow_vector(shared_cow_vector&& other) noexcept : _data(other._data) {
         other._data = nullptr;
      }

      template<class I>
      explicit shared_cow_vector(std::initializer_list<I> init) {
         clear_and_construct(init.size(), 0, [&](void* dest, std::size_t idx) {
            new (dest) T(init[idx]);
         });
      }

      shared_cow_vector& operator=(const shared_cow_vector& other) {
         *this = shared_cow_vector{other};
         return *this;
      }

      shared_cow_vector& operator=(shared_cow_vector&& other) noexcept {
         if (this != &other) {
            dec_refcount();
            _data = other._data;
            other._data = nullptr;
         }
         return *this;
      }

      ~shared_cow_vector() {
         dec_refcount();
         _data = nullptr;
      }

      void clear() {
         dec_refcount();
         _data = nullptr;
      }

      template<typename F>
      void clear_and_construct(std::size_t new_size, std::size_t copy_size, F&& f) {
         assert(copy_size <= new_size);
         assert(copy_size == 0 || (_data && copy_size <= _data->size));
         if (_data && _data->reference_count == 1 && _data->size == new_size)
            std::destroy(_data->data + copy_size, _data->data + new_size);
         else {
            _alloc<false>(data(), new_size, copy_size); // construct == false => uninitialized memory
         }
         for (std::size_t i=copy_size; i<new_size; ++i)
            static_cast<F&&>(f)(_data->data + i, i); // `f` should construct objects in place 
      }

      void assign(const T* ptr, std::size_t size) {
         if (_data && _data->reference_count == 1 && _data->size == size)
            std::copy(ptr, ptr + size, data());
         else {
            _alloc<true>(ptr, size, size);
         }
      }

      const T* data() const {
         return _data ? _data->data : nullptr;
      }

      std::size_t size() const {
         return _data ? _data->size : 0;
      }

      bool empty() const {
         return size() == 0;
      }

      const_iterator begin() const { return data(); }

      const_iterator end() const {
         return _data ? _data->data + _data->size : nullptr;
      }

      const_iterator cbegin() const { return begin(); }
      const_iterator cend()   const { return end(); }

      const T& operator[](std::size_t idx) const { assert(_data); return _data->data[idx]; }

      bool operator==(const shared_cow_vector& rhs) const {
        return size() == rhs.size() && std::memcmp(data(), rhs.data(), size() * sizeof(T)) == 0;
      }

      bool operator!=(const shared_cow_vector& rhs) const { return !(*this == rhs); }

      static allocator_type get_allocator(void* obj) {
         return pinnable_mapped_file::get_allocator<char>(obj);
      }      

      const allocator_type get_allocator() const {
         return get_allocator((void *)this);
      }

    private:
      void dec_refcount(allocator_type alloc) {
         if (_data && --_data->reference_count == 0) {
            assert(_data->size);                                    // if size == 0, _data should be nullptr
            std::destroy(_data->data, _data->data + _data->size);
            alloc.deallocate((char*)&*_data, sizeof(impl) + (_data->size * sizeof(T)));
         }
      }

      void dec_refcount() {
         dec_refcount(get_allocator(this));
      }

      template<bool construct>
      void _alloc(allocator_type alloc, const T* ptr, std::size_t size, std::size_t copy_size) {
         impl* new_data = nullptr;
         if (size > 0) {
            new_data = (impl*)&*alloc.allocate(sizeof(impl) + (size * sizeof(T)));
            new_data->reference_count = 1;
            new_data->size = size;
            if (ptr && copy_size) {
               assert(copy_size <= size);
               std::uninitialized_copy(ptr, ptr + copy_size, new_data->data);
            }
            if constexpr (construct) {
               // construct objects that were not copied
               assert(ptr || copy_size == 0);
               for (std::size_t i=copy_size; i<size; ++i)
                  new (new_data->data + i) T();
            }
         }
         dec_refcount(alloc); // has to be after copy above
         _data = new_data;
      }
      
      template<bool construct>
      void _alloc(const T* ptr, std::size_t size, std::size_t copy_size) {
         _alloc<construct>(get_allocator(this), ptr, size, copy_size);
      }
      
      bip::offset_ptr<impl> _data { nullptr };
   };

}