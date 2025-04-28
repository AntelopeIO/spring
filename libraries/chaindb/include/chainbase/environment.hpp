#pragma once
#include <chainbase/pinnable_mapped_file.hpp>
#include <iomanip>

namespace chainbase {

constexpr size_t header_size = 1024;

// `CHAINB01` reflects changes since `EOSIODB3`.
// `CHAINB02` adds the small size allocator
// Spring 1.0 is compatible with `CHAINB01`.
// Spring 2.0 is compatible with `CHAINB02`.
// ---------------------------------------------
constexpr uint64_t header_id = 0x3230424e49414843ULL; //"CHAINB02" little endian

struct environment  {
   environment() {
      strncpy(compiler, __VERSION__, sizeof(compiler)-1);
   }

   enum os_t : unsigned char {
      OS_LINUX,
      OS_MACOS,
      OS_WINDOWS,
      OS_OTHER
   };
   enum arch_t : unsigned char {
      ARCH_X86_64,
      ARCH_ARM,
      ARCH_RISCV,
      ARCH_OTHER
   };

   bool debug =
#ifndef NDEBUG
      true;
#else
      false;
#endif
   os_t os =
#if defined(__linux__)
      OS_LINUX;
#elif defined(__APPLE__)
      OS_MACOS;
#elif defined(_WIN32)
      OS_WINDOWS;
#else
      OS_OTHER;
#endif
   arch_t arch =
#if defined(__x86_64__)
      ARCH_X86_64;
#elif defined(__aarch64__)
      ARCH_ARM;
#elif defined(__riscv__)
      ARCH_RISCV;
#else
      ARCH_OTHER;
#endif

   unsigned boost_version = BOOST_VERSION;
   uint8_t reserved[512] = {};
   char compiler[256] = {};

   bool operator==(const environment& other) const {
      return !memcmp(this, &other, sizeof(environment));
   } 
   bool operator!=(const environment& other) const {
      return !(*this == other);
   }
} __attribute__ ((packed));

struct db_header  {
   uint64_t               id    = header_id;
   bool                   dirty = false;
   bip::offset_ptr<char>  small_size_allocator;
   environment            dbenviron;
};

constexpr size_t header_dirty_bit_offset = offsetof(db_header, dirty);

static_assert(sizeof(db_header) <= header_size, "DB header struct too large");

std::ostream& operator<<(std::ostream& os, const chainbase::environment& dt);

}
