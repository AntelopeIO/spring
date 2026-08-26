#pragma once

#include <filesystem>
#include <regex>
#include <string_view>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/key.hpp>

#include <eosio/chain/types.hpp>

#include <eosio/state_history/log.hpp>
#include <eosio/state_history/log_config.hpp>

namespace eosio::state_history {

using namespace boost::multi_index;

/// suffix given to bundles force-write moves aside; deliberately matches neither the retained-file
/// regex nor any name this code opens, so orphaned bundles are inert until an operator acts on them
inline constexpr std::string_view orphaned_bundle_infix = "-corrupt-";

struct catalogued_log_file {
   chain::block_num_type            begin_block_num = 0;
   chain::block_num_type            end_block_num = 0;
   std::filesystem::path            path_and_basename;  //example: /some/dir/trace-history-50-59   i.e. does NOT include .log nor .index
   std::optional<state_history_log> log;

   size_t last_used_counter = 0;

   size_t effective_last_used_counter() const {
      if(!log)
         return 0;
      return last_used_counter;
   }

   catalogued_log_file(const catalogued_log_file&) = delete;
   catalogued_log_file& operator=(catalogued_log_file&) = delete;
   catalogued_log_file(chain::block_num_type begin_block_num, chain::block_num_type end_block_num, std::filesystem::path path_and_basename) :
     begin_block_num(begin_block_num), end_block_num(end_block_num), path_and_basename(path_and_basename) {}
};

class log_catalog {
   std::filesystem::path retained_dir;
   std::filesystem::path archive_dir;
   uint32_t              max_retained_files = std::numeric_limits<decltype(max_retained_files)>::max();
   uint32_t              log_rotation_stride = std::numeric_limits<decltype(log_rotation_stride)>::max();

   const state_history_log::non_local_get_block_id_func non_local_get_block_id;

   //when set, conditions that would otherwise prevent the node from running -- a head log that
   // fails its startup checks or cannot accept the next block, or an inconsistent retained set --
   // are handled by moving the offending bundle aside (never deleting data) and continuing with a
   // fresh log. see the state-history-force-write option.
   const bool                                 force_write = false;
   std::optional<state_history::prune_config> head_log_prune_conf;

   struct by_mru {};
   typedef multi_index_container<
     catalogued_log_file,
     indexed_by<
       ordered_unique<key<&catalogued_log_file::begin_block_num>>,
       ordered_non_unique<tag<by_mru>,key<&catalogued_log_file::effective_last_used_counter>, std::greater<size_t>>
     >
   > catalog_t;
   catalog_t retained_log_files;
   std::optional<state_history_log> head_log;
   const std::filesystem::path head_log_path_and_basename;  //example: /some/dir/trace-history   i.e. does NOT include .log nor .index

   size_t global_used_counter = 0;

public:
   log_catalog(const log_catalog&) = delete;
   log_catalog& operator=(log_catalog&) = delete;

   log_catalog(const std::filesystem::path& log_dir, const state_history::state_history_log_config& config, const std::string& log_name,
               state_history_log::non_local_get_block_id_func non_local_get_block_id = state_history_log::no_non_local_get_block_id_func,
               bool force_write = false) :
     non_local_get_block_id(non_local_get_block_id), force_write(force_write),
     head_log_path_and_basename(log_dir / log_name) {
      std::visit(chain::overloaded {
         [this](const std::monostate&) {
            open_head_log();
         },
         [this](const state_history::prune_config& prune) {
            head_log_prune_conf = prune;
            open_head_log();
         },
         [this, &log_dir, &log_name](const state_history::partition_config& partition_config) {
            open_head_log();
            setup_retained_logs_on_init(log_dir, log_name, partition_config);
         }
      }, config);

      assert(!!head_log);
   }

   template <typename F>
   void pack_and_write_entry(const chain::block_id_type& id, const chain::block_id_type& prev_id, F&& pack_to) {
      if(!force_write)
         return do_pack_and_write_entry(id, prev_id, pack_to);

      //force-write: never let the existing logs stop the node from running. First try the normal
      // write; if the head log cannot accept the block (a gap after a snapshot restore, a missed
      // fork change from divergent history, ...), move the head bundle aside and retry with a fresh
      // one. The retry can unrotate a retained bundle into the head and fail on it for the same
      // reason; in that case the whole catalog is moved aside and writing restarts from scratch.
      // Bundles are renamed (kept on disk), never deleted.
      try {
         return do_pack_and_write_entry(id, prev_id, pack_to);
      } catch(const std::bad_alloc&) {
         throw;
      } catch(const std::exception& e) {
         elog("Failed to write block ${b} to ${name}.log (${e}); state-history-force-write is set: moving the head "
              "log aside and retrying with a fresh one",
              ("b", chain::block_header::num_from_id(id))("name", head_log_path_and_basename.string())("e", e.what()));
      }
      head_log.reset();
      orphan_bundle(head_log_path_and_basename);
      open_head_log();
      try {
         return do_pack_and_write_entry(id, prev_id, pack_to);
      } catch(const std::bad_alloc&) {
         throw;
      } catch(const std::exception& e) {
         elog("Still failed to write block ${b} (${e}); moving all retained logs aside and starting over",
              ("b", chain::block_header::num_from_id(id))("e", e.what()));
      }
      while(!retained_log_files.empty()) {
         catalog_t::node_type n = retained_log_files.extract(retained_log_files.begin());
         n.value().log.reset();
         orphan_bundle(n.value().path_and_basename);
      }
      head_log.reset();
      orphan_bundle(head_log_path_and_basename);
      open_head_log();
      //With no retained logs and an empty head log there is nothing left in the catalog for the write
      // to conflict with. It can still be rejected by a disagreement with the chain itself -- the
      // non-local block-id lookup says block-1 has an id other than prev_id -- which no amount of log
      // rewriting can resolve. Honor force-write's promise to keep the node running by skipping the
      // block (it cannot be represented in the state history) rather than throwing.
      try {
         do_pack_and_write_entry(id, prev_id, pack_to);
      } catch(const std::bad_alloc&) {
         throw;
      } catch(const std::exception& e) {
         elog("state-history-force-write could not write block ${b} even into a fresh empty log (${e}); the block "
              "conflicts with the chain rather than the log, so it is skipped and will not be served in the state "
              "history", ("b", chain::block_header::num_from_id(id))("e", e.what()));
      }
   }

private:
   template <typename F>
   void do_pack_and_write_entry(const chain::block_id_type& id, const chain::block_id_type& prev_id, F&& pack_to) {
      const uint32_t block_num = chain::block_header::num_from_id(id);

      if(!retained_log_files.empty()) {
         //always make sure we are going to write to at least the very first block in the catalog
         EOS_ASSERT(block_num >= retained_log_files.begin()->begin_block_num, chain::plugin_exception,
                     "block ${b} is before first block ${s} of ${name}.log",
                     ("b", block_num)("s", retained_log_files.begin()->begin_block_num)("name", retained_log_files.begin()->path_and_basename.string()));

         //check if this log already has the same blockid at the given blocknum. This is indicative of a resync or replay and there is no need to write the
         // same log entry again. otherwise we risk unrotating and blowing away existing log files
         if(get_block_id(block_num) == id)
            return;

         //need to consider "unrotating" the logs. ex: split logs with 234 56789 ABC. "ABC" log is the head log. Any block that is prior to A must result in the removal
         // of the ABC log (this does _not_ invalidate ship_log_entrys from that log!) and then the replacement of 56789 as the head log. If the new block is in the range of 5
         // through 9, we write here to this head log. If the new block is prior to block 5 we unrotate again. Keep performing the unrotation as long as there are retained logs
         // to pull from
         //what's a little annoying is that we maintain an empty head log after rotation, so we can also have 234 56789 (empty), and here we also want to unrotate when writing
         // anything <=9 but not for 10. 9 in this case is actually especially interesting since we'll first unrotate the logs giving 234 56789, overwrite block 9, and then rotate
         // the logs again yielding 234 56789 (empty).
         while(!retained_log_files.empty()) {
            if(!head_log->empty() && (block_num < head_log->block_range().first))
               unrotate_log();
            else if(head_log->empty() && (block_num <= retained_log_files.rbegin()->end_block_num-1))
               unrotate_log();
            else
               break;
         }
      }

      //at this point the head log is certainly the log we want to insert in to
      head_log->pack_and_write_entry(id, prev_id, pack_to);

      if(block_num % log_rotation_stride == 0)
         rotate_logs();
   }

public:
   std::optional<ship_log_entry> get_entry(uint32_t block_num) {
      return call_for_log(block_num, [&](state_history_log&& l) {
         return l.get_entry(block_num);
      });
   }

   std::optional<chain::block_id_type> get_block_id(uint32_t block_num) {
      return call_for_log(block_num, [&](state_history_log&& l) {
         return l.get_block_id(block_num);
      });
   }

   std::pair<uint32_t, uint32_t> block_range() const {
      uint32_t begin = 0;
      uint32_t end = 0;

      if(!retained_log_files.empty()) {
         begin = retained_log_files.begin()->begin_block_num;
         end = retained_log_files.rbegin()->end_block_num;
      }
      if(!head_log->empty()) {
         if(begin == 0)
            begin = head_log->block_range().first;
         end = head_log->block_range().second;
      }

      return {begin, end};
   }

   bool empty() const {
      const auto [first, second] = block_range();
      return first == second;
   }

   void clear() {
      if(empty())
         return;

      while(!retained_log_files.empty())
         delete_bundle(retained_log_files.extract(retained_log_files.begin()).value().path_and_basename);
      delete_head_log();
      open_head_log();
   }

private:
   template<typename F>
   typename std::invoke_result_t<F,state_history_log&&> call_for_log(const uint32_t block_num, F&& f) {
      //watch out that this check will send any requests for block nums *less than* first retained block to head log too
      if(catalog_t::iterator it = retained_log_files.upper_bound(block_num);
        !retained_log_files.empty() && it != retained_log_files.begin() && block_num < std::prev(it)->end_block_num) {
         catalog_t::iterator log_it = std::prev(it);
         retained_log_files.modify(log_it, [&](catalogued_log_file& clf) {
            if(!clf.log)
               clf.log.emplace(clf.path_and_basename, non_local_get_block_id);
            clf.last_used_counter = ++global_used_counter;
         });

         const unsigned num_log_files_to_keep_open = 5;
         if(retained_log_files.size() >= num_log_files_to_keep_open+1)
            retained_log_files.get<by_mru>().modify(std::next(retained_log_files.get<by_mru>().begin(), num_log_files_to_keep_open), [](catalogued_log_file& clf) {
               clf.log.reset();
            });

         return f(std::forward<state_history_log>(const_cast<state_history_log&>(*log_it->log)));
      }
      else
         return f(std::forward<state_history_log>(*head_log));
   }

   void setup_retained_logs_on_init(const std::filesystem::path& log_dir, const std::string& log_name, const state_history::partition_config& partition_config) {
      retained_dir = make_absolute_dir(log_dir, partition_config.retained_dir.empty() ? log_dir : partition_config.retained_dir);
      if(!partition_config.archive_dir.empty())
         archive_dir = make_absolute_dir(log_dir, partition_config.archive_dir);
      max_retained_files = partition_config.max_retained_files;
      log_rotation_stride = partition_config.stride;

      const std::regex retained_logfile_regex("^" + log_name + R"(-\d+-\d+\.log$)");

      for(const std::filesystem::directory_entry& dir_entry : std::filesystem::directory_iterator(retained_dir)) {
         if(!dir_entry.is_regular_file())
            continue;
         if(!std::regex_search(dir_entry.path().filename().string(), retained_logfile_regex))
            continue;

         const std::filesystem::path path_and_basename = dir_entry.path().parent_path() / dir_entry.path().stem();

         try {
            state_history_log log(path_and_basename, [](chain::block_num_type) {return std::nullopt;});
            if(log.empty())
               continue;
            const auto [begin_bnum, end_bnum] = log.block_range();
            retained_log_files.emplace(begin_bnum, end_bnum, path_and_basename);
         } catch(const std::bad_alloc&) {
            throw;
         } catch(const std::exception& e) {
            if(!force_write)
               throw;
            elog("Failed to open retained log ${name}.log (${e}); state-history-force-write is set: leaving it out "
                 "of the catalog, its blocks will not be served",
                 ("name", path_and_basename.string())("e", e.what()));
         }
      }

      //a gap or overlap between retained files is normally fatal; with force-write the files stay in
      // place and the catalog simply cannot serve the missing blocks
      if(retained_log_files.size() > 1)
         for(catalog_t::iterator it = retained_log_files.begin(); it != std::prev(retained_log_files.end()); ++it) {
            if(it->end_block_num == std::next(it)->begin_block_num)
               continue;
            EOS_ASSERT(force_write, chain::plugin_exception,
                       "retained log file ${sf}.log has block range ${sb}-${se} but ${ef}.log has range ${eb}-${ee} which results in a hole",
                       ("sf", it->path_and_basename.native())("sb", it->begin_block_num)("se", it->end_block_num-1)
                       ("ef", std::next(it)->path_and_basename.native())("eb", std::next(it)->begin_block_num)("ee", std::next(it)->end_block_num-1));
            elog("retained log file ${sf}.log has block range ${sb}-${se} but ${ef}.log has range ${eb}-${ee} which "
                 "results in a hole; state-history-force-write is set: blocks in the hole will not be served",
                 ("sf", it->path_and_basename.native())("sb", it->begin_block_num)("se", it->end_block_num-1)
                 ("ef", std::next(it)->path_and_basename.native())("eb", std::next(it)->begin_block_num)("ee", std::next(it)->end_block_num-1));
         }

      if(!retained_log_files.empty() && !head_log->empty() &&
         retained_log_files.rbegin()->end_block_num != head_log->block_range().first) {
         EOS_ASSERT(force_write, chain::plugin_exception,
                    "retained log file ${sf}.log has block range ${sb}-${se} but head log has range ${eb}-${ee} which results in a hole",
                    ("sf", retained_log_files.rbegin()->path_and_basename.native())("sb", retained_log_files.rbegin()->begin_block_num)("se", retained_log_files.rbegin()->end_block_num-1)
                    ("eb", head_log->block_range().first)("ee", head_log->block_range().second-1));
         elog("retained log file ${sf}.log has block range ${sb}-${se} but head log has range ${eb}-${ee} which "
              "results in a hole; state-history-force-write is set: blocks in the hole will not be served",
              ("sf", retained_log_files.rbegin()->path_and_basename.native())("sb", retained_log_files.rbegin()->begin_block_num)("se", retained_log_files.rbegin()->end_block_num-1)
              ("eb", head_log->block_range().first)("ee", head_log->block_range().second-1));
      }
   }

   void unrotate_log() {
      catalog_t::node_type last_catalogued_file = retained_log_files.extract(std::prev(retained_log_files.end()));

      delete_head_log();

      rename_bundle(last_catalogued_file.value().path_and_basename, head_log_path_and_basename);
      head_log = std::move(last_catalogued_file.value().log); //don't reopen the log, if we can avoid it
      if(!head_log)
         open_head_log();
   }

   void rotate_logs() {
      const auto [begin, end] = head_log->block_range();
      std::filesystem::path new_log_basenamepath = retained_dir / head_log_path_and_basename.stem();
      new_log_basenamepath += "-" + std::to_string(begin) + "-" + std::to_string(end-1);

      state_history_log old_head_log = std::move(*head_log);
      rename_bundle(head_log_path_and_basename, new_log_basenamepath);
      try {
         //this one is "risky enough" to attempt to roll back if it fails (too many file descriptors open would be top concern)
         open_head_log();
      } catch(std::bad_alloc&) {
         throw;
      } catch(std::exception& e) {
         wlog("Failed to rotate log ${pbn}", ("pbn", head_log_path_and_basename.string()));
         //remove any potentially created new head log files
         delete_bundle(head_log_path_and_basename);
         //rename old logs back, restore head_log instance that was never closed, and don't continue with rotation
         rename_bundle(new_log_basenamepath, head_log_path_and_basename);
         head_log = std::move(old_head_log);
         return;
      }

      //it looks like ought to move the old_head_log in to this new catalogued_log_file instance. unfortunately, currently the log file cache
      // is only pruned on accesses which means that if there were never any ship clients to access the logs the cache would grow indefinitely
      // if we moved an open log in to the cache here
      retained_log_files.emplace(begin, end, new_log_basenamepath);

      while(retained_log_files.size() > max_retained_files) {
         const catalog_t::iterator it = retained_log_files.begin();
         std::filesystem::path oldest_log_path_and_basename = it->path_and_basename;
         if(archive_dir.empty())
            delete_bundle(oldest_log_path_and_basename);
         else
            rename_bundle(oldest_log_path_and_basename, archive_dir / oldest_log_path_and_basename.filename());
         retained_log_files.erase(it);
      }
   }

   void open_head_log() {
      try {
         head_log.emplace(head_log_path_and_basename, non_local_get_block_id, head_log_prune_conf, force_write);
      } catch(const std::bad_alloc&) {
         throw;
      } catch(const std::exception& e) {
         if(!force_write)
            throw;
         elog("Failed to open ${name}.log (${e}); state-history-force-write is set: moving it aside and starting a "
              "fresh log", ("name", head_log_path_and_basename.string())("e", e.what()));
         head_log.reset();
         orphan_bundle(head_log_path_and_basename);
         head_log.emplace(head_log_path_and_basename, non_local_get_block_id, head_log_prune_conf, force_write);
      }
   }

   /**
    * Move a bundle out of the way to `<stem>-corrupt-<n>` (a name neither the retained-file scan
    * nor this class will ever pick up) so a fresh log can take its place without destroying data.
    * An empty bundle has nothing worth keeping and is simply deleted.
    */
   void orphan_bundle(const std::filesystem::path& path_and_basename) {
      const std::filesystem::path log_file = std::filesystem::path(path_and_basename).replace_extension("log");
      if(!std::filesystem::exists(log_file) || std::filesystem::file_size(log_file) == 0) {
         delete_bundle(path_and_basename);
         return;
      }
      unsigned n = 0;
      std::filesystem::path orphan_base;
      do {
         orphan_base = path_and_basename;
         orphan_base += std::string(orphaned_bundle_infix) + std::to_string(++n);
      } while(std::filesystem::exists(std::filesystem::path(orphan_base).replace_extension("log")));
      wlog("Moving ${from}.log aside to ${to}.log",
           ("from", path_and_basename.string())("to", orphan_base.string()));
      for(const char* ext : {"log", "index"}) {
         const std::filesystem::path from = std::filesystem::path(path_and_basename).replace_extension(ext);
         if(std::filesystem::exists(from))
            std::filesystem::rename(from, std::filesystem::path(orphan_base).replace_extension(ext));
      }
   }

   void delete_head_log() {
      delete_bundle(head_log_path_and_basename);
   }

   void delete_bundle(std::filesystem::path path_and_basename) {
      for(const char* ext : {"log", "index"}) {
         std::filesystem::path fp = path_and_basename.replace_extension(ext);
         if(std::filesystem::exists(fp))
            std::filesystem::remove(fp);
      }
   }

   static std::filesystem::path make_absolute_dir(const std::filesystem::path& base_dir, std::filesystem::path new_dir) {
      if(new_dir.is_relative())
         new_dir = base_dir / new_dir;

      if(!std::filesystem::is_directory(new_dir))
         std::filesystem::create_directories(new_dir);

      return new_dir;
   }

   static void rename_if_not_exists(std::filesystem::path old_name, std::filesystem::path new_name) {
      if(!std::filesystem::exists(new_name)) {
         std::filesystem::rename(old_name, new_name);
      } else {
         std::filesystem::remove(old_name);
         wlog("${new_name} already exists, just removing ${old_name}", ("old_name", old_name.string())("new_name", new_name.string()));
      }
   }

   static void rename_bundle(std::filesystem::path orig_path, std::filesystem::path new_path) {
      rename_if_not_exists(orig_path.replace_extension(".log"), new_path.replace_extension(".log"));
      rename_if_not_exists(orig_path.replace_extension(".index"), new_path.replace_extension(".index"));
   }

};

}