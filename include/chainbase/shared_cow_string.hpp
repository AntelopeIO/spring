#pragma once

#include <boost/container/container_fwd.hpp>
#include <boost/interprocess/offset_ptr.hpp>
#include <boost/interprocess/allocators/allocator.hpp>

#include <cstddef>
#include <cstring>
#include <algorithm>
#include <string>
#include <string_view>
#include <optional>
#include <compare>

#include <chainbase/pinnable_mapped_file.hpp>

namespace chainbase {

   namespace bip = boost::interprocess;

   class shared_cow_string {
      struct impl {
         uint32_t reference_count;
         uint32_t size;
         char data[0];
      };

    public:
      using allocator_type = bip::allocator<char, segment_manager>;
      using iterator       = const char*;
      using const_iterator = const char*;

      shared_cow_string() = default;

      template<typename Iter>
      explicit shared_cow_string(Iter begin, Iter end) {
         std::size_t size = std::distance(begin, end);
         _alloc(&*begin, size);
      }

      explicit shared_cow_string(const char* ptr, std::size_t size) {
         assert(ptr);
         _alloc(ptr, size);
      }

      explicit shared_cow_string(const char* ptr) {
         if (ptr)
            _alloc(ptr, strlen(ptr));
      }

      explicit shared_cow_string(std::string_view sv) {
         _alloc(sv.data(), sv.size());
      }

      explicit shared_cow_string(std::size_t size, boost::container::default_init_t) {
         _alloc(nullptr, size);
      }

      shared_cow_string(const shared_cow_string& o) {
         if (get_allocator(this) == o.get_allocator()) {
            _data = o._data;
            if (_data != nullptr)
               ++_data->reference_count;
         } else {
            if (o._data)
               new (this) shared_cow_string(o.data(), o.size());
         }
      }

      shared_cow_string(shared_cow_string&& o) noexcept {
         if (get_allocator() == o.get_allocator()) {
            _data = o._data;
            o._data = nullptr;
         } else {
            if (o._data)
               new (this) shared_cow_string(o.data(), o.size());
         }
      }

      ~shared_cow_string() {
         dec_refcount();
         _data = nullptr;
      }

      shared_cow_string& operator=(const shared_cow_string& o) {
         if (this != &o) {
            if (get_allocator() == o.get_allocator()) {
               dec_refcount();
               _data = o._data;
               if (_data != nullptr) 
                  ++_data->reference_count;
            } else {
               assign(o.data(), o.size());
            }
         }
         return *this;
      }

      shared_cow_string& operator=(shared_cow_string&& o)  noexcept {
         if (this != &o) {
            if (get_allocator() == o.get_allocator()) {
               dec_refcount();
               _data = o._data;
               o._data = nullptr;
            } else {
               assign(o.data(), o.size());
            }
         }
         return *this;
      }

      shared_cow_string&  operator=(std::string_view sv) {
         if (!copy_in_place(sv.data(), sv.size())) {
            _alloc(sv.data(), sv.size());
         }
         return *this;
      }

      template<typename F>
      void resize_and_fill(std::size_t new_size, F&& f) {
         if (!copy_in_place(nullptr, new_size)) {
            _alloc(nullptr, new_size);
         }
         static_cast<F&&>(f)(_data->data, new_size);
      }

      void assign(const char* ptr, std::size_t size) {
         if (!copy_in_place(ptr, size)) {
            _alloc(ptr, size);
         }
      }

      void assign(const unsigned char* ptr, std::size_t size) {
         assign((const char*)ptr, size);
      }

      const char* data() const {
         return _data ? _data->data : nullptr;
      }

      char* mutable_data() {
         assert (!_data || _data->reference_count == 1);
         return _data ? _data->data : nullptr;
      }

      std::size_t size() const {
         return _data ? _data->size : 0;
      }

      const_iterator begin() const { return data(); }
      const_iterator end() const {
         return _data ? _data->data + _data->size : nullptr;
      }

      int compare(std::size_t start, std::size_t count, const char* other, std::size_t other_size) const {
         std::size_t sz = size();
         if (start > sz) BOOST_THROW_EXCEPTION(std::out_of_range{"shared_cow_string::compare"});
         count = std::min(count, sz - start);
         std::size_t cmp_len = std::min(count, other_size);
         const char* start_ptr = data() + start;
         int result = std::char_traits<char>::compare(start_ptr, other, cmp_len);
         if (result != 0) return result;
         else if (count < other_size) return -1;
         else if (count > other_size) return 1;
         else return 0;
      }

#if defined(__cpp_lib_three_way_comparison) && __cpp_lib_three_way_comparison >= 201907
      std::strong_ordering operator<=>(const shared_cow_string& o) const {
         int res = compare(0, size(), o.data(), o.size());
         if (res == 0)
            return std::strong_ordering::equal;
         return res < 0 ? std::strong_ordering::less : std::strong_ordering::greater;
      }
#else
      bool operator<(const shared_cow_string& o) const {
         return compare(0, size(), o.data(), o.size()) < 0;
      }
#endif

      bool operator==(const shared_cow_string& rhs) const {
        return size() == rhs.size() && std::memcmp(data(), rhs.data(), size()) == 0;
      }

      bool operator!=(const shared_cow_string& rhs) const { return !(*this == rhs); }

      bool operator==(std::string_view sv) const {
        return size() == sv.size() && std::memcmp(data(), sv.data(), size()) == 0;
      }

      bool operator!=(std::string_view sv) const { return !(*this == sv); }

      static std::optional<allocator_type> get_allocator(void* obj) {
         return pinnable_mapped_file::get_allocator<char>(obj);
      }

      std::optional<allocator_type> get_allocator() const {
         return get_allocator((void *)this);
      }

    private:
      template<class Alloc>
      void dec_refcount(Alloc&& alloc) {
         if (_data && --_data->reference_count == 0) {
            assert(_data->size);                                    // if size == 0, _data should be nullptr
            std::forward<Alloc>(alloc).deallocate((char*)&*_data, sizeof(impl) + _data->size + 1);
         }
      }
      
      void dec_refcount() {
         auto alloc = get_allocator(this);
         if (alloc)
            dec_refcount(*alloc);
         else
            dec_refcount(std::allocator<char>());
      }

      bool copy_in_place(const char* ptr, std::size_t size) {
         if (_data && _data->reference_count == 1 && _data->size == size) {
            // we hold the only reference and size matches, not need to dealloc/realloc
            if (ptr)
               std::memcpy(_data->data, ptr, size);
            return true;
         }
         return false;
      }

      template<class Alloc>
      void _alloc(Alloc&& alloc, const char* ptr, std::size_t size) {
         impl* new_data = nullptr;
         if (size > 0) {
            new_data = (impl*)&*std::forward<Alloc>(alloc).allocate(sizeof(impl) + size + 1);
            new_data->reference_count = 1;
            new_data->size = size;
            if (ptr)
               std::memcpy(new_data->data, ptr, size);
            new_data->data[size] = '\0';
         }
         dec_refcount(std::forward<Alloc>(alloc));
         _data = new_data;
      }

      void _alloc(const char* ptr, std::size_t size) {
         auto alloc = get_allocator(this);
         if (alloc)
            _alloc(*alloc, ptr, size);
         else
            _alloc(std::allocator<char>(), ptr, size);
      }

      bip::offset_ptr<impl> _data { nullptr };
   };

}  // namespace chainbase
