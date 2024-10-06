#include <boost/test/unit_test.hpp>
#include <boost/test/data/test_case.hpp>
#include <boost/test/data/monomorphic.hpp>
#include <boost/test/data/monomorphic/generators/xrange.hpp>

#include <fc/io/raw.hpp>
#include <fc/io/cfile.hpp>
#include <fc/bitutil.hpp>
#include <fc/crypto/rand.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/io/fstream.hpp>

#include <eosio/state_history/log_catalog.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/device/back_inserter.hpp>


namespace bdata = boost::unit_test::data;
namespace bio = boost::iostreams;
using namespace eosio;
using namespace eosio::chain;
using namespace fc;
using namespace std::literals;

static block_id_type fake_blockid_for_num(const block_num_type block_num, const uint64_t salt = 0u) {
   block_id_type ret;
   ret._hash[0] = ret._hash[1] = ret._hash[2] = ret._hash[3] = salt;
   ret._hash[0] &= 0xffffffff00000000;
   ret._hash[0] += fc::endian_reverse_u32(block_num);
   return ret;
}

struct random_source {
   typedef char char_type;
   struct category : bio::seekable_device_tag {};
   std::streamsize read(char* s, std::streamsize n) {
      if(n == 0)     //why doesn't restrict() do this for us?
         return -1;
      rand_bytes(s, n);
      return n;
   }
   std::streamsize write(const char_type* s, std::streamsize n) {
      FC_ASSERT(false, "not supported");
   }
   //this isn't valid but Device needs to be Seekable for restrict()
   std::streampos seek(bio::stream_offset off, std::ios_base::seekdir) {
      return off;
   }
};
BOOST_IOSTREAMS_PIPABLE(random_source, 0)

struct sha256_filter {
   typedef char char_type;
   struct category : bio::dual_use, bio::filter_tag, bio::multichar_tag, bio::optimally_buffered_tag {};

   std::streamsize optimal_buffer_size() const {return 4*1024;}

   template<typename Source>
   std::streamsize read(Source& src, char_type* s, std::streamsize n) {
      std::streamsize result = bio::read(src, s, n);
      if(result == -1)
         return -1;
      enc->write(s, result);
      return result;
   }

   template<typename Sink>
   std::streamsize write(Sink& snk, const char_type* s, std::streamsize n) {
      std::streamsize result = bio::write(snk, s, n);
      enc->write(s, result);
      return result;
   }

   //sha256::encoder is not copyable which is a requirement for a Filter when used in a pipeline like done below. for this trivial
   // non-production use case let's just hack the limitation by stuffing it in shared_ptr so it becomes "copyable"
   std::shared_ptr<sha256::encoder> enc = std::make_shared<sha256::encoder>();
};
BOOST_IOSTREAMS_PIPABLE(sha256_filter, 0)

struct ship_log_fixture {
   ship_log_fixture(bool enable_read, bool reopen_on_mark, bool remove_index_on_reopen, bool vacuum_on_exit_if_small, std::optional<uint32_t> prune_blocks) :
     enable_read(enable_read), reopen_on_mark(reopen_on_mark),
     remove_index_on_reopen(remove_index_on_reopen), vacuum_on_exit_if_small(vacuum_on_exit_if_small){
      if (prune_blocks)
         conf = eosio::state_history::prune_config{ .prune_blocks = *prune_blocks };
      bounce();
   }

   void add(uint32_t index, size_t size, char fillchar, char prevchar) {
      std::vector<char> a;
      a.assign(size, fillchar);

      auto block_for_id = [](const uint32_t bnum, const char fillc) {
         fc::sha256 m = fc::sha256::hash(fc::sha256::hash(std::to_string(bnum)+fillc));
         m._hash[0] = fc::endian_reverse_u32(bnum);
         return m;
      };

      log->pack_and_write_entry(block_for_id(index, fillchar), block_for_id(index-1, prevchar), [&](auto& f) {
         bio::write(f, a.data(), a.size());
      });

      if(index + 1 > written_data.size())
         written_data.resize(index + 1);
      written_data.at(index) = a;
   }

   void check_range_present(uint32_t first, uint32_t last) {
      auto r = log->block_range();
      BOOST_REQUIRE_EQUAL(r.first, first);
      BOOST_REQUIRE_EQUAL(r.second-1, last);
      if(enable_read) {
         for(auto i = first; i <= last; i++) {
            std::optional<eosio::state_history::ship_log_entry> entry = log->get_entry(i);
            BOOST_REQUIRE(!!entry);
            bio::filtering_istreambuf istream = entry->get_stream();
            std::vector<char> buff;
            bio::copy(istream, bio::back_inserter(buff));
            BOOST_REQUIRE(buff == written_data.at(i));
         }
      }
   }

   void check_not_present(uint32_t index) {
      BOOST_REQUIRE(!log->get_entry(index));;
   }

   void check_empty() {
      BOOST_REQUIRE(log->empty());
   }

   //double the fun
   template <typename F>
   void check_n_bounce(F&& f) {
      f();
      if(reopen_on_mark) {
         bounce();
         f();
      }
   }

   bool enable_read, reopen_on_mark, remove_index_on_reopen, vacuum_on_exit_if_small;
   eosio::state_history::state_history_log_config conf;
   fc::temp_directory log_dir;

   std::optional<eosio::state_history::log_catalog> log;

   std::vector<std::vector<char>> written_data;

private:
   void bounce() {
      log.reset();
      if(remove_index_on_reopen)
         std::filesystem::remove(log_dir.path()/ (std::string("shipit") + ".index"));
      auto prune_conf = std::get_if<eosio::state_history::prune_config>(&conf);
      if(prune_conf) {
         prune_conf->prune_threshold = 8; //every 8 bytes check in and see if to prune. should make it always check after each entry for us
         if(vacuum_on_exit_if_small)
            prune_conf->vacuum_on_close = 1024*1024*1024; //something large: always vacuum on close for these tests
      }
      log.emplace(log_dir.path(), conf, "shipit");
   }
};

//can only punch holes on filesystem block boundaries. let's make sure the entries we add are larger than that
static size_t larger_than_tmpfile_blocksize() {
   fc::temp_cfile tf;
   auto& cf = tf.file();
   return cf.filesystem_block_size() + cf.filesystem_block_size()/2;
}

BOOST_AUTO_TEST_SUITE(ship_file_tests)

BOOST_DATA_TEST_CASE(basic_prune_test, bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2), enable_read, reopen_on_mark, remove_index_on_reopen, vacuum_on_exit_if_small)  { try {
   ship_log_fixture t(enable_read, reopen_on_mark, remove_index_on_reopen, vacuum_on_exit_if_small, 4);

   t.check_empty();

   //with a small prune blocks value, the log will attempt to prune every filesystem block size. So let's just make
   // every entry be greater than that size
   size_t payload_size = larger_than_tmpfile_blocksize();

   //we'll start at 2 here, since that's what you'd get from starting from genesis, but it really doesn't matter
   // one way or another for the ship log logic
   t.add(2, payload_size, 'A', 'A');
   t.add(3, payload_size, 'B', 'A');
   t.add(4, payload_size, 'C', 'B');
   t.check_n_bounce([&]() {
      t.check_range_present(2, 4);
   });

   t.add(5, payload_size, 'D', 'C');
   t.check_n_bounce([&]() {
      t.check_range_present(2, 5);
   });

   t.add(6, payload_size, 'E', 'D');
   t.check_n_bounce([&]() {
      t.check_not_present(2);
      t.check_range_present(3, 6);
   });

   t.add(7, payload_size, 'F', 'E');
   t.check_n_bounce([&]() {
      t.check_not_present(2);
      t.check_not_present(3);
      t.check_range_present(4, 7);
   });

   //undo 6 & 7 and reapply 6
   t.add(6, payload_size, 'G', 'D');
   t.check_n_bounce([&]() {
      t.check_not_present(2);
      t.check_not_present(3);
      t.check_not_present(7);
      t.check_range_present(4, 6);
   });

   t.add(7, payload_size, 'H', 'G');
   t.check_n_bounce([&]() {
      t.check_not_present(2);
      t.check_not_present(3);
      t.check_range_present(4, 7);
   });

   t.add(8, payload_size, 'I', 'H');
   t.add(9, payload_size, 'J', 'I');
   t.add(10, payload_size, 'K', 'J');
   t.check_n_bounce([&]() {
      t.check_range_present(7, 10);
   });

   //undo back to the first stored block
   t.add(7, payload_size, 'L', 'G');
   t.check_n_bounce([&]() {
      t.check_range_present(7, 7);
      t.check_not_present(6);
      t.check_not_present(8);
   });

   t.add(8, payload_size, 'M', 'L');
   t.add(9, payload_size, 'N', 'M');
   t.add(10, payload_size, 'O', 'N');
   t.add(11, payload_size, 'P', 'O');
   t.check_n_bounce([&]() {
      t.check_range_present(8, 11);
      t.check_not_present(6);
      t.check_not_present(7);
   });

   //pile up more
   t.add(12, payload_size, 'W', 'P');
   t.add(13, payload_size, 'X', 'W');
   t.add(14, payload_size, 'Y', 'X');
   t.add(15, payload_size, 'Z', 'Y');
   t.add(16, payload_size, '1', 'Z');
   t.check_n_bounce([&]() {
      t.check_range_present(13, 16);
      t.check_not_present(12);
      t.check_not_present(17);
   });

   //invalid fork, previous should be 'X'
   BOOST_REQUIRE_EXCEPTION(t.add(14, payload_size, '*', 'W' ), eosio::chain::plugin_exception, [](const eosio::chain::plugin_exception& e) {
      return e.to_detail_string().find("missed a fork change") != std::string::npos;
   });

} FC_LOG_AND_RETHROW() }

BOOST_DATA_TEST_CASE(basic_test, bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2), enable_read, reopen_on_mark, remove_index_on_reopen)  { try {
   ship_log_fixture t(enable_read, reopen_on_mark, remove_index_on_reopen, false, std::optional<uint32_t>());

   t.check_empty();
   size_t payload_size = larger_than_tmpfile_blocksize();

   //we'll start off with a high number; but it really doesn't matter for ship's logs
   t.add(200, payload_size, 'A', 'A');
   t.add(201, payload_size, 'B', 'A');
   t.add(202, payload_size, 'C', 'B');
   t.check_n_bounce([&]() {
      t.check_range_present(200, 202);
   });
   t.add(203, payload_size, 'D', 'C');
   t.add(204, payload_size, 'E', 'D');
   t.add(205, payload_size, 'F', 'E');
   t.add(206, payload_size, 'G', 'F');
   t.add(207, payload_size, 'H', 'G');
   t.check_n_bounce([&]() {
      t.check_range_present(200, 207);
   });

   //fork off G & H
   t.add(206, payload_size, 'I', 'F');
   t.add(207, payload_size, 'J', 'I');
   t.check_n_bounce([&]() {
      t.check_range_present(200, 207);
   });

   t.add(208, payload_size, 'K', 'J');
   t.add(209, payload_size, 'L', 'K');
   t.check_n_bounce([&]() {
      t.check_range_present(200, 209);
      t.check_not_present(199);
      t.check_not_present(210);
   });

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(empty) { try {
   fc::temp_directory log_dir;

   {
      eosio::state_history::state_history_log log(log_dir.path()/ "empty");
      BOOST_REQUIRE(log.empty());
   }
   //reopen
   {
      eosio::state_history::state_history_log log(log_dir.path() / "empty");
      BOOST_REQUIRE(log.empty());
   }
   //reopen but prunned set
   const eosio::state_history::prune_config simple_prune_conf = {
      .prune_blocks = 4
   };
   {
      eosio::state_history::state_history_log log(log_dir.path() / "empty", state_history::state_history_log::no_non_local_get_block_id_func, simple_prune_conf);
      BOOST_REQUIRE(log.empty());
   }
   {
      eosio::state_history::state_history_log log(log_dir.path() / "empty", state_history::state_history_log::no_non_local_get_block_id_func, simple_prune_conf);
      BOOST_REQUIRE(log.empty());
   }
   //back to non pruned
   {
      eosio::state_history::state_history_log log(log_dir.path() / "empty");
      BOOST_REQUIRE(log.empty());
   }
   {
      eosio::state_history::state_history_log log(log_dir.path() / "empty");
      BOOST_REQUIRE(log.empty());
   }

   auto log_file = (log_dir.path()/ (std::string("empty") + ".log")).string();
   auto index_file = (log_dir.path()/ (std::string("empty") + ".index")).string();

   BOOST_REQUIRE(std::filesystem::file_size(log_file.c_str()) == 0);
   BOOST_REQUIRE(std::filesystem::file_size(index_file.c_str()) == 0);

   //one more time to pruned, just to make sure
   {
      eosio::state_history::state_history_log log(log_dir.path()/ "empty", state_history::state_history_log::no_non_local_get_block_id_func, simple_prune_conf);
      BOOST_REQUIRE(log.empty());
   }
   BOOST_REQUIRE(std::filesystem::file_size(log_file.c_str()) == 0);
   BOOST_REQUIRE(std::filesystem::file_size(index_file.c_str()) == 0);
}  FC_LOG_AND_RETHROW() }

BOOST_DATA_TEST_CASE(non_prune_to_prune, bdata::xrange(2) * bdata::xrange(2), enable_read, remove_index_on_reopen)  { try {
   ship_log_fixture t(enable_read, true, remove_index_on_reopen, false, std::optional<uint32_t>());

   t.check_empty();
   size_t payload_size = larger_than_tmpfile_blocksize();

   t.add(2, payload_size, 'A', 'A');
   t.add(3, payload_size, 'B', 'A');
   t.add(4, payload_size, 'C', 'B');
   t.add(5, payload_size, 'D', 'C');
   t.add(6, payload_size, 'E', 'D');
   t.add(7, payload_size, 'F', 'E');
   t.add(8, payload_size, 'G', 'F');
   t.add(9, payload_size, 'H', 'G');
   t.check_n_bounce([&]() {
      t.check_range_present(2, 9);
   });

   //upgrade to pruned...
   t.conf = eosio::state_history::prune_config{ .prune_blocks = 4 };
   t.check_n_bounce([]() {});

   t.check_n_bounce([&]() {
      t.check_range_present(6, 9);
   });
   t.add(10, payload_size, 'I', 'H');
   t.add(11, payload_size, 'J', 'I');
   t.add(12, payload_size, 'K', 'J');
   t.add(13, payload_size, 'L', 'K');
   t.check_n_bounce([&]() {
      t.check_range_present(10, 13);
   });

} FC_LOG_AND_RETHROW() }

BOOST_DATA_TEST_CASE(prune_to_non_prune, bdata::xrange(2) * bdata::xrange(2), enable_read, remove_index_on_reopen)  { try {
   ship_log_fixture t(enable_read, true, remove_index_on_reopen, false, 4);

   t.check_empty();
   size_t payload_size = larger_than_tmpfile_blocksize();

   t.add(2, payload_size, 'A', 'X');
   t.add(3, payload_size, 'B', 'A');
   t.add(4, payload_size, 'C', 'B');
   t.add(5, payload_size, 'D', 'C');
   t.add(6, payload_size, 'E', 'D');
   t.add(7, payload_size, 'F', 'E');
   t.add(8, payload_size, 'G', 'F');
   t.add(9, payload_size, 'H', 'G');
   t.check_n_bounce([&]() {
      t.check_range_present(6, 9);
   });

   //no more pruned
   t.conf = std::monostate{};
   t.check_n_bounce([]() {});

   t.check_n_bounce([&]() {
      t.check_range_present(6, 9);
   });
   t.add(10, payload_size, 'I', 'H');
   t.add(11, payload_size, 'J', 'I');
   t.add(12, payload_size, 'K', 'J');
   t.add(13, payload_size, 'L', 'K');
   t.add(14, payload_size, 'M', 'L');
   t.add(15, payload_size, 'N', 'M');
   t.check_n_bounce([&]() {
      t.check_range_present(6, 15);
   });

} FC_LOG_AND_RETHROW() }

BOOST_DATA_TEST_CASE(prune_to_partitioned, bdata::xrange(2) * bdata::xrange(2), enable_read, remove_index_on_reopen)  { try {
   ship_log_fixture t(enable_read, true, remove_index_on_reopen, false, 4);

   t.check_empty();
   size_t payload_size = larger_than_tmpfile_blocksize();

   t.add(2, payload_size, 'A', 'A');
   t.add(3, payload_size, 'B', 'A');
   t.add(4, payload_size, 'C', 'B');
   t.add(5, payload_size, 'D', 'C');
   t.add(6, payload_size, 'E', 'D');
   t.add(7, payload_size, 'F', 'E');
   t.add(8, payload_size, 'G', 'F');
   t.add(9, payload_size, 'H', 'G');
   t.check_n_bounce([&]() {
      t.check_range_present(6, 9);
   });

   //no more pruned
   t.conf = eosio::state_history::partition_config{
       .stride  = 5
   };

   t.check_n_bounce([]() {});

   t.check_n_bounce([&]() {
      t.check_range_present(6, 9);
   });
   t.add(10, payload_size, 'I', 'H');
   t.add(11, payload_size, 'J', 'I');
   t.add(12, payload_size, 'K', 'J');
   t.add(13, payload_size, 'L', 'K');
   t.add(14, payload_size, 'M', 'L');
   t.add(15, payload_size, 'N', 'M');
   t.check_n_bounce([&]() {
      t.check_range_present(6, 15);
   });

} FC_LOG_AND_RETHROW() }

BOOST_DATA_TEST_CASE(basic, bdata::make({2u, 333u, 578'000u, 3'123'456'789u}) ^ bdata::make({102u, 400u, 578'111u, 3'123'456'900u}), start, end) try {
   const fc::temp_directory tmpdir;

   eosio::state_history::log_catalog lc(tmpdir.path(), std::monostate(), "testlog");
   BOOST_REQUIRE(lc.empty());

   std::map<block_num_type, sha256> wrote_data_for_blocknum;
   std::mt19937 mt_random(0xbeefbeefu * start);

   //write some blocks in order
   for(unsigned i = start; i < end; ++i)
      lc.pack_and_write_entry(fake_blockid_for_num(i), fake_blockid_for_num(i-1), [&](bio::filtering_ostreambuf& obuf) {
         bio::filtering_istreambuf hashed_randomness(sha256_filter() | bio::restrict(random_source(), 0, mt_random()%16*1024*1024));
         bio::copy(hashed_randomness, obuf);
         wrote_data_for_blocknum[i] = hashed_randomness.component<sha256_filter>(0)->enc->result();
      });

   BOOST_REQUIRE_EQUAL(lc.block_range().first, start);
   BOOST_REQUIRE_EQUAL(lc.block_range().second, end);

   //pick some random blocks and read their content back; make sure it matches.
   for(unsigned i = start; i < end; i+=mt_random()%10) {
      std::optional<state_history::ship_log_entry> entry = lc.get_entry(i);
      BOOST_REQUIRE(!!entry);

      std::optional<chain::block_id_type> bid = lc.get_block_id(i);
      BOOST_REQUIRE(!!bid);
      BOOST_REQUIRE_EQUAL(*bid, fake_blockid_for_num(i));

      bio::filtering_ostreambuf hashed_null(sha256_filter() | bio::null_sink());
      bio::filtering_istreambuf log_stream = entry->get_stream();
      bio::copy(log_stream, hashed_null);
      BOOST_REQUIRE_EQUAL(hashed_null.component<sha256_filter>(0)->enc->result(), wrote_data_for_blocknum[i]);
   }

   //pick some blocks outside the range of blocks we wrote and make sure we cannot read them
   for(const unsigned i : {1u, start-34, start-1, end, end+56, end+1004}) {  //start-34 might roll over; no big deal
      std::optional<chain::block_id_type> bid = lc.get_block_id(i);
      std::optional<state_history::ship_log_entry> entry = lc.get_entry(i);
      BOOST_REQUIRE(!bid);
      BOOST_REQUIRE(!entry);
   }

   //"end" would be the next block to be appended
   //attempt to "skip" a block
   {
      unsigned skipped_block = end + 1;
      BOOST_REQUIRE_EXCEPTION(lc.pack_and_write_entry(fake_blockid_for_num(skipped_block), fake_blockid_for_num(skipped_block-1), [&](bio::filtering_ostreambuf& obuf) {
                                 FC_ASSERT(false, "should not reach here");
                              }),
                              plugin_exception,
                              [](const plugin_exception& e) {return e.to_detail_string().find("skips over block") != std::string::npos;});
   }

   //now let's try appending a block that doesn't have the right previous block id
   {
      BOOST_REQUIRE_EXCEPTION(lc.pack_and_write_entry(fake_blockid_for_num(end), fake_blockid_for_num(end-1, 0xbeefUL), [&](bio::filtering_ostreambuf& obuf) {
                                 FC_ASSERT(false, "should not reach here");
                              }),
                              plugin_exception,
                              [](const plugin_exception& e) {return e.to_detail_string().find("missed a fork change") != std::string::npos;});
   }

   //now we're going to try writing identical blockids to the log. These should be silently swallowed as no-ops
   for(unsigned i : {start, start+6, end-5, end-1}) {
      //but block 2 is special. Writing block 2 on a non empty log will fail if the blockid is different (instead of treated it like a fork), but a
      // no-op otherwise. So try a different blockid here to test that
      if(i == 2u)
         //different blockid
         BOOST_REQUIRE_EXCEPTION(lc.pack_and_write_entry(fake_blockid_for_num(i, 0xbeef), fake_blockid_for_num(i-1), [&](bio::filtering_ostreambuf& obuf) {
                                    FC_ASSERT(false, "should not reach here");
                                 }),
                                 plugin_exception,
                                 [](const plugin_exception& e) {return e.to_detail_string().find("when starting from genesis block 2") != std::string::npos;});

      lc.pack_and_write_entry(fake_blockid_for_num(i), fake_blockid_for_num(i-1), [&](bio::filtering_ostreambuf& obuf) {
         FC_ASSERT(false, "should not reach here");
      });
   }

   BOOST_REQUIRE_EQUAL(lc.block_range().first, start);
   BOOST_REQUIRE_EQUAL(lc.block_range().second, end);

   //time for a "fork": we're going to rewrite the last 4 blocks and add 2 new ones as well. But we're going to ensure that old data remains intact during this
   //"overwrite" (as long as the ship_log_entry is alive)
   std::array<std::pair<std::optional<state_history::ship_log_entry>, sha256>, 4> pre_fork_entries_and_expected_hashes = {
      std::make_pair(lc.get_entry(end-1), wrote_data_for_blocknum[end-1]),
      std::make_pair(lc.get_entry(end-3), wrote_data_for_blocknum[end-3]), //out of order for fun
      std::make_pair(lc.get_entry(end-2), wrote_data_for_blocknum[end-2]),
      std::make_pair(lc.get_entry(end-4), wrote_data_for_blocknum[end-4]),
   };

   lc.pack_and_write_entry(fake_blockid_for_num(end-4, 0xdeadUL), fake_blockid_for_num(end-4-1), [&](bio::filtering_ostreambuf& obuf) {
      bio::filtering_istreambuf hashed_randomness(sha256_filter() | bio::restrict(random_source(), 0, mt_random()%16*1024*1024));
      bio::copy(hashed_randomness, obuf);
      wrote_data_for_blocknum[end-4] = hashed_randomness.component<sha256_filter>(0)->enc->result();
   });
   for(const unsigned i : {end-3, end-2, end-1, end, end+1})
      lc.pack_and_write_entry(fake_blockid_for_num(i, 0xdeadUL), fake_blockid_for_num(i-1, 0xdeadUL), [&](bio::filtering_ostreambuf& obuf) {
         bio::filtering_istreambuf hashed_randomness(sha256_filter() | bio::restrict(random_source(), 0, mt_random()%16*1024*1024));
         bio::copy(hashed_randomness, obuf);
         wrote_data_for_blocknum[i] = hashed_randomness.component<sha256_filter>(0)->enc->result();
      });

   //first, check that the pre-fork entries still read their pre-fork data
   ///XXX can we const this please?
   for(std::pair<std::optional<state_history::ship_log_entry>, sha256>& prefork_entry : pre_fork_entries_and_expected_hashes) {
      BOOST_REQUIRE(!!prefork_entry.first);
      bio::filtering_ostreambuf hashed_null(sha256_filter() | bio::null_sink());
      bio::filtering_istreambuf log_stream = prefork_entry.first->get_stream();
      bio::copy(log_stream, hashed_null);
      BOOST_REQUIRE_EQUAL(hashed_null.component<sha256_filter>(0)->enc->result(), prefork_entry.second);
   }
   //now let's check all of the just added blocks; and a couple earlier ones
   for(unsigned i : {end-6, end-5, /*"new fork" blocks:*/end-4, end-3, end-2, end-1, end, end+1}) {
      std::optional<state_history::ship_log_entry> entry = lc.get_entry(i);
      BOOST_REQUIRE(!!entry);

      bio::filtering_ostreambuf hashed_null(sha256_filter() | bio::null_sink());
      bio::filtering_istreambuf log_stream = entry->get_stream();
      bio::copy(log_stream, hashed_null);
      BOOST_REQUIRE_EQUAL(hashed_null.component<sha256_filter>(0)->enc->result(), wrote_data_for_blocknum[i]);
   }

   BOOST_REQUIRE_EQUAL(lc.block_range().first, start);
   BOOST_REQUIRE_EQUAL(lc.block_range().second, end+2);

} FC_LOG_AND_RETHROW();


BOOST_AUTO_TEST_CASE(regen_index) try {
   const fc::temp_directory tmpdir;

   //try recreating the index for an empty log
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), std::monostate(), "empty");
      BOOST_REQUIRE(lc.empty());
   }
   BOOST_REQUIRE(std::filesystem::exists(tmpdir.path() / "empty.index"));
   std::filesystem::remove(tmpdir.path() / "empty.index");
   BOOST_REQUIRE(!std::filesystem::exists(tmpdir.path() / "empty.index"));
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), std::monostate(), "empty");
      BOOST_REQUIRE(lc.empty());
   }

   //fill up a log with a handful of blocks
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), std::monostate(), "newlog");
      BOOST_REQUIRE(lc.empty());

      for(unsigned i = 2; i < 34; ++i)
         lc.pack_and_write_entry(fake_blockid_for_num(i), fake_blockid_for_num(i-1), [&](bio::filtering_ostreambuf& obuf) {
            {
               fc::datastream<bio::filtering_ostreambuf&> ds(obuf);
               fc::raw::pack(ds, i);
            }
            bio::copy(bio::restrict(random_source(), 0, 77777), obuf);
         });

      BOOST_REQUIRE_EQUAL(lc.block_range().first, 2u);
      BOOST_REQUIRE_EQUAL(lc.block_range().second, 34u);
   }
   BOOST_REQUIRE(std::filesystem::exists(tmpdir.path() / "newlog.index"));
   const uintmax_t prev_index_size = std::filesystem::file_size(tmpdir.path() / "newlog.index");
   std::string old_index_contents;
   read_file_contents(tmpdir.path() / "newlog.index", old_index_contents);
   BOOST_REQUIRE_EQUAL(prev_index_size, old_index_contents.size());

   //now remove the index and make sure the recreated index works
   std::filesystem::remove(tmpdir.path() / "newlog.index");
   BOOST_REQUIRE(!std::filesystem::exists(tmpdir.path() / "newlog.index"));
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), std::monostate(), "newlog");
      BOOST_REQUIRE_EQUAL(lc.block_range().first, 2u);
      BOOST_REQUIRE_EQUAL(lc.block_range().second, 34u);

      //read some blocks back
      for(const unsigned i : {2u, 10u, 22u, 33u}) {
         std::optional<state_history::ship_log_entry> entry = lc.get_entry(i);
         BOOST_REQUIRE(!!entry);
         bio::filtering_istreambuf log_stream = entry->get_stream();
         fc::datastream<bio::filtering_istreambuf&> ds(log_stream);
         unsigned red;
         fc::raw::unpack(ds, red);
         BOOST_REQUIRE_EQUAL(red, i);
      }
   }

   //also compare the index contents; should be exactly the same
   std::string new_index_contents;
   read_file_contents(tmpdir.path() / "newlog.index", new_index_contents);
   BOOST_REQUIRE_EQUAL(new_index_contents.size(), old_index_contents.size());
   BOOST_REQUIRE_EQUAL(new_index_contents, old_index_contents);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(empty_empty_empty) try {
   //just opens and closes an empty log a few times
   const fc::temp_directory tmpdir;

   for(unsigned i = 0; i < 4; ++i) {
      eosio::state_history::log_catalog lc(tmpdir.path(), std::monostate(), "empty");
      BOOST_REQUIRE(lc.empty());
   }
   BOOST_REQUIRE(std::filesystem::exists(tmpdir.path() / "empty.log"));
   BOOST_REQUIRE(std::filesystem::exists(tmpdir.path() / "empty.index"));
   BOOST_REQUIRE_EQUAL(std::filesystem::file_size(tmpdir.path() / "empty.log"), 0u);
   BOOST_REQUIRE_EQUAL(std::filesystem::file_size(tmpdir.path() / "empty.index"), 0u);
} FC_LOG_AND_RETHROW();

BOOST_DATA_TEST_CASE(basic_split, boost::unit_test::data::make({5u, 6u, 7u, 8u, 9u, 10u, 578'000u, 3'123'456'789u}) *
                                  boost::unit_test::data::make({5u, 10u}) *
                                  boost::unit_test::data::make({"保留", ""})
                                , start, stride, retained_dir) try {
   const fc::temp_directory tmpdir;

   state_history::partition_config conf = {
      .retained_dir       = retained_dir,
      .archive_dir        = "档案",
      .stride             = stride,
      .max_retained_files = UINT32_MAX
   };

   const unsigned initial_blocks_to_append = 50;
   unsigned end = start+initial_blocks_to_append+1;
   std::map<block_num_type, sha256> wrote_data_for_blocknum;

   {
      eosio::state_history::log_catalog lc(tmpdir.path(), conf, "splitit");
      BOOST_REQUIRE(lc.empty());

      std::mt19937 mt_random(0xbeefbeefu * start);

      for(unsigned i = start; i < end; ++i)
         lc.pack_and_write_entry(fake_blockid_for_num(i), fake_blockid_for_num(i-1), [&](bio::filtering_ostreambuf& obuf) {
            bio::filtering_istreambuf hashed_randomness(sha256_filter() | bio::restrict(random_source(), 0, mt_random()%1024*1024));
            bio::copy(hashed_randomness, obuf);
            wrote_data_for_blocknum[i] = hashed_randomness.component<sha256_filter>(0)->enc->result();
         });

      BOOST_REQUIRE_EQUAL(lc.block_range().first, start);
      BOOST_REQUIRE_EQUAL(lc.block_range().second, end);
   }

   const unsigned expected_log_parts = initial_blocks_to_append/stride + (start%stride == 0);

   for(const std::string& suffix : {"log"s, "index"s}) {
      const std::regex retained_logfile_regex(R"(^splitit-\d+-\d+\.)" + suffix + "$");

      unsigned found = 0;
      for(const std::filesystem::directory_entry& dir_entry : std::filesystem::directory_iterator(tmpdir.path() / conf.retained_dir))
         found += std::regex_search(dir_entry.path().filename().string(), retained_logfile_regex);
      BOOST_REQUIRE_EQUAL(found, expected_log_parts);
      BOOST_REQUIRE(std::filesystem::exists(tmpdir.path() / ("splitit."+suffix)));
   }

   //load the catalog back up and read through all the blocks
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), conf, "splitit");
      BOOST_REQUIRE_EQUAL(lc.block_range().first, start);
      BOOST_REQUIRE_EQUAL(lc.block_range().second, end);

      for(unsigned i = start; i < end; i++) {
         std::optional<state_history::ship_log_entry> entry = lc.get_entry(i);
         BOOST_REQUIRE(!!entry);

         std::optional<chain::block_id_type> bid = lc.get_block_id(i);
         BOOST_REQUIRE(!!bid);
         BOOST_REQUIRE_EQUAL(*bid, fake_blockid_for_num(i));

         bio::filtering_ostreambuf hashed_null(sha256_filter() | bio::null_sink());
         bio::filtering_istreambuf log_stream = entry->get_stream();
         bio::copy(log_stream, hashed_null);
         BOOST_REQUIRE_EQUAL(hashed_null.component<sha256_filter>(0)->enc->result(), wrote_data_for_blocknum[i]);
      }
   }

   //find a log & index file and copy it to a name that does not match expected; it should be silently ignored
   {
      const std::regex regex(R"(^splitit-\d+-\d+\.log$)");

      for(const std::filesystem::directory_entry& dir_entry : std::filesystem::directory_iterator(tmpdir.path() / conf.retained_dir))
         if(std::regex_search(dir_entry.path().filename().string(), regex)) {
            std::filesystem::copy_file(dir_entry.path(), std::filesystem::path(dir_entry.path()).replace_filename("yeeeeehaw-1234.log"));
            std::filesystem::copy_file(std::filesystem::path(dir_entry.path()).replace_extension("index"), std::filesystem::path(dir_entry.path()).replace_filename("yeeeeehaw-1234.index"));
            break;
         }
   }
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), conf, "splitit");
      BOOST_REQUIRE_EQUAL(lc.block_range().first, start);
      BOOST_REQUIRE_EQUAL(lc.block_range().second, end);
   }

   //delete every other .index file. indexes will be recreated for all log parts on construction of the ship_log_catalog
   {
      const std::regex regex(R"(^splitit-\d+-\d+\.index)");

      bool do_this_one = false;
      for(const std::filesystem::directory_entry& dir_entry : std::filesystem::directory_iterator(tmpdir.path() / conf.retained_dir))
         if(std::regex_search(dir_entry.path().filename().string(), regex)) {
            if(do_this_one)
               std::filesystem::remove(dir_entry.path());
            do_this_one = !do_this_one;
         }
   }
   //and we'll go through the process of reading all blocks after the indexes have been recreated
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), conf, "splitit");
      BOOST_REQUIRE_EQUAL(lc.block_range().first, start);
      BOOST_REQUIRE_EQUAL(lc.block_range().second, end);

      for(unsigned i = start; i < end; i++) {
         std::optional<state_history::ship_log_entry> entry = lc.get_entry(i);
         BOOST_REQUIRE(!!entry);

         std::optional<chain::block_id_type> bid = lc.get_block_id(i);
         BOOST_REQUIRE(!!bid);
         BOOST_REQUIRE_EQUAL(*bid, fake_blockid_for_num(i));

         bio::filtering_ostreambuf hashed_null(sha256_filter() | bio::null_sink());
         bio::filtering_istreambuf log_stream = entry->get_stream();
         bio::copy(log_stream, hashed_null);
         BOOST_REQUIRE_EQUAL(hashed_null.component<sha256_filter>(0)->enc->result(), wrote_data_for_blocknum[i]);
      }
   }

   //now switch over to no splitting. this is allowed but old split logs will not be "visible" when configured this way
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), std::monostate(), "splitit");
      if(start % conf.stride == 0) {  //"head log" will be empty in this case
         BOOST_REQUIRE(lc.empty());
      }
      else {
         BOOST_REQUIRE(lc.block_range().first % conf.stride == 1);
         BOOST_REQUIRE_EQUAL(lc.block_range().second, end);
      }

      //let's go create another 100 blocks too!
      std::mt19937 mt_random(0xbeefbeefu * end);
      const unsigned new_end = end + 100;

      for(unsigned i = end; i < new_end; ++i)
         lc.pack_and_write_entry(fake_blockid_for_num(i), fake_blockid_for_num(i-1), [&](bio::filtering_ostreambuf& obuf) {
            bio::filtering_istreambuf hashed_randomness(sha256_filter() | bio::restrict(random_source(), 0, mt_random()%1024*1024));
            bio::copy(hashed_randomness, obuf);
            wrote_data_for_blocknum[i] = hashed_randomness.component<sha256_filter>(0)->enc->result();
         });

      end = new_end;
      BOOST_REQUIRE_EQUAL(lc.block_range().second, end);
   }

   //and back to split log mode. all those retained logs will be visible again
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), conf, "splitit");
      BOOST_REQUIRE_EQUAL(lc.block_range().first, start);
      BOOST_REQUIRE_EQUAL(lc.block_range().second, end);

      //but now let's add enough blocks to trigger a rotation again. This will give us a retained log that is a different span
      // size than all the previous spans
      std::mt19937 mt_random(0xbeefbeefu * end);
      for(unsigned i = end; i < end+conf.stride; ++i)
         lc.pack_and_write_entry(fake_blockid_for_num(i), fake_blockid_for_num(i-1), [&](bio::filtering_ostreambuf& obuf) {
            bio::filtering_istreambuf hashed_randomness(sha256_filter() | bio::restrict(random_source(), 0, mt_random()%1024*1024));
            bio::copy(hashed_randomness, obuf);
            wrote_data_for_blocknum[i] = hashed_randomness.component<sha256_filter>(0)->enc->result();
         });
      end += conf.stride;
      BOOST_REQUIRE_EQUAL(lc.block_range().first, start);
      BOOST_REQUIRE_EQUAL(lc.block_range().second, end);
   }

   //one more time where we read through everything
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), conf, "splitit");
      BOOST_REQUIRE_EQUAL(lc.block_range().first, start);
      BOOST_REQUIRE_EQUAL(lc.block_range().second, end);

      for(unsigned i = start; i < end; i++) {
         std::optional<state_history::ship_log_entry> entry = lc.get_entry(i);
         BOOST_REQUIRE(!!entry);

         std::optional<chain::block_id_type> bid = lc.get_block_id(i);
         BOOST_REQUIRE(!!bid);
         BOOST_REQUIRE_EQUAL(*bid, fake_blockid_for_num(i));

         bio::filtering_ostreambuf hashed_null(sha256_filter() | bio::null_sink());
         bio::filtering_istreambuf log_stream = entry->get_stream();
         bio::copy(log_stream, hashed_null);
         BOOST_REQUIRE_EQUAL(hashed_null.component<sha256_filter>(0)->enc->result(), wrote_data_for_blocknum[i]);
      }
   }

   //set the number of retained logs to 4
   conf.max_retained_files = 4u;
   //and go generate enough blocks to cause a rotation which will move old logs to the archive directory
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), conf, "splitit");
      BOOST_REQUIRE_EQUAL(lc.block_range().first, start);
      BOOST_REQUIRE_EQUAL(lc.block_range().second, end);

      //but now let's add enough blocks to trigger a rotation again. This will give us a retained log that is a different span
      // size than all the previous spans
      std::mt19937 mt_random(0xbeefbeefu * end);
      for(unsigned i = end; i < end+conf.stride; ++i)
         lc.pack_and_write_entry(fake_blockid_for_num(i), fake_blockid_for_num(i-1), [&](bio::filtering_ostreambuf& obuf) {
            bio::filtering_istreambuf hashed_randomness(sha256_filter() | bio::restrict(random_source(), 0, mt_random()%1024*1024));
            bio::copy(hashed_randomness, obuf);
            wrote_data_for_blocknum[i] = hashed_randomness.component<sha256_filter>(0)->enc->result();
         });
      end += conf.stride;

      BOOST_REQUIRE_NE(lc.block_range().first, 2u);
      BOOST_REQUIRE_EQUAL(lc.block_range().second, end);
   }

   //make sure we have only 4 retained logs in the retained dir; and note how many in archived dir
   std::unordered_map<std::string, unsigned> last_archive_dir_count;
   {
      for(const std::string& suffix : {"log"s, "index"s}) {
         const std::regex retained_logfile_regex(R"(^splitit-\d+-\d+\.)" + suffix + "$");

         const unsigned found = std::ranges::count_if(std::filesystem::directory_iterator(tmpdir.path() / conf.retained_dir), [&](const std::filesystem::directory_entry& dir_entry) {
            return std::regex_search(dir_entry.path().filename().string(), retained_logfile_regex);
         });
         BOOST_REQUIRE_EQUAL(found, 4u);
      }
      for(const std::string& suffix : {"log"s, "index"s}) {
         const std::regex retained_logfile_regex(R"(^splitit-\d+-\d+\.)" + suffix + "$");

         last_archive_dir_count[suffix] = std::ranges::count_if(std::filesystem::directory_iterator(tmpdir.path() / conf.archive_dir), [&](const std::filesystem::directory_entry& dir_entry) {
            return std::regex_search(dir_entry.path().filename().string(), retained_logfile_regex);
         });
         BOOST_REQUIRE_NE(last_archive_dir_count[suffix], 0u);
      }
      BOOST_REQUIRE_EQUAL(last_archive_dir_count["log"], last_archive_dir_count["index"]);
   }

   //clear the archive directory. This will cause logs to be removed; reduce max_retain to 3 to make it easier to spot proper behavior
   const std::filesystem::path previous_archive_dir = conf.archive_dir;
   conf.archive_dir.clear();
   conf.max_retained_files = 3u;
   //generate enough blocks for a rotation...
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), conf, "splitit");
      BOOST_REQUIRE_EQUAL(lc.block_range().second, end);

      std::mt19937 mt_random(0xbeefbeefu * end);
      for(unsigned i = end; i < end+conf.stride; ++i)
         lc.pack_and_write_entry(fake_blockid_for_num(i), fake_blockid_for_num(i-1), [&](bio::filtering_ostreambuf& obuf) {
            bio::filtering_istreambuf hashed_randomness(sha256_filter() | bio::restrict(random_source(), 0, mt_random()%1024*1024));
            bio::copy(hashed_randomness, obuf);
            wrote_data_for_blocknum[i] = hashed_randomness.component<sha256_filter>(0)->enc->result();
         });
      end += conf.stride;

      BOOST_REQUIRE_EQUAL(lc.block_range().second, end);
   }

   //check filesystem..
   {
      //should only find 3 logs in retain dir
      for(const std::string& suffix : {"log"s, "index"s}) {
         const std::regex retained_logfile_regex(R"(^splitit-\d+-\d+\.)" + suffix + "$");

         const unsigned found = std::ranges::count_if(std::filesystem::directory_iterator(tmpdir.path() / conf.retained_dir), [&](const std::filesystem::directory_entry& dir_entry) {
            return std::regex_search(dir_entry.path().filename().string(), retained_logfile_regex);
         });
         BOOST_REQUIRE_EQUAL(found, 3u);
      }
      //archive dir should still have same number of files
      for(const std::string& suffix : {"log"s, "index"s}) {
         const std::regex retained_logfile_regex(R"(^splitit-\d+-\d+\.)" + suffix + "$");

         const unsigned found = std::ranges::count_if(std::filesystem::directory_iterator(tmpdir.path() / previous_archive_dir), [&](const std::filesystem::directory_entry& dir_entry) {
            return std::regex_search(dir_entry.path().filename().string(), retained_logfile_regex);
         });
         BOOST_REQUIRE_EQUAL(found, last_archive_dir_count[suffix]);
      }
   }

   //one more pass through all the blocks
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), conf, "splitit");
      BOOST_REQUIRE_EQUAL(lc.block_range().second, end);

      for(unsigned i = lc.block_range().first; i < end; i++) {
         std::optional<state_history::ship_log_entry> entry = lc.get_entry(i);
         BOOST_REQUIRE(!!entry);

         std::optional<chain::block_id_type> bid = lc.get_block_id(i);
         BOOST_REQUIRE(!!bid);
         BOOST_REQUIRE_EQUAL(*bid, fake_blockid_for_num(i));

         bio::filtering_ostreambuf hashed_null(sha256_filter() | bio::null_sink());
         bio::filtering_istreambuf log_stream = entry->get_stream();
         bio::copy(log_stream, hashed_null);
         BOOST_REQUIRE_EQUAL(hashed_null.component<sha256_filter>(0)->enc->result(), wrote_data_for_blocknum[i]);
      }
   }

   //remove one of the reained logs causing a "hole" which is disallowed. to do this reliably and for full coverage, we'll first delete the
   // second newest retained log, and then delete the newest retained log
   std::map<unsigned, std::filesystem::path> found;
   {
      const std::regex retained_logfile_regex(R"(^splitit-\d+-\d+\.log$)");

      for(const std::filesystem::directory_entry& dir_entry : std::filesystem::directory_iterator(tmpdir.path() / conf.retained_dir)) {
         if(!std::regex_search(dir_entry.path().filename().string(), retained_logfile_regex))
            continue;
         found[state_history::state_history_log(std::filesystem::path(dir_entry.path()).replace_extension("")).block_range().second] = dir_entry.path();
      }
      BOOST_REQUIRE_GT(found.size(), 1u);
   }
   std::filesystem::remove(std::next(found.rbegin())->second);
   BOOST_REQUIRE_EXCEPTION(eosio::state_history::log_catalog(tmpdir.path(), conf, "splitit"),
                           plugin_exception,
                           [](const plugin_exception& e) {return e.to_detail_string().find("which results in a hole") != std::string::npos;});
   std::filesystem::remove(found.rbegin()->second);

   //only perform this check if we expect the "head log" to be non-empty
   if(start % conf.stride)
      BOOST_REQUIRE_EXCEPTION(eosio::state_history::log_catalog(tmpdir.path(), conf, "splitit"),
                              plugin_exception,
                              [](const plugin_exception& e) {return e.to_detail_string().find("which results in a hole") != std::string::npos;});
   //unfortuately if the "head log" _is_ empty we're in quite a problem since we won't be able to detect the hole until a block is appended
   //TODO: *is* the above checked?
} FC_LOG_AND_RETHROW();

BOOST_DATA_TEST_CASE(split_forks, bdata::xrange(1u, 6u), fork_size) try {
   const fc::temp_directory tmpdir;

   state_history::partition_config conf = {
      .retained_dir       = "retained",
      .archive_dir        = {},
      .stride             = 10,
      .max_retained_files = UINT32_MAX
   };

   //fill up 50 blocks
   const unsigned start = 2;
   const unsigned end = 53;
   std::map<block_num_type, sha256> wrote_data_for_blocknum;
   std::mt19937 mt_random(0xbeefbeefu * start);

   {
      eosio::state_history::log_catalog lc(tmpdir.path(), conf, "logz");
      BOOST_REQUIRE(lc.empty());

      for(unsigned i = start; i < end; ++i)
         lc.pack_and_write_entry(fake_blockid_for_num(i), fake_blockid_for_num(i-1), [&](bio::filtering_ostreambuf& obuf) {
            bio::filtering_istreambuf hashed_randomness(sha256_filter() | bio::restrict(random_source(), 0, mt_random()%1024*1024));
            bio::copy(hashed_randomness, obuf);
            wrote_data_for_blocknum[i] = hashed_randomness.component<sha256_filter>(0)->enc->result();
         });

      BOOST_REQUIRE_EQUAL(lc.block_range().first, start);
      BOOST_REQUIRE_EQUAL(lc.block_range().second, end);
   }

   //discover the filenames for:
   // head
   // 41-50
   // 31-40
   const std::filesystem::path head_log_path = tmpdir.path() / "logz";
   std::filesystem::path path_31to40;
   std::filesystem::path path_41to50;
   {
      const std::regex retained_logfile_regex(R"(^logz-\d+-\d+\.log$)");

      for(const std::filesystem::directory_entry& dir_entry : std::filesystem::directory_iterator(tmpdir.path() / conf.retained_dir)) {
         if(!std::regex_search(dir_entry.path().filename().string(), retained_logfile_regex))
            continue;
         const std::filesystem::path path_no_ext = std::filesystem::path(dir_entry.path()).replace_extension("");
         const unsigned start_block = state_history::state_history_log(path_no_ext).block_range().first;
         if(start_block == 31)
            path_31to40 = dir_entry.path();
         else if(start_block == 41)
            path_41to50 = dir_entry.path();
      }
      BOOST_REQUIRE(!path_31to40.empty() && !path_41to50.empty());
   }

   const size_t before_head_log_size = std::filesystem::file_size(std::filesystem::path(head_log_path).replace_extension("log"));
   const size_t before_head_index_size = std::filesystem::file_size(std::filesystem::path(head_log_path).replace_extension("index"));
   const size_t before_31to40_log_size = std::filesystem::file_size(std::filesystem::path(path_31to40).replace_extension("log"));
   const size_t before_31to40_index_size = std::filesystem::file_size(std::filesystem::path(path_31to40).replace_extension("index"));
   const size_t before_41to50_log_size = std::filesystem::file_size(std::filesystem::path(path_41to50).replace_extension("log"));
   const size_t before_41to50_index_size = std::filesystem::file_size(std::filesystem::path(path_41to50).replace_extension("index"));

   const unsigned start_fork_at = end-fork_size;
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), conf, "logz");
      BOOST_REQUIRE_EQUAL(lc.block_range().first, start);
      BOOST_REQUIRE_EQUAL(lc.block_range().second, end);

      lc.pack_and_write_entry(fake_blockid_for_num(start_fork_at, 0xdeadUL), fake_blockid_for_num(start_fork_at-1), [&](bio::filtering_ostreambuf& obuf) {
         bio::filtering_istreambuf hashed_randomness(sha256_filter() | bio::restrict(random_source(), 0, mt_random()%1024*1024));
         bio::copy(hashed_randomness, obuf);
         wrote_data_for_blocknum[start_fork_at] = hashed_randomness.component<sha256_filter>(0)->enc->result();
      });
   }

   if(fork_size == 1) {
      //in this case we're just overwriting the last block
      //all indexes should remain the same size
      BOOST_REQUIRE_EQUAL(before_head_index_size, std::filesystem::file_size(std::filesystem::path(head_log_path).replace_extension("index")));
      BOOST_REQUIRE_EQUAL(before_31to40_index_size, std::filesystem::file_size(std::filesystem::path(path_31to40).replace_extension("index")));
      BOOST_REQUIRE_EQUAL(before_41to50_index_size, std::filesystem::file_size(std::filesystem::path(path_41to50).replace_extension("index")));
      //all logs should remain the same size, except the head log which will have grown
      BOOST_REQUIRE_EQUAL(before_31to40_log_size, std::filesystem::file_size(std::filesystem::path(path_31to40).replace_extension("log")));
      BOOST_REQUIRE_EQUAL(before_41to50_log_size, std::filesystem::file_size(std::filesystem::path(path_41to50).replace_extension("log")));
      BOOST_REQUIRE_LT(before_head_log_size, std::filesystem::file_size(std::filesystem::path(head_log_path).replace_extension("log")));
   }
   else if(start_fork_at >= 51) {
      //in this case only the head log will have been modified
      //retained indexes will remain the same size
      BOOST_REQUIRE_EQUAL(before_31to40_index_size, std::filesystem::file_size(std::filesystem::path(path_31to40).replace_extension("index")));
      BOOST_REQUIRE_EQUAL(before_41to50_index_size, std::filesystem::file_size(std::filesystem::path(path_41to50).replace_extension("index")));
      //head index will be smaller
      BOOST_REQUIRE_GT(before_head_index_size, std::filesystem::file_size(std::filesystem::path(head_log_path).replace_extension("index")));
      //retained logs will remain the same size, except head block which will have grown
      BOOST_REQUIRE_EQUAL(before_31to40_log_size, std::filesystem::file_size(std::filesystem::path(path_31to40).replace_extension("log")));
      BOOST_REQUIRE_EQUAL(before_41to50_log_size, std::filesystem::file_size(std::filesystem::path(path_41to50).replace_extension("log")));
      BOOST_REQUIRE_LT(before_head_log_size, std::filesystem::file_size(std::filesystem::path(head_log_path).replace_extension("log")));
   }
   else {
      //in this case we will have "unrotated" a retained log
      //check that 31-40 log and index remains unchanged
      BOOST_REQUIRE_EQUAL(before_31to40_log_size, std::filesystem::file_size(std::filesystem::path(path_31to40).replace_extension("log")));
      BOOST_REQUIRE_EQUAL(before_31to40_index_size, std::filesystem::file_size(std::filesystem::path(path_31to40).replace_extension("index")));
      if(start_fork_at == 50) {
         //if the fork was at 50, we actually both unrotated and then rotated
         //so check that index size for 41-50 is the same, and that its log is larger
         BOOST_REQUIRE_LT(before_41to50_log_size, std::filesystem::file_size(std::filesystem::path(path_41to50).replace_extension("log")));
         BOOST_REQUIRE_EQUAL(before_41to50_index_size, std::filesystem::file_size(std::filesystem::path(path_41to50).replace_extension("index")));
         //and only empty head log is present
         BOOST_REQUIRE_EQUAL(0u, std::filesystem::file_size(std::filesystem::path(head_log_path).replace_extension("log")));
         BOOST_REQUIRE_EQUAL(0u, std::filesystem::file_size(std::filesystem::path(head_log_path).replace_extension("index")));
      }
      else {
         //check that the 41-50 index and log do not exist
         BOOST_REQUIRE(!std::filesystem::exists(std::filesystem::path(path_41to50).replace_extension("log")));
         BOOST_REQUIRE(!std::filesystem::exists(std::filesystem::path(path_41to50).replace_extension("index")));
         //check that the head index is smaller than what 41-50 index previously was
         BOOST_REQUIRE_GT(before_41to50_index_size, std::filesystem::file_size(std::filesystem::path(head_log_path).replace_extension("index")));
         //and that the head log is larger than what 41-50 log previously was
         BOOST_REQUIRE_LT(before_41to50_log_size, std::filesystem::file_size(std::filesystem::path(head_log_path).replace_extension("log")));
      }
   }

   //reopen the log while we're in this shortened fork state
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), conf, "logz");
      BOOST_REQUIRE_EQUAL(lc.block_range().first, start);
      BOOST_REQUIRE_EQUAL(lc.block_range().second, start_fork_at+1);
   }

   //continue on writing to the log replacing all blocks after the fork block
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), conf, "logz");
      for(unsigned i = start_fork_at+1; i < end; ++i)
         lc.pack_and_write_entry(fake_blockid_for_num(i, 0xdeadUL), fake_blockid_for_num(i-1, 0xdeadUL), [&](bio::filtering_ostreambuf& obuf) {
            bio::filtering_istreambuf hashed_randomness(sha256_filter() | bio::restrict(random_source(), 0, mt_random()%1024*1024));
            bio::copy(hashed_randomness, obuf);
            wrote_data_for_blocknum[i] = hashed_randomness.component<sha256_filter>(0)->enc->result();
         });

      BOOST_REQUIRE_EQUAL(lc.block_range().first, start);
      BOOST_REQUIRE_EQUAL(lc.block_range().second, end);
   }

   //check sizes of everything: all index sizes should have been the same as we originally started with
   BOOST_REQUIRE_EQUAL(before_head_index_size, std::filesystem::file_size(std::filesystem::path(head_log_path).replace_extension("index")));
   BOOST_REQUIRE_EQUAL(before_31to40_index_size, std::filesystem::file_size(std::filesystem::path(path_31to40).replace_extension("index")));
   BOOST_REQUIRE_EQUAL(before_41to50_index_size, std::filesystem::file_size(std::filesystem::path(path_41to50).replace_extension("index")));
   BOOST_REQUIRE_LT(before_head_log_size, std::filesystem::file_size(std::filesystem::path(head_log_path).replace_extension("log")));
   if(start_fork_at >= 51)
      BOOST_REQUIRE_EQUAL(before_41to50_log_size, std::filesystem::file_size(std::filesystem::path(path_41to50).replace_extension("log")));
   //don't do this else for now: randomness in the data written may cause this to fail
   //else BOOST_REQUIRE_LT(before_41to50_log_size, std::filesystem::file_size(std::filesystem::path(path_41to50).replace_extension("log")));
   BOOST_REQUIRE_EQUAL(before_31to40_log_size, std::filesystem::file_size(std::filesystem::path(path_31to40).replace_extension("log")));

   //read through all the blocks and validate contents
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), conf, "logz");
      BOOST_REQUIRE_EQUAL(lc.block_range().first, start);
      BOOST_REQUIRE_EQUAL(lc.block_range().second, end);

      for(unsigned i = start; i < end; ++i) {
         std::optional<state_history::ship_log_entry> entry = lc.get_entry(i);
         BOOST_REQUIRE(!!entry);

         bio::filtering_ostreambuf hashed_null(sha256_filter() | bio::null_sink());
         bio::filtering_istreambuf log_stream = entry->get_stream();
         bio::copy(log_stream, hashed_null);
         BOOST_REQUIRE_EQUAL(hashed_null.component<sha256_filter>(0)->enc->result(), wrote_data_for_blocknum[i]);
      }
   }
} FC_LOG_AND_RETHROW();

//(manually) fabricate a leap 3.x ship log format and make sure it's readable
BOOST_AUTO_TEST_CASE(old_log_format) try {
   const temp_directory tmpdir;

   const unsigned begin_block = 2;
   const unsigned end_block = 45;

   std::map<block_num_type, sha256> wrote_data_for_blocknum;

   {
      random_access_file file(tmpdir.path() / "old.log");
      for(unsigned blocknum = begin_block; blocknum < end_block; ++blocknum) {
         const size_t insertpos = file.size();
         std::pair<state_history::log_header, uint32_t> legacy_header = {};
         legacy_header.first.block_id = fake_blockid_for_num(blocknum);

         bio::filtering_istreambuf hashed_randomness(sha256_filter() | bio::restrict(random_source(), 0, 128*1024));
         bio::filtering_ostreambuf output(bio::zlib_compressor() | eosio::detail::counter() | bio::restrict(file.seekable_device(), insertpos + raw::pack_size(legacy_header)));
         bio::copy(hashed_randomness, output);
         wrote_data_for_blocknum[blocknum] = hashed_randomness.component<sha256_filter>(0)->enc->result();
         legacy_header.first.payload_size = output.component<eosio::detail::counter>(1)->characters() + sizeof(decltype(legacy_header.second));

         file.pack_to(legacy_header, insertpos);
         file.pack_to_end(insertpos);
      }
   }

   {
      //will regenerate index too
      eosio::state_history::log_catalog lc(tmpdir.path(), std::monostate(), "old");

      BOOST_REQUIRE_EQUAL(begin_block, lc.block_range().first);
      BOOST_REQUIRE_EQUAL(end_block, lc.block_range().second);

      for(unsigned i = begin_block; i < end_block; ++i) {
         std::optional<state_history::ship_log_entry> entry = lc.get_entry(i);
         BOOST_REQUIRE(!!entry);

         bio::filtering_ostreambuf hashed_null(sha256_filter() | bio::null_sink());
         bio::filtering_istreambuf log_stream = entry->get_stream();
         bio::copy(log_stream, hashed_null);
         BOOST_REQUIRE_EQUAL(hashed_null.component<sha256_filter>(0)->enc->result(), wrote_data_for_blocknum[i]);
      }
   }
} FC_LOG_AND_RETHROW();

//writes a bunch of a blocks, and then writes a bunch of the same blocks (block ids) all over again. this is similar to
// what would occur on a replay or loading a snapshot older than what was the prior head.
const state_history::state_history_log_config log_configs_for_rewrite_same[] = {
   {std::monostate()},
   {state_history::partition_config{
      .retained_dir = "retain here pls",
      .archive_dir = "archive here pls",
      .stride = 10
   }}
};
BOOST_DATA_TEST_CASE(rewrite_same, bdata::make(log_configs_for_rewrite_same), config) try {
   const fc::temp_directory tmpdir;

   std::map<block_num_type, sha256> wrote_data_for_blocknum;
   const unsigned begin_block = 10;
   const unsigned end_block = 105;

   {
      eosio::state_history::log_catalog lc(tmpdir.path(), config, "mr,log");
      for(unsigned i = begin_block; i < end_block; ++i)
         lc.pack_and_write_entry(fake_blockid_for_num(i), fake_blockid_for_num(i-1), [&](bio::filtering_ostreambuf& obuf) {
            bio::filtering_istreambuf hashed_randomness(sha256_filter() | bio::restrict(random_source(), 0, 64*1024));
            bio::copy(hashed_randomness, obuf);
            wrote_data_for_blocknum[i] = hashed_randomness.component<sha256_filter>(0)->enc->result();
         });
   }

   const size_t before_log_size = std::filesystem::file_size(tmpdir.path() / "mr,log.log");
   const size_t before_index_size = std::filesystem::file_size(tmpdir.path() / "mr,log.index");

   //reopen and write different data for each block id. This should silently be swallowed
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), config, "mr,log");
      for(unsigned i = begin_block; i < end_block; ++i)
         lc.pack_and_write_entry(fake_blockid_for_num(i), fake_blockid_for_num(i-1), [&](bio::filtering_ostreambuf& obuf) {
            bio::filtering_istreambuf hashed_randomness(sha256_filter() | bio::restrict(random_source(), 0, 64*1024));
            bio::copy(hashed_randomness, obuf);
         });
   }

   //read the blocks back, making sure the hash of data is what was originally written and that the size of the log remained equal
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), config, "mr,log");
      BOOST_REQUIRE_EQUAL(lc.block_range().first, begin_block);
      BOOST_REQUIRE_EQUAL(lc.block_range().second, end_block);

      for(unsigned i = begin_block; i < end_block; ++i) {
         std::optional<state_history::ship_log_entry> entry = lc.get_entry(i);
         BOOST_REQUIRE(!!entry);

         bio::filtering_ostreambuf hashed_null(sha256_filter() | bio::null_sink());
         bio::filtering_istreambuf log_stream = entry->get_stream();
         bio::copy(log_stream, hashed_null);
         BOOST_REQUIRE_EQUAL(hashed_null.component<sha256_filter>(0)->enc->result(), wrote_data_for_blocknum[i]);
      }
   }
   BOOST_REQUIRE_EQUAL(before_log_size, std::filesystem::file_size(tmpdir.path() / "mr,log.log"));
   BOOST_REQUIRE_EQUAL(before_index_size, std::filesystem::file_size(tmpdir.path() / "mr,log.index"));

} FC_LOG_AND_RETHROW();

//similar to above, but this time configure the logs such that it forgets (prunes/rotates out) early blocks, but then the rewrite of blocks occurs in the range
// of still "remembered" blocks
const state_history::state_history_log_config log_configs_for_rewrite_remembered[] = {
   {state_history::partition_config{.retained_dir = "retain here pls", .archive_dir = "archive here pls", .stride = 10, .max_retained_files = 4}},
   {state_history::prune_config{.prune_blocks = 40, .prune_threshold = 2}}
};
BOOST_DATA_TEST_CASE(rewrite_same_remembered, bdata::make(log_configs_for_rewrite_remembered), config) try {
   const fc::temp_directory tmpdir;

   std::map<block_num_type, sha256> wrote_data_for_blocknum;
   const unsigned begin_block = 10;
   const unsigned end_block = 105;

   //fill up blocks 10 through 104, but many of the early blocks are forgotten
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), config, "huh");
      for(unsigned i = begin_block; i < end_block; ++i)
         lc.pack_and_write_entry(fake_blockid_for_num(i), fake_blockid_for_num(i-1), [&](bio::filtering_ostreambuf& obuf) {
            bio::filtering_istreambuf hashed_randomness(sha256_filter() | bio::restrict(random_source(), 0, 64*1024));
            bio::copy(hashed_randomness, obuf);
            wrote_data_for_blocknum[i] = hashed_randomness.component<sha256_filter>(0)->enc->result();
         });
   }

   const unsigned start_rewrite_block = 70;

   const size_t before_log_size = std::filesystem::file_size(tmpdir.path() / "huh.log");
   const size_t before_index_size = std::filesystem::file_size(tmpdir.path() / "huh.index");

   //rewrite blocks 70-104
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), config, "huh");
      for(unsigned i = start_rewrite_block; i < end_block; ++i)
         lc.pack_and_write_entry(fake_blockid_for_num(i), fake_blockid_for_num(i-1), [&](bio::filtering_ostreambuf& obuf) {
            bio::filtering_istreambuf hashed_randomness(sha256_filter() | bio::restrict(random_source(), 0, 64*1024));
            bio::copy(hashed_randomness, obuf);
         });
   }

   //read the blocks back, making sure the hash of data is what was originally written and that the size of the log remained equal
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), config, "huh");
      BOOST_REQUIRE_LT(lc.block_range().first, start_rewrite_block);
      BOOST_REQUIRE_EQUAL(lc.block_range().second, end_block);

      for(unsigned i = start_rewrite_block; i < end_block; ++i) {
         std::optional<state_history::ship_log_entry> entry = lc.get_entry(i);
         BOOST_REQUIRE(!!entry);

         bio::filtering_ostreambuf hashed_null(sha256_filter() | bio::null_sink());
         bio::filtering_istreambuf log_stream = entry->get_stream();
         bio::copy(log_stream, hashed_null);
         BOOST_REQUIRE_EQUAL(hashed_null.component<sha256_filter>(0)->enc->result(), wrote_data_for_blocknum[i]);
      }

      BOOST_REQUIRE_EQUAL(before_log_size, std::filesystem::file_size(tmpdir.path() / "huh.log"));
      BOOST_REQUIRE_EQUAL(before_index_size, std::filesystem::file_size(tmpdir.path() / "huh.index"));
   }

} FC_LOG_AND_RETHROW();

//similar to above, but this time configure the logs such that it forgets (rotates out) early blocks, and then rewrite a block in that forgotten range
BOOST_AUTO_TEST_CASE(rewrite_same_forgotten) try {
   const fc::temp_directory tmpdir;

   const state_history::state_history_log_config config = {state_history::partition_config{.retained_dir = "retain here pls", .archive_dir = "archive here pls", .stride = 10, .max_retained_files = 4}};

   std::map<block_num_type, sha256> wrote_data_for_blocknum;
   const unsigned begin_block = 10;
   const unsigned end_block = 105;

   //fill up blocks 10 through 104, but many of the early blocks are forgotten
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), config, "huh");
      for(unsigned i = begin_block; i < end_block; ++i)
         lc.pack_and_write_entry(fake_blockid_for_num(i), fake_blockid_for_num(i-1), [&](bio::filtering_ostreambuf& obuf) {
            bio::filtering_istreambuf hashed_randomness(sha256_filter() | bio::restrict(random_source(), 0, 64*1024));
            bio::copy(hashed_randomness, obuf);
            wrote_data_for_blocknum[i] = hashed_randomness.component<sha256_filter>(0)->enc->result();
         });
   }

   const unsigned start_rewrite_block = 30;

   //try and rewrite block 30. This is a forgotten block that is before the first block in the catalog, so it will fail
   eosio::state_history::log_catalog lc(tmpdir.path(), config, "huh");
   BOOST_REQUIRE_EXCEPTION(lc.pack_and_write_entry(fake_blockid_for_num(start_rewrite_block), fake_blockid_for_num(start_rewrite_block-1), [&](bio::filtering_ostreambuf& obuf) {}), chain::plugin_exception,
                           [](const chain::plugin_exception& e) {return e.get_log().at(0).get_message().find("is before first block") != std::string::npos;});
} FC_LOG_AND_RETHROW();

//similar to above, but with pruning. surprise! it's allowed! rewriting pruned blocks is allowed as long as they are after the index's start block (the first block ever written to
// the log)
BOOST_AUTO_TEST_CASE(rewrite_same_forgotten_pruned_range) try {
   const fc::temp_directory tmpdir;

   const state_history::state_history_log_config config = {state_history::prune_config{.prune_blocks = 40, .prune_threshold = 2}};

   std::map<block_num_type, sha256> wrote_data_for_blocknum;
   const unsigned begin_block = 10;
   const unsigned end_block = 105;

   //fill up blocks 10 through 104, but many of the early blocks are forgotten
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), config, "huh");
      for(unsigned i = begin_block; i < end_block; ++i)
         lc.pack_and_write_entry(fake_blockid_for_num(i), fake_blockid_for_num(i-1), [&](bio::filtering_ostreambuf& obuf) {
            bio::filtering_istreambuf hashed_randomness(sha256_filter() | bio::restrict(random_source(), 0, 64*1024));
            bio::copy(hashed_randomness, obuf);
            wrote_data_for_blocknum[i] = hashed_randomness.component<sha256_filter>(0)->enc->result();
         });
   }

   const unsigned start_rewrite_block = 30;

   const size_t before_log_size = std::filesystem::file_size(tmpdir.path() / "huh.log");
   const size_t before_index_size = std::filesystem::file_size(tmpdir.path() / "huh.index");

   //rewrite block 30, and check that the log state seems sane
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), config, "huh");
      lc.pack_and_write_entry(fake_blockid_for_num(start_rewrite_block), fake_blockid_for_num(start_rewrite_block-1), [&](bio::filtering_ostreambuf& obuf) {});
      const auto [after_begin_block, after_end_block] = lc.block_range();

      BOOST_REQUIRE_EQUAL(after_begin_block, start_rewrite_block);
      BOOST_REQUIRE_EQUAL(after_end_block, start_rewrite_block+1u);
   }
   //open again just in case
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), config, "huh");
      const auto [after_begin_block, after_end_block] = lc.block_range();
      BOOST_REQUIRE_EQUAL(after_begin_block, start_rewrite_block);
      BOOST_REQUIRE_EQUAL(after_end_block, start_rewrite_block+1u);
   }

   //and check the file sizes, index should have shrunk, log grown
   BOOST_REQUIRE_LT(before_log_size, std::filesystem::file_size(tmpdir.path() / "huh.log"));
   BOOST_REQUIRE_GT(before_index_size, std::filesystem::file_size(tmpdir.path() / "huh.index"));

} FC_LOG_AND_RETHROW();

//similar to above, but this time write a block that is before the first ever block of the pruned log (index_start_block). this isn't allowed
BOOST_AUTO_TEST_CASE(rewrite_too_old_pruned_block) try {
   const fc::temp_directory tmpdir;

   const state_history::state_history_log_config config = {state_history::prune_config{.prune_blocks = 40, .prune_threshold = 2}};

   std::map<block_num_type, sha256> wrote_data_for_blocknum;
   const unsigned begin_block = 10;
   const unsigned end_block = 105;

   //fill up blocks 10 through 104, but many of the early blocks are forgotten
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), config, "huh");
      for(unsigned i = begin_block; i < end_block; ++i)
         lc.pack_and_write_entry(fake_blockid_for_num(i), fake_blockid_for_num(i-1), [&](bio::filtering_ostreambuf& obuf) {
            bio::filtering_istreambuf hashed_randomness(sha256_filter() | bio::restrict(random_source(), 0, 64*1024));
            bio::copy(hashed_randomness, obuf);
            wrote_data_for_blocknum[i] = hashed_randomness.component<sha256_filter>(0)->enc->result();
         });
   }

   const unsigned start_rewrite_block = 9;

   eosio::state_history::log_catalog lc(tmpdir.path(), config, "huh");
   BOOST_REQUIRE_EXCEPTION(lc.pack_and_write_entry(fake_blockid_for_num(start_rewrite_block), fake_blockid_for_num(start_rewrite_block-1), [&](bio::filtering_ostreambuf& obuf) {}), chain::plugin_exception,
                           [](const chain::plugin_exception& e) {return e.get_log().at(0).get_message().find("is before start block") != std::string::npos;});

} FC_LOG_AND_RETHROW();

//verificaiton of clear()
const state_history::state_history_log_config log_configs_for_clear[] = {
   {std::monostate()},
   {state_history::partition_config{
      .retained_dir = {},
      .archive_dir = {},
      .stride = 5
   }},
   {state_history::partition_config{
      .retained_dir = {},
      .archive_dir = {},
      .stride = 5,
      .max_retained_files = 2
   }},
   {state_history::prune_config{
      .prune_blocks = 5,
      .prune_threshold = 2
   }}
};
BOOST_DATA_TEST_CASE(clear, bdata::make(log_configs_for_clear) * bdata::make({9u, 10u, 11u}), config, after_clear_begin_block) try {
   const fc::temp_directory tmpdir;

   const unsigned before_clear_begin_block = 10;
   const unsigned before_clear_end_block = 42;

   const unsigned after_clear_end_block = after_clear_begin_block+4;

   {
      eosio::state_history::log_catalog lc(tmpdir.path(), config, "clearme");
      for(unsigned i = before_clear_begin_block; i < before_clear_end_block; ++i)
         lc.pack_and_write_entry(fake_blockid_for_num(i), fake_blockid_for_num(i-1), [&](bio::filtering_ostreambuf& obuf) {});

      auto [begin_block, end_block] = lc.block_range();
      //not checking begin_block because logs could have been rotated or pruned depending on test case
      BOOST_REQUIRE_EQUAL(end_block, before_clear_end_block);

      lc.clear();
      BOOST_REQUIRE(lc.empty());
      //head log should be empty
      BOOST_REQUIRE_EQUAL(0u, std::filesystem::file_size(tmpdir.path() / "clearme.log"));
      BOOST_REQUIRE_EQUAL(0u, std::filesystem::file_size(tmpdir.path() / "clearme.index"));
      //make sure no retained logs exist
      for(const std::string& suffix : {"log"s, "index"s}) {
         const std::regex retained_logfile_regex(R"(^clearme-\d+-\d+\.)" + suffix + "$");

         unsigned found = 0;
         for(const std::filesystem::directory_entry& dir_entry : std::filesystem::directory_iterator(tmpdir.path()))
            found += std::regex_search(dir_entry.path().filename().string(), retained_logfile_regex);
         BOOST_REQUIRE_EQUAL(found, 0u);
      }

      for(unsigned i = after_clear_begin_block; i < after_clear_end_block; ++i)
         lc.pack_and_write_entry(fake_blockid_for_num(i), fake_blockid_for_num(i-1), [&](bio::filtering_ostreambuf& obuf) {});

      std::tie(begin_block, end_block) = lc.block_range();
      BOOST_REQUIRE_EQUAL(begin_block, after_clear_begin_block);
      BOOST_REQUIRE_EQUAL(end_block, after_clear_end_block);
   }

   //reopen for sanity check
   {
      eosio::state_history::log_catalog lc(tmpdir.path(), config, "clearme");
      const auto [begin_block, end_block] = lc.block_range();
      BOOST_REQUIRE_EQUAL(begin_block, after_clear_begin_block);
      BOOST_REQUIRE_EQUAL(end_block, after_clear_end_block);
   }
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
