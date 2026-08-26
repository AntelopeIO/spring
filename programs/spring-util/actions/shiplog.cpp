#include "shiplog.hpp"

#include <eosio/state_history/log.hpp>
#include <eosio/state_history/log_utils.hpp>

#include <fc/log/logger.hpp>


#include <algorithm>
#include <iostream>

using namespace eosio::chain;
namespace log_utils = eosio::state_history::log_utils;

namespace {

/// rate-limited progress reporting to the log so multi-minute scans/copies are visibly alive
void report_progress(uint64_t done, uint64_t total) {
   ilog("processed ${done} of ${total} bytes (${pct}%)",
        ("done", done)("total", total)("pct", total ? done * 100 / total : 100));
}

/// one line per valid/damaged region of a scan, in file order
void print_scan(const log_utils::scan_result& scan) {
   std::cout << "  entries scanned: " << scan.entries_scanned;
   if(scan.payloads_validated)
      std::cout << ", payloads validated: " << scan.payloads_validated;
   std::cout << "\n";

   //interleave the two range lists by file offset so the report reads start-to-end
   size_t vi = 0, di = 0;
   while(vi < scan.valid_ranges.size() || di < scan.damaged_ranges.size()) {
      const bool take_valid =
         vi < scan.valid_ranges.size() &&
         (di >= scan.damaged_ranges.size() || scan.valid_ranges[vi].begin_pos < scan.damaged_ranges[di].begin_pos);
      if(take_valid) {
         const log_utils::entry_range& r = scan.valid_ranges[vi++];
         std::cout << "  valid:   blocks " << r.first_block << "-" << r.last_block << " (" << r.entry_count
                   << " entries, bytes " << r.begin_pos << "-" << r.end_pos << ")";
         if(r.canonical_first_block != r.first_block)
            std::cout << " [salvageable from block " << r.canonical_first_block << "]";
         std::cout << "\n";
      } else {
         const log_utils::damaged_range& d = scan.damaged_ranges[di++];
         std::cout << "  DAMAGED: bytes " << d.begin_pos << "-" << d.end_pos << ": " << d.reason << "\n";
      }
   }
}

} // namespace

void shiplog_actions::setup(CLI::App& app) {
   // callback helper with error code handling
   auto err_guard = [this](int (shiplog_actions::*fun)()) {
      try {
         initialize();
         int rc = (this->*fun)();
         if(rc)
            throw(CLI::RuntimeError(rc));
      } catch(CLI::RuntimeError&) {
         throw;
      } catch(...) {
         print_exception();
         throw(CLI::RuntimeError(-1));
      }
   };

   // main command
   auto* sub = app.add_subcommand("ship-log", "State history (SHiP) log utility");
   sub->require_subcommand();
   sub->fallthrough();

   // fallthrough options
   sub->add_option("--state-history-dir", opt->state_history_dir,
                   "The location of the state-history directory (absolute path or relative to the current directory).");
   sub->add_option("--log", opt->log_name,
                   "Which log to operate on: trace_history, chain_state_history, finality_data_history, or a path to "
                   "any <stem>.log / retained <stem>-<first>-<last>.log bundle.");

   // subcommand - info
   sub->add_subcommand("info", "Report each log's version, block range, endpoint block ids, size, and index health by "
                               "examining only its endpoints (fast; does not detect damage in the middle of the "
                               "file). Without --log, every *.log in the state-history directory is reported.")
      ->callback([err_guard]() { err_guard(&shiplog_actions::info); });

   // subcommand - block-id
   auto* bid = sub->add_subcommand("block-id", "Print the block id each log records for a given block number (the "
                                               "canonical entry when a fork switch wrote several). The index is used "
                                               "only after verification, so a stale index cannot misattribute an id. "
                                               "Use when nodeos reports 'missed a fork change': compare what the log "
                                               "recorded against the block log or another node to see which history "
                                               "the log holds. Without --log, every *.log in the directory is "
                                               "queried. Exits 0 when at least one log records the block.")
                  ->callback([err_guard]() { err_guard(&shiplog_actions::block_id); });
   bid->add_option("--block,-b", opt->block_num, "The block number to look up.")->required();

   // subcommand - smoke-test
   auto* smoke = sub->add_subcommand("smoke-test", "Validate every entry of the log (headers, position trailers, "
                                                   "block continuity) and the index, mapping any damaged regions. "
                                                   "Read-only. Without --log, every *.log in the directory is tested.")
                    ->callback([err_guard]() { err_guard(&shiplog_actions::smoke_test); });
   smoke->add_flag("--deep", opt->deep,
                   "Additionally decompress every entry's payload to detect bit-rot the structural checks cannot see.");

   // subcommand - make-index
   sub->add_subcommand("make-index", "Rebuild <log>.index from <log>.log, replacing any existing index. Requires --log.")
      ->callback([err_guard]() { err_guard(&shiplog_actions::make_index); });

   // subcommand - trim
   auto* trim = sub->add_subcommand("trim", "Trim the log so it spans only blocks [first, last]. Requires --log and "
                                            "at least one of --first/--last. Trimming the front rewrites the log and "
                                            "needs free disk space roughly equal to the kept portion.")
                   ->callback([err_guard]() { err_guard(&shiplog_actions::trim); });
   trim->add_option("--first,-f", opt->first_block, "The first block number to keep.");
   trim->add_option("--last,-l", opt->last_block, "The last block number to keep.");

   // subcommand - extract-blocks
   auto* extract = sub->add_subcommand("extract-blocks", "Copy blocks [first, last] of the log into a fresh bundle "
                                                         "in --output-dir, leaving the source untouched. Requires "
                                                         "--log.")
                      ->callback([err_guard]() { err_guard(&shiplog_actions::extract); });
   extract->add_option("--first,-f", opt->first_block, "The first block number to extract.")->required();
   extract->add_option("--last,-l", opt->last_block, "The last block number to extract.")->required();
   extract->add_option("--output-dir", opt->output_dir, "The output directory for the extracted bundle.")->required();

   // subcommand - repair
   auto* repair = sub->add_subcommand("repair", "Repair a damaged log: scan it, then truncate at the first damage "
                                                "(default, like nodeos's automatic recovery) and rebuild the index. "
                                                "An intact log with a bad or missing index just gets the index "
                                                "rebuilt. Requires --log.")
                     ->callback([err_guard]() { err_guard(&shiplog_actions::repair); });
   repair->add_flag("--keep-tail", opt->keep_tail,
                    "Salvage the last valid range instead of the first: keeps recent history when the damage is early "
                    "in the file, discarding everything before it.");
   repair->add_flag("--dry-run", opt->dry_run, "Report the damage map and what would be done without writing anything.");
   repair->add_flag("--deep", opt->deep,
                    "Scan payload content too (see smoke-test --deep); entries with rotten payloads count as damage.");
   repair->add_option("--output-dir", opt->output_dir,
                      "For --keep-tail: write the salvaged bundle here instead of replacing the original.");

   // subcommand - vacuum
   sub->add_subcommand("vacuum", "Convert a pruned log into an un-pruned log. Requires --log.")
      ->callback([err_guard]() { err_guard(&shiplog_actions::do_vacuum); });

   // subcommand - split
   auto* split = sub->add_subcommand("split", "Split the log into retained-style <stem>-<first>-<last> bundles of "
                                              "--stride blocks (boundaries at block numbers divisible by the stride, "
                                              "exactly like state-history-stride rotation) plus a head bundle with "
                                              "the remainder, all written to --output-dir. Requires --log.")
                    ->callback([err_guard]() { err_guard(&shiplog_actions::split); });
   split->add_option("--stride", opt->stride, "The number of blocks per split bundle.")->required();
   split->add_option("--output-dir", opt->output_dir, "The output directory for the split bundles.")->required();

   // subcommand - merge
   auto* merge = sub->add_subcommand("merge", "Merge every contiguous <log>-<first>-<last> retained bundle found "
                                              "directly under --state-history-dir into a single <log>.log/.index "
                                              "bundle in --output-dir (point --state-history-dir at the 'retained' "
                                              "subdirectory if that is where the bundles live). Sources are left "
                                              "unchanged. Requires --log with a plain log name.")
                    ->callback([err_guard]() { err_guard(&shiplog_actions::merge); });
   merge->add_option("--output-dir", opt->output_dir, "The output directory for the merged bundle.")->required();
}

void shiplog_actions::initialize() {
   std::filesystem::path dir = opt->state_history_dir;
   if(dir.is_relative())
      opt->state_history_dir = (std::filesystem::current_path() / dir).string();

   if(!opt->output_dir.empty()) {
      std::filesystem::path od = opt->output_dir;
      if(od.is_relative())
         opt->output_dir = (std::filesystem::current_path() / od).string();
   }
}

std::filesystem::path shiplog_actions::resolve_stem() const {
   EOS_ASSERT(!opt->log_name.empty(), plugin_exception,
              "--log is required: one of trace_history, chain_state_history, finality_data_history, or a path to a "
              "log bundle");
   std::filesystem::path p(opt->log_name);
   if(p.is_relative() && !p.has_parent_path())
      p = std::filesystem::path(opt->state_history_dir) / p;
   else if(p.is_relative())
      p = std::filesystem::current_path() / p;
   return log_utils::normalize_stem(p);
}

std::vector<std::filesystem::path> shiplog_actions::resolve_stems() const {
   if(!opt->log_name.empty())
      return {resolve_stem()};

   const std::filesystem::path dir(opt->state_history_dir);
   EOS_ASSERT(std::filesystem::is_directory(dir), plugin_exception, "${dir} is not a directory", ("dir", dir.string()));
   std::vector<std::filesystem::path> stems;
   for(const std::filesystem::directory_entry& de : std::filesystem::directory_iterator(dir))
      if(de.is_regular_file() && de.path().extension() == ".log")
         stems.push_back(log_utils::normalize_stem(de.path()));
   std::sort(stems.begin(), stems.end());
   EOS_ASSERT(!stems.empty(), plugin_exception, "no *.log files found in ${dir}", ("dir", dir.string()));
   return stems;
}

int shiplog_actions::info() {
   for(const std::filesystem::path& stem : resolve_stems()) {
      const log_utils::log_summary s = log_utils::summarize_log(stem);
      std::cout << stem.string() << ".log:\n";
      std::cout << "  size:    " << s.log_size << " bytes\n";
      if(s.log_size == 0) {
         std::cout << "  empty log\n";
      } else if(!s.valid_first_header) {
         std::cout << "  NOT A SHIP LOG (or first header is damaged)\n";
      } else {
         std::cout << "  version: " << s.version << (s.pruned ? " (pruned" : "");
         if(s.pruned)
            std::cout << ", " << *s.pruned_block_count << " blocks retained)";
         std::cout << "\n";
         if(s.tail_ok)
            std::cout << "  blocks:  " << (s.pruned ? s.last_block - *s.pruned_block_count + 1 : s.first_block) << "-"
                      << s.last_block << "\n";
         else
            std::cout << "  blocks:  starts at " << s.first_block << ", tail is DAMAGED (run smoke-test or repair)\n";
         //a pruned log's first id belongs to its pre-prune first block, hence the explicit block numbers
         if(s.first_block_id)
            std::cout << "  first:   " << s.first_block_id->str() << " (block " << s.first_block << ")\n";
         if(s.last_block_id)
            std::cout << "  last:    " << s.last_block_id->str() << " (block " << s.last_block << ")\n";
      }
      std::cout << "  index:   " << log_utils::to_string(s.index);
      if(s.index_size)
         std::cout << " (" << s.index_size << " bytes)";
      std::cout << "\n";
   }
   return 0;
}

int shiplog_actions::block_id() {
   bool found = false;
   for(const std::filesystem::path& stem : resolve_stems()) {
      std::cout << stem.string() << ".log:\n";
      try {
         if(const std::optional<block_id_type> id = log_utils::find_block_id(stem, opt->block_num, report_progress)) {
            std::cout << "  block " << opt->block_num << ": " << id->str() << "\n";
            found = true;
         } else {
            const log_utils::log_summary s = log_utils::summarize_log(stem);
            if(s.log_size == 0)
               std::cout << "  block " << opt->block_num << " not present (log is empty)\n";
            else
               std::cout << "  block " << opt->block_num << " not present (log spans blocks "
                         << (s.pruned ? s.last_block - *s.pruned_block_count + 1 : s.first_block) << "-"
                         << s.last_block << ")\n";
         }
      } catch(const fc::exception& e) {
         //a damaged log must not end a directory-wide query; report it and keep going
         std::cout << "  ERROR: " << e.top_message() << "\n";
      }
   }
   return found ? 0 : 1;
}

int shiplog_actions::smoke_test() {
   int rc = 0;
   for(const std::filesystem::path& stem : resolve_stems()) {
      std::cout << stem.string() << ".log:\n";
      const log_utils::scan_result scan = log_utils::scan_log(stem, opt->deep, report_progress);
      print_scan(scan);

      log_utils::index_status index = log_utils::index_status::log_damaged;
      if(scan.intact())
         index = log_utils::check_index(stem, true, report_progress);
      std::cout << "  index:   " << log_utils::to_string(index) << "\n";

      if(scan.intact() && index == log_utils::index_status::ok) {
         std::cout << "  result:  OK\n";
      } else {
         std::cout << "  result:  PROBLEMS FOUND\n";
         rc = 1;
      }
   }
   return rc;
}

int shiplog_actions::make_index() {
   const std::filesystem::path stem = resolve_stem();
   const auto [first, last]         = log_utils::build_index(stem, report_progress);
   if(first == 0 && last == 0)
      std::cout << "wrote empty " << stem.string() << ".index for empty log\n";
   else
      std::cout << "wrote " << stem.string() << ".index covering blocks " << first << "-" << last << "\n";
   return 0;
}

int shiplog_actions::trim() {
   const std::filesystem::path stem = resolve_stem();
   //the option defaults double as "not supplied" sentinels: block 0 and UINT32_MAX are never valid
   // ship block numbers, so this can never be confused with a real --first/--last the user passed
   const bool has_first             = opt->first_block != 0;
   const bool has_last              = opt->last_block != std::numeric_limits<uint32_t>::max();
   EOS_ASSERT(has_first || has_last, plugin_exception, "trim requires --first and/or --last");
   EOS_ASSERT(opt->first_block <= opt->last_block, plugin_exception,
              "--first ${f} must not be greater than --last ${l}", ("f", opt->first_block)("l", opt->last_block));

   //trim the end first: it is a cheap in-place truncation and shrinks the copy a front trim makes
   if(has_last) {
      const uint64_t removed = log_utils::truncate_log(stem, opt->last_block, report_progress);
      std::cout << "trimmed " << removed << " bytes off the end of " << stem.string() << ".log\n";
   }
   if(has_first) {
      const uint64_t removed = log_utils::trim_front(stem, opt->first_block, report_progress);
      std::cout << "trimmed " << removed << " bytes off the front of " << stem.string() << ".log\n";
   }

   const log_utils::log_summary s = log_utils::summarize_log(stem);
   std::cout << stem.string() << ".log now spans blocks " << s.first_block << "-" << s.last_block << "\n";
   return 0;
}

int shiplog_actions::extract() {
   const std::filesystem::path stem = resolve_stem();
   std::filesystem::create_directories(opt->output_dir);
   const std::filesystem::path dst = std::filesystem::path(opt->output_dir) / stem.filename();
   const uint64_t bytes = log_utils::extract_blocks(stem, dst, opt->first_block, opt->last_block, report_progress);
   std::cout << "wrote " << dst.string() << ".log covering blocks " << opt->first_block << "-" << opt->last_block
             << " (" << bytes << " bytes)\n";
   return 0;
}

int shiplog_actions::repair() {
   const std::filesystem::path stem = resolve_stem();
   EOS_ASSERT(opt->output_dir.empty() || opt->keep_tail, plugin_exception,
              "--output-dir only applies to --keep-tail repairs");

   std::optional<std::filesystem::path> dst;
   if(!opt->output_dir.empty()) {
      if(!opt->dry_run)
         std::filesystem::create_directories(opt->output_dir);
      dst = std::filesystem::path(opt->output_dir) / stem.filename();
   }

   const log_utils::repair_report rep =
      log_utils::repair_log(stem, opt->keep_tail ? log_utils::repair_mode::keep_tail : log_utils::repair_mode::truncate,
                            opt->dry_run, opt->deep, dst, report_progress);

   std::cout << stem.string() << ".log:\n";
   print_scan(rep.scan);

   if(rep.scan.intact() && !rep.index_rebuilt) {
      std::cout << "  log and index are healthy; nothing to do\n";
      return 0;
   }
   const char* tense = rep.acted ? "" : " (dry run, nothing written)";
   if(rep.scan.intact()) {
      std::cout << "  log is healthy; index " << (rep.acted ? "was rebuilt" : "would be rebuilt") << tense << "\n";
   } else {
      std::cout << "  kept blocks " << rep.first_block << "-" << rep.last_block << ": " << rep.bytes_kept
                << " bytes kept, " << rep.bytes_discarded << " bytes discarded" << tense << "\n";
      if(dst && rep.acted)
         std::cout << "  salvaged bundle written to " << dst->string() << ".log (original untouched)\n";
   }
   return 0;
}

int shiplog_actions::do_vacuum() {
   const std::filesystem::path  stem = resolve_stem();
   const log_utils::log_summary s    = log_utils::summarize_log(stem);
   EOS_ASSERT(s.valid_first_header && s.tail_ok, plugin_exception,
              "${stem}.log is damaged; vacuum requires an intact pruned log", ("stem", stem.string()));
   if(!s.pruned) {
      std::cout << stem.string() << ".log is not pruned; nothing to do\n";
      return 0;
   }
   //opening a pruned log without a prune config vacuums it (and validates/regenerates the index)
   eosio::state_history::state_history_log log(stem);
   const auto [first, last] = log.block_range();
   std::cout << "vacuumed " << stem.string() << ".log; it now spans blocks " << first << "-" << last - 1 << "\n";
   return 0;
}

int shiplog_actions::split() {
   const std::filesystem::path stem = resolve_stem();
   const std::vector<std::filesystem::path> created =
      log_utils::split_log(stem, opt->output_dir, opt->stride, report_progress);
   for(const std::filesystem::path& c : created)
      std::cout << "wrote " << c.string() << ".log\n";
   std::cout << "(the last bundle is the head log; the others are retained bundles)\n";
   return 0;
}

int shiplog_actions::merge() {
   EOS_ASSERT(!opt->log_name.empty() && !std::filesystem::path(opt->log_name).has_parent_path(),
              plugin_exception, "merge requires --log with a plain log name, e.g. trace_history");
   const auto [first, last] =
      log_utils::merge_logs(opt->state_history_dir, opt->log_name, opt->output_dir, report_progress);
   std::cout << "wrote " << (std::filesystem::path(opt->output_dir) / opt->log_name).string() << ".log covering blocks "
             << first << "-" << last << "\n";
   return 0;
}
