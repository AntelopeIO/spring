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
   auto check_variant_round_trip = [&](const fc::bitset& bs) {
      fc::mutable_variant_object mu;
      mu("bs", bs);

      fc::bitset bs2;
      fc::from_variant(mu["bs"], bs2);

      BOOST_TEST(bs2 == bs);
   };

   check_variant_round_trip(fc::bitset(""));
   check_variant_round_trip(fc::bitset("0"));
   check_variant_round_trip(fc::bitset("1"));
   check_variant_round_trip(fc::bitset("01"));
   check_variant_round_trip(fc::bitset("0101"));
   check_variant_round_trip(fc::bitset("010100"));
   check_variant_round_trip(fc::bitset("001010100"));
   check_variant_round_trip(fc::bitset("0101010100"));
   check_variant_round_trip(fc::bitset("0110011010100"));
   check_variant_round_trip(fc::bitset("00110011010100"));
   check_variant_round_trip(fc::bitset("0000000001010100"));
   check_variant_round_trip(fc::bitset("01110011010100101"));
   check_variant_round_trip(fc::bitset("0111001101010010111"));
   check_variant_round_trip(fc::bitset("011100110101001011101"));
   check_variant_round_trip(fc::bitset("0111001101010010111011001"));
   check_variant_round_trip(fc::bitset("01110011010100101110110010"));
   check_variant_round_trip(fc::bitset("01110011010100101110110010010"));
   check_variant_round_trip(fc::bitset("0111001101010010111011001001011"));
   check_variant_round_trip(fc::bitset("011100110101001011101100100100110"));
   check_variant_round_trip(fc::bitset("01110011010100101110110010010011000"));
   check_variant_round_trip(fc::bitset("01110011010100101001001001100000010000110"));
   check_variant_round_trip(fc::bitset("0111001101010010111011001001001100000000000000110"));
   check_variant_round_trip(fc::bitset("01110011010100101111001101100100100111111111111111111001"));
}

BOOST_AUTO_TEST_SUITE_END()
