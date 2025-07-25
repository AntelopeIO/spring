#include <eosio/trace_api/store_provider.hpp>

#include <fc/variant_object.hpp>
#include <fc/log/logger_config.hpp>

namespace {
      static constexpr uint32_t _current_version = 1;
      static constexpr const char* _trace_prefix = "trace_";
      static constexpr const char* _trace_index_prefix = "trace_index_";
      static constexpr const char* _trace_trx_id_prefix = "trace_trx_id_";
      static constexpr const char* _trace_ext = ".log";
      static constexpr const char* _compressed_trace_ext = ".clog";
      static constexpr int _max_filename_size = std::char_traits<char>::length(_trace_index_prefix) + 10 + 1 + 10 + std::char_traits<char>::length(_compressed_trace_ext) + 1; // "trace_index_" + 10-digits + '-' + 10-digits + ".clog" + null-char

      std::string make_filename(const char* slice_prefix, const char* slice_ext, uint32_t slice_number, uint32_t slice_width) {
         char filename[_max_filename_size] = {};
         const uint32_t slice_start = slice_number * slice_width;
         const int size_written = snprintf(filename, _max_filename_size, "%s%010d-%010d%s", slice_prefix, slice_start, (slice_start + slice_width), slice_ext);
         // assert that _max_filename_size is correct
         if ( size_written >= _max_filename_size ) {
            const std::string max_size_str = std::to_string(_max_filename_size - 1); // dropping null character from size
            const std::string size_written_str = std::to_string(size_written);
            throw std::runtime_error("Could not write the complete filename.  Anticipated the max filename characters to be: " +
               max_size_str + " or less, but wrote: " + size_written_str + " characters.  This is likely because the file "
               "format was changed and the code was not updated accordingly. Filename created: " + filename);
         }

         return std::string(filename);
      }
}

namespace eosio::trace_api {
      store_provider::store_provider(const std::filesystem::path& slice_dir, uint32_t stride_width, std::optional<uint32_t> minimum_irreversible_history_blocks,
                                  std::optional<uint32_t> minimum_uncompressed_irreversible_history_blocks, size_t compression_seek_point_stride)
   : _slice_directory(slice_dir, stride_width, minimum_irreversible_history_blocks, minimum_uncompressed_irreversible_history_blocks, compression_seek_point_stride) {
   }

   template<typename BlockTrace>
   void store_provider::append(const BlockTrace& bt) {
      fc::cfile trace;
      fc::cfile index;
      const uint32_t slice_number = _slice_directory.slice_number(bt.number);
      _slice_directory.find_or_create_slice_pair(slice_number, open_state::write, trace, index);
      // storing as static_variant to allow adding other data types to the trace file in the future
      const uint64_t offset = append_store(data_log_entry { bt }, trace);

      auto be = metadata_log_entry { block_entry_v0 { .id = bt.id, .number = bt.number, .offset = offset }};
      append_store(be, index);
   }

   template void store_provider::append<block_trace_v1>(const block_trace_v1& bt);
   template void store_provider::append<block_trace_v2>(const block_trace_v2& bt);

   void store_provider::append_lib(uint32_t lib) {
      fc::cfile index, trx_id;
      const uint32_t slice_number = _slice_directory.slice_number(lib);
      _slice_directory.find_or_create_index_slice(slice_number, open_state::write, index);
      auto le = metadata_log_entry { lib_entry_v0 { .lib = lib }};
      append_store(le, index);
      _slice_directory.find_or_create_trx_id_slice(slice_number, open_state::write, trx_id);
      append_store(le, trx_id);
      _slice_directory.set_lib(lib);
   }

   void store_provider::append_trx_ids(block_trxs_entry tt){
      fc::cfile trx_id_file;
      const uint32_t slice_number = _slice_directory.slice_number(tt.block_num);
      _slice_directory.find_or_create_trx_id_slice(slice_number, open_state::write, trx_id_file);
      auto entry = metadata_log_entry { std::move(tt) };
      append_store(entry, trx_id_file);
   }

   get_block_t store_provider::get_block(uint32_t block_height, const yield_function& yield) {
      std::optional<uint64_t> trace_offset;
      bool irreversible = false;
      scan_metadata_log_from(block_height, 0, [&block_height, &trace_offset, &irreversible](const metadata_log_entry& e) -> bool {
         if (std::holds_alternative<block_entry_v0>(e)) {
            const auto& block = std::get<block_entry_v0>(e);
            if (block.number == block_height) {
               trace_offset = block.offset;
            }
         } else if (std::holds_alternative<lib_entry_v0>(e)) {
            auto lib = std::get<lib_entry_v0>(e).lib;
            if (lib >= block_height) {
               irreversible = true;
               return false;
            }
         }
         return true;
      }, yield);
      if (!trace_offset) {
         return get_block_t{};
      }
      std::optional<data_log_entry> entry = read_data_log(block_height, *trace_offset);
      if (!entry) {
         return get_block_t{};
      }
      return std::make_tuple( entry.value(), irreversible );
   }

   get_block_n store_provider::get_trx_block_number(const chain::transaction_id_type& trx_id, const yield_function& yield) {
      // traversing from last stride to first
      // if we find a trx it is either LIB or it is the latest fork, either way we are done
      std::set<uint32_t> trx_block_nums;

      _slice_directory.for_each_trx_id_slice([&](fc::cfile& trx_id_file) -> bool {
         metadata_log_entry entry;
         auto ds = trx_id_file.create_datastream();
         const uint64_t end = file_size(trx_id_file.get_file_path());
         uint64_t offset = trx_id_file.tellp();
         while (offset < end) {
            yield();
            fc::raw::unpack(ds, entry);
            if (std::holds_alternative<block_trxs_entry>(entry)) {
               const auto& trxs_entry = std::get<block_trxs_entry>(entry);
               bool found_in_block = false;
               for (auto i = 0U; i < trxs_entry.ids.size(); ++i) {
                  if (trxs_entry.ids[i] == trx_id) {
                     trx_block_nums.insert(trxs_entry.block_num);
                     found_in_block = true;
                     break;
                  }
               }
               // block can be seen again when a fork happens, if not in the new block remove it from blocks that have the trx
               if (!found_in_block)
                  trx_block_nums.erase(trxs_entry.block_num);
            } else if (std::holds_alternative<lib_entry_v0>(entry)) {
               auto lib = std::get<lib_entry_v0>(entry).lib;
               if (!trx_block_nums.empty() && lib >= *(--trx_block_nums.end())) {
                  return false; // *(--trx_block_nums.end()) is the block with highest block number which is final
               }
            } else {
               FC_ASSERT( false, "unpacked data should be a block_trxs_entry or a lib_entry_v0" );;
            }
            offset = trx_id_file.tellp();
         }
         // if empty() keep searching
         // if not empty() then we have found the trx and since traversing in reverse order this should be the latest
         return trx_block_nums.empty();
      });

      if (!trx_block_nums.empty())
         return *(--trx_block_nums.end());

      return {};
   }

   slice_directory::slice_directory(const std::filesystem::path& slice_dir, uint32_t width, std::optional<uint32_t> minimum_irreversible_history_blocks, std::optional<uint32_t> minimum_uncompressed_irreversible_history_blocks, size_t compression_seek_point_stride)
   : _slice_dir(slice_dir)
   , _width(width)
   , _minimum_irreversible_history_blocks(minimum_irreversible_history_blocks)
   , _minimum_uncompressed_irreversible_history_blocks(minimum_uncompressed_irreversible_history_blocks)
   , _compression_seek_point_stride(compression_seek_point_stride)
   , _best_known_lib(0) {
      if (!exists(_slice_dir)) {
         std::filesystem::create_directories(slice_dir);
      }
   }

   bool slice_directory::find_or_create_index_slice(uint32_t slice_number, open_state state, fc::cfile& index_file) const {
      const bool found = find_index_slice(slice_number, state, index_file);
      if( !found ) {
         create_new_index_slice_file(index_file);
      }
      return found;
   }

   bool slice_directory::find_index_slice(uint32_t slice_number, open_state state, fc::cfile& index_file, bool open_file) const {
      const bool found = find_slice(_trace_index_prefix, slice_number, index_file, open_file);
      if( !found || !open_file ) {
         return found;
      }

      validate_existing_index_slice_file(index_file, state);
      return true;
   }

   void slice_directory::create_new_index_slice_file(fc::cfile& index_file) const {
      index_file.open(fc::cfile::create_or_update_rw_mode);
      index_header h { .version = _current_version };
      append_store(h, index_file);
   }

   void slice_directory::validate_existing_index_slice_file(fc::cfile& index_file, open_state state) const {
      const auto header = extract_store<index_header>(index_file);
      if (header.version != _current_version) {
         throw old_slice_version("Old slice file with version: " + std::to_string(header.version) +
                                 " is in directory, only supporting version: " + std::to_string(_current_version));
      }

      if( state == open_state::write ) {
         index_file.seek_end(0);
      }
   }

   bool slice_directory::find_or_create_trace_slice(uint32_t slice_number, open_state state, fc::cfile& trace_file) const {
      const bool found = find_trace_slice(slice_number, state, trace_file);

      if( !found ) {
         trace_file.open(fc::cfile::create_or_update_rw_mode);
      }

      return found;
   }

   bool slice_directory::find_trace_slice(uint32_t slice_number, open_state state, fc::cfile& trace_file, bool open_file) const {
      const bool found = find_slice(_trace_prefix, slice_number, trace_file, open_file);

      if( !found || !open_file ) {
         return found;
      }

      if( state == open_state::write ) {
         trace_file.seek_end(0);
      }
      else {
         trace_file.seek(0); // ensure we are at the start of the file
      }
      return true;
   }

   std::optional<compressed_file> slice_directory::find_compressed_trace_slice(uint32_t slice_number, bool open_file ) const {
      auto filename = make_filename(_trace_prefix, _compressed_trace_ext, slice_number, _width);
      const auto slice_path = _slice_dir / filename;
      const bool file_exists = exists(slice_path);

      if (file_exists) {
         auto result = compressed_file(slice_path);
         if (open_file) {
            result.open();
         }

         return std::move(result);
      } else {
         return {};
      }
   }

   bool slice_directory::find_slice(const char* slice_prefix, uint32_t slice_number, fc::cfile& slice_file, bool open_file) const {
      auto filename = make_filename(slice_prefix, _trace_ext, slice_number, _width);
      const auto slice_path = _slice_dir / filename;
      slice_file.set_file_path(slice_path);

      const bool file_exists = exists(slice_path);
      if( !file_exists || !open_file ) {
         return file_exists;
      }

      slice_file.open(fc::cfile::create_or_update_rw_mode);
      // TODO: this is a temporary fix until fc::cfile handles it internally.  OSX and Linux differ on the read offset
      // when opening in "ab+" mode
      slice_file.seek(0);
      return true;
   }


   void slice_directory::find_or_create_slice_pair(uint32_t slice_number, open_state state, fc::cfile& trace, fc::cfile& index) {
      const bool trace_found = find_or_create_trace_slice(slice_number, state, trace);
      const bool index_found = find_or_create_index_slice(slice_number, state, index);
      if (trace_found != index_found) {
         const std::string trace_status = trace_found ? "existing" : "new";
         const std::string index_status = index_found ? "existing" : "new";
         elog("Trace file is ${ts}, but it's metadata file is ${is}. This means the files are not consistent.", ("ts", trace_status)("is", index_status));
      }
   }

   bool slice_directory::find_or_create_trx_id_slice(uint32_t slice_number, open_state state, fc::cfile& trx_id_file) const {
       const bool found = find_trx_id_slice(slice_number, state, trx_id_file);
       if( !found ) {
           trx_id_file.open(fc::cfile::create_or_update_rw_mode);
       }
       return found;
   }

   bool slice_directory::find_trx_id_slice(uint32_t slice_number, open_state state, fc::cfile& trx_id_file, bool open_file) const {
      const bool found = find_slice(_trace_trx_id_prefix, slice_number, trx_id_file, open_file);
      if( !found || !open_file ) {
         return found;
      }
      if( state == open_state::write ) {
          trx_id_file.seek_end(0);
      }
      return true;
   }

   void slice_directory::for_each_trx_id_slice(std::function<bool(fc::cfile&)> callback) const {
      namespace fs = std::filesystem;
      std::vector<fs::directory_entry> trx_id_files;
      for (const auto& entry : fs::directory_iterator(_slice_dir)) {
         if (entry.is_regular_file()) {
            if (entry.path().filename().string().find(_trace_trx_id_prefix) != std::string::npos) {
               trx_id_files.push_back(entry);
            }
         }
      }
      // the trace_trx_id_ files naturally sort via their file names, e.g. trace_trx_id_0211960000-0211970000.log
      // std::filesystem::path is lexicographically compared
      std::sort(trx_id_files.begin(), trx_id_files.end(),
                [&](const fs::directory_entry& a, const fs::directory_entry& b) {
                   return a.path() > b.path();
                });
      fc::cfile slice_file;
      for (const auto& entry : trx_id_files) {
         std::error_code ec;
         if (!entry.exists(ec))
            continue;
         slice_file.set_file_path(entry.path());
         slice_file.open("rb");
         slice_file.seek(0);
         if (!callback(slice_file))
            return;
         slice_file.close();
      }
   }

   void slice_directory::set_lib(uint32_t lib) {
      {
         std::scoped_lock lock(_maintenance_mtx);
         _best_known_lib = lib;
      }
      _maintenance_condition.notify_one();
   }

   void slice_directory::start_maintenance_thread(log_handler log) {
      _maintenance_thread = std::thread([this, log=std::move(log)](){
         fc::set_thread_name( "trace-mx" );
         uint32_t last_lib = 0;

         while(true) {
            std::unique_lock<std::mutex> lock(_maintenance_mtx);
            while ( last_lib >= _best_known_lib && !_maintenance_shutdown ) {
               _maintenance_condition.wait(lock);
            }

            if (_maintenance_shutdown) {
               break;
            }

            uint32_t best_known_lib = _best_known_lib;
            lock.unlock();

            log(std::string("Waking up to handle lib: ") + std::to_string(best_known_lib));

            if (last_lib < best_known_lib) {
               try {
                  run_maintenance_tasks(best_known_lib, log);
                  last_lib = best_known_lib;
               } FC_LOG_AND_DROP();
            }
         }
      });
   }

   void slice_directory::stop_maintenance_thread() {
      {
         std::scoped_lock lock(_maintenance_mtx);
         _maintenance_shutdown = true;
      }
      _maintenance_condition.notify_one();
      _maintenance_thread.join();
   }

   template<typename F>
   void slice_directory::process_irreversible_slice_range(uint32_t lib, uint32_t min_irreversible, std::optional<uint32_t>& lower_bound_slice, F&& f) {
      const uint32_t lib_slice_number = slice_number( lib );
      if (lib_slice_number < 1 || (lower_bound_slice && *lower_bound_slice >= lib_slice_number - 1))
         return;

      const int64_t upper_bound_block_number = static_cast<int64_t>(lib) - static_cast<int64_t>(min_irreversible) - _width;
      if (upper_bound_block_number >= 0) {
         uint32_t upper_bound_slice_num = slice_number(static_cast<uint32_t>(upper_bound_block_number));
         while (!lower_bound_slice || *lower_bound_slice < upper_bound_slice_num) {
            const uint32_t slice_to_process = lower_bound_slice ? *lower_bound_slice + 1 : 0;
            f(slice_to_process);
            lower_bound_slice = slice_to_process;
         }
      }
   }

   void slice_directory::run_maintenance_tasks(uint32_t lib, const log_handler& log) {
      if (_minimum_irreversible_history_blocks) {
         process_irreversible_slice_range(lib, *_minimum_irreversible_history_blocks, _last_cleaned_up_slice, [this, &log](uint32_t slice_to_clean){
            fc::cfile trace;
            fc::cfile index;
            fc::cfile trx_id;

            log(std::string("Attempting Prune of slice: ") + std::to_string(slice_to_clean));

            // cleanup index first to reduce the likelihood of reader finding index, but not finding trace
            const bool dont_open_file = false;
            const bool index_found = find_index_slice(slice_to_clean, open_state::read, index, dont_open_file);
            if (index_found) {
               log(std::string("Removing: ") + index.get_file_path().generic_string());
               std::filesystem::remove(index.get_file_path());
            }
            const bool trace_found = find_trace_slice(slice_to_clean, open_state::read, trace, dont_open_file);
            if (trace_found) {
               log(std::string("Removing: ") + trace.get_file_path().generic_string());
               std::filesystem::remove(trace.get_file_path());
            }
            const bool trx_id_found = find_trx_id_slice(slice_to_clean, open_state::read, trx_id, dont_open_file);
            if (trx_id_found) {
               log(std::string("Removing: ") + trx_id.get_file_path().generic_string());
               std::filesystem::remove(trx_id.get_file_path());
            }

            auto ctrace = find_compressed_trace_slice(slice_to_clean, dont_open_file);
            if (ctrace) {
               log(std::string("Removing: ") + ctrace->get_file_path().generic_string());
               std::filesystem::remove(ctrace->get_file_path());
            }
         });
      }

      // Only process compression if its configured AND there is a range of irreversible blocks which would not also
      // be deleted
      if (_minimum_uncompressed_irreversible_history_blocks &&
          (!_minimum_irreversible_history_blocks || *_minimum_uncompressed_irreversible_history_blocks < *_minimum_irreversible_history_blocks) )
      {
         process_irreversible_slice_range(lib, *_minimum_uncompressed_irreversible_history_blocks, _last_compressed_slice, [this, &log](uint32_t slice_to_compress){
            fc::cfile trace;
            const bool dont_open_file = false;
            const bool trace_found = find_trace_slice(slice_to_compress, open_state::read, trace, dont_open_file);

            log(std::string("Attempting compression of slice: ") + std::to_string(slice_to_compress));

            if (trace_found) {
               auto compressed_path = trace.get_file_path();
               compressed_path.replace_extension(_compressed_trace_ext);

               log(std::string("Compressing: ") + trace.get_file_path().generic_string());
               compressed_file::process(trace.get_file_path(), compressed_path.generic_string(), _compression_seek_point_stride);

               // after compression is complete, delete the old uncompressed file
               log(std::string("Removing: ") + trace.get_file_path().generic_string());
               std::filesystem::remove(trace.get_file_path());
            }
         });
      }
   }
}
