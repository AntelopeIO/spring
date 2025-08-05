#include <boost/test/unit_test.hpp>

#include <fc/crypto/hex.hpp>
#include <fc/crypto/sha3.hpp>
#include <fc/crypto/xxh3.hpp>
#include <fc/utility.hpp>

using namespace fc;

BOOST_AUTO_TEST_SUITE(hash_functions)
BOOST_AUTO_TEST_CASE(sha3) try {

   using test_sha3 = std::tuple<std::string, std::string>;
   const std::vector<test_sha3> tests {
      //test
      {
         "",
         "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a",
      },

      //test
      {
         "abc",
         "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532",
      },

      //test
      {
         "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "41c0dba2a9d6240849100376a8235e2c82e1b9998a999e21db32dd97496d3376",
      }
   };

   for(const auto& test : tests) {
      BOOST_CHECK_EQUAL(fc::sha3::hash(std::get<0>(test), true).str(), std::get<1>(test));
   }

} FC_LOG_AND_RETHROW();


BOOST_AUTO_TEST_CASE(keccak256) try {

   using test_keccak256 = std::tuple<std::string, std::string>;
   const std::vector<test_keccak256> tests {
      //test
      {
         "",
         "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470",
      },

      //test
      {
         "abc",
         "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45",
      },

      //test
      {
         "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "45d3b367a6904e6e8d502ee04999a7c27647f91fa845d456525fd352ae3d7371",
      }
   };

   for(const auto& test : tests) {
      BOOST_CHECK_EQUAL(fc::sha3::hash(std::get<0>(test), false).str(), std::get<1>(test));
   }

} FC_LOG_AND_RETHROW();

#define XSUM_U8 uint8_t
#define XSUM_U32 uint32_t
#define XSUM_U64 uint64_t
typedef struct {
   uint64_t low64;
   uint64_t high64;
} XXH128_hash_t;
#include <xxHash/tests/sanity_test_vectors.h>

BOOST_AUTO_TEST_CASE(xhh3) try {
   //reconstruct test vector input from xxhash library's tests
   char sanity_buffer[4096+64+1];

   uint64_t byte_gen = 2654435761U;
   for(unsigned i = 0; i < sizeof(sanity_buffer); ++i) {
      sanity_buffer[i] = (char)(byte_gen>>56);
      byte_gen *= 11400714785074694797ULL;
   }

   for(const XSUM_testdata64_t& t : XSUM_XXH3_testdata) {
      //non-zero seed not exposed by fc's xxh3 interface
      if(t.seed != 0)
         continue;

      BOOST_CHECK_EQUAL(t.Nresult, xxh3::hash(sanity_buffer, t.len)._hash);
   }

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()