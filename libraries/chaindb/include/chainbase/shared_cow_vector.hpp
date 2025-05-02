#pragma once
#include <boost/interprocess/offset_ptr.hpp>
#include <boost/interprocess/allocators/allocator.hpp>

#include <cstddef>
#include <cstring>
#include <algorithm>
#include <memory>
#include <optional>
#include <string>

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
      using iterator       = const T*;      // const because of copy-on-write
      using const_iterator = const T*;
      using value_type     = T;

      explicit shared_cow_vector() = default;

      template<typename Iter>
      explicit shared_cow_vector(Iter begin, Iter end) {
         std::size_t size = std::distance(begin, end);
         _alloc<false>(&*begin, size, size);
      }

      template<class I, std::enable_if_t<std::is_constructible_v<T, I>, int> = 0>
      explicit shared_cow_vector(const I* ptr, std::size_t size) {
         _alloc<false>(ptr, size, size);
      }

      shared_cow_vector(const shared_cow_vector& o) {
         if (get_allocator(this) == o.get_allocator()) {
            _data = o._data;
            if (_data != nullptr)
               ++_data->reference_count;
         } else {
            if (o._data)
               new (this) shared_cow_vector(o.data(),  o.size()); // call other constructor
         }
      }

      shared_cow_vector(shared_cow_vector&& o) noexcept {
         if (get_allocator() == o.get_allocator()) {
            _data = o._data;
            o._data = nullptr;
         } else {
            if (o._data)
               new (this) shared_cow_vector(o.data(),  o.size());
         }
      }

      template<class I, std::enable_if_t<std::is_constructible_v<T, I>, int> = 0 >
      shared_cow_vector(std::initializer_list<I> init) {
         clear_and_construct(init.size(), 0, [&](T* dest, std::size_t idx) {
            new (dest) T(std::data(init)[idx]);
         });
      }

      template<class I, std::enable_if_t<std::is_constructible_v<T, I>, int> = 0 >
      explicit shared_cow_vector(const std::vector<I>& v) {
         clear_and_construct(v.size(), 0, [&](T* dest, std::size_t idx) {
            new (dest) T(v[idx]);
         });
      }

      template<class I, std::enable_if_t<std::is_constructible_v<T, I>, int> = 0 >
      explicit shared_cow_vector(std::vector<I>&& v) {
         clear_and_construct(v.size(), 0, [&](T* dest, std::size_t idx) {
            new (dest) T(std::move(v[idx]));
         });
      }

      ~shared_cow_vector() {
         dec_refcount();
         _data = nullptr;
      }

      shared_cow_vector& operator=(const shared_cow_vector& o) {
         if (this != &o) {
            if (get_allocator() == o.get_allocator()) {
               dec_refcount();
               _data = o._data;
               if (_data != nullptr) 
                  ++_data->reference_count;
            } else {
               _assign(o.data(), o.size());
            }
         }
         return *this;
      }

      shared_cow_vector& operator=(shared_cow_vector&& o) noexcept {
         if (this != &o) {
            if (get_allocator() == o.get_allocator()) {
               dec_refcount();
               _data = o._data;
               o._data = nullptr;
            } else {
               clear_and_construct(o.size(), 0, [&](T* dest, std::size_t idx) {
                  new (dest) T(std::move(o[idx]));
               });
            }
         }
         return *this;
      }

      template<class I, std::enable_if_t<std::is_constructible_v<T, I>, int> = 0 >
      shared_cow_vector& operator=(const std::vector<I>& v) {
         clear_and_construct(v.size(), 0, [&](T* dest, std::size_t idx) {
            new (dest) T(v[idx]);
         });
         return *this;
      }

      template<class I, std::enable_if_t<std::is_constructible_v<T, I>, int> = 0 >
      shared_cow_vector& operator=(std::vector<I>&& v) {
         clear_and_construct(v.size(), 0, [&](T* dest, std::size_t idx) {
            new (dest) T(std::move(v[idx]));
         });
         return *this;
      }

      template<class I, std::enable_if_t<std::is_constructible_v<T, I>, int> = 0 >
      shared_cow_vector& operator=(std::initializer_list<I> init) {
         clear_and_construct(init.size(), 0, [&](T* dest, std::size_t idx) {
            new (dest) T(std::data(init)[idx]);
         });
         return *this;
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

      template<class... Args>
      void emplace_back(Args&&... args) {
         clear_and_construct(size() + 1, size(), [&](T* dest, std::size_t idx) {
            new (dest) T(std::forward<Args>(args)...);
         });
      }

      // const data access. Do *not* define the non-const version as it breaks copy-on-write
      const T* data() const {
         return _data ? _data->data : nullptr;
      }

      // const data access. Do *not* define the non-const version as it breaks copy-on-write
      const T& operator[](std::size_t idx) const { assert(_data); return _data->data[idx]; }

      std::size_t size() const {
         return _data ? _data->size : 0;
      }

      bool empty() const {
         return size() == 0;
      }

      // Because of copy-on-write, these should be const and return `const *T`
      iterator begin() const { return data(); }
      iterator end() const {
         return _data ? _data->data + _data->size : nullptr;
      }

      const_iterator cbegin() const { return begin(); }
      const_iterator cend()   const { return end(); }

      bool operator==(const shared_cow_vector& rhs) const {
         return _data == rhs._data ||
            (size() == rhs.size() && std::equal(cbegin(), cend(), rhs.cbegin()));
      }

      bool operator!=(const shared_cow_vector& rhs) const { return !(*this == rhs); }

      static std::optional<allocator_type> get_allocator(void* obj) {
         return pinnable_mapped_file::get_allocator<char>(obj);
      }      

      const std::optional<allocator_type> get_allocator() const {
         return get_allocator((void *)this);
      }

    private:
      void _assign(const T* ptr, std::size_t size) {
         if (_data && _data->reference_count == 1 && _data->size == size)
            std::copy(ptr, ptr + size, _data->data);
         else {
            _alloc<false>(ptr, size, size);
         }
      }

      void _assign(const std::vector<T>& v) {
         _assign(v.data(), v.size());
      }

      template<class Alloc>
      void dec_refcount(Alloc&& alloc) {
         if (_data && --_data->reference_count == 0) {
            assert(_data->size);                                    // if size == 0, _data should be nullptr
            std::destroy(_data->data, _data->data + _data->size);
            std::forward<Alloc>(alloc).deallocate((char*)&*_data, sizeof(impl) + (_data->size * sizeof(T)));
         }
      }

      void dec_refcount() {
         auto alloc = get_allocator(this);
         if (alloc)
            dec_refcount(*alloc);
         else
            dec_refcount(std::allocator<char>());
      }

      template<bool construct, class Alloc, class I, std::enable_if_t<std::is_constructible_v<T, I>, int> = 0>
      void _alloc(Alloc&& alloc, const I* ptr, std::size_t size, std::size_t copy_size) {
         impl* new_data = nullptr;
         if (size > 0) {
            new_data = (impl*)&*std::forward<Alloc>(alloc).allocate(sizeof(impl) + (size * sizeof(T)));
            new_data->reference_count = 1;
            new_data->size = size;
            if (ptr && copy_size) {
               assert(copy_size <= size);
               std::uninitialized_copy(ptr, ptr + copy_size, new_data->data);
            }
            if constexpr (construct) {
               // default construct objects that were not copied
               assert(ptr || copy_size == 0);
               for (std::size_t i=copy_size; i<size; ++i)
                  new (new_data->data + i) T();
            }
         }
         dec_refcount(std::forward<Alloc>(alloc)); // has to be after copy above
         _data = new_data;
      }
      
      template<bool construct, class I, std::enable_if_t<std::is_constructible_v<T, I>, int> = 0>
      void _alloc(const I* ptr, std::size_t size, std::size_t copy_size) {
         auto alloc = get_allocator(this);
         if (alloc)
            _alloc<construct>(*alloc, ptr, size, copy_size);
         else
            _alloc<construct>(std::allocator<char>(), ptr, size, copy_size);
      }
      
      bip::offset_ptr<impl> _data { nullptr };
   };

}