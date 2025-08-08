#pragma once

#include <fcntl.h>    // open 
#include <unistd.h>   // pread, sysconf
#include <cstdlib> 
#include <cassert>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <span>
#include <boost/interprocess/managed_mapped_file.hpp>
#include <fc/log/logger.hpp>

namespace chainbase {

namespace bip = boost::interprocess;

class pagemap_accessor {
public:
   ~pagemap_accessor() {
      _close();
   }

   bool clear_refs() const {
      if (!_pagemap_supported)
         return false;
      return _clear_refs();
   }
   
   static bool pagemap_supported() {
      assert(_pagemap_support_checked);
      return _pagemap_supported;
   }

   // returns true if pagemap *is* supported and we successfully performed `clear_refs`
   bool check_pagemap_support_and_clear_refs() {
      if (!_pagemap_support_checked) {
         _pagemap_support_checked = true;

#if defined(__linux__) && defined(__x86_64__)
         std::unique_ptr<char, void(*)(char*)> p { (char *)std::aligned_alloc(pagesz, pagesz), [](char* p) {std::free(p);} };

         if (_clear_refs()) {
            if (!_page_dirty((uintptr_t)p.get())) {
               *p = 1;
               if (_page_dirty((uintptr_t)p.get()))
                  _pagemap_supported = true;
            }
         }
#endif
         ilog("Soft-Dirty pagemap support: ${sd}", ("sd", _pagemap_supported ? "OK" : "Not supported"));
      }
      return _pagemap_supported;
   }
   
   static bool is_marked_dirty(uint64_t entry) {
      return !!(entry & (1Ull << 55));
   }

   static size_t page_size() {
      return pagesz;
   }

   // /proc/pid/pagemap. This file lets a userspace process find out which physical frame each virtual page
   // is mapped to. It contains one 64-bit value for each virtual page, containing the following data
   // (from fs/proc/task_mmu.c, above pagemap_read):
   // 
   // Bits 0-54 page frame number (PFN) if present (note: field is zeroed for non-privileged users)
   // Bits 0-4 swap type if swapped
   // Bits 5-54 swap offset if swapped
   // Bit 55 pte is soft-dirty (see Documentation/admin-guide/mm/soft-dirty.rst)
   // Bit 56 page exclusively mapped (since 4.2)
   // Bit 57 pte is uffd-wp write-protected (since 5.13) (see Documentation/admin-guide/mm/userfaultfd.rst)
   // Bits 58-60 zero
   // Bit 61 page is file-page or shared-anon (since 3.5)
   // Bit 62 page swapped
   // Bit 63 page present
   //
   // Here we are just checking bit #55 (the soft-dirty bit).
   // ----------------------------------------------------------------------------------------------------
   bool read(uintptr_t vaddr, std::span<uint64_t> dest_uint64) const {
      if (!_pagemap_supported)
         return false;
      return _read(vaddr, dest_uint64);
   }

   // copies the modified pages with the virtual address space specified by `rgn` to an
   // equivalent region starting at `offest` within the (open) file pointed by `fd`.
   // The specified region *must* be a multiple of the system's page size, and the specified
   // region should exist in the disk file.
   // --------------------------------------------------------------------------------------
   bool update_file_from_region(std::span<std::byte> rgn, bip::file_mapping& mapping, size_t offset, bool flush, size_t& written_pages) const {
      if (!_pagemap_supported)
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
               written_pages += (j - i);
               i += j - i - 1;
            }
         }
         if (flush && !map_rgn.flush(0, rgn.size(), /* async = */ false))
            wlog("Flushing chainbase buffers failed");
         return true;
      }
      return false;
   }

private:
   bool _clear_refs() const {
      int fd = ::open("/proc/self/clear_refs", O_WRONLY);
      if (fd < 0)
         return false;
      
      // Clear soft-dirty bits from the task's PTEs.
      // This is done by writing "4" into the /proc/PID/clear_refs file of the task in question.
      // 
      // After this, when the task tries to modify a page at some virtual address, the #PF occurs
      // and the kernel sets the soft-dirty bit on the respective PTE.
      // ----------------------------------------------------------------------------------------
      const char *v = "4";
      bool res = write(fd, v, 1) == 1;
      ::close(fd);
      return res;
   }

   bool _read(uintptr_t vaddr, std::span<uint64_t> dest_uint64) const {
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
   
   bool _page_dirty(uintptr_t vaddr) const {
      uint64_t data;
      if (!_read(vaddr, { &data, 1 }))
         return true;
      return this->is_marked_dirty(data);
   }

   bool _open() const {
      if (_pagemap_fd < 0) {
         _pagemap_fd = ::open("/proc/self/pagemap", O_RDONLY);
         if (_pagemap_fd < 0) 
            return false;
      }
      return true;
   }

   bool _close() const {
      if (_pagemap_fd >= 0) {
         ::close(_pagemap_fd);
         _pagemap_fd = -1;
      }
      return true;
   }
   
   static inline size_t pagesz = sysconf(_SC_PAGE_SIZE);
   static inline bool _pagemap_supported = false;
   static inline bool _pagemap_support_checked = false; 
   mutable int _pagemap_fd = -1;
};

} // namespace chainbase