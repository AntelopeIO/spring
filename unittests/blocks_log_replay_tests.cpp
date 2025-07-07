#include <eosio/testing/tester.hpp>
#include <boost/test/unit_test.hpp>

// Test scenarios
//    * replay through blocks log and reversible blocks
//    * replay stopping in the middle of blocks log and resuming
//    * replay stopping in the middle of reversible blocks and resuming
//

BOOST_AUTO_TEST_SUITE(blocks_log_replay_tests)

using namespace eosio::testing;
using namespace eosio::chain;

struct blog_replay_fixture {
   eosio::testing::tester chain;
   uint32_t               last_head_block_num {0}; // head_block_num at stopping
   uint32_t               last_irreversible_block_num {0}; // LIB at stopping

   // Create blocks log
   blog_replay_fixture() {
      // Create a few accounts and produce a few blocks to fill in blocks log
      chain.create_account("replay1"_n);
      chain.produce_blocks(1);
      chain.create_account("replay2"_n);
      chain.produce_blocks(1);
      chain.create_account("replay3"_n);

      chain.produce_blocks(10);

      // Make sure the accounts were created
      BOOST_REQUIRE_NO_THROW(chain.get_account("replay1"_n));
      BOOST_REQUIRE_NO_THROW(chain.get_account("replay2"_n));
      BOOST_REQUIRE_NO_THROW(chain.get_account("replay3"_n));

      // Store head_block_num and irreversible_block_num when the node is stopped
      last_head_block_num = chain.head().block_num();
      last_irreversible_block_num = chain.last_irreversible_block_num();

      // Stop the node and save blocks_log
      chain.close();
   }

   // Stop replay at block number `stop_at` via simulated ctrl-c and resume the replay afterward
   void stop_and_resume_replay(uint32_t stop_at, bool remove_fork_db = false) try {
      controller::config copied_config = chain.get_config();

      auto genesis = block_log::extract_genesis_state(copied_config.blocks_dir); 
      BOOST_REQUIRE(genesis);

      // remove the state files to make sure starting from blocks log
      remove_existing_states(copied_config.state_dir);

      // Create a replay chain without starting it
      eosio::testing::tester replay_chain(copied_config, *genesis, call_startup_t::no);

      // Simulate shutdown by CTRL-C
      bool is_quiting = false;
      auto check_shutdown = [&is_quiting](){ return is_quiting; };

      // Set up shutdown at a particular block number
      replay_chain.control->irreversible_block().connect([&](const block_signal_params& t) {
         const auto& [ block, id ] = t;
         // Stop replay at block `stop_at`
         if (block->block_num() == stop_at) {
            is_quiting = true;
         }
      });

      // Make sure reversible fork_db exists
      BOOST_CHECK(std::filesystem::exists(copied_config.blocks_dir / config::reversible_blocks_dir_name / "fork_db.dat"));

      // Start replay and stop at block `stop_at`
      replay_chain.control->startup( [](){}, check_shutdown, *genesis );
      replay_chain.close();

      // Make sure reversible fork_db still exists
      BOOST_CHECK(std::filesystem::exists(copied_config.blocks_dir / config::reversible_blocks_dir_name / "fork_db.dat"));

      if (remove_fork_db) {
         std::filesystem::remove(copied_config.blocks_dir / config::reversible_blocks_dir_name / "fork_db.dat");
      }

      // Prepare resuming replay
      controller::config copied_config_1 = replay_chain.get_config();

      // Resume replay
      eosio::testing::tester replay_chain_1(copied_config_1, *genesis, call_startup_t::no);
      replay_chain_1.control->startup( [](){}, []()->bool{ return false; } );
      replay_chain_1.apply_blocks();

      // Make sure new chain contain the account created by original chain
      BOOST_REQUIRE_NO_THROW(replay_chain_1.get_account("replay1"_n));
      BOOST_REQUIRE_NO_THROW(replay_chain_1.get_account("replay2"_n));
      BOOST_REQUIRE_NO_THROW(replay_chain_1.get_account("replay3"_n));

      // Make sure replayed irreversible_block_num and head_block_num match
      // with last_irreversible_block_num and last_head_block_num
      BOOST_CHECK(replay_chain_1.last_irreversible_block_num() == last_irreversible_block_num);
      if (!remove_fork_db) {
         BOOST_CHECK(replay_chain_1.head().block_num() == last_head_block_num);
      } else {
         BOOST_CHECK(replay_chain_1.head().block_num() == last_irreversible_block_num);
      }
   } FC_LOG_AND_RETHROW()

   void remove_existing_states(std::filesystem::path& state_path) {
      std::filesystem::remove_all(state_path);
      std::filesystem::create_directories(state_path);
   }
};

// Test replay through blocks log and reversible blocks
BOOST_FIXTURE_TEST_CASE(replay_through, blog_replay_fixture) try {
   eosio::chain::controller::config copied_config = chain.get_config();

   auto genesis = eosio::chain::block_log::extract_genesis_state(copied_config.blocks_dir); 
   BOOST_REQUIRE(genesis);

   // remove the state files to make sure we are starting from block log
   remove_existing_states(copied_config.state_dir);
   eosio::testing::tester replay_chain(copied_config, *genesis);

   // Make sure new chain contain the account created by original chain
   BOOST_REQUIRE_NO_THROW(replay_chain.get_account("replay1"_n));
   BOOST_REQUIRE_NO_THROW(replay_chain.get_account("replay2"_n));
   BOOST_REQUIRE_NO_THROW(replay_chain.get_account("replay3"_n));

   // Make sure replayed irreversible_block_num and head_block_num match
   // with last_irreversible_block_num and last_head_block_num
   BOOST_CHECK(replay_chain.last_irreversible_block_num() == last_irreversible_block_num);
   BOOST_CHECK(replay_chain.head().block_num() == last_head_block_num);
} FC_LOG_AND_RETHROW()

// Test replay stopping in the middle of blocks log and resuming
BOOST_FIXTURE_TEST_CASE(replay_stop_in_middle, blog_replay_fixture) try {
   // block `last_irreversible_block_num - 1` is within blocks log
   stop_and_resume_replay(last_irreversible_block_num - 1);
} FC_LOG_AND_RETHROW()

// Test replay stopping in the middle of blocks log and resuming without fork_db
BOOST_FIXTURE_TEST_CASE(replay_stop_in_middle_rm_fork_db, blog_replay_fixture) try {
   // block `last_irreversible_block_num - 1` is within blocks log
   stop_and_resume_replay(last_irreversible_block_num - 1, true);
} FC_LOG_AND_RETHROW()

// Test replay stopping in the middle of reversible blocks and resuming
BOOST_FIXTURE_TEST_CASE(replay_stop_in_reversible_blocks, blog_replay_fixture) try {
   // block `last_head_block_num - 1` is within reversible_blocks, since in Savanna
   // we have at least 2 reversible blocks
   stop_and_resume_replay(last_head_block_num - 1);
} FC_LOG_AND_RETHROW()

// Test replay stopping in the middle of reversible blocks and resuming
BOOST_FIXTURE_TEST_CASE(replay_stop_multiple, blog_replay_fixture) try {
   stop_and_resume_replay(last_irreversible_block_num - 5);
   stop_and_resume_replay(last_irreversible_block_num);
   stop_and_resume_replay(last_head_block_num - 3);
} FC_LOG_AND_RETHROW()

void currupt_blocks_log(path block_dir, block_num_type block_num) {
   fc::datastream<fc::cfile> index_file;
   index_file.set_file_path(block_dir / "blocks.index");
   index_file.open(fc::cfile::update_rw_mode);
   index_file.seek(sizeof(uint64_t) * (block_num + 1));
   uint64_t pos = 0;
   index_file.read((char*)&pos, sizeof(pos));
   index_file.close();

   fc::datastream<fc::cfile> blockslog;
   std::string bad_str = "bad corruption";
   blockslog.set_file_path(block_dir / "blocks.log");
   blockslog.open(fc::cfile::update_rw_mode);
   blockslog.seek(pos);
   blockslog.write((char*)&bad_str, bad_str.size());
   blockslog.flush();
   blockslog.close();
}

// Test exception during replay
BOOST_FIXTURE_TEST_CASE(replay_exception, blog_replay_fixture) try {
   fc::temp_directory tmp_dir;

   eosio::chain::controller::config copied_config = chain.get_config();

   auto genesis = eosio::chain::block_log::extract_genesis_state(copied_config.blocks_dir);
   BOOST_REQUIRE(genesis);

   // remove the state files to make sure we are starting from block log
   remove_existing_states(copied_config.state_dir);
   // backup
   std::filesystem::copy(copied_config.blocks_dir / "blocks.log", tmp_dir.path() / "blocks.log");
   std::filesystem::copy(copied_config.blocks_dir / "blocks.index", tmp_dir.path() / "blocks.index");

   currupt_blocks_log(copied_config.blocks_dir, last_irreversible_block_num-5);

   bool exception_thrown = false;
   try {
      eosio::testing::tester replay_chain(copied_config, *genesis);
   } catch (std::exception& e) {
      exception_thrown = true;
   }

   BOOST_TEST(exception_thrown);

   // restore
   std::filesystem::copy(tmp_dir.path() / "blocks.log", copied_config.blocks_dir / "blocks.log", std::filesystem::copy_options::overwrite_existing);
   std::filesystem::copy(tmp_dir.path() / "blocks.index", copied_config.blocks_dir / "blocks.index", std::filesystem::copy_options::overwrite_existing);

   // verify can restart and it will work
   eosio::testing::tester replay_chain(copied_config);

   // Make sure replayed irreversible_block_num and head_block_num match
   // with last_irreversible_block_num and last_head_block_num
   BOOST_CHECK(replay_chain.last_irreversible_block_num() == last_irreversible_block_num);
   BOOST_CHECK(replay_chain.head().block_num() == last_head_block_num);

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
