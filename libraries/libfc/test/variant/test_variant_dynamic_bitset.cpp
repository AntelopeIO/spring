#include <fc/variant_dynamic_bitset.hpp>
#include <fc/variant_object.hpp>
#include <fc/exception/exception.hpp>

#include <boost/test/unit_test.hpp>

#include <string>

using namespace fc;
using std::string;

BOOST_AUTO_TEST_SUITE(dynamic_bitset_test_suite)

BOOST_AUTO_TEST_CASE(dynamic_bitset_test)
{
   constexpr uint8_t bits = 0b0000000001010100;
   fc::dynamic_bitset bs(16, bits); // 2 blocks of uint8_t

   fc::mutable_variant_object mu;
   mu("bs", bs);

   fc::dynamic_bitset bs2;
   fc::from_variant(mu["bs"], bs2);

   BOOST_TEST(bs2 == bs);
}

BOOST_AUTO_TEST_SUITE_END()
