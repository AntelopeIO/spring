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

      template<typename Alloc>
      explicit shared_cow_string(Alloc&& ) {}

      template<typename Iter>
      explicit shared_cow_string(Iter begin, Iter end, const allocator_type& alloc) {
         std::size_t size = std::distance(begin, end);
         _alloc(alloc, &*begin, size);
      }

      explicit shared_cow_string(const char* ptr, std::size_t size, const allocator_type& alloc) {
         _alloc(alloc, ptr, size);
      }

      explicit shared_cow_string(std::string_view sv, const allocator_type& alloc) {
         _alloc(alloc, sv.data(), sv.size());
      }

      explicit shared_cow_string(std::size_t size, boost::container::default_init_t, const allocator_type& alloc) {
         _alloc(alloc, nullptr, size);
      }

      shared_cow_string(const shared_cow_string& other) : _data(other._data) {
         if(_data != nullptr) {
            ++_data->reference_count;
         }
      }

      shared_cow_string(shared_cow_string&& other) noexcept : _data(other._data) {
         other._data = nullptr;
      }

      shared_cow_string& operator=(const shared_cow_string& other) {
         *this = shared_cow_string{other};
         return *this;
      }

      shared_cow_string& operator=(shared_cow_string&& other)  noexcept {
         if (this != &other) {
            dec_refcount();
            _data = other._data;
            other._data = nullptr;
         }
         return *this;
      }

      ~shared_cow_string() {
         dec_refcount();
      }

      template<typename F>
      void resize_and_fill(std::size_t new_size, F&& f) {
         dec_refcount();
         _alloc(nullptr, new_size);
         static_cast<F&&>(f)(_data->data, new_size);
      }

      void assign(const char* ptr, std::size_t size) {
         dec_refcount();
         _alloc(ptr, size);
      }

      void assign(std::string_view sv) {
         dec_refcount();
         _alloc(sv.data(), sv.size());
      }

      void assign(const unsigned char* ptr, std::size_t size) {
         dec_refcount();
         _alloc((const char*)ptr, size);
      }

      const char * data() const {
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

      int compare(std::size_t start, std::size_t count, const char* other, std::size_t other_size) const {
         std::size_t sz = size();
         if(start > sz) BOOST_THROW_EXCEPTION(std::out_of_range{"shared_cow_string::compare"});
         count = std::min(count, sz - start);
         std::size_t cmp_len = std::min(count, other_size);
         const char* start_ptr = data() + start;
         int result = std::char_traits<char>::compare(start_ptr, other, cmp_len);
         if (result != 0) return result;
         else if (count < other_size) return -1;
         else if(count > other_size) return 1;
         else return 0;
      }

      std::strong_ordering operator<=>(const shared_cow_string& o) const {
         int res = compare(0, size(), o.data(), o.size());
         if (res == 0)
            return std::strong_ordering::equal;
         return res < 0 ? std::strong_ordering::less : std::strong_ordering::greater;
      }

      bool operator==(const shared_cow_string& rhs) const {
        return size() == rhs.size() && std::memcmp(data(), rhs.data(), size()) == 0;
      }

      bool operator!=(const shared_cow_string& rhs) const { return !(*this == rhs); }

      bool operator==(std::string_view sv) const {
        return size() == sv.size() && std::memcmp(data(), sv.data(), size()) == 0;
      }

      bool operator!=(std::string_view sv) const { return !(*this == sv); }

      static allocator_type get_allocator(void* obj) {
         return pinnable_mapped_file::get_allocator<char>(obj);
      }

      // tdb: remove as not needed ... need to update libraries/chain/include/eosio/chain/database_utils.hpp
      // also should use `assign()` instead of `s = eosio::chain::shared_string(_s.begin(), _s.end(), ...`
      allocator_type get_allocator() const {
         return get_allocator((void *)this);
      }

    private:
      void dec_refcount() {
         if(_data && --_data->reference_count == 0) {
            get_allocator(this).deallocate((char*)&*_data, sizeof(impl) + _data->size + 1);
         }
      }

      void _alloc(allocator_type alloc, const char* ptr, std::size_t size) {
         impl* new_data = nullptr;
         if (size > 0) {
            new_data = (impl*)&*alloc.allocate(sizeof(impl) + size + 1);
            new_data->reference_count = 1;
            new_data->size = size;
            if (ptr)
               std::memcpy(new_data->data, ptr, size);
            new_data->data[size] = '\0';
         }
         _data = new_data;
      }

      void _alloc(const char* ptr, std::size_t size) {
         _alloc(get_allocator(this), ptr, size);
      }

      bip::offset_ptr<impl> _data { nullptr };
   };

}  // namespace chainbase
