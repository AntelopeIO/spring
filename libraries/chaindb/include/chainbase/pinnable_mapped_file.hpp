#pragma once

#include <system_error>
#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/container/flat_map.hpp>
#include <filesystem>
#include <vector>
#include <optional>
#include <memory>

namespace chainbase {

namespace bip = boost::interprocess;

enum db_error_code {
   ok = 0,
   dirty,
   incompatible,
   incorrect_db_version,
   not_found,
   bad_size,
   unsupported_win32_mode,
   bad_header,
   no_access,
   aborted,
   no_mlock,
   clear_refs_failed,
   tempfs_incompatible_mode
};

const std::error_category& chainbase_error_category();

inline std::error_code make_error_code(db_error_code e) noexcept {
   return std::error_code(static_cast<int>(e), chainbase_error_category());
}

class chainbase_error_category : public std::error_category {
public:
   const char* name() const noexcept override;
   std::string message(int ev) const override;
};

using segment_manager = bip::managed_mapped_file::segment_manager;

template<typename T>
using allocator = bip::allocator<T, segment_manager>;

class pinnable_mapped_file {
   public:
      enum map_mode {
         mapped,        // file is mmaped in MAP_SHARED mode. Only mode where changes can be seen by another chainbase instance
         mapped_private,// file is mmaped in MAP_PRIVATE mode, and only updated at exit
         heap,          // file is copied at startup to an anonymous mapping using huge pages (if available)
         locked         // file is copied at startup to an anonymous mapping using huge pages (if available) and locked in memory
      };

      pinnable_mapped_file(const std::filesystem::path& dir, bool writable, uint64_t shared_file_size, bool allow_dirty, map_mode mode);
      pinnable_mapped_file(pinnable_mapped_file&& o) noexcept ;
      pinnable_mapped_file& operator=(pinnable_mapped_file&&) noexcept ;
      pinnable_mapped_file(const pinnable_mapped_file&) = delete;
      pinnable_mapped_file& operator=(const pinnable_mapped_file&) = delete;
      ~pinnable_mapped_file();

      segment_manager* get_segment_manager() const { return _segment_manager;}
      size_t           check_memory_and_flush_if_needed();

      template<typename T>
      static std::optional<allocator<T>> get_allocator(void *object) {
         if (!_segment_manager_map.empty()) {
            auto it = _segment_manager_map.upper_bound(object);
            if(it == _segment_manager_map.begin())
               return {};
            auto [seg_start, seg_end] = *(--it);
            // important: we need to check whether the pointer is really within the segment, as shared objects'
            // can also be created on the stack (in which case the data is actually allocated on the heap using
            // std::allocator). This happens for example when `shared_cow_string`s are inserted into a bip::multimap,
            // and temporary pairs are created on the stack by the bip::multimap code.
            if (object < seg_end)
               return allocator<T>(reinterpret_cast<segment_manager *>(seg_start));
         }
         return {};
      }

   private:
      void                                          set_mapped_file_db_dirty(bool);
      void                                          load_database_file(boost::asio::io_context& sig_ios);
      void                                          save_database_file(bool flush = true);
      static bool                                   all_zeros(const std::byte* data, size_t sz);
      void                                          setup_non_file_mapping();
      void                                          setup_copy_on_write_mapping();
      std::pair<std::byte*, size_t>                 get_region_to_save() const;

      bip::file_lock                                _mapped_file_lock;
      std::filesystem::path                         _data_file_path;
      std::string                                   _database_name;
      size_t                                        _database_size = 0;
      bool                                          _writable = false;
      bool                                          _sharable = false;

      bip::file_mapping                             _file_mapping;
      bip::mapped_region                            _file_mapped_region;
      void*                                         _non_file_mapped_mapping = nullptr;
      size_t                                        _non_file_mapped_mapping_size = 0;

#ifdef _WIN32
      bip::permissions                              _db_permissions;
#else
      bip::permissions                              _db_permissions{S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH};
#endif

      segment_manager*                              _segment_manager = nullptr;

      static std::vector<pinnable_mapped_file*>     _instance_tracker;

      using segment_manager_map_t = boost::container::flat_map<void*, void *>;
      static segment_manager_map_t                  _segment_manager_map;

      constexpr static unsigned                     _db_size_multiple_requirement = 1024*1024; //1MB
      constexpr static size_t                       _db_size_copy_increment       = 1024*1024*1024; //1GB
};

std::istream& operator>>(std::istream& in, pinnable_mapped_file::map_mode& runtime);
std::ostream& operator<<(std::ostream& osm, pinnable_mapped_file::map_mode m);

}
