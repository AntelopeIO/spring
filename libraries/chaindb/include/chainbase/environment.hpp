#pragma once
#include <chainbase/pinnable_mapped_file.hpp>
#include <iomanip>
#include <fc/variant.hpp>
#include <fc/io/raw.hpp>

consteval uint64_t operator""_u64str(const char* s, const size_t size) {
   assert(size == 8);

   uint64_t result = 0;
   for(size_t i = 0; i < 8; ++i)
      result |= static_cast<uint64_t>(s[i]) << (i * 8);

   return result;
}

namespace chainbase {

constexpr size_t header_size = 1024;
// `CHAINB01` reflects changes since `EOSIODB3`.
// Spring 1.0 is compatible with `CHAINB01`.
constexpr uint64_t header_id = "CHAINB01"_u64str;

struct environment  {
   environment() {
      strncpy(compiler.data(), __VERSION__, compiler.size()-1);
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

   uint32_t boost_version = BOOST_VERSION;
   std::array<char,256> compiler = {};

   friend bool operator==(const environment&, const environment&) = default;

   template <typename DataStream>
   friend DataStream& operator<<(DataStream& ds, const environment& e) {
      fc::raw::pack(ds, e.debug);
      fc::raw::pack(ds, (uint8_t)e.os);
      fc::raw::pack(ds, (uint8_t)e.arch);
      fc::raw::pack(ds, e.boost_version);
      ds.skip(512); //the old 'reserved' field
      fc::raw::pack(ds, e.compiler);
      return ds;
   }

   template <typename DataStream>
   friend DataStream& operator>>(DataStream& ds, environment& e) {
      fc::raw::unpack(ds, e.debug);
      unsigned char tmp;
      fc::raw::unpack(ds, tmp);
      e.os = (os_t)tmp;
      fc::raw::unpack(ds, tmp);
      e.arch = (arch_t)tmp;
      fc::raw::unpack(ds, e.boost_version);
      ds.skip(512); //the old 'reserved' field
      fc::raw::unpack(ds, e.compiler);
      return ds;
   }
};

struct db_header  {
   uint64_t id = header_id;
   bool dirty = false;
   environment dbenviron;
};

constexpr size_t header_dirty_bit_offset = offsetof(db_header, dirty);
//older implementations had a packed db_header; double check that non-packed db_header struct still has this in the right spot. Reconsider post-CHAINB01
static_assert(header_dirty_bit_offset == 8, "DB dirty byte is expected to be at offset 8");

//really what we want to check here is the pack_size() of a db_header, but that isn't constexpr. Reconsider post-CHAINB01
static_assert(sizeof(db_header) <= header_size, "DB header struct too large");

}

FC_REFLECT_ENUM(chainbase::environment::os_t, (OS_LINUX)(OS_MACOS)(OS_WINDOWS)(OS_OTHER))
FC_REFLECT_ENUM(chainbase::environment::arch_t, (ARCH_X86_64)(ARCH_ARM)(ARCH_RISCV)(ARCH_OTHER))

FC_REFLECT(chainbase::db_header, (id)(dirty)(dbenviron));

namespace fc {
   void to_variant(const chainbase::environment& var, variant& vo);
}