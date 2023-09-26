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
   template<typename F>
   class scoped_exit {
   public:
      template<typename C>
      [[nodiscard]] scoped_exit(C&& c): callback(std::forward<C>(c)){}

      scoped_exit(scoped_exit&& mv) = default;
      scoped_exit(const scoped_exit&) = delete;
      scoped_exit& operator=(const scoped_exit&) = delete;

      ~scoped_exit() { try { callback(); } catch(...) {} }
   private:
      F callback;
   };

   template<typename F>
   static scoped_exit<F> make_scoped_exit(F&& c) { return scoped_exit<F>(std::forward<F>(c)); }
   
   struct pagemap_entry {
      uint64_t pfn        : 54;
      uint64_t soft_dirty : 1;
      uint64_t exclusive  : 1;
      uint64_t file_page  : 1;
      uint64_t swapped    : 1;
      uint64_t present    : 1;

      void print(uintptr_t addr, const char *name) {
         // printf("%jx %jx %u %u %u %u %s\n", (uintmax_t)addr, (uintmax_t)pfn, soft_dirty,
         //        file_page, swapped, present, name);
      }
   };

   ~pagemap_accessor() {
      _close();
   }

   void clear_refs() const {
      int fd = ::open("/proc/self/clear_refs", O_WRONLY);
      if (fd < 0) {
         perror("open clear_refs file failed");
         exit(1);
      }
      auto cleanup = make_scoped_exit([fd] { ::close(fd); });
      
      const char *v = "4";
      if (write(fd, v, 1) < 1) {
         perror("Can't clear soft-dirty bit");
         exit(1);
      }
   }
   
   std::optional<pagemap_entry> get_entry(uintptr_t vaddr) const {
      uint64_t data;
      read(vaddr, { &data, 1 });
      return pagemap_entry {
         .pfn        = data & (((uint64_t)1 << 54) - 1),
         .soft_dirty = (data >> 55) & 1,
         .exclusive  = (data >> 56) & 1,
         .file_page  = (data >> 61) & 1,
         .swapped    = (data >> 62) & 1,
         .present    = (data >> 63) & 1
      };
   }

   static bool is_marked_dirty(uint64_t entry) {
      return !!(entry & (1Ull << 55));
   }

   static size_t page_size() {
      return pagesz;
   }

   bool page_dirty(uintptr_t vaddr) const {
      uint64_t data;
      read(vaddr, { &data, 1 });
      return this->is_marked_dirty(data);
   }

   bool read(uintptr_t vaddr, std::span<uint64_t> dest_uint64) const {
      _open(); // make sure file is open
      assert(_pagemap_fd >= 0);
      auto dest = std::as_writable_bytes(dest_uint64);
      std::byte* cur = dest.data();
      size_t bytes_remaining = dest.size();
      uintptr_t offset = (vaddr / pagesz) * sizeof(uint64_t);
      while (bytes_remaining != 0) {
         ssize_t ret = pread(_pagemap_fd, cur, bytes_remaining, offset + (cur - dest.data()));
         if (ret < 0) {
            perror("Can't read pagemap");
            exit(1);
            return false;
         }
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
      assert(rgn.size() % pagesz == 0);
      size_t num_pages = rgn.size() / pagesz;
      std::vector<uint64_t> pm(num_pages);
      
      // get modified pages
      read((uintptr_t)rgn.data(), pm);
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
         if (flush)
            map_rgn.flush(0, rgn.size(), /* async = */ false);
         return true;
      }
      return false;
   }
   
private:
   void _open() const {
      if (_pagemap_fd < 0) {
         _pagemap_fd = ::open("/proc/self/pagemap", O_RDONLY);
         if (_pagemap_fd < 0) {
            perror("open pagemap failed");
            exit(1);
         }
      }
   }

   void _close() const {
      if (_pagemap_fd >= 0) {
         ::close(_pagemap_fd);
         _pagemap_fd = -1;
      }
   }
   
   static inline size_t pagesz = sysconf(_SC_PAGE_SIZE);
   
   mutable int _pagemap_fd = -1;
};

} // namespace chainbase