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
         _alloc(alloc, &*begin, size, size);
      }

      explicit shared_cow_vector(const T* ptr, std::size_t size, const allocator_type& alloc) {
         _alloc(alloc, ptr, size, size);
      }

#if 0
      explicit shared_cow_vector(std::size_t size, boost::container::default_init_t, const allocator_type& alloc) {
         _alloc(alloc, nullptr, size, 0);
      }
#endif

      shared_cow_vector(const shared_cow_vector& other) : _data(other._data) {
         if (_data != nullptr) {
            ++_data->reference_count;
         }
      }

      shared_cow_vector(shared_cow_vector&& other) noexcept : _data(other._data) {
         other._data = nullptr;
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
      }

      void clear() {
         dec_refcount();
         _data = nullptr;
      }

#if 0
      void resize(std::size_t new_size, boost::container::default_init_t) {
         dec_refcount();
         _alloc(nullptr, new_size, 0);
      }

      template<typename F>
      void resize_and_fill(std::size_t new_size, boost::container::default_init_t, F&& f) {
         resize(new_size, boost::container::default_init);
         static_cast<F&&>(f)(_data->data, new_size);
      }
#endif
      
      template<typename F>
      void resize_and_fill(std::size_t new_size, F&& f) {
         resize(new_size);
         static_cast<F&&>(f)(_data->data, new_size);
      }

      void assign(const T* ptr, std::size_t size) {
         if (_data && _data->reference_count == 1 && _data->size == size)
            std::copy(ptr, ptr + size, data());
         else {
            dec_refcount();
            _alloc(ptr, size, size);
         }
      }

      const T* data() const {
         return _data ? _data->data : nullptr;
      }

      std::size_t size() const {
         return _data ? _data->size : 0;
      }

      const_iterator begin() const { return data(); }

      const_iterator end() const {
         return _data ? _data->data + _data->size : nullptr;
      }

      const_iterator cbegin() const { return begin(); }
      const_iterator cend()   const { return end(); }

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
      void resize(std::size_t new_size) {
         if (_data && _data->reference_count == 1 && _data->size == new_size)
            return; // already the correct size and not shared => do nothing
         dec_refcount();
         std::size_t copy_size = std::min<std::size_t>(new_size, size());
         if (new_size < size())
            std::destroy(_data->data + new_size, _data->data + _data->size);
         _alloc(data(), new_size, copy_size);
      }

      void dec_refcount() {
         if (_data && --_data->reference_count == 0) {
            assert(_data->size);                                    // if size == 0, _data should be nullptr
            std::destroy(_data->data, _data->data + _data->size);
            get_allocator(this).deallocate((char*)&*_data, sizeof(impl) + (_data->size * sizeof(T)));
            _data = nullptr;
         }
      }

      void _alloc(allocator_type alloc, const T* ptr, std::size_t size, std::size_t copy_size) {
         impl* new_data = nullptr;
         if (size > 0) {
            new_data = (impl*)&*alloc.allocate(sizeof(impl) + (size * sizeof(T)));
            new_data->reference_count = 1;
            new_data->size = size;
            if (ptr && copy_size) {
               if (ptr == data())
                  std::uninitialized_move(ptr, ptr + copy_size, new_data->data);
               else
                  std::uninitialized_copy(ptr, ptr + copy_size, new_data->data);
            }
            
            // construct objects that were not copied
            assert(ptr || copy_size == 0);
            for (std::size_t  i=copy_size; i<size; ++i)
               new (new_data->data + i) T();
         }
         _data = new_data;
      }

      void _alloc(const T* ptr, std::size_t size, std::size_t copy_size) {
         _alloc(get_allocator(this), ptr, size, copy_size);
      }
      
      bip::offset_ptr<impl> _data { nullptr };
   };

}