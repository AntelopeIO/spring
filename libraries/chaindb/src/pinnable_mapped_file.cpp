#include <chainbase/pinnable_mapped_file.hpp>
#include <chainbase/environment.hpp>
#include <chainbase/pagemap_accessor.hpp>
#include <chainbase/scope_exit.hpp>
#include <boost/asio/signal_set.hpp>
#include <fc/io/raw.hpp>
#include <fc/log/logger.hpp>
#include <fc/reflect/variant.hpp>
#include <iostream>
#include <fstream>
//#include <unistd.h>

#ifdef __linux__
#include <linux/mman.h>
#include <sys/vfs.h>
#include <linux/magic.h>
#include <sys/sysinfo.h>
#include <sys/resource.h>
#endif

// use mlock2() on Linux to avoid a noop intercept of mlock() when ASAN is enabled (still present in compiler-rt 18.1)
#ifdef __linux__
#define MLOCK(a, b) mlock2(a, b, 0)
#else
#define MLOCK(a, b) mlock(a, b)
#endif

namespace chainbase {

std::vector<pinnable_mapped_file*> pinnable_mapped_file::_instance_tracker;
pinnable_mapped_file::segment_manager_map_t  pinnable_mapped_file::_segment_manager_map;
   
const char* chainbase_error_category::name() const noexcept {
   return "chainbase";
}

std::string chainbase_error_category::message(int ev) const {
   switch(ev) {
      case db_error_code::ok:
         return "Ok";
      case db_error_code::dirty:
         return "Database dirty flag set";
      case db_error_code::incompatible:
         return "Database incompatible; All environment parameters must match";
      case db_error_code::incorrect_db_version:
         return "Database format not compatible with this version of chainbase";
      case db_error_code::not_found:
         return "Database file not found";
      case db_error_code::bad_size:
         return "Bad size";
      case db_error_code::unsupported_win32_mode:
         return "Heap and locked mode are not supported on win32";
      case db_error_code::bad_header:
         return "Failed to read DB header";
      case db_error_code::no_access:
         return "Could not gain write access to the shared memory file";
      case db_error_code::aborted:
         return "Database load aborted";
      case db_error_code::no_mlock:
         return "Failed to mlock database";
      case db_error_code::clear_refs_failed:
         return "Failed to clear Soft-Dirty bits";
      case tempfs_incompatible_mode:
         return "We recommend storing the state db file on tmpfs only when database-map-mode=mapped";
      default:
         return "Unrecognized error code";
   }
}

const std::error_category& chainbase_error_category() {
   static class chainbase_error_category the_category;
   return the_category;
}

static bool on_tempfs_filesystem(std::filesystem::path& path) {
#ifdef __linux__
   struct statfs info;
   statfs(path.generic_string().c_str(), &info);
   return info.f_type == TMPFS_MAGIC;
#endif
   return false;
}

pinnable_mapped_file::pinnable_mapped_file(const std::filesystem::path& dir, bool writable, uint64_t shared_file_size, bool allow_dirty, map_mode mode) :
   _data_file_path(std::filesystem::absolute(dir/"shared_memory.bin")),
   _database_name(dir.filename().string()),
   _database_size(shared_file_size),
   _writable(writable),
   _sharable(mode == mapped)
{
   if(shared_file_size % _db_size_multiple_requirement) {
      std::string what_str("Database must be mulitple of " + std::to_string(_db_size_multiple_requirement) + " bytes");
      BOOST_THROW_EXCEPTION(std::system_error(make_error_code(db_error_code::bad_size), what_str));
   }
#ifdef _WIN32
   if(mode != mapped && mode != mapped_private)
      BOOST_THROW_EXCEPTION(std::system_error(make_error_code(db_error_code::unsupported_win32_mode)));
#endif
   if(!_writable && !std::filesystem::exists(_data_file_path)){
      std::string what_str("database file not found at " + _data_file_path.string());
      BOOST_THROW_EXCEPTION(std::system_error(make_error_code(db_error_code::not_found), what_str));
   }

   std::filesystem::create_directories(dir);

   if(std::filesystem::exists(_data_file_path)) {
      char header[header_size];
      std::ifstream hs(_data_file_path.generic_string(), std::ifstream::binary);
      hs.read(header, header_size);
      if(hs.fail())
         BOOST_THROW_EXCEPTION(std::system_error(make_error_code(db_error_code::bad_header)));

      const db_header dbheader = fc::raw::unpack<db_header>(header, sizeof(header));
      if(dbheader.id != header_id) {
         std::string what_str("\"" + _database_name + "\" database format not compatible with this version of chainbase.");
         BOOST_THROW_EXCEPTION(std::system_error(make_error_code(db_error_code::incorrect_db_version), what_str));
      }
      if(!allow_dirty && dbheader.dirty) {
         std::string what_str("\"" + _database_name + "\" database dirty flag set");
         BOOST_THROW_EXCEPTION(std::system_error(make_error_code(db_error_code::dirty)));
      }
      if(dbheader.dbenviron != environment()) {
         elog("\"${dbname}\" database was created with a chainbase from a different environment\n"
              "Current compiler environment: ${current}\n"
              "DB created with compiler environment: ${createdby}",
              ("dbname", _database_name)("current", environment())("createdby", dbheader.dbenviron));
         BOOST_THROW_EXCEPTION(std::system_error(make_error_code(db_error_code::incompatible)));
      }
   }

   segment_manager* file_mapped_segment_manager = nullptr;
   if(!std::filesystem::exists(_data_file_path)) {
      std::ofstream ofs(_data_file_path.generic_string(), std::ofstream::trunc);
      ofs.close();
      std::filesystem::resize_file(_data_file_path, shared_file_size);
      _file_mapping = bip::file_mapping(_data_file_path.generic_string().c_str(), bip::read_write);
      _file_mapped_region = bip::mapped_region(_file_mapping, bip::read_write);
      file_mapped_segment_manager = new ((char*)_file_mapped_region.get_address()+header_size) segment_manager(shared_file_size-header_size);
      fc::raw::pack((char*)_file_mapped_region.get_address(), (uint32_t)header_size, db_header());
   }
   else if(_writable) {
         auto existing_file_size = std::filesystem::file_size(_data_file_path);
         size_t grow = 0;
         if(shared_file_size > existing_file_size) {
            grow = shared_file_size - existing_file_size;
            std::filesystem::resize_file(_data_file_path, shared_file_size);
         }
         else if(shared_file_size < existing_file_size) {
            _database_size = existing_file_size;
            wlog("\"${dbname}\" requested size of ${reqsz} is less than existing size of ${existingsz}. This database will not be shrunk and will remain at ${existingsz}",
               ("dbname", _database_name)("reqsz", shared_file_size)("existingsz", existing_file_size));
         }
         _file_mapping = bip::file_mapping(_data_file_path.generic_string().c_str(), bip::read_write);
         _file_mapped_region = bip::mapped_region(_file_mapping, bip::read_write);
         file_mapped_segment_manager = reinterpret_cast<segment_manager*>((char*)_file_mapped_region.get_address()+header_size);
         if(grow)
            file_mapped_segment_manager->grow(grow);
   }
   else {
         _file_mapping = bip::file_mapping(_data_file_path.generic_string().c_str(), bip::read_only);
         _file_mapped_region = bip::mapped_region(_file_mapping, bip::read_only);
         file_mapped_segment_manager = reinterpret_cast<segment_manager*>((char*)_file_mapped_region.get_address()+header_size);
   }

   if(_writable) {
      //remove meta file created in earlier versions
      std::error_code ec;
      std::filesystem::remove(std::filesystem::absolute(dir/"shared_memory.meta"), ec);

      _mapped_file_lock = bip::file_lock(_data_file_path.generic_string().c_str());
      if(!_mapped_file_lock.try_lock())
         BOOST_THROW_EXCEPTION(std::system_error(make_error_code(db_error_code::no_access)));

      set_mapped_file_db_dirty(true);
   }

   auto reset_on_ctor_fail = scope_fail([&]() {
      _file_mapped_region = bip::mapped_region();
      if(_non_file_mapped_mapping && _non_file_mapped_mapping != MAP_FAILED)
         munmap(_non_file_mapped_mapping, _non_file_mapped_mapping_size);

      if(_writable)
         set_mapped_file_db_dirty(false);
      std::erase(_instance_tracker, this);
   });

   if(mode == mapped || mode == mapped_private) {
      if (_writable && !_sharable) {
         // First make sure the db file is not on a ram-based tempfs, as it would be an
         // unnecessary waste of RAM to have both the db file *and* the modified pages in RAM.
         // ----------------------------------------------------------------------------------
         if (on_tempfs_filesystem(_data_file_path))
            BOOST_THROW_EXCEPTION(std::system_error(make_error_code(db_error_code::tempfs_incompatible_mode)));

         // previous mapped region was RW so we could set the dirty flag in it... recreate it
         // with an `copy_on_write` mapping, so the disk file will not be updated (until we do
         // it manually when `this` is destroyed).
         // ----------------------------------------------------------------------------------
         _file_mapped_region = bip::mapped_region(); // delete old r/w mapping before creating new one

         setup_copy_on_write_mapping();
      } else {
         _segment_manager = file_mapped_segment_manager;
      }
   }
   else {
      // First make sure the db file is not on a ram-based tempfs, as it would be an unnecessary
      // waste of RAM to have both the db file *and* a separate private mapping in RAM.
      // ---------------------------------------------------------------------------------------
      if (on_tempfs_filesystem(_data_file_path))
         BOOST_THROW_EXCEPTION(std::system_error(make_error_code(db_error_code::tempfs_incompatible_mode)));

      boost::asio::io_context sig_ios;
      boost::asio::signal_set sig_set(sig_ios, SIGINT, SIGTERM);
#ifdef SIGPIPE
      sig_set.add(SIGPIPE);
#endif
      sig_set.async_wait([](const boost::system::error_code&, int) {
         BOOST_THROW_EXCEPTION(std::system_error(make_error_code(db_error_code::aborted)));
      });

      setup_non_file_mapping();
      _file_mapped_region = bip::mapped_region();
      load_database_file(sig_ios);

#ifndef _WIN32
      if(mode == locked) {
         if(MLOCK(_non_file_mapped_mapping, _non_file_mapped_mapping_size)) {
            struct rlimit resouce_limit;
            std::string details;

            // Query the current locked memory limit and build detailed error message
            if (getrlimit(RLIMIT_MEMLOCK, &resouce_limit) == 0) {
               details = "Current locked memory ";
               details += "soft limit: " + std::to_string(resouce_limit.rlim_cur) + " bytes, ";
               details += "hard limit: " + std::to_string(resouce_limit.rlim_max) + " bytes. ";
            } else {
               details = "getrlimit for RLIMIT_MEMLOCK failed: ";
               details += std::strerror(errno);
               details += ".";
            }
            details += "Run \"ulimit -l\" to increase locked memory limit.";

            std::string what_str("Failed to mlock database \"" + _database_name + "\". " + details);
            BOOST_THROW_EXCEPTION(std::system_error(make_error_code(db_error_code::no_mlock), what_str));
         }
         ilog("Database \"${dbname}\" has been successfully locked in memory", ("dbname", _database_name));
      }
#endif

      _segment_manager = reinterpret_cast<segment_manager*>((char*)_non_file_mapped_mapping+header_size);
   }
   std::byte* start = (std::byte*)_segment_manager;
   assert(_segment_manager_map.find(start) == _segment_manager_map.end());
   _segment_manager_map[start] = start + _segment_manager->get_size();
}

void pinnable_mapped_file::setup_copy_on_write_mapping() {
   // before we clear the Soft-Dirty bits for the whole process, make sure all writable,
   // non-sharable chainbase dbs using mapped mode are flushed to disk
   // ----------------------------------------------------------------------------------
   for (auto pmm : _instance_tracker) {
      // we only populate _instance_tracker if pagemap *is* supported
      assert(pagemap_accessor::pagemap_supported());
      pmm->save_database_file(true); 
   }

   _file_mapped_region = bip::mapped_region(_file_mapping, bip::copy_on_write);
   *((char*)_file_mapped_region.get_address()+header_dirty_bit_offset) = dirty; // set dirty bit in our memory mapping

   _segment_manager = reinterpret_cast<segment_manager*>((char*)_file_mapped_region.get_address()+header_size);

   // then clear the Soft-Dirty bits
   // ------------------------------
   pagemap_accessor pagemap;
   if (pagemap.check_pagemap_support_and_clear_refs()) {
      _instance_tracker.push_back(this); // so we can save dirty pages before another instance calls `clear_refs()`
   }
}

// returns the number of pages flushed to disk
size_t pinnable_mapped_file::check_memory_and_flush_if_needed() {
   size_t written_pages {0};
#if 0
   if (_non_file_mapped_mapping || _sharable || !_writable)
      return written_pages;

   // we are in `copy_on_write` mode.
   static time_t check_time = 0;
   constexpr int check_interval = 60; // seconds
   constexpr size_t one_gb = 1ull << 30;

   const time_t current_time = time(NULL);
   if(current_time >= check_time) {
      check_time = current_time + check_interval;

      size_t avail_ram_gb = (get_avphys_pages() * sysconf(_SC_PAGESIZE)) / one_gb;
      if (avail_ram_gb <= 2) {
         auto [src, sz] = get_region_to_save();
         pagemap_accessor pagemap;
         size_t offset = 0;
         while(offset != sz && written_pages < (one_gb / sysconf(_SC_PAGESIZE))) {
            size_t copy_size = std::min(_db_size_copy_increment,  sz - offset);
            if (!pagemap.update_file_from_region({ src + offset, copy_size }, _file_mapping, offset, false, written_pages))
               break;
            offset += copy_size;
         }
      }
   }
#endif
   return written_pages;
}

void pinnable_mapped_file::setup_non_file_mapping() {
   int common_map_opts = MAP_PRIVATE|MAP_ANONYMOUS;

   _non_file_mapped_mapping_size = _file_mapped_region.get_size();
   auto round_up_mmaped_size = [this](unsigned r) {
      _non_file_mapped_mapping_size = (_non_file_mapped_mapping_size + (r-1u))/r*r;
   };

   const unsigned _1gb = 1u<<30u;
   const unsigned _2mb = 1u<<21u;

#if defined(MAP_HUGETLB) && defined(MAP_HUGE_1GB)
   _non_file_mapped_mapping = mmap(NULL, _non_file_mapped_mapping_size, PROT_READ|PROT_WRITE, common_map_opts|MAP_HUGETLB|MAP_HUGE_1GB, -1, 0);
   if(_non_file_mapped_mapping != MAP_FAILED) {
      round_up_mmaped_size(_1gb);
      ilog("Database \"${dbname}\" using 1GB pages", ("dbname", _database_name));
      return;
   }
#endif

#if defined(MAP_HUGETLB) && defined(MAP_HUGE_2MB)
   //in the future as we expand to support other platforms, consider not specifying any size here so we get the default size. However
   // when mapping the default hugepage size, we'll need to go figure out that size so that the munmap() can be specified correctly
   _non_file_mapped_mapping = mmap(NULL, _non_file_mapped_mapping_size, PROT_READ|PROT_WRITE, common_map_opts|MAP_HUGETLB|MAP_HUGE_2MB, -1, 0);
   if(_non_file_mapped_mapping != MAP_FAILED) {
      round_up_mmaped_size(_2mb);
      ilog("Database \"${dbname}\" using 2MB pages", ("dbname", _database_name));
      return;
   }
#endif

#if defined(VM_FLAGS_SUPERPAGE_SIZE_2MB)
   round_up_mmaped_size(_2mb);
   _non_file_mapped_mapping = mmap(NULL, _non_file_mapped_mapping_size, PROT_READ|PROT_WRITE, common_map_opts, VM_FLAGS_SUPERPAGE_SIZE_2MB, 0);
   if(_non_file_mapped_mapping != MAP_FAILED) {
      ilog("Database \"${dbname}\" using 2MB pages", ("dbname", _database_name));
      return;
   }
   _non_file_mapped_mapping_size = _file_mapped_region.get_size();  //restore to non 2MB rounded size
#endif

#ifndef _WIN32
   _non_file_mapped_mapping = mmap(NULL, _non_file_mapped_mapping_size, PROT_READ|PROT_WRITE, common_map_opts, -1, 0);
   if(_non_file_mapped_mapping == MAP_FAILED)
      BOOST_THROW_EXCEPTION(std::runtime_error(std::string("Failed to map database ") + _database_name + ": " + strerror(errno)));
#endif
}

void pinnable_mapped_file::load_database_file(boost::asio::io_context& sig_ios) {
   ilog("Preloading \"${dbname}\" database file, this could take a moment...", ("dbname", _database_name));
   char* const dst = (char*)_non_file_mapped_mapping;
   size_t offset = 0;
   time_t t = time(nullptr);
   while(offset != _database_size) {
      size_t copy_size = std::min(_db_size_copy_increment, _database_size - offset);
      bip::mapped_region src_rgn(_file_mapping, bip::read_only, offset, copy_size);
      memcpy(dst+offset, src_rgn.get_address(), copy_size);
      offset += copy_size;

      if(time(nullptr) != t) {
         t = time(nullptr);
         ilog("Preloading \"${dbname}\" database file, ${pct}% complete...", ("dbname", _database_name)("pct", offset/(_database_size/100)));
      }
      sig_ios.poll();
   }
   ilog("Preloading \"${dbname}\" database file, complete.", ("dbname", _database_name));
}

bool pinnable_mapped_file::all_zeros(const std::byte* data, size_t sz) {
   uint64_t* p = (uint64_t*)data;
   uint64_t* end = p+sz/sizeof(uint64_t);
   while(p != end) {
      if(*p++ != 0)
         return false;
   }
   return true;
}

std::pair<std::byte*, size_t> pinnable_mapped_file::get_region_to_save() const {
   if (_non_file_mapped_mapping)
      return { (std::byte*)_non_file_mapped_mapping, _database_size };
   return { (std::byte*)_file_mapped_region.get_address(), _database_size };
}

void pinnable_mapped_file::save_database_file(bool flush /* = true */) {
   assert(_writable);
   ilog("Writing \"${dbname}\" database file, this could take a moment...", ("dbname", _database_name));
   size_t offset = 0;
   time_t t = time(nullptr);
   pagemap_accessor pagemap;
   size_t written_pages {0};
   auto [src, sz] = get_region_to_save();
   
   while(offset != sz) {
      size_t copy_size = std::min(_db_size_copy_increment,  sz - offset);
      bool mapped_writable_instance = std::find(_instance_tracker.begin(), _instance_tracker.end(), this) != _instance_tracker.end();
      if (!mapped_writable_instance ||
          !pagemap.update_file_from_region({ src + offset, copy_size }, _file_mapping, offset, flush, written_pages)) {
         if (mapped_writable_instance)
            wlog("pagemap update of db file failed... using non-pagemap version");
         if(!all_zeros(src+offset, copy_size)) {
            bip::mapped_region dst_rgn(_file_mapping, bip::read_write, offset, copy_size);
            char *dst = (char *)dst_rgn.get_address();
            memcpy(dst, src+offset, copy_size);

            if (flush) {
               if(dst_rgn.flush(0, 0, false) == false)
                  wlog("flushing buffers failed");
            }
         }
      }
      offset += copy_size;

      if(time(nullptr) != t) {
         t = time(nullptr);
         ilog("Writing \"${dbname}\" database file, ${pct}% complete...", ("dbname", _database_name)("pct", offset/(sz/100)));
      }
   }
   ilog("Writing \"${dbname}\" database file, complete.", ("dbname", _database_name));
}

pinnable_mapped_file::pinnable_mapped_file(pinnable_mapped_file&& o) noexcept
{
   // all members are correctly default-initialized, so we can just move into *this
   *this = std::move(o);
}

pinnable_mapped_file& pinnable_mapped_file::operator=(pinnable_mapped_file&& o) noexcept {
   std::swap(_mapped_file_lock, o._mapped_file_lock);
   std::swap(_data_file_path, o._data_file_path);
   std::swap(_database_name, o._database_name);
   std::swap(_database_size, o._database_size);
   std::swap(_writable, o._writable);
   std::swap(_sharable, o._sharable);
   std::swap(_file_mapping, o._file_mapping);
   std::swap(_file_mapped_region, o._file_mapped_region);
   std::swap(_non_file_mapped_mapping, o._non_file_mapped_mapping);
   std::swap(_non_file_mapped_mapping_size, o._non_file_mapped_mapping_size);
   std::swap(_db_permissions, o._db_permissions);
   std::swap(_segment_manager, o._segment_manager);
   return *this;
}

pinnable_mapped_file::~pinnable_mapped_file() {
   if(_writable) {
      if(_non_file_mapped_mapping) { //in heap or locked mode
         save_database_file();
#ifndef _WIN32
         if(munmap(_non_file_mapped_mapping, _non_file_mapped_mapping_size))
            wlog("Database unmapping failed: ${err}", ("err",strerror(errno)));
#endif
      } else {
         if (_sharable) {
            if(_file_mapped_region.flush(0, 0, false) == false)
               wlog("syncing buffers failed");
         } else {
            save_database_file(); // must be before `this` is removed from _instance_tracker
            if (auto it = std::find(_instance_tracker.begin(), _instance_tracker.end(), this); it != _instance_tracker.end())
               _instance_tracker.erase(it);
            _file_mapped_region = bip::mapped_region();
            set_mapped_file_db_dirty(false);
         }
      }
      set_mapped_file_db_dirty(false);
   }
   if (_segment_manager)
      _segment_manager_map.erase(_segment_manager);
}

void pinnable_mapped_file::set_mapped_file_db_dirty(bool dirty) {
   assert(_writable);
   if (_file_mapped_region.get_address() == nullptr)
      _file_mapped_region = bip::mapped_region(_file_mapping, bip::read_write, 0, _db_size_multiple_requirement);
   *((char*)_file_mapped_region.get_address()+header_dirty_bit_offset) = dirty;
   if (_file_mapped_region.flush(0, 0, false) == false)
      wlog("syncing buffers failed");
}

std::istream& operator>>(std::istream& in, pinnable_mapped_file::map_mode& runtime) {
   std::string s;
   in >> s;
   if (s == "mapped")
      runtime = pinnable_mapped_file::map_mode::mapped;
   else if (s == "mapped_private")
      runtime = pinnable_mapped_file::map_mode::mapped_private;
   else if (s == "heap")
      runtime = pinnable_mapped_file::map_mode::heap;
   else if (s == "locked")
      runtime = pinnable_mapped_file::map_mode::locked;
   else
      in.setstate(std::ios_base::failbit);
   return in;
}

std::ostream& operator<<(std::ostream& osm, pinnable_mapped_file::map_mode m) {
   if(m == pinnable_mapped_file::map_mode::mapped)
      osm << "mapped";
   else if(m == pinnable_mapped_file::map_mode::mapped_private)
      osm << "mapped_private";
   else if (m == pinnable_mapped_file::map_mode::heap)
      osm << "heap";
   else if (m == pinnable_mapped_file::map_mode::locked)
      osm << "locked";

   return osm;
}

}

namespace fc {
//Reconsider post-CHAINB01, when compiler can be stored as a proper string
void to_variant(const chainbase::environment& bi, variant& v) {
   v = fc::mutable_variant_object()("debug", bi.debug)
                                   ("os", bi.os)
                                   ("arch", bi.arch)
                                   ("boost_version", bi.boost_version)
                                   ("compiler", bi.compiler.data());
}
}
