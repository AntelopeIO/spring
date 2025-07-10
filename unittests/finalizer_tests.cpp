#include <eosio/chain/finalizer.hpp>

#include <boost/test/unit_test.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/testing/bls_utils.hpp>
#include <fc/io/cfile.hpp>
#include <fc/io/fstream.hpp>
#include <test-data.hpp>

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;
using namespace std::string_literals;

using tstamp  = block_timestamp_type;
using fsi_t   = finalizer_safety_information;
using fsi_map = my_finalizers_t::fsi_map;

struct bls_keys_t {
   bls_private_key privkey;
   bls_public_key  pubkey;
   std::string     privkey_str;
   std::string     pubkey_str;

   bls_keys_t(name n) {
      bls_signature pop;
      std::tie(privkey, pubkey, pop)    = eosio::testing::get_bls_key(n);
      std::tie(privkey_str, pubkey_str) = std::pair{ privkey.to_string(), pubkey.to_string() };
   }
};

// -------------------------------------------------------------------------------------
//                       **DO NOT MODIFY**
//                       -----------------
// Do not modify the existing data provided by this function (additions are OK) because
// it was used for generating the reference files in `test-data/fsi`, and is used
// to generate the new file used in the test `finalizer_safety_file_versioning`
// -------------------------------------------------------------------------------------
template<class FSI>
std::vector<FSI> create_random_fsi(size_t count) {
   std::vector<FSI> res;
   res.reserve(count);
   // generation numbers in `block_ref` constructor have to be 0 as they are not saved in the fsi file,
   // but compared to loaded ones which get the default values of 0.
   for (size_t i = 0; i < count; ++i) {
      res.push_back(FSI{
         .last_vote             = block_ref{sha256::hash("vote"s + std::to_string(i)),
                                            tstamp(i * 100 + 3),
                                            sha256::hash("vote_digest"s + std::to_string(i)), 0, 0},
         .lock                  = block_ref{sha256::hash("lock"s + std::to_string(i)),
                                            tstamp(i * 100),
                                            sha256::hash("lock_digest"s + std::to_string(i)), 0, 0},
         .other_branch_latest_time = block_timestamp_type{}
      });
      if (i)
         assert(res.back() != res[0]);
   }
   return res;
}

std::vector<block_ref> create_proposal_refs(size_t count) {
   std::vector<block_ref> res;
   res.reserve(count);
   for (size_t i=0; i<count; ++i) {
      std::string id_str {"vote"};
      id_str += std::to_string(i);
      auto id = sha256::hash(id_str.c_str());
      // we use bogus generation numbers in `block_ref` constructor, but these are unused in the test
      res.push_back(block_ref{id, tstamp(i), id, 0, 0}); // generation numbers both 0 as not saved in fsi file
   }
   return res;
}

std::vector<bls_keys_t> create_keys(size_t count) {
   std::vector<bls_keys_t> res;
   res.reserve(count);
   for (size_t i=0; i<count; ++i) {
      std::string s("alice");
      s.append(3, 'a'+i);
      res.push_back(bls_keys_t(name(s)));
      if (i)
         assert(res.back().privkey != res[0].privkey);
   }
   return res;
}

template <size_t... I>
bls_pub_priv_key_map_t create_local_finalizers(const std::vector<bls_keys_t>& keys) {
   bls_pub_priv_key_map_t res;
   ((res[keys[I].pubkey_str] = keys[I].privkey_str), ...);
   return res;
}

template <class FSI_VEC, size_t... I>
void set_fsi(my_finalizers_t& fset, const std::vector<bls_keys_t>& keys, const FSI_VEC& fsi) {
   ((fset.set_fsi(keys[I].pubkey, fsi[I])), ...);
}

// sleep for n periods of the file clock
// --------------------------------------
void sleep_for_n_file_clock_periods(uint32_t n) {
   using file_clock = std::chrono::file_clock;
   auto n_periods = file_clock::duration(n);
   auto sleep_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(n_periods);

   std::this_thread::sleep_for(sleep_duration);
}

BOOST_AUTO_TEST_SUITE(finalizer_tests)

BOOST_AUTO_TEST_CASE( basic_finalizer_safety_file_io ) try {
   fc::temp_directory tempdir;
   auto safety_file_path = tempdir.path() / "finalizers" / "safety.dat";
   auto proposals { create_proposal_refs(10) };

   fsi_t fsi { .last_vote = proposals[6],
               .lock = proposals[2],
               .other_branch_latest_time = block_timestamp_type{} };

   bls_keys_t k("alice"_n);
   bls_pub_priv_key_map_t local_finalizers = { { k.pubkey_str, k.privkey_str } };

   {
      my_finalizers_t fset{safety_file_path};
      fset.set_keys(local_finalizers);

      fset.set_fsi(k.pubkey, fsi);
      fset.save_finalizer_safety_info();

      // at this point we have saved the finalizer safety file
      // so destroy the my_finalizers_t object
   }

   {
      my_finalizers_t fset{safety_file_path};
      fset.set_keys(local_finalizers); // that's when the finalizer safety file is read

      // make sure the safety info for our finalizer that we saved above is restored correctly
      BOOST_CHECK_EQUAL(fset.get_fsi(k.pubkey), fsi);
   }

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE( corrupt_finalizer_safety_file ) try {
   fc::temp_directory tempdir;
   auto safety_file_path = tempdir.path() / "finalizers" / "safety.dat";
   auto proposals { create_proposal_refs(10) };

   fsi_t fsi { .last_vote = proposals[6],
               .lock = proposals[2],
               .other_branch_latest_time = block_timestamp_type{} };

   bls_keys_t k("alice"_n);
   bls_pub_priv_key_map_t local_finalizers = { { k.pubkey_str, k.privkey_str } };

   {
      my_finalizers_t fset{safety_file_path};
      fset.set_keys(local_finalizers);

      fset.set_fsi(k.pubkey, fsi);
      fset.save_finalizer_safety_info();

      // at this point we have saved the finalizer safety file
      // corrupt it, so we can check that we throw an exception when reading it later.

      fc::datastream<fc::cfile> f;
      f.set_file_path(safety_file_path);
      f.open(fc::cfile::truncate_rw_mode);
      size_t junk_data = 0xf0f0f0f0f0f0f0f0ull;
      fc::raw::pack(f, junk_data);
   }

   {
      my_finalizers_t fset{safety_file_path};
      BOOST_REQUIRE_THROW(fset.set_keys(local_finalizers),     // that's when the finalizer safety file is read
                          finalizer_safety_exception);

      // make sure the safety info for our finalizer that we saved above is restored correctly
      BOOST_CHECK(!fset.contains(k.pubkey));
   }

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE( finalizer_safety_file_io ) try {
   fc::temp_directory tempdir;
   auto safety_file_path = tempdir.path() / "finalizers" / "safety.dat";

   std::vector<fsi_t> fsi = create_random_fsi<fsi_t>(10);
   std::vector<bls_keys_t> keys = create_keys(10);

   {
      my_finalizers_t fset{safety_file_path};
      bls_pub_priv_key_map_t local_finalizers = create_local_finalizers<1, 3, 5, 6>(keys);
      fset.set_keys(local_finalizers);

      set_fsi<decltype(fsi), 1, 3, 5, 6>(fset, keys, fsi);
      fset.save_finalizer_safety_info();

      // at this point we have saved the finalizer safety file, containing a specific fsi for finalizers <1, 3, 5, 6>
      // so destroy the my_finalizers_t object
   }

   {
      my_finalizers_t fset{safety_file_path};
      bls_pub_priv_key_map_t local_finalizers = create_local_finalizers<3>(keys);
      fset.set_keys(local_finalizers);

      // make sure the safety info for our finalizer that we saved above is restored correctly
      BOOST_CHECK_EQUAL(fset.get_fsi(keys[3].pubkey), fsi[3]);

      // OK, simulate a couple rounds of voting
      fset.set_fsi(keys[3].pubkey, fsi[4]);
      fset.save_finalizer_safety_info();

      // now finalizer 3 should have fsi[4] saved
   }

   {
      my_finalizers_t fset{safety_file_path};
      bls_pub_priv_key_map_t local_finalizers = create_local_finalizers<3>(keys);
      fset.set_keys(local_finalizers);

      // make sure the safety info for our finalizer that we saved above is restored correctly
      BOOST_CHECK_EQUAL(fset.get_fsi(keys[3].pubkey), fsi[4]);
   }

   // even though we didn't activate finalizers 1, 5, or 6 in the prior test, and we wrote the safety file,
   // make sure we have not lost the fsi that was set originally for these finalizers.
   {
      my_finalizers_t fset{safety_file_path};
      bls_pub_priv_key_map_t local_finalizers = create_local_finalizers<1, 5, 6>(keys);
      fset.set_keys(local_finalizers);

      // make sure the safety info for our previously inactive finalizer was preserved
      BOOST_CHECK_EQUAL(fset.get_fsi(keys[1].pubkey), fsi[1]);
      BOOST_CHECK_EQUAL(fset.get_fsi(keys[5].pubkey), fsi[5]);
      BOOST_CHECK_EQUAL(fset.get_fsi(keys[6].pubkey), fsi[6]);
   }

} FC_LOG_AND_RETHROW()

namespace fs = std::filesystem;

void create_fsi_reference(my_finalizers_t& fset) {
   std::vector<bls_keys_t> keys = create_keys(3);
   std::vector<fsi_t> fsi = create_random_fsi<fsi_t>(3);

   bls_pub_priv_key_map_t local_finalizers = create_local_finalizers<0, 1, 2>(keys);
   fset.set_keys(local_finalizers);
   set_fsi<decltype(fsi), 0, 1, 2>(fset, keys, fsi);
}

void create_fsi_reference_file(const fs::path& safety_file_path) {
   my_finalizers_t fset{safety_file_path};
   create_fsi_reference(fset);
   fset.save_finalizer_safety_info();
}

fs::path mk_versioned_fsi_file_path(uint32_t v) {
   fs::path test_data_path { UNITTEST_TEST_DATA_DIR };
   auto fsi_reference_dir = test_data_path / "fsi";

   return fsi_reference_dir / ("safety_v"s + std::to_string(v) + ".dat");
}

std::string read_file(const fs::path& path) {
   std::string res;
   fc::read_file_contents(path, res);
   return res;
}

BOOST_AUTO_TEST_CASE( finalizer_safety_file_versioning ) try {
   auto current_version = my_finalizers_t::current_safety_file_version;

   // run this unittest with the option `-- --save-fsi-ref` to save ref file for the current version.
   // -----------------------------------------------------------------------------------------------
   bool save_fsi_reference_file = [](){
      auto argc = boost::unit_test::framework::master_test_suite().argc;
      auto argv = boost::unit_test::framework::master_test_suite().argv;
      return std::any_of(argv, argv + argc, [&](const std::string &a){ return a == "--save-fsi-ref";} );
   }();

   if (save_fsi_reference_file)
      create_fsi_reference_file(mk_versioned_fsi_file_path(current_version));

   auto load_fsi_map = [&](const fs::path& safety_file_path, bool save_after_load) {
      BOOST_REQUIRE(fs::exists(safety_file_path));
      my_finalizers_t fset{safety_file_path};
      auto map = fset.load_finalizer_safety_info();
      if (save_after_load) {
         bls_pub_priv_key_map_t local_finalizers = create_local_finalizers<0, 1, 2>(create_keys(3));
         fset.set_keys(local_finalizers);   // need to call set_keys otherwise inactive keys not saved.
         fset.save_finalizer_safety_info();

      }
      return map;
   };

   // Make sure we can read previous versions of the safety file correctly.
   // --------------------------------------------------------------------
   fc::temp_directory tempdir;

   for (size_t i=0; i<current_version; ++i) {
      auto ref_path = mk_versioned_fsi_file_path(i);
      auto copy_path = tempdir.path() / ref_path.filename();
      fs::copy_file(ref_path, copy_path, fs::copy_options::none);

      sleep_for_n_file_clock_periods(2);

      // first load the reference file in the old format, and then save it in the new version format
      // -------------------------------------------------------------------------------------------
      auto last_write = fs::last_write_time(copy_path);
      auto last_size  = fs::file_size(copy_path);
      auto fsi_map_vi = load_fsi_map(copy_path, true);

      BOOST_REQUIRE(fs::last_write_time(copy_path) > last_write); // just a sanity check.
      BOOST_REQUIRE_NE(fs::file_size(copy_path), last_size);      // we expect the size to be different if the format changes

      // then load it again as the new version
      auto fsi_map_vn = load_fsi_map(copy_path, false);

      BOOST_REQUIRE(fsi_map_vi == fsi_map_vn);
   }

} FC_LOG_AND_RETHROW()

// Verify that we have not changed the fsi file serialiization
// ------------------------------------------------------------
BOOST_AUTO_TEST_CASE( finalizer_safety_file_serialization_unchanged ) try {
   auto current_version = my_finalizers_t::current_safety_file_version;
   auto ref_path = mk_versioned_fsi_file_path(current_version);  // the saved file for current_version

   fc::temp_directory tempdir;
   auto tmp_path = tempdir.path() / "new_safety.dat";
   create_fsi_reference_file(tmp_path);                          // save a new file in tmp_path

   BOOST_REQUIRE(read_file(ref_path) == read_file(tmp_path));

} FC_LOG_AND_RETHROW()


// Verify that the current version of safety.dat file committed to the repo can be loaded on
// nodeos startup (it is not saved until we actually vote, and voting would change the fsi).
// -----------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE( finalizer_safety_file_serialization_io ) try {
   fc::temp_directory tempdir;
   auto [cfg, genesis_state] = tester::default_config(tempdir);

   fs::path tmp_path = cfg.finalizers_dir / config::safety_filename;

   auto current_version = my_finalizers_t::current_safety_file_version;
   fs::path ref_path = mk_versioned_fsi_file_path(current_version);  // the saved file for current_version

   tester t( tempdir, true );

   fs::create_directory(cfg.finalizers_dir);
   fs::copy_file(ref_path, tmp_path, fs::copy_options::none);
   auto initial_time = fs::last_write_time(tmp_path);

   sleep_for_n_file_clock_periods(2);

   // set finalizer, so that the file is overwritten. set the last one so that order is unchanged.
   std::vector<bls_keys_t> keys = create_keys(3);
   bls_pub_priv_key_map_t local_finalizer_keys;
   local_finalizer_keys[keys.back().pubkey_str] = keys.back().privkey_str;
   t.control->set_node_finalizer_keys(local_finalizer_keys);

   // Since we didn't vote, the file time should not have changed.
   auto last_time = fs::last_write_time(tmp_path);
   BOOST_REQUIRE(last_time == initial_time);

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_SUITE_END()
