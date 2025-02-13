#include <eosio/chain/block_log.hpp>
#include <eosio/testing/tester.hpp> // for fc_exception_message_contains

#include <fc/io/cfile.hpp>

#include <boost/test/unit_test.hpp>

using namespace eosio::chain;
using namespace eosio::testing;

struct block_log_get_block_fixture {
   block_log_get_block_fixture() {
      block_dir = dir.path();

      log.emplace(block_dir);

      log->reset(genesis_state(), signed_block::create_signed_block(signed_block::create_mutable_block({})));
      BOOST_REQUIRE_EQUAL(log->first_block_num(), 1u);
      BOOST_REQUIRE_EQUAL(log->head()->block_num(), 1u);

      for(uint32_t i = 2; i < last_block_num + 1; ++i) {
         auto p = signed_block::create_mutable_block({});
         p->previous._hash[0] = fc::endian_reverse_u32(i-1);
         auto sp = signed_block::create_signed_block(std::move(p));
         log->append(sp, sp->calculate_id());
      }
      BOOST_REQUIRE_EQUAL(log->head()->block_num(), last_block_num);
   };

   void test_read_serialized_block(const block_log& blog, uint32_t block_num) { 
      // read the serialized block
      auto serialized_block = blog.read_serialized_block_by_num(block_num);

      // the serialized block can be deserialized
      BOOST_REQUIRE_NO_THROW(fc::raw::unpack<signed_block>(serialized_block));

      // read the signed block by regular read_block_by_num
      signed_block_ptr block = blog.read_block_by_num(block_num);
      BOOST_REQUIRE(block);

      // the serialized block should match the signed block's serialized form
      BOOST_REQUIRE(serialized_block == fc::raw::pack(*block));
   }

   fc::temp_directory        dir;
   std::filesystem::path     block_dir;
   std::optional<block_log>  log;
   static constexpr uint32_t last_block_num = 50;
};

BOOST_AUTO_TEST_SUITE(block_log_get_block_tests)

BOOST_FIXTURE_TEST_CASE(basic_block_log, block_log_get_block_fixture) try {
   // test reading a non-last-block
   test_read_serialized_block(*log, last_block_num - 2);

   // test reading last block
   test_read_serialized_block(*log, last_block_num);
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(splitted_block_log, block_log_get_block_fixture) try {
   uint32_t stride = last_block_num / 2;
   auto retained_dir = block_dir / "retained";

   block_log::split_blocklog(block_dir, retained_dir, stride);

   std::filesystem::remove(block_dir / "blocks.log");
   std::filesystem::remove(block_dir / "blocks.index");

   block_log blog(block_dir, partitioned_blocklog_config{ .retained_dir = retained_dir });

   // test reading a block in the first partitioned log
   test_read_serialized_block(blog, stride - 1);

   // test reading a block in the second partitioned log
   test_read_serialized_block(blog, stride + 1);
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(nonexisting_block_num, block_log_get_block_fixture) try {
   // read a non-existing block
   auto serialized_block = log->read_serialized_block_by_num(last_block_num + 1);

   // should return an empty vector of char
   BOOST_REQUIRE(serialized_block.empty());
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(corrupted_next_block_position, block_log_get_block_fixture) try {
   // intentionally modify block position for next block (which is the last block)
   uint64_t bad_pos = sizeof(uint64_t) * (last_block_num);
   fc::datastream<fc::cfile> index_file;
   index_file.set_file_path(block_dir / "blocks.index");
   index_file.open(fc::cfile::update_rw_mode);
   index_file.seek_end(-sizeof(uint64_t));
   index_file.write((char*)&bad_pos, sizeof(bad_pos));
   index_file.flush();
   index_file.close();

   BOOST_CHECK_EXCEPTION(log->read_serialized_block_by_num(last_block_num - 1),
                         block_log_exception,
                         fc_exception_message_contains("next block position"));
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(corrupted_file_size, block_log_get_block_fixture) try {
   // corrupt file size by truncating it
   auto new_size = log->get_block_pos(last_block_num) + sizeof(uint64_t);
   std::filesystem::resize_file(block_dir / "blocks.log", new_size);

   BOOST_CHECK_EXCEPTION(log->read_serialized_block_by_num(last_block_num),
                         block_log_exception,
                         fc_exception_message_contains("block log file size"));
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
