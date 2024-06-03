#pragma once

#include <filesystem>
#include <regex>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/key.hpp>

#include <eosio/chain/types.hpp>

#include <eosio/state_history/log.hpp>
#include <eosio/state_history/log_config.hpp>

#include <fc/scoped_exit.hpp>

namespace eosio::state_history {

using namespace boost::multi_index;

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

   //cache not just an optimization: when a log file is opened the last block in its log file is used as a determination of the log's end block,
   // so we don't want to close an old log file while it's being written to during a fork event otherwise we'd effectively corrupt the catalog state
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
               state_history_log::non_local_get_block_id_func non_local_get_block_id = state_history_log::no_non_local_get_block_id_func) :
     non_local_get_block_id(non_local_get_block_id), head_log_path_and_basename(log_dir / log_name) {
      std::visit(chain::overloaded {
         [this](const std::monostate&) {
            open_head_log();
         }, //nothing needed
         [this](const state_history::prune_config& prune) {
            open_head_log(prune);
         },
         [this, &log_dir, &log_name](const state_history::partition_config& partition_config) {
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

               state_history_log log(path_and_basename, [](chain::block_num_type) {return std::nullopt;});
               if(log.empty())
                  continue;
               const auto [begin_bnum, end_bnum] = log.block_range();
               retained_log_files.emplace(begin_bnum, end_bnum, path_and_basename);
            }

            if(retained_log_files.size() > 1)
               for(decltype(retained_log_files)::iterator it = retained_log_files.begin(); it != std::prev(retained_log_files.end()); ++it)
                  EOS_ASSERT(it->end_block_num == std::next(it)->begin_block_num, chain::plugin_exception,
                             "retained log file ${sf}.log has block range ${sb}-${se} but ${ef}.log has range ${eb}-${ee} which results in a hole",
                             ("sf", it->path_and_basename.native())("sb", it->begin_block_num)("se", it->end_block_num-1)
                             ("ef", std::next(it)->path_and_basename.native())("eb", std::next(it)->begin_block_num)("ee", std::next(it)->end_block_num-1));


            open_head_log();
            if(!retained_log_files.empty() && !head_log->empty())
               EOS_ASSERT(retained_log_files.rbegin()->end_block_num == head_log->block_range().first, chain::plugin_exception,
                          "retained log file ${sf}.log has block range ${sb}-${se} but head log has range ${eb}-${ee} which results in a hole",
                          ("sf", retained_log_files.rbegin()->path_and_basename.native())("sb", retained_log_files.rbegin()->begin_block_num)("se", retained_log_files.rbegin()->end_block_num-1)
                          ("eb", head_log->block_range().first)("ee", head_log->block_range().second-1));
         }
      }, config);

      assert(!!head_log);
   }

   template <typename F>
   void pack_and_write_entry(const chain::block_id_type& id, const chain::block_id_type& prev_id, F&& pack_to) {
      const uint32_t block_num = chain::block_header::num_from_id(id);

      //we need this check for the case where the retained catalog has, say, 1000-4999, the head log is empty, and block 70 is to be written.
      // call_for_log() will refer us to the empty head log since 50 is not in the retained catalog range but we don't want to add there, obviously.
      // whereas for block 5000 in this case we do want to act on the empty head log.
      if(!retained_log_files.empty())
          EOS_ASSERT(block_num >= retained_log_files.begin()->begin_block_num, chain::plugin_exception,
                     "block ${b} is before first block ${s} of ${name}.log",
                     ("b", block_num)("s", retained_log_files.begin()->begin_block_num)("name", retained_log_files.begin()->path_and_basename.string()));

      call_for_log(block_num, [&](state_history_log&& l) {
         l.pack_and_write_entry(id, prev_id, pack_to);
      });

      //don't look at the just written block_num here since we might have not written to the head log, just consider the state of the head log
      if(!head_log->empty() && (head_log->block_range().second-1) % log_rotation_stride == 0)
         rotate_logs();
   }

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

   void rotate_logs() {
      const auto [begin, end] = head_log->block_range();
      std::filesystem::path new_log_basenamepath = retained_dir / head_log_path_and_basename.stem();
      new_log_basenamepath += "-" + std::to_string(begin) + "-" + std::to_string(end-1);
      head_log.reset();
      //try and make sure we don't leave head_log unset if something throws below. any throw below should cause the node to
      // stop, but during teardown of everything something might access head_log which is assumed to always be set elsewhere
      // TODO: only close the old log once the new log is opened to avoid problems when opening throws too
      auto reopen_head_log = fc::make_scoped_exit([this]() {
         open_head_log();
      });

      rename_bundle(head_log_path_and_basename, new_log_basenamepath);
      retained_log_files.emplace(begin, end, new_log_basenamepath);

      while(retained_log_files.size() > max_retained_files) {
         const catalog_t::iterator it = retained_log_files.begin();
         std::filesystem::path oldest_log_path_and_basename = it->path_and_basename;
         if(archive_dir.empty()) {
            std::filesystem::remove(oldest_log_path_and_basename.replace_extension("log"));
            std::filesystem::remove(oldest_log_path_and_basename.replace_extension("index"));
         } else {
            rename_bundle(oldest_log_path_and_basename, archive_dir / oldest_log_path_and_basename.filename());
         }
         retained_log_files.erase(it);
      }
   }

   void open_head_log(std::optional<state_history::prune_config> prune_config = std::nullopt) {
      head_log.emplace(head_log_path_and_basename, non_local_get_block_id, prune_config);
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