#include <boost/test/unit_test.hpp>

#include <eosio/state_history/log_catalog.hpp>
#include <eosio/state_history/log_utils.hpp>

#include <fc/bitutil.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/filesystem.hpp>

#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/device/back_inserter.hpp>

#include <fstream>

namespace bio = boost::iostreams;
using namespace eosio;
using namespace eosio::chain;
namespace log_utils = eosio::state_history::log_utils;

namespace {

/**
 * Writes real ship logs through log_catalog (the same code path nodeos uses) while remembering
 * every block's payload, then hands the closed files to the log_utils functions under test.
 */
struct utils_fixture {
   explicit utils_fixture(std::optional<uint32_t> prune_blocks = std::nullopt) {
      if(prune_blocks)
         conf = eosio::state_history::prune_config{.prune_blocks = *prune_blocks, .prune_threshold = 8};
      open();
   }

   static block_id_type id_for(uint32_t block_num, char fill) {
      fc::sha256 id = fc::sha256::hash(std::to_string(block_num) + fill);
      id._hash[0]   = fc::endian_reverse_u32(block_num);
      return id;
   }

   /// append one block whose payload is `size` copies of `fill`; prev_fill names the parent's variant
   void add(uint32_t block_num, char fill, char prev_fill, size_t size = 192) {
      std::vector<char> payload(size, fill);
      log->pack_and_write_entry(id_for(block_num, fill), id_for(block_num - 1, prev_fill),
                                [&](auto& f) { bio::write(f, payload.data(), payload.size()); });
      if(written.size() < block_num + 1)
         written.resize(block_num + 1);
      written[block_num] = std::move(payload);
   }

   /// append blocks [first, last] all with the same fill lineage
   void add_range(uint32_t first, uint32_t last, char fill, char prev_fill, size_t size = 192) {
      for(uint32_t b = first; b <= last; ++b)
         add(b, fill, b == first ? prev_fill : fill, size);
   }

   void close() { log.reset(); }
   void open(bool force_write = false) {
      log.emplace(dir.path(), conf, name, eosio::state_history::state_history_log::no_non_local_get_block_id_func,
                  force_write);
   }

   std::filesystem::path stem() const { return dir.path() / name; }
   std::filesystem::path log_path() const { return dir.path() / (name + ".log"); }
   std::filesystem::path index_path() const { return dir.path() / (name + ".index"); }

   /// entry position of a block, read straight from the on-disk index (for surgical corruption)
   uint64_t index_slot(uint32_t block_num, uint32_t index_first_block) const {
      std::ifstream in(index_path(), std::ios::binary);
      in.seekg((uint64_t(block_num) - index_first_block) * sizeof(uint64_t));
      uint64_t pos = 0;
      in.read(reinterpret_cast<char*>(&pos), sizeof(pos));
      BOOST_REQUIRE(in.good());
      return pos;
   }

   /// rewrite the block id recorded by the header at log offset `pos`, leaving the entry otherwise
   /// structurally intact (magic, payload, and position trailer untouched) so it still passes every
   /// check_entry test except the block-number range — used to forge an out-of-range block number
   void set_block_id_at(uint64_t pos, const block_id_type& id) {
      std::fstream f(log_path(), std::ios::binary | std::ios::in | std::ios::out);
      f.seekp(pos + sizeof(uint64_t)); //past the 8-byte magic, onto block_id
      f.write(id.data(), id.data_size());
      BOOST_REQUIRE(f.good());
   }

   /// overwrite `len` bytes of the log at `pos` with bytes that can never look like a ship header
   void clobber(uint64_t pos, size_t len) {
      std::fstream f(log_path(), std::ios::binary | std::ios::in | std::ios::out);
      f.seekp(pos);
      const std::vector<char> junk(len, '\x5a');
      f.write(junk.data(), junk.size());
      BOOST_REQUIRE(f.good());
   }

   void append_garbage(size_t len) {
      std::ofstream f(log_path(), std::ios::binary | std::ios::app);
      const std::vector<char> junk(len, '\x5a');
      f.write(junk.data(), junk.size());
      BOOST_REQUIRE(f.good());
   }

   /// verify (through the real library) that the bundle serves exactly [first, last] with the written payloads
   void check_serves(uint32_t first, uint32_t last) {
      eosio::state_history::log_catalog reopened(dir.path(), conf, name);
      const auto [begin, end] = reopened.block_range();
      BOOST_REQUIRE_EQUAL(begin, first);
      BOOST_REQUIRE_EQUAL(end, last + 1);
      for(uint32_t b = first; b <= last; ++b) {
         std::optional<eosio::state_history::ship_log_entry> entry = reopened.get_entry(b);
         BOOST_REQUIRE_MESSAGE(!!entry, "block " + std::to_string(b) + " missing");
         bio::filtering_istreambuf stream = entry->get_stream();
         std::vector<char>         got;
         bio::copy(stream, bio::back_inserter(got));
         BOOST_REQUIRE_MESSAGE(got == written.at(b), "block " + std::to_string(b) + " content mismatch");
      }
   }

   static std::vector<char> slurp(const std::filesystem::path& p) {
      std::ifstream     in(p, std::ios::binary);
      std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      return bytes;
   }

   std::string                                    name = "shipit";
   eosio::state_history::state_history_log_config conf;
   fc::temp_directory                             dir;
   std::optional<eosio::state_history::log_catalog> log;
   std::vector<std::vector<char>>                 written;
};

} // namespace

BOOST_AUTO_TEST_SUITE(ship_log_utils_tests)

BOOST_AUTO_TEST_CASE(scan_intact_and_summary) { try {
   utils_fixture t;
   t.add_range(2, 50, 'A', 'A');
   t.close();

   const log_utils::scan_result scan = log_utils::scan_log(t.stem(), true);
   BOOST_REQUIRE(scan.intact());
   BOOST_REQUIRE(!scan.pruned);
   BOOST_REQUIRE_EQUAL(scan.valid_ranges.size(), 1u);
   BOOST_REQUIRE_EQUAL(scan.valid_ranges[0].first_block, 2u);
   BOOST_REQUIRE_EQUAL(scan.valid_ranges[0].last_block, 50u);
   BOOST_REQUIRE_EQUAL(scan.valid_ranges[0].canonical_first_block, 2u);
   BOOST_REQUIRE_EQUAL(scan.valid_ranges[0].entry_count, 49u);
   BOOST_REQUIRE_EQUAL(scan.entries_scanned, 49u);
   BOOST_REQUIRE_EQUAL(scan.payloads_validated, 49u);
   BOOST_REQUIRE_EQUAL(scan.valid_ranges[0].end_pos, scan.file_size);

   const log_utils::log_summary s = log_utils::summarize_log(t.stem());
   BOOST_REQUIRE(s.valid_first_header);
   BOOST_REQUIRE(s.tail_ok);
   BOOST_REQUIRE(!s.pruned);
   BOOST_REQUIRE_EQUAL(s.first_block, 2u);
   BOOST_REQUIRE_EQUAL(s.last_block, 50u);
   BOOST_REQUIRE(s.index == log_utils::index_status::ok);

   BOOST_REQUIRE(log_utils::check_index(t.stem(), true) == log_utils::index_status::ok);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(empty_log) { try {
   utils_fixture t;
   t.close();

   BOOST_REQUIRE(log_utils::scan_log(t.stem(), true).intact());
   BOOST_REQUIRE(log_utils::summarize_log(t.stem()).index == log_utils::index_status::ok);
   BOOST_REQUIRE(log_utils::check_index(t.stem(), true) == log_utils::index_status::ok);
   const auto [first, last] = log_utils::build_index(t.stem());
   BOOST_REQUIRE_EQUAL(first, 0u);
   BOOST_REQUIRE_EQUAL(last, 0u);

   const log_utils::repair_report rep = log_utils::repair_log(t.stem(), log_utils::repair_mode::truncate, false, true);
   BOOST_REQUIRE(!rep.acted);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(make_index_matches_library_including_forks) { try {
   utils_fixture t;
   t.add_range(2, 10, 'A', 'A');
   //fork: rewrite 8..10, then continue
   t.add(8, 'B', 'A');
   t.add_range(9, 12, 'B', 'B');
   //fork again right at the end so stale entries trail the final head
   t.add(11, 'C', 'B');
   t.close();

   const std::vector<char> library_index = utils_fixture::slurp(t.index_path());
   BOOST_REQUIRE(!library_index.empty());

   std::filesystem::remove(t.index_path());
   const auto [first, last] = log_utils::build_index(t.stem());
   BOOST_REQUIRE_EQUAL(first, 2u);
   BOOST_REQUIRE_EQUAL(last, 11u);

   const std::vector<char> rebuilt_index = utils_fixture::slurp(t.index_path());
   BOOST_REQUIRE(library_index == rebuilt_index);

   t.check_serves(2, 11);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(index_status_detection) { try {
   utils_fixture t;
   t.add_range(2, 30, 'A', 'A');
   t.close();

   BOOST_REQUIRE(log_utils::check_index(t.stem(), false) == log_utils::index_status::ok);

   //wrong size: lop 8 bytes off
   const std::vector<char> good = utils_fixture::slurp(t.index_path());
   std::filesystem::resize_file(t.index_path(), good.size() - sizeof(uint64_t));
   BOOST_REQUIRE(log_utils::check_index(t.stem(), false) == log_utils::index_status::wrong_size);
   BOOST_REQUIRE(log_utils::summarize_log(t.stem()).index == log_utils::index_status::wrong_size);

   //missing
   std::filesystem::remove(t.index_path());
   BOOST_REQUIRE(log_utils::check_index(t.stem(), false) == log_utils::index_status::missing);

   //right size but an interior slot lies: only the full check sees it
   {
      std::ofstream out(t.index_path(), std::ios::binary);
      out.write(good.data(), good.size());
   }
   {
      std::fstream f(t.index_path(), std::ios::binary | std::ios::in | std::ios::out);
      f.seekp(5 * sizeof(uint64_t));
      const uint64_t lie = 1;
      f.write(reinterpret_cast<const char*>(&lie), sizeof(lie));
   }
   BOOST_REQUIRE(log_utils::check_index(t.stem(), false) == log_utils::index_status::ok);
   BOOST_REQUIRE(log_utils::check_index(t.stem(), true) == log_utils::index_status::mismatched);

   //repair fixes it
   const log_utils::repair_report rep = log_utils::repair_log(t.stem(), log_utils::repair_mode::truncate, false, false);
   BOOST_REQUIRE(rep.acted);
   BOOST_REQUIRE(rep.index_rebuilt);
   BOOST_REQUIRE(log_utils::check_index(t.stem(), true) == log_utils::index_status::ok);
   t.check_serves(2, 30);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(find_block_id_and_endpoint_ids) { try {
   utils_fixture t;
   t.add_range(2, 40, 'A', 'A');
   //fork: rewrite 30..32 so those heights have canonical 'B' entries with stale 'A' entries lingering
   t.add(30, 'B', 'A');
   t.add_range(31, 32, 'B', 'B');
   t.close();

   //the summary reports the ids recorded at the endpoints
   const log_utils::log_summary s = log_utils::summarize_log(t.stem());
   BOOST_REQUIRE(s.first_block_id && *s.first_block_id == utils_fixture::id_for(2, 'A'));
   BOOST_REQUIRE(s.last_block_id && *s.last_block_id == utils_fixture::id_for(32, 'B'));

   //index fast path, including the canonical (latest-written) answer for fork-overwritten heights
   BOOST_REQUIRE(log_utils::find_block_id(t.stem(), 2) == utils_fixture::id_for(2, 'A'));
   BOOST_REQUIRE(log_utils::find_block_id(t.stem(), 29) == utils_fixture::id_for(29, 'A'));
   BOOST_REQUIRE(log_utils::find_block_id(t.stem(), 30) == utils_fixture::id_for(30, 'B'));
   BOOST_REQUIRE(log_utils::find_block_id(t.stem(), 32) == utils_fixture::id_for(32, 'B'));

   //out of range: before the first block, and past the head even though stale 'A' entries for
   // 33..40 physically remain in the file
   BOOST_REQUIRE(!log_utils::find_block_id(t.stem(), 1));
   BOOST_REQUIRE(!log_utils::find_block_id(t.stem(), 33));

   //no index at all: the read-only walk gives the same answers
   std::filesystem::remove(t.index_path());
   BOOST_REQUIRE(log_utils::find_block_id(t.stem(), 2) == utils_fixture::id_for(2, 'A'));
   BOOST_REQUIRE(log_utils::find_block_id(t.stem(), 30) == utils_fixture::id_for(30, 'B'));
   BOOST_REQUIRE(!std::filesystem::exists(t.index_path())); //and it really was read-only
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(find_block_id_distrusts_bad_index) { try {
   utils_fixture t;
   t.add_range(2, 25, 'A', 'A');
   t.close();

   //point block 10's slot at block 11's entry: the index keeps its expected size and its final
   // slot, so the shallow open-time checks (and a naive lookup) would happily trust it
   const uint64_t pos11 = t.index_slot(11, 2);
   {
      std::fstream f(t.index_path(), std::ios::binary | std::ios::in | std::ios::out);
      f.seekp((10 - 2) * sizeof(uint64_t));
      f.write(reinterpret_cast<const char*>(&pos11), sizeof(pos11));
      BOOST_REQUIRE(f.good());
   }
   //the poisoned slot is detected and the answer comes from walking the log instead
   BOOST_REQUIRE(log_utils::find_block_id(t.stem(), 10) == utils_fixture::id_for(10, 'A'));
   //unpoisoned slots still resolve (fast path)
   BOOST_REQUIRE(log_utils::find_block_id(t.stem(), 11) == utils_fixture::id_for(11, 'A'));

   //a final slot that disagrees with the log fails the shallow check; everything walks
   {
      std::fstream f(t.index_path(), std::ios::binary | std::ios::in | std::ios::out);
      f.seekp((25 - 2) * sizeof(uint64_t));
      const uint64_t junk = 1;
      f.write(reinterpret_cast<const char*>(&junk), sizeof(junk));
      BOOST_REQUIRE(f.good());
   }
   BOOST_REQUIRE(log_utils::find_block_id(t.stem(), 25) == utils_fixture::id_for(25, 'A'));
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(find_block_id_pruned) { try {
   utils_fixture t(4 /*prune_blocks*/);
   const size_t entry_size = 4096 + 2048; //large enough that hole punching actually frees blocks
   t.add_range(2, 20, 'A', 'A', entry_size);
   t.close();

   const log_utils::log_summary s = log_utils::summarize_log(t.stem());
   BOOST_REQUIRE(s.pruned && s.tail_ok);
   //a pruned log's first header is the stub for its pre-prune first block; the id is still real
   BOOST_REQUIRE(s.first_block_id && *s.first_block_id == utils_fixture::id_for(2, 'A'));
   BOOST_REQUIRE(s.last_block_id && *s.last_block_id == utils_fixture::id_for(20, 'A'));

   const uint32_t first_servable = s.last_block - *s.pruned_block_count + 1;
   BOOST_REQUIRE(log_utils::find_block_id(t.stem(), 20) == utils_fixture::id_for(20, 'A'));
   BOOST_REQUIRE(log_utils::find_block_id(t.stem(), first_servable) == utils_fixture::id_for(first_servable, 'A'));
   BOOST_REQUIRE(!log_utils::find_block_id(t.stem(), first_servable - 1)); //pruned away
   BOOST_REQUIRE(!log_utils::find_block_id(t.stem(), 21));

   //the backward trailer-chain walk handles pruned logs when the index is gone
   std::filesystem::remove(t.index_path());
   BOOST_REQUIRE(log_utils::find_block_id(t.stem(), 20) == utils_fixture::id_for(20, 'A'));
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(fork_mismatch_messages_carry_ids) { try {
   utils_fixture t;
   t.add_range(2, 10, 'A', 'A');

   //rewind path: the expected id, the recorded id, and the recorded id's own block number must all be
   // reported, so a corrupt index (which resolves to some unrelated block's entry) is tellable from a
   // genuine fork without further digging
   BOOST_CHECK_EXCEPTION(t.add(8, 'X', 'Z'), plugin_exception, [](const plugin_exception& e) {
      const std::string d = e.to_detail_string();
      return d.find("missed a fork change") != std::string::npos &&
             d.find(utils_fixture::id_for(7, 'Z').str()) != std::string::npos && //what the incoming block expected
             d.find(utils_fixture::id_for(7, 'A').str()) != std::string::npos && //what the log recorded
             d.find("an id for block 7") != std::string::npos;
   });

   //append path: same information, sourced from the in-memory last_block_id
   BOOST_CHECK_EXCEPTION(t.add(11, 'X', 'Z'), plugin_exception, [](const plugin_exception& e) {
      const std::string d = e.to_detail_string();
      return d.find("missed a fork change") != std::string::npos &&
             d.find(utils_fixture::id_for(10, 'Z').str()) != std::string::npos &&
             d.find(utils_fixture::id_for(10, 'A').str()) != std::string::npos &&
             d.find("block 10") != std::string::npos;
   });

   //neither failed write may disturb the log
   t.close();
   t.check_serves(2, 10);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(find_block_id_empty_and_damaged) { try {
   utils_fixture empty;
   empty.close();
   BOOST_REQUIRE(!log_utils::find_block_id(empty.stem(), 1));
   BOOST_REQUIRE(!log_utils::summarize_log(empty.stem()).first_block_id);
   BOOST_REQUIRE(!log_utils::summarize_log(empty.stem()).last_block_id);

   utils_fixture t;
   t.add_range(2, 10, 'A', 'A');
   t.close();
   t.append_garbage(33); //torn tail

   const log_utils::log_summary s = log_utils::summarize_log(t.stem());
   BOOST_REQUIRE(s.first_block_id && !s.last_block_id);
   BOOST_CHECK_EXCEPTION(log_utils::find_block_id(t.stem(), 5), plugin_exception, [](const plugin_exception& e) {
      return e.to_detail_string().find("damaged") != std::string::npos;
   });
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(repair_truncated_tail) { try {
   utils_fixture t;
   t.add_range(2, 40, 'A', 'A');
   t.close();

   const uint64_t good_size = std::filesystem::file_size(t.log_path());
   t.append_garbage(100); //simulates a torn write at shutdown

   log_utils::scan_result scan = log_utils::scan_log(t.stem(), false);
   BOOST_REQUIRE(!scan.intact());
   BOOST_REQUIRE_EQUAL(scan.valid_ranges.size(), 1u);
   BOOST_REQUIRE_EQUAL(scan.damaged_ranges.size(), 1u);
   BOOST_REQUIRE_EQUAL(scan.damaged_ranges[0].begin_pos, good_size);

   //dry run changes nothing
   const log_utils::repair_report dry = log_utils::repair_log(t.stem(), log_utils::repair_mode::truncate, true, false);
   BOOST_REQUIRE(!dry.acted);
   BOOST_REQUIRE_EQUAL(std::filesystem::file_size(t.log_path()), good_size + 100);

   const log_utils::repair_report rep = log_utils::repair_log(t.stem(), log_utils::repair_mode::truncate, false, false);
   BOOST_REQUIRE(rep.acted);
   BOOST_REQUIRE_EQUAL(rep.first_block, 2u);
   BOOST_REQUIRE_EQUAL(rep.last_block, 40u);
   BOOST_REQUIRE_EQUAL(std::filesystem::file_size(t.log_path()), good_size);
   t.check_serves(2, 40);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(repair_mid_file_damage_truncate_and_keep_tail) { try {
   utils_fixture t;
   t.add_range(2, 60, 'A', 'A');
   t.close();

   //clobber the header of block 30's entry: everything from there is structurally unreachable
   // until the scanner resynchronizes at block 31's entry
   const uint64_t pos30 = t.index_slot(30, 2);
   const uint64_t pos31 = t.index_slot(31, 2);
   t.clobber(pos30, 16);

   const log_utils::scan_result scan = log_utils::scan_log(t.stem(), false);
   BOOST_REQUIRE(!scan.intact());
   BOOST_REQUIRE_EQUAL(scan.valid_ranges.size(), 2u);
   BOOST_REQUIRE_EQUAL(scan.damaged_ranges.size(), 1u);
   BOOST_REQUIRE_EQUAL(scan.valid_ranges[0].first_block, 2u);
   BOOST_REQUIRE_EQUAL(scan.valid_ranges[0].last_block, 29u);
   BOOST_REQUIRE_EQUAL(scan.damaged_ranges[0].begin_pos, pos30);
   BOOST_REQUIRE_EQUAL(scan.damaged_ranges[0].end_pos, pos31);
   BOOST_REQUIRE_EQUAL(scan.valid_ranges[1].first_block, 31u);
   BOOST_REQUIRE_EQUAL(scan.valid_ranges[1].last_block, 60u);

   //keep-tail to a separate output leaves the original untouched
   {
      fc::temp_directory out_dir;
      const std::filesystem::path  out_stem = out_dir.path() / "salvaged";
      const log_utils::repair_report rep =
         log_utils::repair_log(t.stem(), log_utils::repair_mode::keep_tail, false, false, out_stem);
      BOOST_REQUIRE(rep.acted);
      BOOST_REQUIRE_EQUAL(rep.first_block, 31u);
      BOOST_REQUIRE_EQUAL(rep.last_block, 60u);

      utils_fixture::slurp(out_stem.string() + ".log"); //exists and is readable
      eosio::state_history::state_history_log salvaged(out_stem);
      const auto [begin, end] = salvaged.block_range();
      BOOST_REQUIRE_EQUAL(begin, 31u);
      BOOST_REQUIRE_EQUAL(end, 61u);
   }

   //truncate repair keeps the prefix
   const log_utils::repair_report rep = log_utils::repair_log(t.stem(), log_utils::repair_mode::truncate, false, false);
   BOOST_REQUIRE(rep.acted);
   BOOST_REQUIRE_EQUAL(rep.first_block, 2u);
   BOOST_REQUIRE_EQUAL(rep.last_block, 29u);
   t.check_serves(2, 29);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(repair_keep_tail_in_place) { try {
   utils_fixture t;
   t.add_range(2, 50, 'A', 'A');
   t.close();

   t.clobber(t.index_slot(10, 2), 16);

   const log_utils::repair_report rep =
      log_utils::repair_log(t.stem(), log_utils::repair_mode::keep_tail, false, false);
   BOOST_REQUIRE(rep.acted);
   BOOST_REQUIRE_EQUAL(rep.first_block, 11u);
   BOOST_REQUIRE_EQUAL(rep.last_block, 50u);
   t.check_serves(11, 50);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(repair_damaged_from_start) { try {
   utils_fixture t;
   t.add_range(2, 20, 'A', 'A');
   t.close();

   t.clobber(0, 16); //first header gone

   const log_utils::scan_result scan = log_utils::scan_log(t.stem(), false);
   BOOST_REQUIRE(!scan.intact());
   BOOST_REQUIRE(!scan.valid_ranges.empty());
   BOOST_REQUIRE(scan.valid_ranges[0].begin_pos > 0);

   //nothing before the damage, so a truncate repair has nothing to keep
   BOOST_REQUIRE_THROW(log_utils::repair_log(t.stem(), log_utils::repair_mode::truncate, false, false),
                       plugin_exception);

   const log_utils::repair_report rep =
      log_utils::repair_log(t.stem(), log_utils::repair_mode::keep_tail, false, false);
   BOOST_REQUIRE(rep.acted);
   BOOST_REQUIRE_EQUAL(rep.first_block, 3u);
   BOOST_REQUIRE_EQUAL(rep.last_block, 20u);
   t.check_serves(3, 20);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(deep_scan_catches_payload_bitflip) { try {
   utils_fixture t;
   t.add_range(2, 30, 'A', 'A');
   t.close();

   //flip one byte inside block 15's compressed payload: structure stays perfectly valid
   const uint64_t pos15 = t.index_slot(15, 2);
   t.clobber(pos15 + 70, 1); //48-byte header + 12-byte preamble + a few bytes into the zlib stream

   BOOST_REQUIRE(log_utils::scan_log(t.stem(), false).intact());

   const log_utils::scan_result deep = log_utils::scan_log(t.stem(), true);
   BOOST_REQUIRE(!deep.intact());
   BOOST_REQUIRE_EQUAL(deep.damaged_ranges.size(), 1u);
   BOOST_REQUIRE_EQUAL(deep.damaged_ranges[0].begin_pos, pos15);
   BOOST_REQUIRE_EQUAL(deep.valid_ranges.size(), 2u);
   BOOST_REQUIRE_EQUAL(deep.valid_ranges[0].last_block, 14u);
   BOOST_REQUIRE_EQUAL(deep.valid_ranges[1].first_block, 16u);

   //a plain repair sees nothing wrong; a deep repair truncates at the rotten entry
   const log_utils::repair_report rep = log_utils::repair_log(t.stem(), log_utils::repair_mode::truncate, false, true);
   BOOST_REQUIRE(rep.acted);
   BOOST_REQUIRE_EQUAL(rep.last_block, 14u);
   t.check_serves(2, 14);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(keep_tail_canonical_start_with_forks) { try {
   utils_fixture t;
   t.add_range(2, 10, 'A', 'A');
   //fork: rewrite 8..10 with different content
   t.add(8, 'B', 'A');
   t.add(9, 'B', 'B');
   t.add(10, 'B', 'B');
   t.close();

   //destroy block 7's entry; the tail range then starts at the STALE block-8 entry, and a salvaged
   // bundle must instead begin at the fork's rewritten block 8
   const uint64_t stale8 = [&]() {
      //the stale entry for 8 sits right after block 7's entry; find 7's start from the index and
      // walk one entry forward using its header
      const uint64_t pos7 = t.index_slot(7, 2);
      std::ifstream  in(t.log_path(), std::ios::binary);
      in.seekg(pos7 + 40); //past magic + block_id to payload_size
      uint64_t psz = 0;
      in.read(reinterpret_cast<char*>(&psz), sizeof(psz));
      return pos7 + 48 + psz + 8;
   }();
   t.clobber(t.index_slot(7, 2), 16);

   const log_utils::scan_result scan = log_utils::scan_log(t.stem(), false);
   BOOST_REQUIRE(!scan.intact());
   const log_utils::entry_range& tail = scan.valid_ranges.back();
   BOOST_REQUIRE_EQUAL(tail.begin_pos, stale8);       //resynchronized at the stale fork entry
   BOOST_REQUIRE_EQUAL(tail.first_block, 8u);
   BOOST_REQUIRE_EQUAL(tail.canonical_first_block, 8u);
   BOOST_REQUIRE_GT(tail.canonical_begin_pos, stale8); //canonical start is the rewritten 8, later in the file

   const log_utils::repair_report rep =
      log_utils::repair_log(t.stem(), log_utils::repair_mode::keep_tail, false, false);
   BOOST_REQUIRE(rep.acted);
   BOOST_REQUIRE_EQUAL(rep.first_block, 8u);
   BOOST_REQUIRE_EQUAL(rep.last_block, 10u);
   t.check_serves(8, 10); //written[] holds the fork's 'B' payloads, so this proves the new content survived
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(trim_end_and_front) { try {
   utils_fixture t;
   t.add_range(2, 60, 'A', 'A');
   t.close();

   const uint64_t end_removed = log_utils::truncate_log(t.stem(), 50);
   BOOST_REQUIRE_GT(end_removed, 0u);
   BOOST_REQUIRE_EQUAL(log_utils::truncate_log(t.stem(), 50), 0u); //no-op at the boundary

   const uint64_t front_removed = log_utils::trim_front(t.stem(), 10);
   BOOST_REQUIRE_GT(front_removed, 0u);
   BOOST_REQUIRE_EQUAL(log_utils::trim_front(t.stem(), 10), 0u);

   BOOST_REQUIRE(log_utils::check_index(t.stem(), true) == log_utils::index_status::ok);
   t.check_serves(10, 50);

   BOOST_REQUIRE_THROW(log_utils::truncate_log(t.stem(), 9), plugin_exception);  //outside range
   BOOST_REQUIRE_THROW(log_utils::trim_front(t.stem(), 51), plugin_exception);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(trim_front_without_index) { try {
   utils_fixture t;
   t.add_range(2, 30, 'A', 'A');
   t.close();

   std::filesystem::remove(t.index_path()); //forces the in-memory forward-walk fallback
   const uint64_t removed = log_utils::trim_front(t.stem(), 12);
   BOOST_REQUIRE_GT(removed, 0u);
   t.check_serves(12, 30);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(extract_blocks_basic) { try {
   utils_fixture t;
   t.add_range(2, 40, 'A', 'A');
   t.close();

   fc::temp_directory          out_dir;
   const std::filesystem::path out_stem = out_dir.path() / "slice";
   const uint64_t              bytes    = log_utils::extract_blocks(t.stem(), out_stem, 10, 20);
   BOOST_REQUIRE_GT(bytes, 0u);

   eosio::state_history::state_history_log slice(out_stem);
   const auto [begin, end] = slice.block_range();
   BOOST_REQUIRE_EQUAL(begin, 10u);
   BOOST_REQUIRE_EQUAL(end, 21u);

   //source untouched
   t.check_serves(2, 40);

   //destination collision refused
   BOOST_REQUIRE_THROW(log_utils::extract_blocks(t.stem(), out_stem, 10, 20), plugin_exception);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(split_then_serve_through_catalog_then_merge) { try {
   utils_fixture t;
   t.add_range(2, 25, 'A', 'A');
   t.close();

   fc::temp_directory split_dir;
   const std::vector<std::filesystem::path> created = log_utils::split_log(t.stem(), split_dir.path(), 10);
   //blocks 2-25 with stride 10 -> retained 2-10, 11-20, and head 21-25
   BOOST_REQUIRE_EQUAL(created.size(), 3u);
   BOOST_REQUIRE_EQUAL(created[0].filename().string(), "shipit-2-10");
   BOOST_REQUIRE_EQUAL(created[1].filename().string(), "shipit-11-20");
   BOOST_REQUIRE_EQUAL(created[2].filename().string(), "shipit");

   //the real consumer check: a log_catalog with a partition config must serve every block from the split set
   {
      eosio::state_history::state_history_log_config split_conf =
         eosio::state_history::partition_config{.retained_dir = split_dir.path(), .archive_dir = "", .stride = 10};
      eosio::state_history::log_catalog catalog(split_dir.path(), split_conf, t.name);
      const auto [begin, end] = catalog.block_range();
      BOOST_REQUIRE_EQUAL(begin, 2u);
      BOOST_REQUIRE_EQUAL(end, 26u);
      for(uint32_t b = 2; b <= 25; ++b) {
         std::optional<eosio::state_history::ship_log_entry> entry = catalog.get_entry(b);
         BOOST_REQUIRE(!!entry);
         bio::filtering_istreambuf stream = entry->get_stream();
         std::vector<char>         got;
         bio::copy(stream, bio::back_inserter(got));
         BOOST_REQUIRE(got == t.written.at(b));
      }
   }

   //merge the retained bundles back into one log
   fc::temp_directory merge_dir;
   const auto [first, last] = log_utils::merge_logs(split_dir.path(), t.name, merge_dir.path());
   BOOST_REQUIRE_EQUAL(first, 2u);
   BOOST_REQUIRE_EQUAL(last, 20u);
   eosio::state_history::state_history_log merged(merge_dir.path() / t.name);
   const auto [mbegin, mend] = merged.block_range();
   BOOST_REQUIRE_EQUAL(mbegin, 2u);
   BOOST_REQUIRE_EQUAL(mend, 21u);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(split_exact_boundary_leaves_empty_head) { try {
   utils_fixture t;
   t.add_range(2, 20, 'A', 'A');
   t.close();

   fc::temp_directory split_dir;
   const std::vector<std::filesystem::path> created = log_utils::split_log(t.stem(), split_dir.path(), 10);
   BOOST_REQUIRE_EQUAL(created.size(), 3u);
   BOOST_REQUIRE_EQUAL(created.back().filename().string(), "shipit");
   BOOST_REQUIRE_EQUAL(std::filesystem::file_size(created.back().string() + ".log"), 0u);

   //merge refuses nothing here: both retained bundles are contiguous
   fc::temp_directory merge_dir;
   const auto [first, last] = log_utils::merge_logs(split_dir.path(), t.name, merge_dir.path());
   BOOST_REQUIRE_EQUAL(first, 2u);
   BOOST_REQUIRE_EQUAL(last, 20u);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(merge_refuses_gaps) { try {
   utils_fixture t;
   t.add_range(2, 30, 'A', 'A');
   t.close();

   fc::temp_directory split_dir;
   log_utils::split_log(t.stem(), split_dir.path(), 10);
   //remove the middle bundle to create a hole
   std::filesystem::remove(split_dir.path() / "shipit-11-20.log");
   std::filesystem::remove(split_dir.path() / "shipit-11-20.index");

   fc::temp_directory merge_dir;
   BOOST_REQUIRE_THROW(log_utils::merge_logs(split_dir.path(), t.name, merge_dir.path()), plugin_exception);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(pruned_log_scan_index_and_refusals) { try {
   utils_fixture t(4 /*prune_blocks*/);
   const size_t entry_size = 4096 + 2048; //large enough that hole punching actually frees blocks
   t.add_range(2, 20, 'A', 'A', entry_size);
   t.close();

   const log_utils::log_summary s = log_utils::summarize_log(t.stem());
   BOOST_REQUIRE(s.pruned);
   BOOST_REQUIRE(s.tail_ok);

   const log_utils::scan_result scan = log_utils::scan_log(t.stem(), true);
   BOOST_REQUIRE(scan.pruned);
   BOOST_REQUIRE(scan.intact());
   BOOST_REQUIRE_EQUAL(scan.valid_ranges.size(), 1u);
   BOOST_REQUIRE_EQUAL(scan.valid_ranges[0].last_block, 20u);
   BOOST_REQUIRE_EQUAL(scan.valid_ranges[0].first_block, 20u - *scan.pruned_block_count + 1);

   BOOST_REQUIRE(log_utils::check_index(t.stem(), true) == log_utils::index_status::ok);

   //the library's regenerated index and ours must agree byte for byte
   {
      t.open(); //library reopen (still pruned)
      t.close();
      std::filesystem::remove(t.index_path());
      t.open(); //library regenerates the index
      t.close();
      const std::vector<char> library_index = utils_fixture::slurp(t.index_path());
      std::filesystem::remove(t.index_path());
      log_utils::build_index(t.stem());
      const std::vector<char> ours = utils_fixture::slurp(t.index_path());
      BOOST_REQUIRE(library_index == ours);
   }

   //mutating operations refuse pruned logs
   BOOST_REQUIRE_THROW(log_utils::truncate_log(t.stem(), 19), plugin_exception);
   BOOST_REQUIRE_THROW(log_utils::trim_front(t.stem(), 19), plugin_exception);
   BOOST_REQUIRE_THROW(log_utils::split_log(t.stem(), t.dir.path() / "split", 5), plugin_exception);
   fc::temp_directory out_dir;
   BOOST_REQUIRE_THROW(log_utils::extract_blocks(t.stem(), out_dir.path() / "x", 19, 20), plugin_exception);

   //a damaged pruned log is beyond repair, matching the library
   t.clobber(t.index_slot(19, 2), 16);
   BOOST_REQUIRE(!log_utils::scan_log(t.stem(), false).intact());
   BOOST_REQUIRE_THROW(log_utils::repair_log(t.stem(), log_utils::repair_mode::truncate, false, false),
                       plugin_exception);
} FC_LOG_AND_RETHROW() }

// A deep fork switch in a pruned log's retained tail leaves superseded entries whose block number is
// ABOVE the final head (the library's "we see block 7 and 6 when reading" case). The library skips
// them when regenerating the index rather than treating them as damage; make-index, full check-index
// and block-id must do the same and never index past the slot vector. Without the high-side skip in
// compute_index_pruned this backward walk writes out of bounds even though the log is perfectly valid.
BOOST_AUTO_TEST_CASE(pruned_index_skips_superseded_above_head) { try {
   utils_fixture t(6 /*prune_blocks*/);
   const size_t entry_size = 4096 + 2048; //large enough that hole punching actually frees blocks
   t.add_range(2, 20, 'A', 'A', entry_size); //head climbs to 20
   //fork: rewrite block 19 on a 'B' lineage; the head retreats to 19 while the now-stale 'A' entry
   // for block 20 stays in the retained tail with a block number above the head
   t.add(19, 'B', 'A', entry_size);
   t.close();

   const log_utils::log_summary s = log_utils::summarize_log(t.stem());
   BOOST_REQUIRE(s.pruned && s.tail_ok);
   BOOST_REQUIRE_EQUAL(s.last_block, 19u);
   const uint32_t first_servable = s.last_block - *s.pruned_block_count + 1;

   //the library's regenerated index is the reference: it skips the above-head block-20 entry
   t.open(); //library reopen (still pruned) confirms the forked log is accepted
   t.close();
   std::filesystem::remove(t.index_path());
   t.open(); //library regenerates the index
   t.close();
   const std::vector<char> library_index = utils_fixture::slurp(t.index_path());

   //ours must match it byte for byte rather than crash or diverge
   std::filesystem::remove(t.index_path());
   const auto [first, last] = log_utils::build_index(t.stem());
   BOOST_REQUIRE_EQUAL(first, first_servable);
   BOOST_REQUIRE_EQUAL(last, 19u);
   BOOST_REQUIRE(utils_fixture::slurp(t.index_path()) == library_index);

   BOOST_REQUIRE(log_utils::check_index(t.stem(), true) == log_utils::index_status::ok);
   BOOST_REQUIRE(log_utils::scan_log(t.stem(), true).intact());

   //id lookups return the canonical (latest-written) content and refuse the above-head block
   BOOST_REQUIRE(log_utils::find_block_id(t.stem(), 19) == utils_fixture::id_for(19, 'B'));
   BOOST_REQUIRE(log_utils::find_block_id(t.stem(), first_servable) == utils_fixture::id_for(first_servable, 'A'));
   BOOST_REQUIRE(!log_utils::find_block_id(t.stem(), 20)); //above the head: not served

   //the backward walk (index removed) reaches the same answer without indexing out of range
   std::filesystem::remove(t.index_path());
   BOOST_REQUIRE(log_utils::find_block_id(t.stem(), 19) == utils_fixture::id_for(19, 'B'));

   t.check_serves(first_servable, 19);
} FC_LOG_AND_RETHROW() }

// A structurally valid entry whose recorded block number is BELOW the log's first block cannot occur
// in a healthy log (nothing is ever written before the first block), so the inspection tooling must
// report it as damage and fail cleanly instead of indexing below slot 0. This is the underflow half
// of the ci.slots[c.block_num - ci.index_first] concern: build_index, full check-index, the scanner,
// and find_block_id must all reject it rather than read out of bounds.
BOOST_AUTO_TEST_CASE(pruned_out_of_range_block_num_is_damage) { try {
   utils_fixture t(4 /*prune_blocks*/);
   const size_t entry_size = 4096 + 2048;
   t.add_range(2, 20, 'A', 'A', entry_size);
   t.close();

   //forge a retained entry so it decodes to block 1, before the log's first block (2); the magic,
   // payload, and position trailer stay valid, so only the range check stands between this entry and
   // an out-of-bounds slot index
   t.set_block_id_at(t.index_slot(19, 2), utils_fixture::id_for(1, 'A'));

   BOOST_REQUIRE_THROW(log_utils::build_index(t.stem()), plugin_exception);
   BOOST_REQUIRE(log_utils::check_index(t.stem(), true) == log_utils::index_status::log_damaged);
   BOOST_REQUIRE(!log_utils::scan_log(t.stem(), false).intact());

   std::filesystem::remove(t.index_path());
   BOOST_REQUIRE_THROW(log_utils::find_block_id(t.stem(), 20), plugin_exception);
} FC_LOG_AND_RETHROW() }

// find_block_id's fallback — taken when the on-disk index is missing or untrusted — recomputes the
// index by walking the log. The pruned backward walk does not require contiguous block numbers, so a
// damaged retained chain can leave a block inside the servable range with no entry of its own: a zero
// slot. Reading that slot blindly unpacks the header at offset 0 (the pruned stub, whose id is a real
// but unrelated block) and returns the wrong id. The fallback must validate the slot the same way the
// fast path does and report the log as damaged instead. Verified to return block 2's stub id, not
// throw, without the slot_holds_block check in the fallback.
BOOST_AUTO_TEST_CASE(find_block_id_fallback_rejects_orphaned_servable_block) { try {
   utils_fixture t(6 /*prune_blocks*/);
   const size_t entry_size = 4096 + 2048; //large enough that hole punching actually frees blocks
   t.add_range(2, 20, 'A', 'A', entry_size);
   t.close();

   const log_utils::log_summary s = log_utils::summarize_log(t.stem());
   BOOST_REQUIRE(s.pruned && s.tail_ok);
   const uint32_t first_servable = s.last_block - *s.pruned_block_count + 1;
   const uint32_t orphan         = first_servable + 1; //comfortably inside (first_servable, last_block)
   BOOST_REQUIRE(orphan > first_servable && orphan < s.last_block);

   //forge the orphan's retained entry so it decodes as its successor — a block already covered by its
   // own real entry. Nothing now decodes to `orphan`, so the recomputed index leaves its slot at zero,
   // while positions are untouched so the trailer chain still walks cleanly (no damage detected there).
   t.set_block_id_at(t.index_slot(orphan, 2), utils_fixture::id_for(orphan + 1, 'A'));

   //force the fallback: with no on-disk index find_block_id must recompute by walking the log
   std::filesystem::remove(t.index_path());

   //the orphaned block is inside the servable range but absent from the log: report damage, never the
   // stub's (block 2's) id that unpacking offset 0 would otherwise yield
   BOOST_REQUIRE_THROW(log_utils::find_block_id(t.stem(), orphan), plugin_exception);

   //blocks that are present still resolve to their real ids through the same fallback
   BOOST_REQUIRE(log_utils::find_block_id(t.stem(), s.last_block) == utils_fixture::id_for(s.last_block, 'A'));
   BOOST_REQUIRE(log_utils::find_block_id(t.stem(), first_servable) == utils_fixture::id_for(first_servable, 'A'));
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(force_write_head_gap) { try {
   utils_fixture t;
   t.add_range(2, 10, 'A', 'A');
   t.close();

   //without force-write, a gap (e.g. after restoring a snapshot beyond the log's head) is fatal
   t.open();
   BOOST_REQUIRE_EXCEPTION(t.add(50, 'A', 'A'), plugin_exception, [](const plugin_exception& e) {
      return e.to_detail_string().find("skips over") != std::string::npos;
   });
   t.close();

   //with force-write the old bundle is moved aside and writing continues in a fresh log
   t.open(true);
   t.add(50, 'A', 'A');
   t.add(51, 'A', 'A');
   t.close();
   t.check_serves(50, 51);

   const std::filesystem::path orphan = t.dir.path() / (t.name + "-corrupt-1.log");
   BOOST_REQUIRE(std::filesystem::exists(orphan));
   BOOST_REQUIRE_GT(std::filesystem::file_size(orphan), 0u);
   //the orphan is itself a healthy bundle holding the old blocks
   const log_utils::scan_result orphan_scan = log_utils::scan_log(t.dir.path() / (t.name + "-corrupt-1"), true);
   BOOST_REQUIRE(orphan_scan.intact());
   BOOST_REQUIRE_EQUAL(orphan_scan.valid_ranges[0].first_block, 2u);
   BOOST_REQUIRE_EQUAL(orphan_scan.valid_ranges[0].last_block, 10u);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(force_write_fork_mismatch) { try {
   utils_fixture t;
   t.add_range(2, 10, 'A', 'A');
   t.close();

   t.open();
   BOOST_REQUIRE_EXCEPTION(t.add(10, 'X', 'Z'), plugin_exception, [](const plugin_exception& e) {
      return e.to_detail_string().find("missed a fork change") != std::string::npos;
   });
   t.close();

   t.open(true);
   t.add(10, 'X', 'Z'); //a different chain's history: old bundle orphaned, fresh log starts at 10
   t.close();
   t.check_serves(10, 10);
   BOOST_REQUIRE(std::filesystem::exists(t.dir.path() / (t.name + "-corrupt-1.log")));
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(force_write_index_disagreement_self_heals) { try {
   utils_fixture t;
   t.add_range(2, 10, 'A', 'A');
   t.close();

   //right-size index whose final position lies: only check_log_and_index_on_init notices
   const uint64_t index_size = std::filesystem::file_size(t.index_path());
   {
      std::fstream f(t.index_path(), std::ios::binary | std::ios::in | std::ios::out);
      f.seekp(index_size - sizeof(uint64_t));
      const uint64_t lie = 1;
      f.write(reinterpret_cast<const char*>(&lie), sizeof(lie));
   }

   BOOST_REQUIRE_THROW(t.open(), plugin_exception);
   t.close();

   t.open(true); //regenerates the index instead of failing, and must not orphan anything
   t.close();
   BOOST_REQUIRE(!std::filesystem::exists(t.dir.path() / (t.name + "-corrupt-1.log")));
   t.check_serves(2, 10);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(force_write_retained_gap_tolerated) { try {
   utils_fixture t;
   t.add_range(2, 30, 'A', 'A');
   t.close();

   fc::temp_directory split_dir;
   log_utils::split_log(t.stem(), split_dir.path(), 10);
   std::filesystem::remove(split_dir.path() / "shipit-11-20.log");
   std::filesystem::remove(split_dir.path() / "shipit-11-20.index");

   const eosio::state_history::state_history_log_config split_conf =
      eosio::state_history::partition_config{.retained_dir = split_dir.path(), .archive_dir = "", .stride = 10};

   //the hole is fatal without force-write
   BOOST_REQUIRE_THROW(eosio::state_history::log_catalog(split_dir.path(), split_conf, t.name), plugin_exception);

   //with force-write the catalog opens; blocks in the hole are simply not served
   eosio::state_history::log_catalog catalog(split_dir.path(), split_conf, t.name,
                                             eosio::state_history::state_history_log::no_non_local_get_block_id_func,
                                             true);
   BOOST_REQUIRE(!!catalog.get_entry(5));
   BOOST_REQUIRE(!catalog.get_entry(15));
   BOOST_REQUIRE(!!catalog.get_entry(25));
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(force_write_below_retained_history) { try {
   //a write below the first retained block (a chain wiped and resynced from scratch) cannot be
   // absorbed by orphaning just the head log; force-write must move the whole catalog aside
   utils_fixture t;
   t.add_range(2, 25, 'A', 'A');
   t.close();

   fc::temp_directory          catalog_dir;
   const std::filesystem::path retained = catalog_dir.path() / "retained";
   log_utils::split_log(t.stem(), retained, 10); //retained 2-10, 11-20, head 21-25
   std::filesystem::rename(log_utils::normalize_stem(retained / t.name).string() + ".log",
                           (catalog_dir.path() / (t.name + ".log")).string());
   std::filesystem::rename(log_utils::normalize_stem(retained / t.name).string() + ".index",
                           (catalog_dir.path() / (t.name + ".index")).string());

   const eosio::state_history::state_history_log_config conf =
      eosio::state_history::partition_config{.retained_dir = retained, .archive_dir = "", .stride = 10};

   eosio::state_history::log_catalog catalog(catalog_dir.path(), conf, t.name,
                                             eosio::state_history::state_history_log::no_non_local_get_block_id_func,
                                             true);
   std::vector<char> payload(64, 'Z');
   catalog.pack_and_write_entry(utils_fixture::id_for(1, 'Z'), utils_fixture::id_for(0, 'Z'),
                                [&](auto& f) { bio::write(f, payload.data(), payload.size()); });

   const auto [begin, end] = catalog.block_range();
   BOOST_REQUIRE_EQUAL(begin, 1u);
   BOOST_REQUIRE_EQUAL(end, 2u);
   //every pre-existing bundle was moved aside, none deleted
   unsigned orphans = 0;
   for(const auto& de : std::filesystem::recursive_directory_iterator(catalog_dir.path()))
      if(de.is_regular_file() && de.path().filename().string().find("-corrupt-") != std::string::npos &&
         de.path().extension() == ".log")
         ++orphans;
   BOOST_REQUIRE_EQUAL(orphans, 3u); //two retained bundles + the old head
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(force_write_skips_block_the_chain_rejects) { try {
   //the cascade's last resort -- writing into a fresh empty log -- can still be rejected when the
   // disagreement is with the chain itself (the non-local block-id lookup), not with any log the
   // catalog holds. force-write must skip such a block and keep running rather than throw.
   utils_fixture t;
   t.add_range(2, 10, 'A', 'A');
   t.close();

   //a non-local lookup that always disagrees stands in for a block whose parent id no log can ever
   // satisfy, so every write tier -- existing head, fresh head, fresh head after orphaning all -- is
   // rejected, exercising the final guarded attempt
   auto always_disagrees = [](block_num_type) -> std::optional<block_id_type> {
      return utils_fixture::id_for(1, 'Z');
   };
   eosio::state_history::log_catalog catalog(t.dir.path(), t.conf, t.name, always_disagrees, true /*force_write*/);

   //block 50 skips over the head's next block (11) so the existing head rejects it outright, and the
   // disagreeing lookup makes every fresh-log retry reject it too; the call must return, not throw
   const std::vector<char> payload(64, 'A');
   BOOST_REQUIRE_NO_THROW(catalog.pack_and_write_entry(
      utils_fixture::id_for(50, 'A'), utils_fixture::id_for(49, 'A'),
      [&](auto& f) { bio::write(f, payload.data(), payload.size()); }));

   //the conflicting block was skipped (nothing serves it) while the original blocks were set aside
   BOOST_REQUIRE(!catalog.get_entry(50));
   BOOST_REQUIRE(std::filesystem::exists(t.dir.path() / (t.name + "-corrupt-1.log")));
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(repaired_logs_reopen_and_continue) { try {
   //after a truncate repair, nodeos must be able to append right where the log now ends
   utils_fixture t;
   t.add_range(2, 30, 'A', 'A');
   t.close();
   t.append_garbage(33);

   const log_utils::repair_report rep = log_utils::repair_log(t.stem(), log_utils::repair_mode::truncate, false, false);
   BOOST_REQUIRE(rep.acted);

   t.open();
   t.add(31, 'A', 'A'); //would throw if the repaired log were inconsistent
   t.close();
   t.check_serves(2, 31);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
