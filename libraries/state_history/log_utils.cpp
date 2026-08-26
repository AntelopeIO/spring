#include <eosio/state_history/log_utils.hpp>
#include <eosio/state_history/log.hpp>

#include <fc/io/random_access_file.hpp>

#include <boost/iostreams/device/null.hpp>

#include <algorithm>
#include <cassert>
#include <regex>

namespace eosio::state_history::log_utils {

namespace {

namespace bio = boost::iostreams;

constexpr size_t default_window_size = 8 * 1024 * 1024;

/// progress callbacks fire after at least this many bytes of new work
constexpr uint64_t progress_granularity = 256 * 1024 * 1024;

/// in-memory index construction refuses block ranges larger than this (2 GiB of slots); no real
/// chain's ship log is within orders of magnitude of such a range
constexpr uint64_t max_in_memory_index_blocks = uint64_t(1) << 28;

const size_t packed_header_size = fc::raw::pack_size(log_header());

constexpr size_t marker_size  = sizeof(log_header_with_sizes::compressed_size);
constexpr size_t trailer_size = sizeof(uint64_t);
const size_t     min_entry_size = packed_header_size + marker_size + trailer_size;

/// payload preamble layout markers (see state_history_log::get_entry() for the format history)
constexpr uint32_t leap4_compressed_marker = 1;
const size_t       leap4_preamble_size = marker_size + sizeof(log_header_with_sizes::uncompressed_size);

constexpr std::string_view log_extension   = "log";
constexpr std::string_view index_extension = "index";
//deliberately dot-free: log_path_of/index_path_of use replace_extension, and a dotted suffix would
// be treated as an extension and replaced, making the temporary paths collide with the originals
constexpr std::string_view tmp_suffix      = "-repair-tmp"; ///< temporary bundle suffix used before atomic rename

//extension handling matches state_history_log's constructor (replace_extension), so a stem of
// "trace_history" or a retained "trace_history-2-1000" maps to the same files the library opens
std::filesystem::path log_path_of(const std::filesystem::path& stem) {
   return std::filesystem::path(stem).replace_extension(log_extension);
}
std::filesystem::path index_path_of(const std::filesystem::path& stem) {
   return std::filesystem::path(stem).replace_extension(index_extension);
}

/// Sliding read window over a random_access_file so entry-chain walks do sequential bulk reads
/// instead of one small pread per header and trailer.
struct window_reader {
   window_reader(fc::random_access_file& f, uint64_t file_size, size_t window_size = default_window_size)
      : dev(f.seekable_device()), file_size(file_size), buf(window_size) {}

   /// pointer to `need` contiguous bytes at file offset `pos`; `need` must fit the window and the file
   const char* view(uint64_t pos, size_t need) {
      assert(need <= buf.size());
      assert(pos + need <= file_size);
      if(pos < base || pos + need > base + len) {
         base = pos;
         len  = static_cast<size_t>(std::min<uint64_t>(buf.size(), file_size - pos));
         dev.seek(base, std::ios_base::beg);
         size_t got = 0;
         while(got < len) {
            const std::streamsize red = dev.read(buf.data() + got, len - got);
            EOS_ASSERT(red > 0, chain::plugin_exception, "unexpected end of file reading at offset ${pos}",
                       ("pos", base + got));
            got += static_cast<size_t>(red);
         }
      }
      return buf.data() + (pos - base);
   }

   fc::random_access_file::device dev;
   uint64_t                       file_size;
   std::vector<char>              buf;
   uint64_t                       base = 0;
   size_t                         len  = 0;
};

/// rate-limited progress reporting
struct progress_ticker {
   progress_ticker(const progress_func& f, uint64_t total) : f(f), total(total) {}
   void tick(uint64_t done) {
      if(f && done - last >= progress_granularity) {
         last = done;
         f(done, total);
      }
   }
   const progress_func& f;
   uint64_t             total;
   uint64_t             last = 0;
};

template<typename T>
T parse_at(const char* p, size_t len) {
   T t;
   fc::datastream<const char*> ds(p, len);
   fc::raw::unpack(ds, t);
   return t;
}

/// outcome of validating a single entry at a given offset
struct entry_check {
   bool        ok              = false;
   bool        structurally_ok = false; ///< header/bounds/trailer fine; the deep payload check may still have failed
   uint32_t    block_num       = 0;     ///< meaningful when structurally_ok
   uint64_t    end_pos         = 0;     ///< one past the entry's position trailer; meaningful when structurally_ok
   std::string reason;                  ///< first validation failure when !ok
};

/**
 * Validate the entry at `pos`: ship magic with the supported version and no feature flags, payload
 * preamble coherent with its format marker, payload within bounds, block number plausible against
 * `prev_block`/`floor_block` when provided, and the position trailer pointing back at `pos`. With
 * `deep`, additionally decompress the payload and, for entries that record an uncompressed size,
 * compare against it.
 */
entry_check check_entry(window_reader& w, fc::random_access_file& f, uint64_t pos, uint64_t file_size,
                        std::optional<uint32_t> prev_block, std::optional<uint32_t> floor_block, bool deep) {
   entry_check r;
   if(pos + min_entry_size > file_size) {
      r.reason = "remaining bytes are too few to hold an entry";
      return r;
   }

   const log_header hdr = parse_at<log_header>(w.view(pos, packed_header_size), packed_header_size);
   if(!is_ship(hdr.magic)) {
      r.reason = "header magic missing";
      return r;
   }
   if(!is_ship_supported_version(hdr.magic) || get_ship_features(hdr.magic) != 0) {
      r.reason = "unexpected version or feature flags in header magic";
      return r;
   }

   r.block_num = chain::block_header::num_from_id(hdr.block_id);
   if(r.block_num == 0) {
      r.reason = "block number 0 is not valid";
      return r;
   }
   if(prev_block && r.block_num > *prev_block + 1) {
      r.reason = "block " + std::to_string(r.block_num) + " skips over block " + std::to_string(*prev_block + 1);
      return r;
   }
   if(floor_block && r.block_num < *floor_block) {
      r.reason = "block " + std::to_string(r.block_num) + " is before the log's first block " +
                 std::to_string(*floor_block);
      return r;
   }

   if(hdr.payload_size < marker_size || hdr.payload_size > file_size ||
      pos + packed_header_size + hdr.payload_size + trailer_size > file_size) {
      r.reason = "payload size " + std::to_string(hdr.payload_size) + " does not fit in the file";
      return r;
   }

   const uint32_t marker = parse_at<uint32_t>(w.view(pos + packed_header_size, marker_size), marker_size);
   size_t                  preamble = marker_size;
   std::optional<uint64_t> recorded_uncompressed;
   if(marker == leap4_compressed_marker) {
      if(hdr.payload_size < leap4_preamble_size) {
         r.reason = "payload too small for its format";
         return r;
      }
      preamble = leap4_preamble_size;
      recorded_uncompressed =
         parse_at<uint64_t>(w.view(pos + packed_header_size + marker_size, sizeof(uint64_t)), sizeof(uint64_t));
   } else if(marker != 0 && marker != hdr.payload_size - marker_size) {
      //pre-Leap-3 entries recorded the actual compressed size here; anything else is damage
      r.reason = "payload format marker is incoherent with the payload size";
      return r;
   }

   const uint64_t trailer_pos = pos + packed_header_size + hdr.payload_size;
   if(parse_at<uint64_t>(w.view(trailer_pos, trailer_size), trailer_size) != pos) {
      r.reason = "position trailer does not point back at the entry";
      return r;
   }

   r.structurally_ok = true;
   r.end_pos         = trailer_pos + trailer_size;

   if(deep) {
      try {
         bio::filtering_istreambuf strm(bio::zlib_decompressor() |
                                        bio::restrict(f.seekable_device(), pos + packed_header_size + preamble,
                                                      hdr.payload_size - preamble));
         const uint64_t decompressed = bio::copy(strm, bio::null_sink());
         if(recorded_uncompressed && decompressed != *recorded_uncompressed) {
            r.reason = "payload decompressed to " + std::to_string(decompressed) + " bytes but the entry recorded " +
                       std::to_string(*recorded_uncompressed);
            return r;
         }
      } catch(const std::exception& e) {
         r.reason = std::string("payload decompression failed: ") + e.what();
         return r;
      }
   }

   r.ok = true;
   return r;
}

/**
 * Search forward from `from` for the next offset holding a structurally valid entry. Entries of a
 * forward-scannable (non-pruned) log always bear the current version with no feature flags, so the
 * serialized magic is a fixed 8-byte needle; candidates are then fully validated, making a false
 * resynchronization on payload bytes that happen to contain the needle all but impossible (the
 * candidate's position trailer would have to point back at it exactly).
 */
std::optional<uint64_t> find_next_entry(window_reader& w, fc::random_access_file& f, uint64_t from,
                                        uint64_t file_size, std::optional<uint32_t> floor_block) {
   char                  needle[sizeof(uint64_t)];
   fc::datastream<char*> nds(needle, sizeof(needle));
   fc::raw::pack(nds, ship_magic(ship_current_version, 0));

   uint64_t pos = from;
   while(pos + min_entry_size <= file_size) {
      const size_t span = static_cast<size_t>(std::min<uint64_t>(w.buf.size(), file_size - pos));
      const char*  p    = w.view(pos, span);
      const char*  hit  = std::search(p, p + span, needle, needle + sizeof(needle));
      if(hit != p + span) {
         const uint64_t cand = pos + (hit - p);
         if(check_entry(w, f, cand, file_size, std::nullopt, floor_block, false).structurally_ok)
            return cand;
         pos = cand + 1;
      } else {
         if(pos + span == file_size)
            break;
         pos += span - (sizeof(needle) - 1); //overlap windows so a needle straddling them is still found
      }
   }
   return std::nullopt;
}

/// incrementally builds scan_result valid_ranges from entries visited in file order
struct range_builder {
   explicit range_builder(scan_result& r) : r(r) {}

   void good(const entry_check& c, uint64_t pos) {
      if(!cur) {
         cur = entry_range{c.block_num, c.block_num, pos, c.end_pos, 1, pos, c.block_num};
      } else {
         cur->last_block = c.block_num;
         cur->end_pos    = c.end_pos;
         ++cur->entry_count;
         //an entry at-or-below the current canonical start supersedes it: everything before this
         // entry is now on an abandoned fork
         if(c.block_num <= cur->canonical_first_block) {
            cur->canonical_first_block = c.block_num;
            cur->canonical_begin_pos   = pos;
         }
      }
   }

   void close() {
      if(cur) {
         r.valid_ranges.push_back(*cur);
         cur.reset();
      }
   }

   scan_result&               r;
   std::optional<entry_range> cur;
};

/// endpoint-only probe shared by summarize_log() and the mutating operations' sanity checks
struct endpoints {
   bool                    valid_first = false; ///< first header parses with ship magic and supported version
   uint16_t                version     = 0;
   bool                    pruned      = false;
   std::optional<uint32_t> pruned_count;
   uint32_t                first_block = 0;
   bool                    tail_ok        = false; ///< trailing position trailer leads to a coherent last entry
   uint32_t                last_block     = 0;
   uint64_t                last_entry_pos = 0;
   chain::block_id_type    first_id; ///< id recorded for first_block (valid_first only)
   chain::block_id_type    last_id;  ///< id recorded for last_block (tail_ok only)
};

endpoints probe_endpoints(fc::random_access_file& log, uint64_t size) {
   endpoints e;
   if(size < min_entry_size)
      return e;
   try {
      const log_header first = log.unpack_from<log_header>(0);
      if(!is_ship(first.magic) || !is_ship_supported_version(first.magic))
         return e;
      e.valid_first = true;
      e.version     = get_ship_version(first.magic);
      e.pruned      = is_ship_log_pruned(first.magic);
      e.first_block = chain::block_header::num_from_id(first.block_id);
      e.first_id    = first.block_id;
      if(e.pruned)
         e.pruned_count = log.unpack_from<uint32_t>(size - sizeof(uint32_t));

      const uint64_t end_of_entries = size - (e.pruned ? sizeof(uint32_t) : 0);
      const uint64_t last_pos       = log.unpack_from<uint64_t>(end_of_entries - trailer_size);
      if(last_pos + packed_header_size + trailer_size > end_of_entries)
         return e;
      const log_header last = log.unpack_from<log_header>(last_pos);
      if(!is_ship(last.magic) || !is_ship_supported_version(last.magic))
         return e;
      if(last_pos + packed_header_size + last.payload_size + trailer_size != end_of_entries)
         return e;
      e.tail_ok        = true;
      e.last_block     = chain::block_header::num_from_id(last.block_id);
      e.last_entry_pos = last_pos;
      e.last_id        = last.block_id;
   } catch(const std::exception&) {
      //treat any short read or parse failure as "endpoint not OK"; the flags already say so
   }
   return e;
}

/// expected index content computed from the log itself
struct computed_index {
   uint32_t              index_first = 0; ///< block number slot 0 corresponds to
   uint32_t              first_block = 0; ///< first servable block (> index_first only for pruned logs)
   uint32_t              last_block  = 0;
   std::vector<uint64_t> slots;           ///< latest entry position per block, 0 for pruned-away slots
};

void grow_slots(std::vector<uint64_t>& slots, uint32_t index_first, uint32_t block_num,
                const std::filesystem::path& stem) {
   //callers guarantee block_num >= index_first via check_entry's floor check; spell the precondition
   // out so the unsigned subtraction below can never silently wrap into a huge allocation request
   assert(block_num >= index_first);
   const uint64_t needed = uint64_t(block_num) - index_first + 1;
   EOS_ASSERT(needed <= max_in_memory_index_blocks, chain::plugin_exception,
              "${stem}.log spans more than ${mimib} blocks which exceeds what this tool supports",
              ("stem", stem.string())("mimib", max_in_memory_index_blocks));
   if(slots.size() < needed)
      slots.resize(needed, 0);
}

/**
 * Forward-walk a non-pruned log computing every block's latest entry position. Equivalent to the
 * backward regeneration state_history_log performs on open, but sequential (and so considerably
 * faster on most storage). Throws on the first sign of damage.
 */
computed_index compute_index_forward(fc::random_access_file& log, uint64_t size, const std::filesystem::path& stem,
                                     const progress_func& progress) {
   computed_index  ci;
   window_reader   w(log, size);
   progress_ticker ticker(progress, size);

   uint64_t                pos = 0;
   std::optional<uint32_t> prev, floor;
   while(pos < size) {
      const entry_check c = check_entry(w, log, pos, size, prev, floor, false);
      EOS_ASSERT(c.ok, chain::plugin_exception, "${stem}.log is damaged at offset ${pos} (${reason}); repair it first",
                 ("stem", stem.string())("pos", pos)("reason", c.reason));
      if(!floor) {
         floor          = c.block_num;
         ci.index_first = ci.first_block = c.block_num;
      }
      grow_slots(ci.slots, ci.index_first, c.block_num, stem);
      ci.slots[c.block_num - ci.index_first] = pos;
      ci.last_block                          = c.block_num;
      prev                                   = c.block_num;
      pos                                    = c.end_pos;
      ticker.tick(pos);
   }
   //a fork switch near the end can leave entries for blocks past the final head; those slots are stale
   ci.slots.resize(uint64_t(ci.last_block) - ci.index_first + 1);
   return ci;
}

/// where and why a backward pruned-chain walk stopped short of its begin block
struct chain_break {
   uint64_t    at_pos; ///< offset the walk could not get past
   std::string reason;
};

/**
 * Walk a pruned log's position-trailer chain from its tail entry back toward `begin_block` (the
 * oldest retained block), invoking `visit(pos, entry_check&&)` for every entry, newest first. This
 * is the one place the pruned trailer chain is validated; both index regeneration and the smoke-test
 * scanner drive it so they cannot drift apart. It mirrors the library's pruned-index regeneration in
 * state_history_log::check_index_on_init: entries must abut their successors and the chain is
 * followed strictly backward.
 *
 * `floor_block` is the lowest block the log can describe — slot 0 of its index. A decoded block
 * number below it cannot occur in a healthy log (nothing is ever written before the log's first
 * block), so check_entry rejects it and the walk stops as damage, never indexing below slot 0. No
 * upper bound is imposed: a deep fork switch in the retained tail legitimately leaves superseded
 * entries whose block number exceeds the head's (the library's "we see block 7 and 6 when reading"
 * case), so a caller that uses block_num as a slot index must range-check the high side itself.
 *
 * Returns std::nullopt once an entry for `begin_block` is visited, otherwise a chain_break naming the
 * offset and reason the walk stopped early. With `deep`, each entry's payload is also validated; a
 * payload-only failure does not stop the walk (the entry is still visited, with entry_check::ok
 * false) so the caller can report it as a damaged region.
 */
template<typename Visit>
std::optional<chain_break> walk_pruned_chain(window_reader& w, fc::random_access_file& log, uint64_t file_size,
                                             uint64_t last_entry_pos, uint32_t begin_block, uint32_t floor_block,
                                             bool deep, Visit&& visit) {
   uint64_t next_begin = file_size - sizeof(uint32_t); //the pruned block-count trailer occupies the final 4 bytes
   uint64_t pos        = last_entry_pos;
   while(true) {
      entry_check c = check_entry(w, log, pos, file_size, std::nullopt, floor_block, deep);
      if(!c.structurally_ok || c.end_pos != next_begin)
         return chain_break{pos, c.reason.empty() ? std::string("entry does not abut its successor") : c.reason};
      const uint32_t block_num = c.block_num;
      visit(pos, std::move(c));
      if(block_num == begin_block)
         return std::nullopt;
      if(pos < packed_header_size + trailer_size)
         return chain_break{pos, "ran out of entries before reaching block " + std::to_string(begin_block)};
      const uint64_t trailer_at = pos - trailer_size;
      next_begin                = pos;
      pos                       = parse_at<uint64_t>(w.view(trailer_at, trailer_size), trailer_size);
      if(pos + min_entry_size > next_begin)
         return chain_break{trailer_at, "position trailer points outside the file"};
   }
}

/**
 * Backward-walk a pruned log through its position-trailer chain, mirroring the library's index
 * regeneration: only the first-seen (i.e. latest-written) entry per block counts, and the walk ends
 * at the first entry seen for the begin block. Throws if the chain cannot be walked.
 */
computed_index compute_index_pruned(fc::random_access_file& log, uint64_t size, const endpoints& e,
                                    const std::filesystem::path& stem, const progress_func& progress) {
   EOS_ASSERT(e.valid_first && e.tail_ok, chain::plugin_exception,
              "pruned log ${stem}.log is damaged and cannot be repaired", ("stem", stem.string()));
   EOS_ASSERT(*e.pruned_count > 0 && *e.pruned_count <= e.last_block, chain::plugin_exception,
              "pruned log ${stem}.log has an implausible trailing block count ${pc}",
              ("stem", stem.string())("pc", *e.pruned_count));

   computed_index ci;
   ci.index_first = e.first_block;
   ci.first_block = e.last_block - *e.pruned_count + 1;
   ci.last_block  = e.last_block;
   EOS_ASSERT(ci.first_block >= ci.index_first, chain::plugin_exception,
              "pruned log ${stem}.log has a trailing block count ${pc} reaching before its first block ${if}",
              ("stem", stem.string())("pc", *e.pruned_count)("if", ci.index_first));
   grow_slots(ci.slots, ci.index_first, ci.last_block, stem);

   window_reader                    w(log, size);
   progress_ticker                  ticker(progress, size);
   const std::optional<chain_break> stop = walk_pruned_chain(
      w, log, size, e.last_entry_pos, ci.first_block, ci.index_first, false, [&](uint64_t pos, entry_check&& c) {
         //a deep fork switch in the retained tail can leave superseded entries above the head; the
         // library skips them when regenerating the index (read_block_num < _end_block), so do the same
         // here rather than index past the slot vector — they are valid data, not damage. The chain
         // walk's floor check already guarantees c.block_num >= ci.index_first.
         if(c.block_num <= ci.last_block && ci.slots[c.block_num - ci.index_first] == 0)
            ci.slots[c.block_num - ci.index_first] = pos;
         ticker.tick(size - pos);
      });
   EOS_ASSERT(!stop, chain::plugin_exception,
              "pruned log ${stem}.log is damaged at offset ${pos} (${why}) and cannot be repaired",
              ("stem", stem.string())("pos", stop ? stop->at_pos : 0)("why", stop ? stop->reason : std::string{}));
   return ci;
}

/// write a computed index to disk, replacing whatever was there
void write_index_file(const std::filesystem::path& index_path, const computed_index& ci) {
   fc::random_access_file index(index_path);
   index.resize(0);
   fc::random_access_file::write_datastream ds = index.write_ds(0);
   //slot values are written with fc::raw's little-endian encoding, matching the library's index format
   for(const uint64_t pos : ci.slots)
      fc::raw::pack(ds, pos);
   ds.flush();
}

/// open a log read-only with a friendlier error than the raw open failure
fc::random_access_file open_log_readonly(const std::filesystem::path& stem) {
   const std::filesystem::path lp = log_path_of(stem);
   EOS_ASSERT(std::filesystem::exists(lp), chain::plugin_exception, "${lp} does not exist", ("lp", lp.string()));
   return fc::random_access_file(lp, fc::random_access_file::read_only);
}

/**
 * True iff the entry at `pos` is a structurally valid ship header for exactly `block_num`. This is the
 * shared "does this slot really hold the block we asked for" check behind every index lookup. An
 * on-disk index can be stale, and a computed index legitimately carries zero slots for blocks a
 * (possibly damaged) log does not retain; a zero or otherwise wrong slot would unpack the header at
 * the wrong offset -- offset 0 is the log's first/stub header -- and silently misattribute a
 * neighbouring block's id. A slot is therefore trusted only after its entry is read back and confirmed
 * to decode to the requested block.
 */
bool slot_holds_block(fc::random_access_file& log, uint64_t log_size, uint64_t pos, uint32_t block_num) {
   if(pos + packed_header_size > log_size)
      return false;
   const log_header hdr = log.unpack_from<log_header>(pos);
   return is_ship(hdr.magic) && is_ship_supported_version(hdr.magic) &&
          chain::block_header::num_from_id(hdr.block_id) == block_num;
}

/// positions of blocks in a log, from its index file when that is usable, otherwise from a forward walk
struct position_lookup {
   position_lookup(fc::random_access_file& log, uint64_t log_size, const endpoints& e,
                   const std::filesystem::path& stem, const progress_func& progress)
      : log(log), log_size(log_size), stem(stem), first_block(e.first_block) {
      const std::filesystem::path ip            = index_path_of(stem);
      const uint64_t              expected_size = (uint64_t(e.last_block) - e.first_block + 1) * sizeof(uint64_t);
      if(std::filesystem::exists(ip) && std::filesystem::file_size(ip) == expected_size) {
         index.emplace(ip, fc::random_access_file::read_only);
         if(index->unpack_from<uint64_t>(expected_size - sizeof(uint64_t)) == e.last_entry_pos)
            return;
         index.reset();
      }
      computed    = compute_index_forward(log, log_size, stem, progress);
      first_block = computed->index_first;
   }

   /// position of the latest entry for block_num, validated to actually hold that block
   uint64_t entry_pos_of(uint32_t block_num) {
      uint64_t pos;
      if(index)
         pos = index->unpack_from<uint64_t>((uint64_t(block_num) - first_block) * sizeof(uint64_t));
      else
         pos = computed->slots[block_num - first_block];
      EOS_ASSERT(slot_holds_block(log, log_size, pos, block_num), chain::plugin_exception,
                 "index entry for block ${bn} of ${stem}.log does not point at that block; run make-index first",
                 ("bn", block_num)("stem", stem.string()));
      return pos;
   }

   /// one past the end of the latest entry for block_num (including its position trailer)
   uint64_t entry_end_of(uint32_t block_num) {
      const uint64_t   pos = entry_pos_of(block_num);
      const log_header hdr = log.unpack_from<log_header>(pos);
      const uint64_t   end = pos + packed_header_size + hdr.payload_size + trailer_size;
      EOS_ASSERT(end <= log_size, chain::plugin_exception,
                 "entry for block ${bn} of ${stem}.log extends past the end of the log",
                 ("bn", block_num)("stem", stem.string()));
      return end;
   }

   bool used_file_index() const { return !!index; }

   fc::random_access_file&               log;
   uint64_t                              log_size;
   std::filesystem::path                 stem;
   uint32_t                              first_block;
   std::optional<fc::random_access_file> index;    ///< fast path: shallow-validated on-disk index
   std::optional<computed_index>         computed; ///< fallback: in-memory forward walk
};

/// accumulates the index of a bundle being written by append_entries()
struct index_accumulator {
   uint32_t              first = 0;
   uint32_t              last  = 0;
   bool                  any   = false;
   std::vector<uint64_t> slots;
};

/**
 * Copy the entries occupying [begin, end) of src onto dst's write stream, which must be positioned
 * at dst_base, rewriting each entry's position trailer for its new offset and recording new index
 * slots in acc. Every entry is re-validated during the copy. prev_block carries the block-number
 * chain across consecutive calls (merging) and must start empty for a bundle's first range.
 */
void append_entries(fc::random_access_file& src, uint64_t src_size, uint64_t begin, uint64_t end,
                    fc::random_access_file::write_datastream& dst, uint64_t dst_base,
                    const std::filesystem::path& src_stem, const std::filesystem::path& dst_stem,
                    index_accumulator& acc, std::optional<uint32_t>& prev_block, progress_ticker& ticker) {
   window_reader w(src, src_size);
   uint64_t      pos = begin;
   while(pos < end) {
      const entry_check c = check_entry(w, src, pos, src_size, prev_block,
                                        acc.any ? std::optional<uint32_t>(acc.first) : std::nullopt, false);
      EOS_ASSERT(c.ok, chain::plugin_exception, "${ss}.log is damaged at offset ${pos} (${reason}); repair it first",
                 ("ss", src_stem.string())("pos", pos)("reason", c.reason));
      EOS_ASSERT(c.end_pos <= end, chain::plugin_exception,
                 "entry at offset ${pos} of ${ss}.log crosses the end of the requested range",
                 ("pos", pos)("ss", src_stem.string()));

      if(!acc.any) {
         acc.any   = true;
         acc.first = c.block_num;
      }
      const uint64_t dst_pos = dst_base + (pos - begin);
      grow_slots(acc.slots, acc.first, c.block_num, dst_stem);
      acc.slots[c.block_num - acc.first] = dst_pos;
      acc.last                           = c.block_num;

      //copy the header and payload bytes verbatim, then write the rebased position trailer
      uint64_t remaining = (c.end_pos - trailer_size) - pos;
      uint64_t at        = pos;
      while(remaining) {
         const size_t n = static_cast<size_t>(std::min<uint64_t>(remaining, w.buf.size()));
         dst.write(w.view(at, n), n);
         at += n;
         remaining -= n;
      }
      fc::raw::pack(dst, dst_pos);

      prev_block = c.block_num;
      pos        = c.end_pos;
      ticker.tick(dst_base + (pos - begin));
   }
   EOS_ASSERT(pos == end, chain::plugin_exception, "range of ${ss}.log does not end on an entry boundary",
              ("ss", src_stem.string()));
}

/// assert that neither file of a destination bundle exists yet
void assert_bundle_creatable(const std::filesystem::path& stem) {
   EOS_ASSERT(!std::filesystem::exists(log_path_of(stem)), chain::plugin_exception, "${path} already exists",
              ("path", log_path_of(stem).string()));
   EOS_ASSERT(!std::filesystem::exists(index_path_of(stem)), chain::plugin_exception, "${path} already exists",
              ("path", index_path_of(stem).string()));
}

/// finalize an index_accumulator into a computed_index (drops slots past the final head block)
computed_index finalize_accumulator(index_accumulator&& acc) {
   computed_index ci;
   ci.index_first = ci.first_block = acc.first;
   ci.last_block                   = acc.last;
   acc.slots.resize(uint64_t(acc.last) - acc.first + 1);
   ci.slots = std::move(acc.slots);
   return ci;
}

/// escape a literal string for interpolation into a std::regex pattern
std::string regex_escape(const std::string& s) {
   static const std::regex special(R"([.^$|()\[\]{}*+?\\])");
   return std::regex_replace(s, special, R"(\$&)");
}

/// remove both files of a partially written destination bundle after a failure
void remove_bundle(const std::filesystem::path& stem) noexcept {
   std::error_code ec;
   std::filesystem::remove(log_path_of(stem), ec);
   std::filesystem::remove(index_path_of(stem), ec);
}

/**
 * Create a fresh bundle at dst_stem from the entries occupying [begin, end) of the log at
 * src_stem, cleaning up the partial bundle if anything fails.
 *
 * @return (first block, last block) of the created bundle
 */
std::pair<uint32_t, uint32_t> extract_byte_range(const std::filesystem::path& src_stem, uint64_t begin, uint64_t end,
                                                 const std::filesystem::path& dst_stem,
                                                 const progress_func& progress) {
   assert_bundle_creatable(dst_stem);
   try {
      fc::random_access_file                   src = open_log_readonly(src_stem);
      fc::random_access_file                   dst(log_path_of(dst_stem));
      fc::random_access_file::write_datastream ds = dst.write_ds(0);
      index_accumulator                        acc;
      std::optional<uint32_t>                  prev;
      progress_ticker                          ticker(progress, end - begin);
      append_entries(src, src.size(), begin, end, ds, 0, src_stem, dst_stem, acc, prev, ticker);
      ds.flush();
      EOS_ASSERT(acc.any, chain::plugin_exception, "no entries found in the requested range of ${ss}.log",
                 ("ss", src_stem.string()));
      const uint32_t got_first = acc.first;
      const uint32_t got_last  = acc.last;
      write_index_file(index_path_of(dst_stem), finalize_accumulator(std::move(acc)));
      return {got_first, got_last};
   } catch(...) {
      remove_bundle(dst_stem);
      throw;
   }
}

} //anonymous namespace

std::filesystem::path normalize_stem(const std::filesystem::path& p) {
   const std::string ext = p.extension().string();
   if(ext == std::string(".").append(log_extension) || ext == std::string(".").append(index_extension)) {
      std::filesystem::path r = p;
      r.replace_extension();
      return r;
   }
   return p;
}

scan_result scan_log(const std::filesystem::path& stem_in, bool deep, const progress_func& progress) {
   const std::filesystem::path stem = normalize_stem(stem_in);
   fc::random_access_file      log  = open_log_readonly(stem);

   scan_result r;
   r.file_size = log.size();
   if(r.file_size == 0)
      return r;

   if(r.file_size < min_entry_size) {
      r.damaged_ranges.push_back({0, r.file_size, "file is too small to hold an entry"});
      return r;
   }

   //a first header without ship magic gets no special treatment: the forward scan below reports it
   // as damage and resynchronizes, which both diagnoses a destroyed first header and yields one
   // whole-file damaged range for a file that is not a ship log at all
   const log_header first = log.unpack_from<log_header>(0);
   if(is_ship(first.magic)) {
      EOS_ASSERT(is_ship_supported_version(first.magic), chain::plugin_exception, "${stem}.log has an unsupported version",
                 ("stem", stem.string()));
      r.version = get_ship_version(first.magic);
      r.pruned  = is_ship_log_pruned(first.magic);
   }

   window_reader   w(log, r.file_size);
   progress_ticker ticker(progress, r.file_size);
   range_builder   rb(r);

   if(r.pruned) {
      //pruned logs have a punched-out hole after the first header, so they are walked backward
      // through the position-trailer chain; entries the chain cannot reach are beyond what the
      // format can describe, which is also why the library refuses to repair a damaged pruned log
      r.pruned_block_count = log.unpack_from<uint32_t>(r.file_size - sizeof(uint32_t));
      const endpoints e    = probe_endpoints(log, r.file_size);
      if(!e.tail_ok || *r.pruned_block_count == 0 || *r.pruned_block_count > e.last_block) {
         r.damaged_ranges.push_back({0, r.file_size, "pruned log's trailing entry or block count is damaged"});
         return r;
      }
      const uint32_t begin_block = e.last_block - *r.pruned_block_count + 1;

      //collect entries newest to oldest (bounded by the log's retained-block count) via the shared
      // chain walk, then build ranges in file order. e.first_block is the index floor: a block number
      // below it is corruption, not a valid (superseded) entry, so the walk reports it as damage.
      struct seen_entry {
         uint64_t    pos;
         entry_check check;
      };
      std::vector<seen_entry>          entries;
      const std::optional<chain_break> stop = walk_pruned_chain(
         w, log, r.file_size, e.last_entry_pos, begin_block, e.first_block, deep,
         [&](uint64_t pos, entry_check&& c) {
            ++r.entries_scanned;
            if(deep)
               ++r.payloads_validated;
            entries.push_back({pos, std::move(c)});
            ticker.tick(r.file_size - pos);
         });
      if(stop)
         r.damaged_ranges.push_back(
            {0, entries.empty() ? r.file_size : entries.back().pos, "backward chain broken: " + stop->reason});

      for(auto it = entries.rbegin(); it != entries.rend(); ++it) {
         if(it->check.ok) {
            rb.good(it->check, it->pos);
         } else { //structurally fine, deep payload check failed
            rb.close();
            r.damaged_ranges.push_back({it->pos, it->check.end_pos, it->check.reason});
         }
      }
      rb.close();
      return r;
   }

   uint64_t                pos = 0;
   std::optional<uint32_t> prev_block, floor_block;
   while(pos + min_entry_size <= r.file_size) {
      entry_check c = check_entry(w, log, pos, r.file_size, prev_block, floor_block, deep);
      if(c.structurally_ok) {
         ++r.entries_scanned;
         if(deep)
            ++r.payloads_validated;
         if(pos == 0)
            floor_block = c.block_num;
      }
      if(c.ok) {
         rb.good(c, pos);
         prev_block = c.block_num;
         pos        = c.end_pos;
         ticker.tick(pos);
         continue;
      }
      if(c.structurally_ok) { //payload damage confined to one entry
         rb.close();
         r.damaged_ranges.push_back({pos, c.end_pos, c.reason});
         prev_block = c.block_num;
         pos        = c.end_pos;
         continue;
      }
      //structural damage: extent unknown, search for the next valid entry
      rb.close();
      const std::optional<uint64_t> next = find_next_entry(w, log, pos + 1, r.file_size, floor_block);
      r.damaged_ranges.push_back({pos, next.value_or(r.file_size), c.reason});
      if(!next) {
         pos = r.file_size;
         break;
      }
      pos = *next;
      prev_block.reset(); //fresh block-number baseline on the far side of the damage
   }
   if(pos < r.file_size) {
      rb.close();
      r.damaged_ranges.push_back({pos, r.file_size, "trailing bytes too small to hold an entry"});
   }
   rb.close();
   return r;
}

std::string_view to_string(index_status s) {
   switch(s) {
      case index_status::ok:          return "ok";
      case index_status::missing:     return "missing";
      case index_status::wrong_size:  return "wrong_size";
      case index_status::mismatched:  return "mismatched";
      case index_status::log_damaged: return "log_damaged";
   }
   return "unknown";
}

index_status check_index(const std::filesystem::path& stem_in, bool full, const progress_func& progress) {
   const std::filesystem::path stem = normalize_stem(stem_in);
   fc::random_access_file      log  = open_log_readonly(stem);
   const uint64_t              size = log.size();
   const std::filesystem::path ip   = index_path_of(stem);

   if(size == 0)
      return !std::filesystem::exists(ip) || std::filesystem::file_size(ip) == 0 ? index_status::ok
                                                                                 : index_status::wrong_size;

   const endpoints e = probe_endpoints(log, size);
   if(!e.valid_first || !e.tail_ok)
      return index_status::log_damaged;

   if(!std::filesystem::exists(ip))
      return index_status::missing;
   const uint64_t expected_size = (uint64_t(e.last_block) - e.first_block + 1) * sizeof(uint64_t);
   if(std::filesystem::file_size(ip) != expected_size)
      return index_status::wrong_size;

   fc::random_access_file index(ip, fc::random_access_file::read_only);
   if(index.unpack_from<uint64_t>(expected_size - sizeof(uint64_t)) != e.last_entry_pos)
      return index_status::mismatched;
   if(!full)
      return index_status::ok;

   computed_index ci;
   try {
      ci = e.pruned ? compute_index_pruned(log, size, e, stem, progress)
                    : compute_index_forward(log, size, stem, progress);
   } catch(const fc::exception&) {
      return index_status::log_damaged;
   }
   if(ci.slots.size() * sizeof(uint64_t) != expected_size)
      return index_status::mismatched;

   //pruning punches holes in the log but leaves the index alone, so slots below the first servable
   // block legitimately hold either stale positions (organic) or zeros (after a regeneration); only
   // the servable slots are comparable
   const uint64_t first_comparable = uint64_t(ci.first_block) - ci.index_first;
   window_reader  iw(index, expected_size, static_cast<size_t>(std::min<uint64_t>(default_window_size, expected_size)));
   for(uint64_t i = first_comparable; i < ci.slots.size(); ++i)
      if(parse_at<uint64_t>(iw.view(i * sizeof(uint64_t), sizeof(uint64_t)), sizeof(uint64_t)) != ci.slots[i])
         return index_status::mismatched;
   return index_status::ok;
}

std::pair<uint32_t, uint32_t> build_index(const std::filesystem::path& stem_in, const progress_func& progress) {
   const std::filesystem::path stem = normalize_stem(stem_in);
   fc::random_access_file      log  = open_log_readonly(stem);
   const uint64_t              size = log.size();

   if(size == 0) {
      fc::random_access_file index(index_path_of(stem));
      index.resize(0);
      return {0, 0};
   }

   const endpoints e = probe_endpoints(log, size);
   computed_index  ci;
   if(e.valid_first && e.pruned)
      ci = compute_index_pruned(log, size, e, stem, progress);
   else
      ci = compute_index_forward(log, size, stem, progress);
   write_index_file(index_path_of(stem), ci);
   return {ci.first_block, ci.last_block};
}

uint64_t truncate_log(const std::filesystem::path& stem_in, uint32_t last_block_to_keep,
                      const progress_func& progress) {
   const std::filesystem::path stem = normalize_stem(stem_in);
   fc::random_access_file      log  = open_log_readonly(stem);
   const uint64_t              size = log.size();
   const endpoints             e    = probe_endpoints(log, size);

   EOS_ASSERT(e.valid_first && e.tail_ok, chain::plugin_exception, "${stem}.log is damaged; run repair instead of trim",
              ("stem", stem.string()));
   EOS_ASSERT(!e.pruned, chain::plugin_exception, "${stem}.log is pruned; vacuum it first", ("stem", stem.string()));
   EOS_ASSERT(last_block_to_keep >= e.first_block && last_block_to_keep <= e.last_block, chain::plugin_exception,
              "block ${lbtk} is outside the ${fb}-${lb} range of ${stem}.log",
              ("lbtk", last_block_to_keep)("fb", e.first_block)("lb", e.last_block)("stem", stem.string()));
   if(last_block_to_keep == e.last_block)
      return 0;

   position_lookup lookup(log, size, e, stem, progress);
   const uint64_t  new_size = lookup.entry_end_of(last_block_to_keep);

   {
      fc::random_access_file rw_log(log_path_of(stem));
      rw_log.resize(new_size);
   }
   if(lookup.used_file_index()) {
      //the on-disk index was validated; kept entries' positions are unchanged, so shrinking suffices
      fc::random_access_file index(index_path_of(stem));
      index.resize((uint64_t(last_block_to_keep) - e.first_block + 1) * sizeof(uint64_t));
   } else {
      lookup.computed->slots.resize(uint64_t(last_block_to_keep) - lookup.computed->index_first + 1);
      lookup.computed->last_block = last_block_to_keep;
      write_index_file(index_path_of(stem), *lookup.computed);
   }
   return size - new_size;
}

uint64_t extract_blocks(const std::filesystem::path& src_stem_in, const std::filesystem::path& dst_stem_in,
                        uint32_t first_block, uint32_t last_block, const progress_func& progress) {
   const std::filesystem::path src_stem = normalize_stem(src_stem_in);
   const std::filesystem::path dst_stem = normalize_stem(dst_stem_in);
   EOS_ASSERT(first_block <= last_block, chain::plugin_exception, "first block ${fb} is after last block ${lb}",
              ("fb", first_block)("lb", last_block));

   fc::random_access_file src  = open_log_readonly(src_stem);
   const uint64_t         size = src.size();
   const endpoints        e    = probe_endpoints(src, size);
   EOS_ASSERT(e.valid_first && e.tail_ok, chain::plugin_exception, "${ss}.log is damaged; repair it first",
              ("ss", src_stem.string()));
   EOS_ASSERT(!e.pruned, chain::plugin_exception, "${ss}.log is pruned; vacuum it first", ("ss", src_stem.string()));
   EOS_ASSERT(first_block >= e.first_block && last_block <= e.last_block, chain::plugin_exception,
              "blocks ${fb}-${lb} are outside the ${fb1}-${lb1} range of ${ss}.log",
              ("fb", first_block)("lb", last_block)("fb1", e.first_block)("lb1", e.last_block)("ss", src_stem.string()));

   position_lookup lookup(src, size, e, src_stem, progress);
   const uint64_t  begin_pos = lookup.entry_pos_of(first_block);
   const uint64_t  end_pos   = lookup.entry_end_of(last_block);
   EOS_ASSERT(begin_pos < end_pos, chain::plugin_exception,
              "entry positions of blocks ${fb} and ${lb} of ${ss}.log are out of order; run make-index first",
              ("fb", first_block)("lb", last_block)("ss", src_stem.string()));

   const auto [got_first, got_last] = extract_byte_range(src_stem, begin_pos, end_pos, dst_stem, progress);
   EOS_ASSERT(got_first == first_block && got_last == last_block, chain::plugin_exception,
              "extracted range ${gf}-${gl} does not match requested ${fb}-${lb}",
              ("gf", got_first)("gl", got_last)("fb", first_block)("lb", last_block));
   return end_pos - begin_pos;
}

uint64_t trim_front(const std::filesystem::path& stem_in, uint32_t first_block_to_keep,
                    const progress_func& progress) {
   const std::filesystem::path stem = normalize_stem(stem_in);

   uint32_t last_block;
   uint64_t old_size;
   {
      fc::random_access_file log = open_log_readonly(stem);
      old_size                   = log.size();
      const endpoints e          = probe_endpoints(log, old_size);
      EOS_ASSERT(e.valid_first && e.tail_ok, chain::plugin_exception, "${stem}.log is damaged; run repair instead of trim",
                 ("stem", stem.string()));
      EOS_ASSERT(!e.pruned, chain::plugin_exception, "${stem}.log is pruned; vacuum it first", ("stem", stem.string()));
      EOS_ASSERT(first_block_to_keep >= e.first_block && first_block_to_keep <= e.last_block, chain::plugin_exception,
                 "block ${fbtk} is outside the ${fb}-${lb} range of ${stem}.log",
                 ("fbtk", first_block_to_keep)("fb", e.first_block)("lb", e.last_block)("stem", stem.string()));
      if(first_block_to_keep == e.first_block)
         return 0;
      last_block = e.last_block;
   }

   std::filesystem::path tmp_stem = stem;
   tmp_stem += std::string(tmp_suffix);
   remove_bundle(tmp_stem);

   const uint64_t bytes = extract_blocks(stem, tmp_stem, first_block_to_keep, last_block, progress);

   //the .log and .index renames are not atomic as a pair; a crash between them leaves the new log
   // beside the old index, but state_history_log regenerates a wrong-sized index on its next open,
   // so the bundle self-heals
   std::filesystem::rename(log_path_of(tmp_stem), log_path_of(stem));
   std::filesystem::rename(index_path_of(tmp_stem), index_path_of(stem));
   return old_size - bytes;
}

std::vector<std::filesystem::path> split_log(const std::filesystem::path& src_stem_in,
                                             const std::filesystem::path& dst_dir, uint32_t stride,
                                             const progress_func& progress) {
   const std::filesystem::path src_stem = normalize_stem(src_stem_in);
   EOS_ASSERT(stride > 0, chain::plugin_exception, "stride must be non-zero");

   fc::random_access_file src  = open_log_readonly(src_stem);
   const uint64_t         size = src.size();
   const endpoints        e    = probe_endpoints(src, size);
   EOS_ASSERT(e.valid_first && e.tail_ok, chain::plugin_exception, "${ss}.log is damaged; repair it first",
              ("ss", src_stem.string()));
   EOS_ASSERT(!e.pruned, chain::plugin_exception, "${ss}.log is pruned; vacuum it first", ("ss", src_stem.string()));
   //each output bundle is extracted separately, so an unusable index would mean one full log walk
   // per bundle; require a healthy index up front instead
   EOS_ASSERT(summarize_log(src_stem).index == index_status::ok, chain::plugin_exception,
              "${ss}.index is missing or unusable; run make-index first", ("ss", src_stem.string()));

   //a retained-style "-N-M" suffix on the source must not leak into output names
   const std::string base = std::regex_replace(src_stem.filename().string(), std::regex(R"(-\d+-\d+$)"), "");
   std::filesystem::create_directories(dst_dir);

   std::vector<std::filesystem::path> created;
   try {
      uint32_t b = e.first_block;
      while(true) {
         //log rotation archives the head right after writing a block divisible by the stride, so
         // every retained bundle ends on such a block; the remainder past the last boundary stays
         // the head log (empty when the source ends exactly on a boundary, just as after a rotation)
         const uint32_t boundary = b % stride == 0 ? b : b - (b % stride) + stride;
         if(boundary > e.last_block)
            break;
         const std::filesystem::path dst = dst_dir / (base + "-" + std::to_string(b) + "-" + std::to_string(boundary));
         extract_blocks(src_stem, dst, b, boundary, progress);
         created.push_back(dst);
         b = boundary + 1;
         if(boundary == e.last_block)
            break;
      }

      const std::filesystem::path head = dst_dir / base;
      if(b <= e.last_block) {
         extract_blocks(src_stem, head, b, e.last_block, progress); //self-cleans its partial bundle on throw
         created.push_back(head);
      } else {
         //register the head before creating its files so a throw during the resizes still cleans the
         // partial bundle; the order also keeps assert_bundle_creatable from ever marking a
         // pre-existing file (which we must not delete) for cleanup
         assert_bundle_creatable(head);
         created.push_back(head);
         fc::random_access_file(log_path_of(head)).resize(0);
         fc::random_access_file(index_path_of(head)).resize(0);
      }
   } catch(...) {
      for(const std::filesystem::path& stem : created)
         remove_bundle(stem);
      throw;
   }
   return created;
}

std::pair<uint32_t, uint32_t> merge_logs(const std::filesystem::path& src_dir, const std::string& log_name,
                                         const std::filesystem::path& dst_dir, const progress_func& progress) {
   EOS_ASSERT(std::filesystem::is_directory(src_dir), chain::plugin_exception, "${sd} is not a directory",
              ("sd", src_dir.string()));

   struct source {
      uint32_t              first;
      uint32_t              last;
      std::filesystem::path stem;
   };
   std::vector<source> sources;
   const std::regex    retained_re("^" + regex_escape(log_name) + R"(-\d+-\d+\.log$)");
   for(const std::filesystem::directory_entry& de : std::filesystem::directory_iterator(src_dir)) {
      if(!de.is_regular_file() || !std::regex_search(de.path().filename().string(), retained_re))
         continue;
      const std::filesystem::path stem = normalize_stem(de.path());
      fc::random_access_file      log  = open_log_readonly(stem);
      const endpoints             e    = probe_endpoints(log, log.size());
      EOS_ASSERT(e.valid_first && e.tail_ok, chain::plugin_exception, "${stem}.log is damaged; repair it first",
                 ("stem", stem.string()));
      EOS_ASSERT(!e.pruned, chain::plugin_exception, "${stem}.log is pruned and cannot be merged", ("stem", stem.string()));
      sources.push_back({e.first_block, e.last_block, stem});
   }
   EOS_ASSERT(!sources.empty(), chain::plugin_exception, "no ${ln}-<first>-<last>.log bundles found in ${sd}",
              ("ln", log_name)("sd", src_dir.string()));

   std::sort(sources.begin(), sources.end(), [](const source& a, const source& b) { return a.first < b.first; });
   for(size_t i = 1; i < sources.size(); ++i)
      EOS_ASSERT(sources[i].first == sources[i - 1].last + 1, chain::plugin_exception,
                 "${ps}.log ends at block ${pl} but ${ns}.log begins at block ${nf}; bundles must be contiguous to merge",
                 ("ps", sources[i - 1].stem.string())("pl", sources[i - 1].last)
                 ("ns", sources[i].stem.string())("nf", sources[i].first));

   std::filesystem::create_directories(dst_dir);
   const std::filesystem::path dst_stem = dst_dir / log_name;
   assert_bundle_creatable(dst_stem);

   uint64_t total_bytes = 0;
   for(const source& s : sources)
      total_bytes += std::filesystem::file_size(log_path_of(s.stem));

   try {
      fc::random_access_file                   dst(log_path_of(dst_stem));
      fc::random_access_file::write_datastream ds = dst.write_ds(0);
      index_accumulator                        acc;
      std::optional<uint32_t>                  prev;
      progress_ticker                          ticker(progress, total_bytes);
      uint64_t                                 dst_base = 0;
      for(const source& s : sources) {
         fc::random_access_file log  = open_log_readonly(s.stem);
         const uint64_t         size = log.size();
         append_entries(log, size, 0, size, ds, dst_base, s.stem, dst_stem, acc, prev, ticker);
         dst_base += size;
      }
      ds.flush();
      write_index_file(index_path_of(dst_stem), finalize_accumulator(std::move(acc)));
   } catch(...) {
      remove_bundle(dst_stem);
      throw;
   }
   return {sources.front().first, sources.back().last};
}

repair_report repair_log(const std::filesystem::path& stem_in, repair_mode mode, bool dry_run, bool deep,
                         const std::optional<std::filesystem::path>& dst_stem_in, const progress_func& progress) {
   const std::filesystem::path stem = normalize_stem(stem_in);
   repair_report               rep;
   rep.scan = scan_log(stem, deep, progress);

   EOS_ASSERT(!rep.scan.pruned || rep.scan.intact(), chain::plugin_exception,
              "${stem}.log is pruned and damaged; the format cannot locate entries on the far side of a hole, so it "
              "cannot be repaired", ("stem", stem.string()));

   if(rep.scan.intact()) {
      if(!rep.scan.valid_ranges.empty()) {
         rep.first_block = rep.scan.valid_ranges.front().first_block;
         rep.last_block  = rep.scan.valid_ranges.front().last_block;
      }
      rep.bytes_kept = rep.scan.file_size;
      //the scan says nothing about the index; judge it cheaply first and verify slot-by-slot only
      // when the cheap checks pass. The full check (and a rebuild, if needed) each forward-walk the
      // log again, so a healthy bundle is walked twice and a same-size-but-wrong index thrice; that
      // redundancy is accepted because repair is an offline, infrequent operation
      index_status st = check_index(stem, false);
      if(st == index_status::ok)
         st = check_index(stem, true, progress);
      if(st != index_status::ok) {
         rep.index_rebuilt = true;
         if(!dry_run) {
            build_index(stem, progress);
            rep.acted = true;
         }
      }
      return rep;
   }

   EOS_ASSERT(!rep.scan.valid_ranges.empty(), chain::plugin_exception, "${stem}.log contains no salvageable entries",
              ("stem", stem.string()));

   if(mode == repair_mode::truncate) {
      EOS_ASSERT(!dst_stem_in, chain::plugin_exception, "an output location only applies to keep-tail repairs");
      const entry_range& r = rep.scan.valid_ranges.front();
      EOS_ASSERT(r.begin_pos == 0, chain::plugin_exception,
                 "${stem}.log is damaged from its very start so there is no valid prefix to keep; consider a keep-tail "
                 "repair", ("stem", stem.string()));
      rep.first_block     = r.first_block;
      rep.last_block      = r.last_block;
      rep.bytes_kept      = r.end_pos;
      rep.bytes_discarded = rep.scan.file_size - r.end_pos;
      rep.index_rebuilt   = true;
      if(!dry_run) {
         {
            fc::random_access_file log(log_path_of(stem));
            log.resize(r.end_pos);
         }
         build_index(stem, progress);
         rep.acted = true;
      }
      return rep;
   }

   //keep-tail: salvage the last valid range into a fresh bundle
   const entry_range& r = rep.scan.valid_ranges.back();
   rep.first_block      = r.canonical_first_block;
   rep.last_block       = r.last_block;
   rep.bytes_kept       = r.end_pos - r.canonical_begin_pos;
   rep.bytes_discarded  = rep.scan.file_size - rep.bytes_kept;
   rep.index_rebuilt    = true;
   if(dry_run)
      return rep;

   std::filesystem::path dst_stem;
   if(dst_stem_in) {
      dst_stem = normalize_stem(*dst_stem_in);
   } else {
      dst_stem = stem;
      dst_stem += std::string(tmp_suffix);
      remove_bundle(dst_stem);
   }
   const auto [got_first, got_last] =
      extract_byte_range(stem, r.canonical_begin_pos, r.end_pos, dst_stem, progress);
   EOS_ASSERT(got_first == rep.first_block && got_last == rep.last_block, chain::plugin_exception,
              "salvaged range ${gf}-${gl} does not match the scan's ${fb}-${lb}",
              ("gf", got_first)("gl", got_last)("fb", rep.first_block)("lb", rep.last_block));
   if(!dst_stem_in) {
      //as in trim_front, the two renames are not atomic as a pair; a crash between them leaves a
      // wrong-sized index that state_history_log regenerates on next open, so the bundle self-heals
      std::filesystem::rename(log_path_of(dst_stem), log_path_of(stem));
      std::filesystem::rename(index_path_of(dst_stem), index_path_of(stem));
   }
   rep.acted = true;
   return rep;
}

log_summary summarize_log(const std::filesystem::path& stem_in) {
   const std::filesystem::path stem = normalize_stem(stem_in);
   fc::random_access_file      log  = open_log_readonly(stem);

   log_summary s;
   s.log_size = log.size();

   const endpoints e    = probe_endpoints(log, s.log_size);
   s.valid_first_header = e.valid_first;
   s.version            = e.version;
   s.pruned             = e.pruned;
   s.pruned_block_count = e.pruned_count;
   s.first_block        = e.first_block;
   s.tail_ok            = e.tail_ok;
   s.last_block         = e.last_block;
   if(e.valid_first)
      s.first_block_id = e.first_id;
   if(e.tail_ok)
      s.last_block_id = e.last_id;

   const std::filesystem::path ip = index_path_of(stem);
   if(std::filesystem::exists(ip))
      s.index_size = std::filesystem::file_size(ip);

   if(s.log_size == 0)
      s.index = (!std::filesystem::exists(ip) || s.index_size == 0) ? index_status::ok : index_status::wrong_size;
   else if(!e.valid_first || !e.tail_ok)
      s.index = index_status::log_damaged;
   else if(!std::filesystem::exists(ip))
      s.index = index_status::missing;
   else if(s.index_size != (uint64_t(e.last_block) - e.first_block + 1) * sizeof(uint64_t))
      s.index = index_status::wrong_size;
   else {
      fc::random_access_file index(ip, fc::random_access_file::read_only);
      s.index = index.unpack_from<uint64_t>(s.index_size - sizeof(uint64_t)) == e.last_entry_pos
                   ? index_status::ok
                   : index_status::mismatched;
   }
   return s;
}

std::optional<chain::block_id_type> find_block_id(const std::filesystem::path& stem_in, uint32_t block_num,
                                                  const progress_func& progress) {
   const std::filesystem::path stem = normalize_stem(stem_in);
   fc::random_access_file      log  = open_log_readonly(stem);
   const uint64_t              size = log.size();
   if(size == 0)
      return std::nullopt;

   const endpoints e = probe_endpoints(log, size);
   EOS_ASSERT(e.valid_first && e.tail_ok, chain::plugin_exception,
              "${stem}.log is damaged; run smoke-test to map the damage or repair it first", ("stem", stem.string()));

   uint32_t first_servable = e.first_block;
   if(e.pruned) {
      EOS_ASSERT(*e.pruned_count > 0 && *e.pruned_count <= e.last_block, chain::plugin_exception,
                 "pruned log ${stem}.log has an implausible trailing block count ${pc}",
                 ("stem", stem.string())("pc", *e.pruned_count));
      first_servable = e.last_block - *e.pruned_count + 1;
   }
   if(block_num < first_servable || block_num > e.last_block)
      return std::nullopt;

   //index fast path: the same shallow validation state_history_log applies on open, plus a check
   // that the slot's entry really holds the requested block; any disagreement falls through to
   // walking the log so a stale index cannot misattribute an id
   const std::filesystem::path ip            = index_path_of(stem);
   const uint64_t              expected_size = (uint64_t(e.last_block) - e.first_block + 1) * sizeof(uint64_t);
   if(std::filesystem::exists(ip) && std::filesystem::file_size(ip) == expected_size) {
      fc::random_access_file index(ip, fc::random_access_file::read_only);
      if(index.unpack_from<uint64_t>(expected_size - sizeof(uint64_t)) == e.last_entry_pos) {
         const uint64_t pos = index.unpack_from<uint64_t>((uint64_t(block_num) - e.first_block) * sizeof(uint64_t));
         if(slot_holds_block(log, size, pos, block_num))
            return log.unpack_from<log_header>(pos).block_id;
      }
   }

   //fallback: the on-disk index was missing or untrusted, so walk the log to recompute the slot. A
   // computed slot can still be zero for a block the log fails to retain inside its servable range (a
   // missing or duplicated entry in a damaged retained chain); validate that it points at the requested
   // block before trusting it, exactly as the fast path above does, so a bad slot reports damage rather
   // than unpacking offset 0's stub header and returning the wrong block's id.
   const computed_index ci  = e.pruned ? compute_index_pruned(log, size, e, stem, progress)
                                       : compute_index_forward(log, size, stem, progress);
   const uint64_t       pos = ci.slots[uint64_t(block_num) - ci.index_first];
   EOS_ASSERT(slot_holds_block(log, size, pos, block_num), chain::plugin_exception,
              "${stem}.log is damaged: block ${bn} is within its servable ${fs}-${lb} range but has no valid entry",
              ("stem", stem.string())("bn", block_num)("fs", first_servable)("lb", e.last_block));
   return log.unpack_from<log_header>(pos).block_id;
}

} // namespace eosio::state_history::log_utils
