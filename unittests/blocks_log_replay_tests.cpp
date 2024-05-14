#include <eosio/testing/tester.hpp>
#include <boost/test/unit_test.hpp>

// Test scenarios
//    * replay through blocks log and reverible blocks
//    * replay stopping in the middle of blocks log and resuming
//    * replay stopping in the middle of reverible blocks and resuming
//

BOOST_AUTO_TEST_SUITE(blocks_log_replay_tests)

using namespace eosio::testing;
using namespace eosio::chain;

struct blog_replay_fixture {
   eosio::testing::tester chain;
   uint32_t               last_head_block_num {0}; // head_block_num at stopping
   uint32_t               last_irreversible_block_num {0}; // LIB at stopping

   // Activate Savanna and create blocks log
   blog_replay_fixture() {
      // Activate Savanna
      size_t num_keys    = 4u;
      size_t finset_size = 4u;
      finalizer_keys fin_keys(chain, num_keys, finset_size); // Create finalizer keys
      fin_keys.set_node_finalizers(0u, num_keys); // set finalizers on current node
      fin_keys.set_finalizer_policy(0u);
      fin_keys.transition_to_savanna();

      // Create a few accounts and produce a few blocks to fill in blocks log
      chain.create_account("replay1"_n);
      chain.produce_blocks(1);
      chain.create_account("replay2"_n);
      chain.produce_blocks(1);
      chain.create_account("replay3"_n);

      chain.produce_blocks(10);

      // Make sure the accounts were created
      BOOST_REQUIRE_NO_THROW(chain.control->get_account("replay1"_n));
      BOOST_REQUIRE_NO_THROW(chain.control->get_account("replay2"_n));
      BOOST_REQUIRE_NO_THROW(chain.control->get_account("replay3"_n));

      // Store head_block_num and irreversible_block_num when the node is stopped
      last_head_block_num = chain.control->head_block_num();
      last_irreversible_block_num = chain.control->last_irreversible_block_num();

      // Stop the node and save blocks_log
      chain.close();
   }

   // Stop replay at block number `stop_at` and resume the replay afterwards
   void stop_and_resume_replay(uint32_t stop_at) try {
      controller::config copied_config = chain.get_config();

      auto genesis = block_log::extract_genesis_state(copied_config.blocks_dir); 
      BOOST_REQUIRE(genesis);

      // remove the state files to make sure starting from blocks log
      remove_existing_states(copied_config);

      // Create a replay chain without starting it
      eosio::testing::tester replay_chain(copied_config, *genesis, false); // false for not starting the chain

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

      // Prepare resuming replay
      controller::config copied_config_1 = replay_chain.get_config();
      auto genesis_1 = block_log::extract_genesis_state(copied_config_1.blocks_dir); 
      BOOST_REQUIRE(genesis_1);

      // Remove the state files to make sure starting from block log
      remove_existing_states(copied_config_1);

      // Resume replay
      eosio::testing::tester replay_chain_1(copied_config_1, *genesis);
      
      // Make sure new chain contain the account created by original chain
      BOOST_REQUIRE_NO_THROW(replay_chain_1.control->get_account("replay1"_n));
      BOOST_REQUIRE_NO_THROW(replay_chain_1.control->get_account("replay2"_n));
      BOOST_REQUIRE_NO_THROW(replay_chain_1.control->get_account("replay3"_n));

      // Make sure replayed irreversible_block_num and head_block_num match
      // with last_irreversible_block_num and last_head_block_num
      BOOST_CHECK(replay_chain_1.control->last_irreversible_block_num() == last_irreversible_block_num);
      BOOST_CHECK(replay_chain_1.control->head_block_num() == last_head_block_num);
   } FC_LOG_AND_RETHROW()

   void remove_existing_states(eosio::chain::controller::config& config) {
      auto state_path = config.state_dir;
      remove_all(state_path);
      std::filesystem::create_directories(state_path);
   }
};

// Test replay through blocks log and reverible blocks
BOOST_FIXTURE_TEST_CASE(replay_through, blog_replay_fixture) try {
   eosio::chain::controller::config copied_config = chain.get_config();

   auto genesis = eosio::chain::block_log::extract_genesis_state(copied_config.blocks_dir); 
   BOOST_REQUIRE(genesis);

   // remove the state files to make sure we are starting from block log
   remove_existing_states(copied_config);
   eosio::testing::tester replay_chain(copied_config, *genesis);

   // Make sure new chain contain the account created by original chain
   BOOST_REQUIRE_NO_THROW(replay_chain.control->get_account("replay1"_n));
   BOOST_REQUIRE_NO_THROW(replay_chain.control->get_account("replay2"_n));
   BOOST_REQUIRE_NO_THROW(replay_chain.control->get_account("replay3"_n));

   // Make sure replayed irreversible_block_num and head_block_num match
   // with last_irreversible_block_num and last_head_block_num
   BOOST_CHECK(replay_chain.control->last_irreversible_block_num() == last_irreversible_block_num);
   BOOST_CHECK(replay_chain.control->head_block_num() == last_head_block_num);
} FC_LOG_AND_RETHROW()

// Test replay stopping in the middle of blocks log and resuming
BOOST_FIXTURE_TEST_CASE(replay_stop_in_middle, blog_replay_fixture) try {
   // block `last_irreversible_block_num - 1` is within blocks log
   stop_and_resume_replay(last_irreversible_block_num - 1);
} FC_LOG_AND_RETHROW()

// Test replay stopping in the middle of reverible blocks and resuming
BOOST_FIXTURE_TEST_CASE(replay_stop_in_reversible_blocks, blog_replay_fixture) try {
   // block `last_head_block_num - 1` is within reversible_blocks, since in Savanna
   // we have at least 2 reversible blocks
   stop_and_resume_replay(last_head_block_num - 1);
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
