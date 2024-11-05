#pragma once

#include <fc/io/raw.hpp>

#include <string>
#include <optional>

#include <sys/resource.h>

#ifndef __has_feature
#define __has_feature(x) 0
#endif

namespace eosio { namespace chain { namespace eosvmoc {

struct subjective_compile_limits {
   // subjective limits for OC compilation.
   // nodeos enforces the limits by the default values unless account is whitelisted.
   // libtester disables the limits in all tests, except enforces the limits
   // in the tests in unittests/eosvmoc_limits_tests.cpp.
   std::optional<rlim_t>   cpu_limit {20u};
#if __has_feature(undefined_behavior_sanitizer) || __has_feature(address_sanitizer)
   std::optional<rlim_t>   vm_limit; // UBSAN & ASAN can add massive virtual memory usage; don't enforce vm limits when either of them are enabled
#else
   std::optional<rlim_t>   vm_limit  {512u*1024u*1024u};
#endif
   std::optional<uint64_t> stack_size_limit {16u*1024u};
   std::optional<size_t>   generated_code_size_limit {16u*1024u*1024u};
};

struct config {
   uint64_t cache_size = 1024u*1024u*1024u;
   uint64_t threads    = 1u;
   subjective_compile_limits non_whitelisted_limits;
};

//work around unexpected std::optional behavior
template <typename DS>
inline DS& operator>>(DS& ds, eosio::chain::eosvmoc::subjective_compile_limits& cl) {
   auto optional_unpack_with_reset = [&]<typename T>(std::optional<T>& t) {
      bool b; fc::raw::unpack( ds, b );
      if(b) { t = T(); fc::raw::unpack( ds, *t ); }
      else { t.reset(); }
   };
   optional_unpack_with_reset(cl.cpu_limit);
   optional_unpack_with_reset(cl.vm_limit);
   optional_unpack_with_reset(cl.stack_size_limit);
   optional_unpack_with_reset(cl.generated_code_size_limit);

   return ds;
}

template <typename DS>
inline DS& operator<<(DS& ds, const eosio::chain::eosvmoc::subjective_compile_limits& cl) {
   fc::raw::pack(ds, cl.cpu_limit);
   fc::raw::pack(ds, cl.vm_limit);
   fc::raw::pack(ds, cl.stack_size_limit);
   fc::raw::pack(ds, cl.generated_code_size_limit);
   return ds;
}

}}}

FC_REFLECT(eosio::chain::eosvmoc::config, (cache_size)(threads)(non_whitelisted_limits))