#pragma once

#include <istream>
#include <ostream>
#include <vector>
#include <string>

#include <fc/io/raw.hpp>

#include <sys/resource.h>

#ifndef __has_feature
#define __has_feature(x) 0
#endif

namespace eosio { namespace chain { namespace eosvmoc {

struct config {
   uint64_t get_cache_size() const { return cache_size; }
   uint64_t get_threads() const { return threads; }

   std::optional<rlim_t> get_cpu_limit() const { return !whitelisted ? cpu_limit : std::optional<rlim_t>{}; }
   std::optional<rlim_t> get_vm_limit() const { return !whitelisted ? vm_limit : std::optional<rlim_t>{}; }

   uint64_t get_stack_size_limit() const {
      return !whitelisted
                ? stack_size_limit
                     ? *stack_size_limit
                     : std::numeric_limits<uint64_t>::max()
                : std::numeric_limits<uint64_t>::max();
   }

   size_t get_generated_code_size_limit() const {
      return !whitelisted
                ? generated_code_size_limit
                     ? *generated_code_size_limit
                     : std::numeric_limits<uint64_t>::max()
                : std::numeric_limits<uint64_t>::max();
   }

   bool whitelisted = false;
   uint64_t cache_size = 1024u*1024u*1024u;
   uint64_t threads    = 1u;

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

//work around unexpected std::optional behavior
template <typename DS>
inline DS& operator>>(DS& ds, eosio::chain::eosvmoc::config& cfg) {
   fc::raw::unpack(ds, cfg.whitelisted);
   fc::raw::unpack(ds, cfg.cache_size);
   fc::raw::unpack(ds, cfg.threads);

   auto better_optional_unpack = [&]<typename T>(std::optional<T>& t) {
      bool b; fc::raw::unpack( ds, b );
      if(b) { t = T(); fc::raw::unpack( ds, *t ); }
      else { t.reset(); }
   };
   better_optional_unpack(cfg.cpu_limit);
   better_optional_unpack(cfg.vm_limit);
   better_optional_unpack(cfg.stack_size_limit);
   better_optional_unpack(cfg.generated_code_size_limit);

   return ds;
}

template <typename DS>
inline DS& operator<<(DS& ds, const eosio::chain::eosvmoc::config& cfg) {
   fc::raw::pack(ds, cfg.whitelisted);
   fc::raw::pack(ds, cfg.cache_size);
   fc::raw::pack(ds, cfg.threads);
   fc::raw::pack(ds, cfg.cpu_limit);
   fc::raw::pack(ds, cfg.vm_limit);
   fc::raw::pack(ds, cfg.stack_size_limit);
   fc::raw::pack(ds, cfg.generated_code_size_limit);
   return ds;
}

}}}
