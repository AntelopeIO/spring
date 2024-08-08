#include <eosio/chain/finality/finalizer.hpp>

#include <boost/test/unit_test.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/testing/bls_utils.hpp>
#include <fc/io/cfile.hpp>
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
   for (size_t i = 0; i < count; ++i) {
      res.push_back(FSI{
         .last_vote_range_start = tstamp(i),
         .last_vote             = block_ref{.block_id        = sha256::hash("vote"s + std::to_string(i)),
                                            .timestamp       = tstamp(i * 100 + 3),
                                            .finality_digest = sha256::hash("vote_digest"s + std::to_string(i))},
         .lock                  = block_ref{.block_id        = sha256::hash("lock"s + std::to_string(i)),
                                            .timestamp       = tstamp(i * 100),
                                            .finality_digest = sha256::hash("lock_digest"s + std::to_string(i))}
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
      res.push_back(block_ref{sha256::hash(id_str.c_str()), tstamp(i)});
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

BOOST_AUTO_TEST_SUITE(finalizer_tests)

BOOST_AUTO_TEST_CASE( basic_finalizer_safety_file_io ) try {
   fc::temp_directory tempdir;
   auto safety_file_path = tempdir.path() / "finalizers" / "safety.dat";
   auto proposals { create_proposal_refs(10) };

   fsi_t fsi { .last_vote_range_start = tstamp(0),
               .last_vote = proposals[6],
               .lock = proposals[2] };

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

   fsi_t fsi { .last_vote_range_start = tstamp(0),
               .last_vote = proposals[6],
               .lock = proposals[2] };

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
      BOOST_CHECK_NE(fset.get_fsi(k.pubkey), fsi);
      BOOST_CHECK_EQUAL(fset.get_fsi(k.pubkey), fsi_t());
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


BOOST_AUTO_TEST_CASE( finalizer_safety_file_versioning ) try {
   namespace fs = std::filesystem;

   fs::path test_data_path { UNITTEST_TEST_DATA_DIR };
   auto fsi_reference_dir = test_data_path / "fsi";

   auto create_fsi_reference = [&](my_finalizers_t& fset) {
      std::vector<bls_keys_t> keys = create_keys(3);
      std::vector<fsi_t> fsi = create_random_fsi<fsi_t>(3);

      bls_pub_priv_key_map_t local_finalizers = create_local_finalizers<0, 1, 2>(keys);
      fset.set_keys(local_finalizers);
      set_fsi<decltype(fsi), 0, 1, 2>(fset, keys, fsi);
   };

   auto create_fsi_reference_file = [&](const fs::path& safety_file_path) {
      my_finalizers_t fset{safety_file_path};
      create_fsi_reference(fset);
      fset.save_finalizer_safety_info();
   };

   auto mk_versioned_fsi_file_path = [&](uint32_t v) {
      return fsi_reference_dir / ("safety_v"s + std::to_string(v) + ".dat");
   };

   auto current_version = my_finalizers_t::current_safety_file_version;

   // run this unittest with the option `--save-fsi-ref` to save ref file for the current version.
   // --------------------------------------------------------------------------------------------
   bool save_fsi_reference_file = [](){
      auto argc = boost::unit_test::framework::master_test_suite().argc;
      auto argv = boost::unit_test::framework::master_test_suite().argv;
      return std::find(argv, argv + argc, std::string("--save-fsi-ref")) != (argv + argc);
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
      std::this_thread::sleep_for(std::chrono::milliseconds{10});

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


BOOST_AUTO_TEST_SUITE_END()
