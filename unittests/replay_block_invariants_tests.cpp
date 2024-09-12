#include <eosio/testing/tester.hpp>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(replay_block_invariants_tests)

using namespace eosio::testing;
using namespace eosio::chain;

struct blog_replay_fixture {
   eosio::testing::tester chain;

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

      // Stop the node and save blocks_log
      chain.close();
   }

   void remove_existing_states(std::filesystem::path& state_path) {
      std::filesystem::remove_all(state_path);
      std::filesystem::create_directories(state_path);
   }

   void corrupt_qc_extension_in_block_log() {
      controller::config config = chain.get_config();
      auto blocks_dir = chain.get_config().blocks_dir;

      block_log blog(blocks_dir, chain.get_config().blog);

      // find the first block which has QC extension starting from the end of
      // block log and going backward
      uint32_t block_num = blog.head()->block_num();
      auto qc_ext_id = quorum_certificate_extension::extension_id();
      while (!blog.read_block_by_num(block_num)->contains_extension(qc_ext_id)) {
         --block_num;
         BOOST_REQUIRE(block_num != 0);
      }

      // clone the QC block
      signed_block_ptr qc_block = std::make_shared<signed_block>(blog.read_block_by_num(block_num)->clone());
      BOOST_TEST(qc_block);

      // remove qc block until the end from block log
      BOOST_REQUIRE_NO_THROW(block_log::trim_blocklog_end(blocks_dir, block_num-1));
      BOOST_REQUIRE_NO_THROW(block_log::smoke_test(blocks_dir, 1));

      // retrieve QC block extension from QC block
      auto block_exts = qc_block->validate_and_extract_extensions();
      auto qc_ext_itr = block_exts.find(qc_ext_id);
      auto& qc_ext    = std::get<quorum_certificate_extension>(qc_ext_itr->second);
      auto& qc        = qc_ext.qc;

      // remove QC block extension from QC block
      auto& exts = qc_block->block_extensions;
      std::pair<uint16_t,vector<char>> target{qc_ext_id, {}};
      auto itr = std::lower_bound(exts.begin(), exts.end(), target, [](const auto& ext1, const auto& ext2){
         return ext1.first < ext2.first;
      });
      BOOST_REQUIRE(itr != exts.end());
      qc_block->block_extensions.erase(itr);

      // intentionally corrupt QC's strength.
      if (qc.active_policy_sig.strong_votes) {
         (*qc.active_policy_sig.strong_votes)[0] ^= 1; // flip one bit
      } else if (qc.active_policy_sig.weak_votes) {
         (*qc.active_policy_sig.weak_votes)[0] ^= 1; // flip one bit
      }

      // add the corrupted QC block extension back to the block
      emplace_extension(exts, qc_ext_id, fc::raw::pack(qc_ext));

      // add the corrupted block back to block log
      block_log new_blog(blocks_dir, config.blog);
      new_blog.append(qc_block, qc_block->calculate_id());
   }

   void corrupt_finality_extension_in_block_log(uint32_t new_qc_claim_block_num) {
      controller::config config = chain.get_config();
      auto blocks_dir = chain.get_config().blocks_dir;

      block_log blog(blocks_dir, chain.get_config().blog);

      // retrieve the last block in block log
      uint32_t  last_block_num = blog.head()->block_num();
      signed_block_ptr last_block = std::make_shared<signed_block>(blog.read_block_by_num(last_block_num)->clone());
      BOOST_TEST(last_block);

      // remove last block from block log
      BOOST_REQUIRE_NO_THROW(block_log::trim_blocklog_end(blocks_dir, last_block_num-1));
      BOOST_REQUIRE_NO_THROW(block_log::smoke_test(blocks_dir, 1));

      // retrieve finality extension
      std::optional<block_header_extension> head_fin_ext = last_block->extract_header_extension(finality_extension::extension_id());
      BOOST_TEST(!!head_fin_ext);

      // remove finality extension from extensions
      auto& exts = last_block->header_extensions;
      std::pair<uint16_t,vector<char>> target{finality_extension::extension_id(), {}};
      auto itr = std::lower_bound(exts.begin(), exts.end(), target, [](const auto& ext1, const auto& ext2){
         return ext1.first < ext2.first;
      });
      BOOST_REQUIRE(itr != exts.end());
      exts.erase(itr);

      // intentionally corrupt finality extension by changing its block_num
      auto& f_ext = std::get<finality_extension>(*head_fin_ext);
      f_ext.qc_claim.block_num = new_qc_claim_block_num;

      // add the corrupted finality extension back to last block
      emplace_extension(exts, finality_extension::extension_id(), fc::raw::pack(f_ext));

      // add the corrupted block to block log
      block_log new_blog(blocks_dir, config.blog);
      new_blog.append(last_block, last_block->calculate_id());
   }
};

// Test replay with invalid QC claim -- claimed block number goes backward
BOOST_FIXTURE_TEST_CASE(invalid_qc, blog_replay_fixture) try {
   controller::config config = chain.get_config();
   auto blocks_dir = chain.get_config().blocks_dir;

   // set claimed block number backward
   corrupt_finality_extension_in_block_log(0);

   // retrieve genesis
   block_log blog(blocks_dir, chain.get_config().blog);
   auto genesis = block_log::extract_genesis_state(blocks_dir);
   BOOST_REQUIRE(genesis);

   // remove the state files to make sure we are starting from block log
   remove_existing_states(config.state_dir);

   try {
      eosio::testing::tester replay_chain(config, *genesis); // // start replay
      BOOST_FAIL("replay should have failed with invalid_qc_claim exception");
   } catch (invalid_qc_claim &) {
   } catch (...) {
      BOOST_FAIL("replay failed with non invalid_qc_claim exception");
   }
} FC_LOG_AND_RETHROW()

// Test replay with irrelevant QC -- claims a block number other than the one
// claimed in the block header
BOOST_FIXTURE_TEST_CASE(irrelevant_qc, blog_replay_fixture) try {
   controller::config config = chain.get_config();
   auto blocks_dir = chain.get_config().blocks_dir;

   block_log blog(blocks_dir, chain.get_config().blog);

   // retrieve the last block in block log
   uint32_t  last_block_num = blog.head()->block_num();

   // set claimed block number to a non-existent number
   corrupt_finality_extension_in_block_log(last_block_num + 1);

   // retrieve genesis
   auto genesis = block_log::extract_genesis_state(blocks_dir); 
   BOOST_REQUIRE(genesis);

   // remove the state files to make sure we are starting from block log
   remove_existing_states(config.state_dir);

   try {
      eosio::testing::tester replay_chain(config, *genesis); // start replay
      BOOST_FAIL("replay should have failed with block_validate_exception exception");
   } catch (block_validate_exception &) {
   } catch (...) {
      BOOST_FAIL("replay failed with non block_validate_exception exception");
   }
} FC_LOG_AND_RETHROW()

// Test replay with bad QC (signature validation), but run without --force-all-checks.
// Replay should pass as QC is not validated.
BOOST_FIXTURE_TEST_CASE(bad_qc_no_force_all_checks, blog_replay_fixture) try {
   controller::config config = chain.get_config();
   auto blocks_dir = chain.get_config().blocks_dir;

   corrupt_qc_extension_in_block_log();

   // remove the state files to make sure we are starting from block log
   remove_existing_states(config.state_dir);

   auto genesis = block_log::extract_genesis_state(blocks_dir); 
   BOOST_REQUIRE(genesis);

   try {
      eosio::testing::tester replay_chain(config, *genesis); // start replay
   } catch (...) {
      BOOST_FAIL("replay should not fail without --force-all-checks");
   }
} FC_LOG_AND_RETHROW()

// Test replay with bad QC (signature validation), but run with --force-all-checks.
// Replay should fail as QC is validated.
BOOST_FIXTURE_TEST_CASE(bad_qc_force_all_checks, blog_replay_fixture) try {
   controller::config config = chain.get_config();
   auto blocks_dir = chain.get_config().blocks_dir;

   corrupt_qc_extension_in_block_log();

   // remove the state files to make sure we are starting from block log
   remove_existing_states(config.state_dir);

   auto genesis = block_log::extract_genesis_state(blocks_dir); 
   BOOST_REQUIRE(genesis);

   config.force_all_checks = true;

   try {
      eosio::testing::tester replay_chain(config, *genesis); // start replay
      BOOST_FAIL("replay should have failed with --force-all-checks");
   } catch (block_validate_exception& e) {
      BOOST_REQUIRE(e.to_detail_string().find("strong quorum is not met") != std::string::npos);
   } catch (...) {
      BOOST_FAIL("replay failed with non block_validate_exception exception");
   }
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
