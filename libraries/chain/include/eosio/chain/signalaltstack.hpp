#pragma once

#include <signal.h>
#include <sys/mman.h>
#include <fc/log/logger.hpp>

namespace eosio:: chain {

struct signal_alt_stack {
   signal_alt_stack() {
      stack_t old_ss{};
      FC_ASSERT(sigaltstack(nullptr, &old_ss) == 0, "Failed to query sigaltstack");
      FC_ASSERT(old_ss.ss_flags & SS_DISABLE, "sigaltstack already installed");

      ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
      FC_ASSERT(ptr != MAP_FAILED, "Failed to mmap sigaltstack");

      stack_t ss{
         .ss_sp   = ptr,
         .ss_size = size
      };

      if(sigaltstack(&ss, nullptr) == -1) {
         munmap(ptr, size);
         FC_ASSERT(false, "Failed to sigaltstack");
      }
   }

   ~signal_alt_stack() {
      stack_t ss{.ss_flags = SS_DISABLE};

      if(sigaltstack(&ss, nullptr) == -1) {
         elog("Failed to clear sigaltstack");
         return;
      }
      if (munmap(ptr, size) == -1)
         elog("Failed to unmap sigaltstack");
   }

   void* ptr = nullptr;
   std::size_t size = SIGSTKSZ*2;

   signal_alt_stack(const signal_alt_stack&) = delete;
   signal_alt_stack& operator=(const signal_alt_stack&) = delete;
};

}
