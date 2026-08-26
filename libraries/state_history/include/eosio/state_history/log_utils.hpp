#pragma once

#include <eosio/chain/types.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/**
 * Offline inspection and repair utilities for state history (SHiP) log bundles.
 *
 * A "bundle" is a `<stem>.log` / `<stem>.index` file pair as written by state_history_log
 * (e.g. `trace_history.log` + `trace_history.index`, or a retained `trace_history-2-1000.log`
 * pair produced by log rotation).
 *
 * Unlike state_history_log -- whose constructor auto-repairs (truncates a corrupt tail,
 * regenerates a bad index, vacuums a pruned log) -- everything here that promises to be
 * read-only really is read-only, so a damaged log can be inspected before deciding how to
 * act on it. All functions take the bundle's *stem* path; a trailing ".log" or ".index"
 * extension is tolerated and stripped.
 */
namespace eosio::state_history::log_utils {

/// Periodic progress callback for long-running operations: (bytes_processed, bytes_total).
using progress_func = std::function<void(uint64_t, uint64_t)>;

/// Strip a trailing ".log" or ".index" extension so users may pass either the stem or a file path.
std::filesystem::path normalize_stem(const std::filesystem::path& p);

/// A contiguous run of structurally valid entries found by scan_log().
struct entry_range {
   uint32_t first_block = 0; ///< block number of the run's first entry
   uint32_t last_block  = 0; ///< block number of the run's last entry
   uint64_t begin_pos   = 0; ///< file offset of the first entry's header
   uint64_t end_pos     = 0; ///< one past the last byte of the run (end of the last entry's position trailer)
   uint64_t entry_count = 0; ///< entries in the run, including entries superseded by fork switches

   /// Earliest entry in the run not later superseded by a fork switch. A run normally starts at
   /// such an entry, but when a scan resynchronizes inside a cluster of fork-overwritten blocks it
   /// can begin at an entry a later fork switch replaced; a bundle salvaged from this run must
   /// start here instead so its first entry's block number is a floor for everything after it,
   /// which is what state_history_log requires of a log's first entry.
   uint64_t canonical_begin_pos   = 0;
   uint32_t canonical_first_block = 0; ///< block number of the entry at canonical_begin_pos
};

/// A byte range that failed validation during scan_log().
struct damaged_range {
   uint64_t    begin_pos = 0; ///< file offset where validation first failed
   uint64_t    end_pos   = 0; ///< file offset where the next valid entry begins (or the file size)
   std::string reason;        ///< description of the first validation failure in this range
};

/// Result of a full scan of one ship log file.
struct scan_result {
   uint64_t                file_size = 0;
   uint16_t                version   = 0;     ///< ship format version from the first header; meaningful only when
                                              ///< at least one valid_range exists (stays 0 -- itself a valid version
                                              ///< -- for a file whose first header is not a ship header)
   bool                    pruned    = false; ///< first header carries the pruned-log feature flag
   std::optional<uint32_t> pruned_block_count; ///< trailing block count, present only for pruned logs
   uint64_t                entries_scanned    = 0;
   uint64_t                payloads_validated = 0; ///< number of payloads decompressed (deep scans only)

   std::vector<entry_range>   valid_ranges;
   std::vector<damaged_range> damaged_ranges;

   /// True when the whole file is one valid run of entries (or the file is empty).
   bool intact() const { return damaged_ranges.empty() && valid_ranges.size() <= 1; }
};

/**
 * Read-only structural scan of a ship log.
 *
 * Walks the entry chain from the start of the file validating each entry's header magic, version,
 * payload bounds, block-number monotonicity, and position trailer. After a validation failure the
 * scanner searches forward for the next valid entry, so a single scan maps every undamaged region
 * of the file, not just the prefix.
 *
 * With @p deep set, every entry's compressed payload is additionally decompressed (zlib's adler32
 * makes this detect payload bit-rot that the structural walk cannot see) and, for entries that
 * record an uncompressed size, the decompressed size is checked against it. A payload failure in a
 * structurally valid entry is reported as a damaged range covering exactly that entry.
 *
 * Pruned logs are scanned backward through the position-trailer chain (the punched-out hole after
 * the first header makes a forward walk meaningless); @p deep applies to the entries that remain.
 *
 * @throws chain::plugin_exception if the file cannot be read or its version is unsupported
 */
scan_result scan_log(const std::filesystem::path& stem, bool deep, const progress_func& progress = {});

/// Index health, as judged against the log it belongs to. See to_string() for a human-readable
/// spelling.
enum class index_status {
   ok,         ///< present, expected size, and consistent with the log
   missing,    ///< no index file exists
   wrong_size, ///< size does not match the log's block range
   mismatched, ///< right size but at least one position disagrees with the log
   log_damaged ///< the log itself is damaged, so the index cannot be judged
};

/// Human-readable spelling of an index_status, for reports and error messages.
std::string_view to_string(index_status s);

/**
 * Check a bundle's index against its log.
 *
 * The shallow check (@p full = false) mirrors state_history_log's open-time validation: the index
 * must exist, have the size implied by the log's first and last entries, and its final position
 * must point at the log's last entry. The full check forward-walks the entire log and verifies
 * every index slot points at the latest entry written for its block. Read-only.
 *
 * @throws chain::plugin_exception if the log's first header is unreadable or of unsupported version
 */
index_status check_index(const std::filesystem::path& stem, bool full, const progress_func& progress = {});

/**
 * Build (or rebuild) a bundle's index from its log, replacing any existing index file.
 *
 * Equivalent to the regeneration state_history_log performs when it sees a bad index, but
 * standalone, and implemented as a sequential forward walk for non-pruned logs (a backward
 * trailer-chain walk, matching the library, for pruned ones). The log must be undamaged.
 *
 * @return the (first, last) block range the index covers
 * @throws chain::plugin_exception if the log is damaged (run repair_log first)
 */
std::pair<uint32_t, uint32_t> build_index(const std::filesystem::path& stem, const progress_func& progress = {});

/**
 * Trim the end of a bundle in place so that @p last_block_to_keep is its final block.
 *
 * The log file is truncated at the end of the latest entry written for @p last_block_to_keep and
 * the index is shrunk (or rebuilt when it was unusable) to match. Refuses pruned logs. The kept
 * prefix is not re-validated when the existing index is usable -- run smoke-test for that.
 *
 * @return bytes removed from the log file
 * @throws chain::plugin_exception if the block is outside the log's range or the log's endpoints
 *         are damaged
 */
uint64_t truncate_log(const std::filesystem::path& stem, uint32_t last_block_to_keep,
                      const progress_func& progress = {});

/**
 * Copy blocks [@p first_block, @p last_block] of @p src_stem into a freshly created bundle at
 * @p dst_stem, rewriting each copied entry's position trailer for its new offset and building the
 * new bundle's index. The source is not modified. Refuses pruned logs; the destination files must
 * not already exist.
 *
 * @return the number of log bytes copied
 * @throws chain::plugin_exception on range errors, damage, or pre-existing destination files
 */
uint64_t extract_blocks(const std::filesystem::path& src_stem, const std::filesystem::path& dst_stem,
                        uint32_t first_block, uint32_t last_block, const progress_func& progress = {});

/**
 * Trim the front of a bundle in place so that @p first_block_to_keep is its first block.
 *
 * Implemented as extract_blocks() into temporary files in the bundle's directory followed by an
 * atomic rename over the originals, so roughly the retained tail's size in free disk space is
 * required. Refuses pruned logs (vacuum them first).
 *
 * @return bytes removed from the log file
 * @throws chain::plugin_exception if the block is outside the log's range or the log is damaged
 */
uint64_t trim_front(const std::filesystem::path& stem, uint32_t first_block_to_keep,
                    const progress_func& progress = {});

/**
 * Split a bundle into retained-style bundles named `<stem>-<first>-<last>` in @p dst_dir, plus a
 * head bundle `<stem>` holding the remainder, mirroring the boundaries log rotation would have
 * produced: every output bundle except the head ends on a multiple of @p stride. Any `-N-M`
 * suffix already on the source stem is dropped when naming outputs. The source is not modified.
 *
 * @return the stems (without extension) of every bundle created, in block order, head last
 * @throws chain::plugin_exception if the log is damaged, pruned, or empty, or outputs already exist
 */
std::vector<std::filesystem::path> split_log(const std::filesystem::path& src_stem,
                                             const std::filesystem::path& dst_dir, uint32_t stride,
                                             const progress_func& progress = {});

/**
 * Merge every retained bundle in @p src_dir matching `<log_name>-N-M` into a single head-style
 * bundle `<log_name>` in @p dst_dir. The sources' block ranges (read from file content, not file
 * names) must be contiguous. Sources are not modified.
 *
 * @return the (first, last) block range of the merged bundle
 * @throws chain::plugin_exception if no bundles match, ranges have gaps or overlap, a source is
 *         damaged, or the destination files already exist
 */
std::pair<uint32_t, uint32_t> merge_logs(const std::filesystem::path& src_dir, const std::string& log_name,
                                         const std::filesystem::path& dst_dir, const progress_func& progress = {});

/// What repair_log() should do with the undamaged data it finds.
enum class repair_mode {
   truncate, ///< keep the valid prefix: truncate the log at the first damage, like nodeos's auto-recovery
   keep_tail ///< keep the last valid range instead: salvage recent history when damage is early in the file
};

/// Outcome of repair_log().
struct repair_report {
   scan_result scan;            ///< the damage map the decision was based on
   bool        acted = false;   ///< true when files were modified (always false for dry runs)
   bool        index_rebuilt = false; ///< true when (re)building the index was part of the action
   uint32_t    first_block = 0; ///< first block of the resulting (or would-be resulting) bundle
   uint32_t    last_block  = 0; ///< last block of the resulting (or would-be resulting) bundle
   uint64_t    bytes_kept      = 0;
   uint64_t    bytes_discarded = 0;
};

/**
 * Repair a damaged bundle, or fix up its index when the log itself is intact.
 *
 * The log is scanned first (see scan_log(); @p deep extends the scan to payloads). Then:
 *  - intact log, healthy index: nothing to do.
 *  - intact log, missing/bad index: the index is rebuilt.
 *  - damaged log, repair_mode::truncate: the log is truncated at the end of the valid range that
 *    starts at offset 0 and the index is rebuilt -- the offline equivalent of nodeos's automatic
 *    tail recovery. Fails if the damage starts at offset 0 (nothing to keep; consider keep_tail).
 *  - damaged log, repair_mode::keep_tail: the last valid range is copied into a fresh bundle
 *    (trailers rewritten, index built) which replaces the original via rename, or is written to
 *    @p dst_stem when given, leaving the original untouched. The copy begins at the range's
 *    canonical start: the earliest entry not later superseded by a fork switch, so the resulting
 *    bundle is exactly what state_history_log expects.
 *
 * With @p dry_run the damage map and would-be outcome are reported but nothing is written.
 * Pruned logs are refused: the format cannot locate entries past damage in a log with a hole.
 *
 * @throws chain::plugin_exception if the log is pruned, unreadable, or has nothing salvageable
 */
repair_report repair_log(const std::filesystem::path& stem, repair_mode mode, bool dry_run, bool deep,
                         const std::optional<std::filesystem::path>& dst_stem = std::nullopt,
                         const progress_func& progress = {});

/// Endpoint-only inspection of one bundle, cheap enough for an `info` listing. Read-only.
struct log_summary {
   uint64_t                log_size = 0;
   bool                    valid_first_header = false; ///< first header parses with ship magic + supported version
   uint16_t                version            = 0;
   bool                    pruned             = false;
   std::optional<uint32_t> pruned_block_count;
   uint32_t                first_block = 0; ///< from the first header (valid_first_header only)
   bool                    tail_ok     = false; ///< trailing position trailer leads to a coherent last entry
   uint32_t                last_block  = 0;     ///< from the last entry (tail_ok only)
   uint64_t                index_size  = 0;     ///< bytes; 0 when the index file is missing
   index_status            index       = index_status::missing; ///< shallow assessment only

   std::optional<chain::block_id_type> first_block_id; ///< id recorded for first_block (valid_first_header only)
   std::optional<chain::block_id_type> last_block_id;  ///< id recorded for last_block (tail_ok only)
};

/**
 * Summarize a bundle by examining only its endpoints: first header, trailing position trailer, and
 * index size/final slot. Damage in the middle of the file is not detected -- use scan_log() for
 * that. Never throws on corruption; problems are reported through the summary's flags.
 */
log_summary summarize_log(const std::filesystem::path& stem);

/**
 * Read the block id the log records for @p block_num. The on-disk index is used only after it
 * passes the same shallow checks state_history_log applies on open AND its slot's entry is
 * verified to actually hold @p block_num; on any disagreement the answer comes from a read-only
 * walk of the log instead, so a stale or corrupt index can never misattribute an id. When a fork
 * switch left multiple entries for @p block_num the id of the latest-written (canonical) entry is
 * returned, matching what the library serves. Read-only.
 *
 * This is the tool for diagnosing nodeos's "missed a fork change" error: it prints which history
 * the log actually recorded so it can be compared against the block log or another node.
 *
 * @return the recorded id, or nullopt when the log does not contain @p block_num (outside its
 *         block range, or pruned away)
 * @throws chain::plugin_exception if the log's endpoints are damaged (run repair first)
 */
std::optional<chain::block_id_type> find_block_id(const std::filesystem::path& stem, uint32_t block_num,
                                                  const progress_func& progress = {});

} // namespace eosio::state_history::log_utils
