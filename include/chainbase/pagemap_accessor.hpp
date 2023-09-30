#pragma once

#include <fcntl.h>    // open 
#include <unistd.h>   // pread, sysconf
#include <cstring>
#include <cstdlib> 
#include <cassert>
#include <optional>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <span>
#include <boost/interprocess/managed_external_buffer.hpp>
#include <boost/interprocess/anonymous_shared_memory.hpp>

namespace chainbase {

namespace bip = boost::interprocess;

class pagemap_accessor {
public:
   ~pagemap_accessor() {
      _close();
   }

   bool clear_refs() const {
      if constexpr (!_pagemap_supported)
         return false;
      
      int fd = ::open("/proc/self/clear_refs", O_WRONLY);
      if (fd < 0)
         return false;
      
      const char *v = "4";
      bool res = write(fd, v, 1) == 1;
      ::close(fd);
      return res;
   }
   
   static constexpr bool pagemap_supported() {
      return _pagemap_supported;
   }
   
   static bool is_marked_dirty(uint64_t entry) {
      return !!(entry & (1Ull << 55));
   }

   static size_t page_size() {
      return pagesz;
   }

   bool page_dirty(uintptr_t vaddr) const {
      uint64_t data;
      if (!read(vaddr, { &data, 1 }))
         return true;
      return this->is_marked_dirty(data);
   }

   bool read(uintptr_t vaddr, std::span<uint64_t> dest_uint64) const {
      if constexpr (!_pagemap_supported)
         return false;
      
      if (!_open()) // make sure file is open
         return false;
      assert(_pagemap_fd >= 0);
      auto dest = std::as_writable_bytes(dest_uint64);
      std::byte* cur = dest.data();
      size_t bytes_remaining = dest.size();
      uintptr_t offset = (vaddr / pagesz) * sizeof(uint64_t);
      while (bytes_remaining != 0) {
         ssize_t ret = pread(_pagemap_fd, cur, bytes_remaining, offset + (cur - dest.data()));
         if (ret < 0)
            return false;
         bytes_remaining -= (size_t)ret;
         cur += ret;
      }
      return true;
   }

   // copies the modified pages with the virtual address space specified by `rgn` to an
   // equivalent region starting at `offest` within the (open) file pointed by `fd`.
   // The specified region *must* be a multiple of the system's page size, and the specified
   // regioon should exist in the disk file.
   // --------------------------------------------------------------------------------------
   bool update_file_from_region(std::span<std::byte> rgn, bip::file_mapping& mapping, size_t offset, bool flush) const {
      if constexpr (!_pagemap_supported)
         return false;
      
      assert(rgn.size() % pagesz == 0);
      size_t num_pages = rgn.size() / pagesz;
      std::vector<uint64_t> pm(num_pages);
      
      // get modified pages
      if (!read((uintptr_t)rgn.data(), pm))
         return false;
      bip::mapped_region map_rgn(mapping, bip::read_write, offset, rgn.size());
      std::byte* dest = (std::byte*)map_rgn.get_address();
      if (dest) {
         for (size_t i=0; i<num_pages; ++i) {
            if (is_marked_dirty(pm[i])) {
               size_t j = i + 1;
               while (j<num_pages && is_marked_dirty(pm[j]))
                  ++j;
               memcpy(dest + (i * pagesz), rgn.data() + (i * pagesz), pagesz * (j - i));
               i += j - i - 1;
            }
         }
         if (flush && !map_rgn.flush(0, rgn.size(), /* async = */ false))
            std::cerr << "CHAINBASE: ERROR: flushing buffers failed" << '\n';
         return true;
      }
      return false;
   }

private:
   bool _open() const {
      assert(_pagemap_supported);
      if (_pagemap_fd < 0) {
         _pagemap_fd = ::open("/proc/self/pagemap", O_RDONLY);
         if (_pagemap_fd < 0) 
            return false;
      }
      return true;
   }

   bool _close() const {
      if (_pagemap_fd >= 0) {
         assert(_pagemap_supported);
         ::close(_pagemap_fd);
         _pagemap_fd = -1;
      }
      return true;
   }
   
   static inline size_t pagesz = sysconf(_SC_PAGE_SIZE);
   
#if defined(__linux__) && defined(__x86_64__)
   static constexpr bool _pagemap_supported = true;
#else
   static constexpr bool _pagemap_supported = false;
#endif
   
   mutable int _pagemap_fd = -1;
};

} // namespace chainbase