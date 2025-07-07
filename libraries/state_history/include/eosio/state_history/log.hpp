#pragma once

#include <eosio/chain/block_header.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/types.hpp>
#include <eosio/state_history/log_config.hpp>
#include <eosio/state_history/counter.hpp>

#include <fc/io/random_access_file.hpp>
#include <fc/log/logger.hpp>
#include <fc/log/logger_config.hpp> //set_thread_name

#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/restrict.hpp>
#include <boost/iostreams/operations.hpp>
#include <boost/iostreams/copy.hpp>

#include <cstdint>

namespace eosio::state_history {
namespace bio = boost::iostreams;

/*
 *   *.log:
 *   +---------+----------------+-----------+------------------+-----+---------+----------------+
 *   | Entry i | Pos of Entry i | Entry i+1 | Pos of Entry i+1 | ... | Entry z | Pos of Entry z |
 *   +---------+----------------+-----------+------------------+-----+---------+----------------+
 *
 *   *.index:
 *   +----------------+------------------+-----+----------------+
 *   | Pos of Entry i | Pos of Entry i+1 | ... | Pos of Entry z |
 *   +----------------+------------------+-----+----------------+
 *
 * each entry:
 *    state_history_log_header
 *    payload
 *
 * When block pruning is enabled, a slight modification to the format is as followed:
 * For first entry in log, a unique version is used to indicate the log is a "pruned log": this prevents
 *  older versions from trying to read something with holes in it
 * The end of the log has a 4 byte value that indicates guaranteed number of blocks the log has at its
 *  end (this can be used to reconstruct an index of the log from the end even when there is a hole in
 *  the middle of the log)
 */

inline uint64_t       ship_magic(uint16_t version, uint16_t features = 0) {
   using namespace eosio::chain::literals;
   return "ship"_n.to_uint64_t() | version | features<<16;
}
inline bool is_ship(uint64_t magic) {
   using namespace eosio::chain::literals;
   return (magic & 0xffff'ffff'0000'0000) == "ship"_n.to_uint64_t();
}
inline uint16_t       get_ship_version(uint64_t magic) { return magic; }
inline uint16_t       get_ship_features(uint64_t magic) { return magic>>16; }
inline bool           is_ship_supported_version(uint64_t magic) { return get_ship_version(magic) == 0; }
static const uint16_t ship_current_version = 0;
static const uint16_t ship_feature_pruned_log = 1;
inline bool           is_ship_log_pruned(uint64_t magic) { return get_ship_features(magic) & ship_feature_pruned_log; }
inline uint64_t       clear_ship_log_pruned_feature(uint64_t magic) { return ship_magic(get_ship_version(magic), get_ship_features(magic) & ~ship_feature_pruned_log); }

struct log_header {
   uint64_t             magic        = ship_magic(ship_current_version);
   chain::block_id_type block_id     = {};
   uint64_t             payload_size = 0;
};
struct log_header_with_sizes : log_header {
   uint32_t compressed_size = 0;
   uint64_t uncompressed_size = 0;
};

struct ship_log_entry {
   uint64_t get_uncompressed_size() {
      if(!uncompressed_size) {
         bio::filtering_istreambuf buf(bio::zlib_decompressor() | bio::restrict(device, compressed_data_offset, compressed_data_size));
         uncompressed_size = bio::copy(buf, bio::null_sink());
      }
      return *uncompressed_size;
   }

   bio::filtering_istreambuf get_stream() {
      return bio::filtering_istreambuf(bio::zlib_decompressor() | bio::restrict(device, compressed_data_offset, compressed_data_size));
   }

   fc::random_access_file::device device;
   uint64_t                       compressed_data_offset;
   uint64_t                       compressed_data_size;
   std::optional<uint64_t>        uncompressed_size;
};

class state_history_log {
public:
   using non_local_get_block_id_func = std::function<std::optional<chain::block_id_type>(chain::block_num_type)>;

   static std::optional<chain::block_id_type> no_non_local_get_block_id_func(chain::block_num_type) {
      return std::nullopt;
   }
private:
   std::optional<state_history::prune_config> prune_config;
   non_local_get_block_id_func                non_local_get_block_id;

   fc::random_access_file       log;
   fc::random_access_file       index;
   uint32_t                     _begin_block       = 0;  //always tracks the first block available even after pruning
   uint32_t                     _index_begin_block = 0;  //the first block of the file; even after pruning. it's what index 0 in the index file points to
   uint32_t                     _end_block         = 0;  //one-past-the-last block of the file
   chain::block_id_type         last_block_id;

   inline static const unsigned packed_header_size = fc::raw::pack_size(log_header());
   inline static const unsigned packed_header_with_sizes_size = fc::raw::pack_size(log_header_with_sizes());

 public:
   state_history_log(const state_history_log&) = delete;
   state_history_log& operator=(state_history_log&) = delete;
   state_history_log(state_history_log&&) = default;
   state_history_log& operator=(state_history_log&&) = default;

   state_history_log(const std::filesystem::path& log_dir_and_stem,
                     non_local_get_block_id_func non_local_get_block_id = no_non_local_get_block_id_func,
                     const std::optional<state_history::prune_config>& prune_conf = std::nullopt) :
     prune_config(prune_conf), non_local_get_block_id(non_local_get_block_id),
     log(std::filesystem::path(log_dir_and_stem).replace_extension("log")),
     index(std::filesystem::path(log_dir_and_stem).replace_extension("index")) {
      EOS_ASSERT(!!non_local_get_block_id, chain::plugin_exception, "misuse of get_block_id");

      if(prune_config) {
         EOS_ASSERT(prune_config->prune_blocks, chain::plugin_exception, "state history log prune configuration requires at least one block");
         EOS_ASSERT(__builtin_popcount(prune_config->prune_threshold) == 1, chain::plugin_exception, "state history prune threshold must be power of 2");
         //switch this over to the mask that will be used
         prune_config->prune_threshold = ~(prune_config->prune_threshold-1);
      }

      check_log_on_init();
      check_index_on_init();
      check_log_and_index_on_init();

      //check for conversions to/from pruned log, as long as log contains something
      if(!empty()) {
         log_header first_header = log.unpack_from<decltype(first_header)>(0);

         if(!is_ship_log_pruned(first_header.magic) && prune_conf) {          //non-pruned to pruned
            //need to convert non-pruned to pruned; first prune any ranges we can up-front (might be none)
            prune();

            //update first header to indicate prune feature is enabled
            first_header.magic = ship_magic(get_ship_version(first_header.magic), ship_feature_pruned_log);
            log.pack_to(first_header, 0);

            //write trailer on log with num blocks
            log.pack_to_end<uint32_t>(_end_block - _begin_block);
         }
         else if(is_ship_log_pruned(first_header.magic) && !prune_config) {  //pruned to non-pruned
            vacuum();
         }
      }
   }

   ~state_history_log() {
      //we're a carcass of a state_history_log that was moved out of
      if(!log.is_valid() || !index.is_valid())
         return;
      //nothing to do if log is empty or we aren't pruning
      if(empty())
         return;

      if(!prune_config || !prune_config->vacuum_on_close)
         return;

      const size_t first_data_pos = get_pos(_begin_block);
      const size_t last_data_pos = log.size();
      if(last_data_pos - first_data_pos < *prune_config->vacuum_on_close)
         vacuum();
   }

   //        begin     end
   std::pair<uint32_t, uint32_t> block_range() const {
      return {_begin_block, _end_block};
   }

   bool empty() const {
      const auto [first, second] = block_range();
      return first == second;
   }

   std::optional<ship_log_entry> get_entry(uint32_t block_num) {
      if(block_num < _begin_block || block_num >= _end_block)
         return std::nullopt;

      const uint64_t log_pos = get_pos(block_num);
      log_header_with_sizes header = log.unpack_from<decltype(header)>(log_pos);

      //There are three types of "payload headers" that trail the magic/block_id/payload_size header:
      // 1) up through and including EOSIO 2.0 would add an uint32_t indicating compressed message size
      // 2) Leap 3.x would hardcode this uint32_t to 0
      // 3) Leap 4.0+ would hardcode this uint32_t to 1, and then add an uint64_t with the _uncompressed_ size
      //     (knowing the uncompressed size ahead of time makes it convenient to stream the data to the client which
      //      needs uncompressed size ahead of time)
      // 1 & 2 are problematic for the current streaming of the logs to clients. There appears to be no option other
      //  then making two passes through the compressed data: once to figure out the uncompressed size to send up front
      //  to the client, then a second time to actually decompress the data to send to the client. But don't do the first
      //  pass here -- delay that until we're on the ship thread.
      constexpr size_t prel4_head_size = sizeof(log_header_with_sizes::compressed_size);
      constexpr size_t l4_head_size = sizeof(log_header_with_sizes::compressed_size) + sizeof(log_header_with_sizes::uncompressed_size);
      return ship_log_entry{
         .device                 = log.seekable_device(),
         .compressed_data_offset = log_pos + packed_header_size + (header.compressed_size == 1 ? l4_head_size : prel4_head_size),
         .compressed_data_size   = header.payload_size          - (header.compressed_size == 1 ? l4_head_size : prel4_head_size),
         .uncompressed_size      =                                (header.compressed_size == 1 ? std::optional<uint64_t>(header.uncompressed_size) : std::nullopt)
      };
   }

   template <typename F>
   void pack_and_write_entry(const chain::block_id_type& id, const chain::block_id_type& prev_id, F&& pack_to) {
      log_header_with_sizes header = {{ship_magic(ship_current_version, 0), id}, 1};
      const uint32_t block_num = chain::block_header::num_from_id(header.block_id);

      if(!empty())
         EOS_ASSERT(block_num <= _end_block, chain::plugin_exception, "block ${b} skips over block ${e} in ${name}", ("b", block_num)("e", _end_block)("name", log.display_path()));
      EOS_ASSERT(block_num >= _index_begin_block, chain::plugin_exception, "block ${b} is before start block ${s} of ${name}", ("b", block_num)("s", _begin_block)("name", log.display_path()));
      if(block_num == _end_block) //appending at the end of known blocks; can shortcut some checks since we have last_block_id readily available
         EOS_ASSERT(prev_id == last_block_id, chain::plugin_exception, "missed a fork change in ${name}", ("name", log.display_path()));
      else {                      //seeing a block num we've seen before OR first block in the log; prepare some extra checks
         //find the previous block id as a sanity check. This might not be in our log due to log splitting. It also might not be present at all if this is the first
         // block written, so don't require this lookup to succeed, just require the id to match if the lookup succeeded.
         if(std::optional<chain::block_id_type> local_id_found = get_block_id(block_num-1))
            EOS_ASSERT(local_id_found == prev_id, chain::plugin_exception, "missed a fork change in ${name}", ("name", log.display_path()));
         else if(std::optional<chain::block_id_type> non_local_id_found = non_local_get_block_id(block_num-1))
            EOS_ASSERT(non_local_id_found == prev_id, chain::plugin_exception, "missed a fork change in ${name}", ("name", log.display_path()));
         //we don't want to re-write blocks that we already have, so check if the existing block_id recorded in the log matches and if so, bail
         if(get_block_id(block_num) == id)
            return;
         //but if it doesn't match, and log isn't empty, ensure not writing a new genesis block to guard against accidental rewinding of the entire ship log
         if(!empty())
            EOS_ASSERT(block_num > 2u, chain::plugin_exception, "existing ship log with ${eb} blocks when starting from genesis block ${b}", ("eb", _end_block-_begin_block)("b", block_num));
      }

      ssize_t log_insert_pos = log.size();
      if(prune_config) {
         if(!empty())  //overwrite the prune trailer that is at the end of the log
            log_insert_pos -= sizeof(uint32_t);
         else          //we're operating on a pruned block log and this is the first entry in the log, make note of the feature in the header
            header.magic = ship_magic(get_ship_version(header.magic), ship_feature_pruned_log);
      }

      const ssize_t payload_insert_pos = log_insert_pos + packed_header_with_sizes_size;

      bio::filtering_ostreambuf buf(detail::counter() | bio::zlib_compressor(bio::zlib::no_compression) | detail::counter() | bio::restrict(log.seekable_device(), payload_insert_pos));
      pack_to(buf);
      bio::close(buf);
      header.uncompressed_size = buf.component<detail::counter>(0)->characters();
      header.payload_size = buf.component<detail::counter>(2)->characters() + sizeof(header.compressed_size) + sizeof(header.uncompressed_size);
      log.pack_to(header, log_insert_pos);

      fc::random_access_file::write_datastream appender = log.append_ds();
      fc::raw::pack(appender, (uint64_t)log_insert_pos);

      const bool was_empty = empty();
      if(was_empty)
         _index_begin_block = _begin_block = block_num;
      else if(block_num < _begin_block)        //the log wasn't empty, but this block is before the first available block in a pruned log: reset the beginning
         _begin_block = _end_block = block_num;

      if(block_num < _end_block-1) //writing a block num less than previous head; truncate index to avoid mixup on re-open where index would indicate more blocks than really exist
         index.resize((block_num-_index_begin_block)*sizeof(uint64_t));

      last_block_id = header.block_id;
      _end_block    = block_num + 1;

      index.pack_to((uint64_t)log_insert_pos, (block_num-_index_begin_block)*sizeof(uint64_t));

      if(prune_config) {
         if((log_insert_pos&prune_config->prune_threshold) != (log.size()&prune_config->prune_threshold))
            prune();

         const uint32_t num_blocks_in_log = _end_block - _begin_block;
         fc::raw::pack(appender, num_blocks_in_log);
      }

      appender.flush();
   }

   std::optional<chain::block_id_type> get_block_id(uint32_t block_num) {
      if(block_num >= _begin_block && block_num < _end_block)
         return log.unpack_from<log_header>(get_pos(block_num)).block_id;
      return std::nullopt;
   }

 private:
   void prune() {
      if(!prune_config)
         return;
      if(_end_block - _begin_block <= prune_config->prune_blocks)
         return;

      const uint32_t prune_to_num = _end_block - prune_config->prune_blocks;
      ///TODO: we should cap this to the lowest position there are any active entries reading from, see https://github.com/AntelopeIO/spring/pull/237
      uint64_t prune_to_pos = get_pos(prune_to_num);
      log.punch_hole(fc::raw::pack_size(log_header()), prune_to_pos);

      _begin_block = prune_to_num;
      ilog("${name} pruned to blocks ${b}-${e}", ("name", log.display_path())("b", _begin_block)("e", _end_block - 1));
   }

   bool discover_and_check_last_block_ok(bool is_pruned) {
      try {
         //fetch the last block header from the log solely using the log (i.e. not the index: so don't use get_pos()). This is a sanity check.
         const uint64_t last_header_pos = log.unpack_from<std::decay_t<decltype(last_header_pos)>>(log.size() - sizeof(uint64_t) - (is_pruned ? sizeof(uint32_t) : 0));
         log_header last_header = log.unpack_from<decltype(last_header)>(last_header_pos);
         FC_ASSERT(is_ship(last_header.magic) && is_ship_supported_version(last_header.magic), "Unexpected header magic on last block");
         _end_block    = chain::block_header::num_from_id(last_header.block_id) + 1;
         last_block_id = last_header.block_id;
         FC_ASSERT(_begin_block < _end_block, "Block number ${hbn} from head and block number ${tbn} from tail of log are not expected", ("hbn", _begin_block)("tbn", _end_block-1));
      }
      catch(const std::bad_alloc&) {
         throw;
      }
      catch(const std::exception& e) {
         ilog("Failure while checking ${name}: ${m}", ("name", log.display_path())("m", e.what()));
         return false;
      }
      return true;
   }

   //only works on non-pruned logs since it has to work tail to head
   void recover_blocks() {
      const size_t size         = log.size();
      const size_t header_size  = fc::raw::pack_size(log_header());
      size_t pos                = 0;
      uint32_t num_found        = 0;

      while (true) {
         if(pos + header_size > size)
            break;
         log_header header = log.unpack_from<decltype(header)>(pos);

         uint64_t suffix;
         if(!is_ship(header.magic) || !is_ship_supported_version(header.magic) || header.payload_size > size ||
             pos + header_size + header.payload_size + sizeof(suffix) > size) {
            EOS_ASSERT(!is_ship(header.magic) || is_ship_supported_version(header.magic), chain::plugin_exception,
                       "${name} has an unsupported version", ("name", log.display_path()));
            break;
         }
         suffix = log.unpack_from<decltype(suffix)>(pos + header_size + header.payload_size);
         if(suffix != pos)
            break;
         pos += header_size + header.payload_size + sizeof(suffix);
         if(!(++num_found % 10000)) {
            ilog("${num_found} blocks found, log pos = ${pos}", ("num_found", num_found)("pos", pos));
         }
      }
      ilog("recovery of ${fn} complete, ${b} blocks found in ${bytes} bytes", ("fn", log.display_path())("b", num_found)("bytes", pos));
      log.resize(pos);
   }

   void check_log_on_init() {
      if(log.size() == 0)
         return;

      try {
         log_header first_header = log.unpack_from<decltype(first_header)>(0);
         FC_ASSERT(is_ship(first_header.magic) && is_ship_supported_version(first_header.magic), "Unexpected header magic");

         std::optional<uint32_t> pruned_count;
         if(is_ship_log_pruned(first_header.magic))
            pruned_count = log.unpack_from<uint32_t>(log.size() - sizeof(uint32_t));

         _index_begin_block = _begin_block  = chain::block_header::num_from_id(first_header.block_id);
         last_block_id = first_header.block_id;

         if(!discover_and_check_last_block_ok(!!pruned_count)) {
            FC_ASSERT(!is_ship_log_pruned(first_header.magic), "Pruned log is corrupted");
            ilog("Attempting to recover ${n}", ("n", log.display_path()));
            recover_blocks();
            FC_ASSERT(discover_and_check_last_block_ok(!!pruned_count), "Failed to recover blocks");
         }

         if(pruned_count)
            _begin_block = _end_block - *pruned_count;
      } EOS_RETHROW_EXCEPTIONS(chain::plugin_exception, "${name} is corrupted and cannot be repaired", ("name", log.display_path()));
   }

   void check_index_on_init() {
      const uint64_t expected_index_size = (_end_block - _index_begin_block) * sizeof(uint64_t);
      if(index.size() == expected_index_size)
         return;

      ilog("Regenerate ${name}", ("name", index.display_path()));
      index.resize(0);

      if(log.size()) {
         ssize_t next_logpos = log.size() - sizeof(uint64_t);
         index.resize(expected_index_size);

         log_header header = log.unpack_from<decltype(header)>(0);
         if(is_ship_log_pruned(header.magic))
            next_logpos -= sizeof(uint32_t);

         do {
            const uint64_t logpos = log.unpack_from<uint64_t>(next_logpos);
            header = log.unpack_from<decltype(header)>(logpos);
            EOS_ASSERT(is_ship(header.magic) && is_ship_supported_version(header.magic), chain::plugin_exception, "corrupt ${name}, unknown header magic", ("name", log.display_path()));

            const uint32_t read_block_num = chain::block_header::num_from_id(header.block_id);
            //may need to skip blocks if log was closed when a shorter fork has been applied; ex: log contains 2345675 (begin=2, end=6, but we see block 7 and 6 when reading)
            if(read_block_num < _end_block) {
               const uint64_t index_offset_for_bnum = (read_block_num - _index_begin_block)*sizeof(uint64_t);
               if(index.unpack_from<uint64_t>(index_offset_for_bnum) == 0)  //don't overwrite newer blocks for a given blocknum, for example 234564567 only take first (from end) 6, 5, 4 blocks
                  index.pack_to(logpos, index_offset_for_bnum);
            }

            next_logpos = logpos - sizeof(uint64_t);
            if (!(chain::block_header::num_from_id(header.block_id) % 10000))
               ilog("${r} blocks remaining, log pos = ${pos}", ("r", chain::block_header::num_from_id(header.block_id) - _begin_block)("pos", logpos));
         } while(chain::block_header::num_from_id(header.block_id) != _begin_block);
      }

      ilog("${name} regeneration complete", ("name", index.display_path()));
   }

   void check_log_and_index_on_init() {
      if(log.size() == 0)
         return;

      try {
         log_header first_header = log.unpack_from<decltype(first_header)>(0);
         FC_ASSERT(is_ship(first_header.magic) && is_ship_supported_version(first_header.magic), "Unexpected header magic");
         bool is_pruned = is_ship_log_pruned(first_header.magic);

         //fetch the last block header from the log solely using the log (i.e. not the index: so don't use get_pos()). This is a sanity check.
         const uint64_t last_header_pos = log.unpack_from<std::decay_t<decltype(last_header_pos)>>(log.size() - sizeof(uint64_t) - (is_pruned ? sizeof(uint32_t) : 0));
         //verify last index position matches last log entry
         const uint64_t index_pos = get_pos(_end_block-1);
         FC_ASSERT(index_pos == last_header_pos, "Last index position ${ip} does not match last entry in log ${lp}", ("ip", index_pos)("lp", last_header_pos));
      } EOS_RETHROW_EXCEPTIONS(chain::plugin_exception, "${name} is corrupted and cannot be repaired, will be automatically regenerated if removed.", ("name", index.display_path()));
   }

   uint64_t get_pos(uint32_t block_num) {
      assert(block_num >= _begin_block && block_num < _end_block);
      return index.unpack_from<uint64_t>((block_num - _index_begin_block) * sizeof(uint64_t));
   }

   /*
    * A pruned log will have a gap where data has been erased (via "poking holes"). for example,
    * _index_begin_block=1, _begin_block=5, _end_block=9
    * index:  1|2|3|4|5|6|7|8
    * log:    Hxxxxxx|5|6|7|8𝑡  (H is a stub log header, 𝑡 is the pruned-log-specific trailer)
    * Vacuuming will collapse the gap resulting in a non-pruned log and index:
    * _index_begin_block=5, _begin_block=5, _end_block=9
    * index:  5|6|7|8
    * log:    5|6|7|8
    */
   void vacuum() {
      //a completely empty log should have nothing on disk; don't touch anything
      if(empty())
         return;

      log_header first_header = log.unpack_from<decltype(first_header)>(0);
      EOS_ASSERT(is_ship_log_pruned(first_header.magic), chain::plugin_exception, "vacuum can only be performed on pruned logs");

      //may happen if _begin_block is still first block on-disk of log. clear the pruned feature flag & erase
      // the 4 byte trailer. The pruned flag is only set on the first header in the log, so it does not need
      // to be touched up if we actually vacuum up any other blocks to the front.
      if(_begin_block == _index_begin_block) {
         log.pack_to(clear_ship_log_pruned_feature(first_header.magic), 0);
         log.resize(log.size() - sizeof(uint32_t));
         return;
      }

      ilog("Vacuuming pruned log ${n}", ("n", log.display_path()));

      size_t copy_from_pos = get_pos(_begin_block);
      size_t copy_to_pos = 0;

      const size_t offset_bytes = copy_from_pos - copy_to_pos;
      const size_t offset_blocks = _begin_block - _index_begin_block;
      size_t copy_sz = log.size() - copy_from_pos - sizeof(uint32_t); //don't copy trailer in to new unpruned log
      const uint32_t num_blocks_in_log = _end_block - _begin_block;

      std::vector<char> buff;
      buff.resize(4*1024*1024);
      fc::random_access_file::device log_device = log.seekable_device();

      auto tick = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
      while(copy_sz) {
         const size_t copy_this_round = std::min(buff.size(), copy_sz);
         log_device.seek(copy_from_pos, std::ios_base::beg);
         log_device.read(buff.data(), copy_this_round);  //iostreams Blocking concept requires reading all
         log.punch_hole(copy_to_pos, copy_from_pos+copy_this_round);
         log_device.seek(copy_to_pos, std::ios_base::beg);
         log_device.write(buff.data(), copy_this_round);

         copy_from_pos += copy_this_round;
         copy_to_pos += copy_this_round;
         copy_sz -= copy_this_round;

         const auto tock = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
         if(tick < tock - std::chrono::seconds(5)) {
            ilog("Vacuuming pruned log ${n}, ${b} bytes remaining", ("b", copy_sz)("n", log.display_path()));
            tick = tock;
         }
      }
      log.resize(copy_to_pos);

      {
         boost::interprocess::mapped_region index_mapped(index, boost::interprocess::read_write);
         uint64_t* index_ptr = (uint64_t*)index_mapped.get_address();

         for(uint32_t new_block_num = 0; new_block_num < num_blocks_in_log; ++new_block_num) {
            const uint64_t new_pos = index_ptr[new_block_num + offset_blocks] - offset_bytes;
            index_ptr[new_block_num] = new_pos;

            if(new_block_num + 1 != num_blocks_in_log)
               log.pack_to(new_pos, index_ptr[new_block_num + offset_blocks + 1] - offset_bytes - sizeof(uint64_t));
            else
               log.pack_to(new_pos, log.size()-sizeof(uint64_t));
         }
      }
      index.resize(num_blocks_in_log*sizeof(uint64_t));

      _index_begin_block = _begin_block;
      ilog("Vacuum of pruned log ${n} complete",("n", log.display_path()));
   }
};

}

FC_REFLECT(eosio::state_history::log_header, (magic)(block_id)(payload_size))
FC_REFLECT_DERIVED(eosio::state_history::log_header_with_sizes, (eosio::state_history::log_header), (compressed_size)(uncompressed_size));