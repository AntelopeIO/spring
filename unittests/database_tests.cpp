#include <eosio/chain/global_property_object.hpp>
#include <eosio/testing/tester.hpp>

#include <boost/test/unit_test.hpp>

using namespace eosio::chain;
using namespace eosio::testing;

BOOST_AUTO_TEST_SUITE(database_tests)

   // Simple tests of undo infrastructure
   BOOST_AUTO_TEST_CASE_TEMPLATE( undo_test, T, validating_testers ) {
      try {
         T test;

         // Bypass read-only restriction on state DB access for this unit test which really needs to mutate the DB to properly conduct its test.
         eosio::chain::database& db = const_cast<eosio::chain::database&>( test.control->db() );

         auto ses = db.start_undo_session(true);

         // Create an account
         db.create<account_object>([](account_object &a) {
            a.name = name("billy");
         });

         // Make sure we can retrieve that account by name
         auto ptr = db.find<account_object, by_name>(name("billy"));
         BOOST_TEST(ptr != nullptr);

         // Undo creation of the account
         ses.undo();

         // Make sure we can no longer find the account
         ptr = db.find<account_object, by_name>(name("billy"));
         BOOST_TEST(ptr == nullptr);
      } FC_LOG_AND_RETHROW()
   }

   // Test the block fetching methods on database, fetch_bock_by_id, and fetch_block_by_number
   BOOST_AUTO_TEST_CASE_TEMPLATE( get_blocks, T, validating_testers ) {
      try {
         T test;
         vector<block_id_type> block_ids;

         const uint32_t num_of_blocks_to_prod = 20;
         // Produce 20 blocks and check their IDs should match the above
         test.produce_blocks(num_of_blocks_to_prod);
         for (uint32_t i = 0; i < num_of_blocks_to_prod; ++i) {
            block_ids.emplace_back(test.fetch_block_by_number(i + 1)->calculate_id());
            BOOST_TEST(block_header::num_from_id(block_ids.back()) == i + 1);
            BOOST_TEST(test.fetch_block_by_number(i + 1)->calculate_id() == block_ids.back());
         }

         // Check the last irreversible block number is set correctly.
         if constexpr (std::is_same_v<T, savanna_validating_tester>) {
            // In Savanna, after 2-chain finality is achieved.
            const auto expected_last_irreversible_block_number = test.head().block_num() - num_chains_to_final;
            BOOST_TEST(test.last_irreversible_block_num() == expected_last_irreversible_block_number);
         } else {
            // With one producer, irreversibility should only just 1 block before
            const auto expected_last_irreversible_block_number = test.head().block_num() - 1;
            BOOST_TEST(test.control->head_block_state_legacy()->dpos_irreversible_blocknum == expected_last_irreversible_block_number);
         }

         // Ensure that future block doesn't exist
         const auto nonexisting_future_block_num = test.head().block_num() + 1;
         BOOST_TEST(test.fetch_block_by_number(nonexisting_future_block_num) == nullptr);

         const uint32_t next_num_of_blocks_to_prod = 10;
         test.produce_blocks(next_num_of_blocks_to_prod);

         // Check the last irreversible block number is updated correctly
         if constexpr (std::is_same_v<T, savanna_validating_tester>) {
            const auto next_expected_last_irreversible_block_number = test.head().block_num() - num_chains_to_final;
            BOOST_TEST(test.last_irreversible_block_num() == next_expected_last_irreversible_block_number);
         } else {
            const auto next_expected_last_irreversible_block_number = test.head().block_num() - 1;
            BOOST_TEST(test.control->head_block_state_legacy()->dpos_irreversible_blocknum == next_expected_last_irreversible_block_number);
         }
         // Previous nonexisting future block should exist by now
         BOOST_CHECK_NO_THROW(test.fetch_block_by_number(nonexisting_future_block_num));
         // Check the latest head block match
         BOOST_TEST(test.fetch_block_by_number(test.head().block_num())->calculate_id() ==
                    test.head().id());

         // Verify LIB can be found
         const auto lib_num = test.last_irreversible_block_num();
         auto lib           = test.fetch_block_by_number(lib_num);
         BOOST_REQUIRE(lib);
         auto lib_id = lib->calculate_id();
         BOOST_TEST(lib_num == lib->block_num());
         lib = test.fetch_block_by_id(lib_id);
         BOOST_REQUIRE(lib);
         BOOST_TEST(lib->calculate_id() == lib_id);

      } FC_LOG_AND_RETHROW()
   }

   // Test the block fetching methods on database, fetch_bock_by_id, and fetch_block_by_number
   BOOST_AUTO_TEST_CASE_TEMPLATE( get_blocks_no_block_log, T, validating_testers ) {
      try {
         fc::temp_directory tempdir;

         constexpr bool use_genesis = true;
         T test(
            tempdir,
            [&](controller::config& cfg) {
               cfg.blog = eosio::chain::empty_blocklog_config{};
            },
            use_genesis
         );

         // Ensure that future block doesn't exist
         const auto nonexisting_future_block_num = test.head().block_num() + 1;
         BOOST_TEST(test.fetch_block_by_number(nonexisting_future_block_num) == nullptr);
         BOOST_TEST(test.fetch_block_by_id(sha256::hash("xx")) == nullptr);

         test.produce_block();

         // Previous nonexisting future block should exist now
         BOOST_CHECK_NO_THROW(test.fetch_block_by_number(nonexisting_future_block_num));
         // Check the latest head block match
         BOOST_TEST(test.fetch_block_by_number(test.head().block_num())->calculate_id() == test.head().id());
         BOOST_TEST(test.fetch_block_by_id(test.head().id())->calculate_id() == test.head().id());

         // Verify LIB can be found
         const auto lib_num = test.last_irreversible_block_num();
         auto lib           = test.fetch_block_by_number(lib_num);
         BOOST_REQUIRE(lib);
         auto lib_id = lib->calculate_id();
         BOOST_TEST(lib_num == lib->block_num());
         lib = test.fetch_block_by_id(lib_id);
         BOOST_REQUIRE(lib);
         BOOST_TEST(lib->calculate_id() == lib_id);

      } FC_LOG_AND_RETHROW()
   }

BOOST_AUTO_TEST_SUITE_END()
