#pragma once

/*
 * A basic intrusive CoW pointer, that can be used via chaindb's allocator
 */

#include <chainbase/pinnable_mapped_file.hpp>

#include <boost/scope/scope_fail.hpp>

#include <concepts>

namespace chainbase {

template<typename T>
concept CoWPtrRefCounted = requires(T& obj) {
   { +obj.ref_count } -> std::unsigned_integral;
};

template<CoWPtrRefCounted T, typename Allocator>
class cow_ptr {
   using pointer_type = typename std::allocator_traits<Allocator>::pointer;
   static constexpr auto max_ref_count = std::numeric_limits<decltype(T::ref_count)>::max(); //TODO we should probably be checking this for wrap around

   pointer_type ptr{};

public:
   cow_ptr() = default;

   cow_ptr(const cow_ptr& other) : ptr(other.ptr) {
      if(ptr)
         ++ptr->ref_count;
   }

   cow_ptr(cow_ptr&& other) : ptr(std::exchange(other.ptr, pointer_type{})) {}

   cow_ptr& operator=(const cow_ptr& other) {
      if(this != &other) {
         reset();
         ptr = other.ptr;
         if(ptr)
            ++ptr->ref_count;
      }
      return *this;
   }

   cow_ptr& operator=(cow_ptr&& other) {
      if(this != &other) {
         reset();
         ptr = std::exchange(other.ptr, pointer_type{});
      }
      return *this;
   }

   T& write() {
      Allocator alloc = get_allocator();
      if(!ptr) {
         ptr = alloc.allocate(1);
         boost::scope::scope_fail failure_guard([&] {alloc.deallocate(ptr, 1);});
         std::construct_at(std::to_address(ptr));
         ptr->ref_count = 1;
      } else if(ptr->ref_count > 1) {
         auto new_ptr = alloc.allocate(1);
         boost::scope::scope_fail failure_guard([&] {alloc.deallocate(ptr, 1);});
         std::construct_at(std::to_address(new_ptr), *ptr);
         new_ptr->ref_count = 1;
         --ptr->ref_count;
         ptr = new_ptr;
      }
      return *ptr;
   }

   const T* read() const {
      return ptr ? std::to_address(ptr) : nullptr;
   }

   bool valid() const {
      return !!ptr;
   }

   ~cow_ptr() {
      reset();
   }

private:
   void reset() {
      Allocator alloc = get_allocator();
      if(ptr && --ptr->ref_count == 0) {
         std::destroy_at(std::to_address(ptr));
         alloc.deallocate(ptr, 1);
      }
      ptr = pointer_type{};
   }

   Allocator get_allocator() {
      if constexpr (std::same_as<Allocator, allocator<T>>)
         return *pinnable_mapped_file::get_allocator<T>(this);
      else
         return Allocator();
   }
};

template<typename T>
using shared_cow_ptr = cow_ptr<T, allocator<T>>;

}