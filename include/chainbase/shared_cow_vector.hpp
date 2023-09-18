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
      using allocator_type = bip::allocator<char, pinnable_mapped_file::segment_manager>;
      using iterator = const T*;
      using const_iterator = const T*;
      explicit shared_cow_vector(const allocator_type& alloc) : _data(nullptr) {
         if (!_s_alloc.has_value()) {
            _s_alloc.emplace(alloc);
         } else {
            assert(_s_alloc == alloc);
         }
      }
      template<typename Iter>
      explicit shared_cow_vector(Iter begin, Iter end, const allocator_type& alloc) : shared_cow_vector(alloc) {
         std::size_t size = std::distance(begin, end);
         if (size > 0) {
            impl* new_data = (impl*)&*_s_alloc.value().allocate(sizeof(impl) + (size * sizeof(T)));
            new_data->reference_count = 1;
            new_data->size = size;
            std::copy(begin, end, new_data->data);
            _data = new_data;
         } else {
            _data = nullptr;
         }
      }
      explicit shared_cow_vector(const char* ptr, std::size_t size, const allocator_type& alloc) : shared_cow_vector(alloc) {
         assign(ptr, size);
      }
      explicit shared_cow_vector(std::size_t size, boost::container::default_init_t, const allocator_type& alloc) : shared_cow_vector(alloc) {
         impl* new_data = nullptr;
         if (size > 0) {
            new_data = (impl*)&*_s_alloc.value().allocate(sizeof(impl) + (size * sizeof(T)));
            new_data->reference_count = 1;
            new_data->size = size;
         }
         _data = new_data;
      }
      shared_cow_vector(const shared_cow_vector& other) : _data(other._data) {
         if(_data != nullptr) {
            ++_data->reference_count;
         }
         assert(_s_alloc.has_value());
      }
      shared_cow_vector(shared_cow_vector&& other) : _data(other._data) {
         other._data = nullptr;
         assert(_s_alloc.has_value());
      }
      shared_cow_vector& operator=(const shared_cow_vector& other) {
         *this = shared_cow_vector{other};
         return *this;
      }
      shared_cow_vector& operator=(shared_cow_vector&& other) {
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

      void resize(std::size_t new_size, boost::container::default_init_t) {
         impl* new_data = nullptr; 
         if (new_size > 0 ) {
            new_data = (impl*)&*_s_alloc.value().allocate(sizeof(impl) + (new_size * sizeof(T)));
            new_data->reference_count = 1;
            new_data->size = new_size;
         }
         dec_refcount();
         _data = new_data;
      }

      void resize(std::size_t new_size) {
         impl* new_data = nullptr; 
         if (new_size > 0 ) {
            new_data = (impl*)&*_s_alloc.value().allocate(sizeof(impl) + (new_size * sizeof(T)));
            new_data->reference_count = 1;
            new_data->size = new_size;
            if (_data) {
               std::size_t copy_size = std::min<std::size_t>(new_size, _data->size);
               std::copy(_data->data, _data->data + copy_size, new_data->data);
            }
         }
         dec_refcount();
         _data = new_data;
      }

      template<typename F>
      void resize_and_fill(std::size_t new_size, boost::container::default_init_t, F&& f) {
         resize(new_size, boost::container::default_init);
         static_cast<F&&>(f)(_data->data, new_size);
      }

      template<typename F>
      void resize_and_fill(std::size_t new_size, F&& f) {
         resize(new_size);
         static_cast<F&&>(f)(_data->data, new_size);
      }

      void assign(const T* ptr, std::size_t size) {
         impl* new_data = nullptr;

         if(size > 0) {
            new_data = (impl*)&*_s_alloc.value().allocate(sizeof(impl) + (size * sizeof(T)));
            new_data->reference_count = 1;
            new_data->size = size;
            std::copy(ptr, ptr + size, new_data->data);
         }

         dec_refcount();
         _data = new_data;
      }
      void assign(const unsigned char* ptr, std::size_t size) {
         assign((char*)ptr, size);
      }
      const T * data() const {
         if (_data) return _data->data;
         else return nullptr;
      }
      std::size_t size() const {
         if (_data) return _data->size;
         else return 0;
      }
      const_iterator begin() const { return data(); }
      const_iterator end() const {
         if (_data) return _data->data + _data->size;
         else return nullptr;
      }
      
      const_iterator cbegin() const { return data(); }
      const_iterator cend() const {
         if (_data) return _data->data + _data->size;
         else return nullptr;
      }

      bool operator==(const shared_cow_vector& rhs) const {
        return size() == rhs.size() && std::memcmp(data(), rhs.data(), size() * sizeof(T)) == 0;
      }
      bool operator!=(const shared_cow_vector& rhs) const { return !(*this == rhs); }

      const allocator_type& get_allocator() const { return _s_alloc.value(); }
    private:
      void dec_refcount() {
         if(_data && --_data->reference_count == 0) {
            _s_alloc.value().deallocate((char*)&*_data, sizeof(impl) + (_data->size * sizeof(T)));
         }
      }
      bip::offset_ptr<impl> _data;
      static inline std::optional<allocator_type> _s_alloc;
   };

}