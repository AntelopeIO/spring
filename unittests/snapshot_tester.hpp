#pragma once

#include <eosio/chain/block_log.hpp>
#include <eosio/chain/snapshot.hpp>
#include <eosio/testing/tester.hpp>


inline std::filesystem::path get_parent_path(std::filesystem::path blocks_dir, int ordinal) {
   std::filesystem::path leaf_dir = blocks_dir.filename();
   if (leaf_dir.generic_string() == std::string("blocks")) {
      blocks_dir = blocks_dir.parent_path();
      leaf_dir = blocks_dir.filename();
      try {
         boost::lexical_cast<int>(leaf_dir.generic_string());
         blocks_dir = blocks_dir.parent_path();
      }
      catch(const boost::bad_lexical_cast& ) {
         // no extra ordinal directory added to path
      }
   }
   return blocks_dir / std::to_string(ordinal);
}

inline controller::config copy_config(const controller::config& config, int ordinal) {
   controller::config copied_config = config;
   auto parent_path = get_parent_path(config.blocks_dir, ordinal);
   copied_config.finalizers_dir   = parent_path / config.finalizers_dir.filename().generic_string();;
   copied_config.blocks_dir = parent_path / config.blocks_dir.filename().generic_string();
   copied_config.state_dir  = parent_path / config.state_dir.filename().generic_string();
   return copied_config;
}

inline controller::config copy_config_and_files(const controller::config& config, int ordinal) {
   controller::config copied_config = copy_config(config, ordinal);
   std::filesystem::create_directories(copied_config.blocks_dir);
   std::filesystem::copy_file(config.blocks_dir / "blocks.log", copied_config.blocks_dir / "blocks.log", std::filesystem::copy_options::none);
   std::filesystem::copy_file(config.blocks_dir / "blocks.index", copied_config.blocks_dir / "blocks.index", std::filesystem::copy_options::none);
   return copied_config;
}

class snapshotted_tester : public base_tester {
public:
   enum config_file_handling { dont_copy_config_files, copy_config_files };
   snapshotted_tester(const controller::config& config, const snapshot_reader_ptr& snapshot, int ordinal,
           config_file_handling copy_files_from_config = config_file_handling::dont_copy_config_files) {
      FC_ASSERT(config.blocks_dir.filename().generic_string() != "."
                && config.state_dir.filename().generic_string() != ".", "invalid path names in controller::config");

      controller::config copied_config = (copy_files_from_config == copy_config_files)
                                         ? copy_config_and_files(config, ordinal) : copy_config(config, ordinal);

      BOOST_CHECK_GT(snapshot->total_row_count(), 0u);
      init(copied_config, snapshot);
   }

   produce_block_result_t produce_block_ex( fc::microseconds skip_time = default_skip_time, bool no_throw = false )override {
      return _produce_block(skip_time, false, no_throw);
   }

   signed_block_ptr produce_block( fc::microseconds skip_time = default_skip_time, bool no_throw = false )override {
      return produce_block_ex(skip_time, no_throw).block;
   }

   signed_block_ptr produce_empty_block( fc::microseconds skip_time = default_skip_time )override {
      control->abort_block();
      return _produce_block(skip_time, true);
   }

   signed_block_ptr finish_block()override {
      return _finish_block();
   }

   bool validate() { return true; }
};
