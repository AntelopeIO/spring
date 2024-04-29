#include <fc/exception/exception.hpp>
#include <fc/bitutil.hpp>
#include <fc/io/raw.hpp>

#include <boost/test/unit_test.hpp>

using namespace fc;

BOOST_AUTO_TEST_SUITE(raw_test_suite)


BOOST_AUTO_TEST_CASE(dynamic_bitset_test)
{
   constexpr uint8_t bits = 0b00011110;
   fc::dynamic_bitset bs1(8, bits); // bit set size 8
   
   char buff[32];
   datastream<char*> ds(buff, sizeof(buff));

   fc::raw::pack( ds, bs1 );

   fc::dynamic_bitset bs2(8);
   ds.seekp(0);
   fc::raw::unpack( ds, bs2 );

   // 0b00011110
   BOOST_CHECK(!bs2.test(0));
   BOOST_CHECK(bs2.test(1));
   BOOST_CHECK(bs2.test(2));
   BOOST_CHECK(bs2.test(2));
   BOOST_CHECK(bs2.test(3));
   BOOST_CHECK(bs2.test(4));
   BOOST_CHECK(!bs2.test(5));
   BOOST_CHECK(!bs2.test(6));
   BOOST_CHECK(!bs2.test(7));
}

BOOST_AUTO_TEST_CASE(dynamic_bitset_large_test)
{
   fc::dynamic_bitset bs1;
   bs1.resize(12345);

   bs1.set(42);
   bs1.set(23);
   bs1.set(12000);

   auto packed = fc::raw::pack(bs1);
   auto unpacked = fc::raw::unpack<fc::dynamic_bitset>(packed);

   BOOST_TEST(unpacked.at(42));
   BOOST_TEST(unpacked.at(23));
   BOOST_TEST(unpacked.at(12000));
   unpacked.flip(42);
   unpacked.flip(23);
   unpacked.flip(12000);
   BOOST_TEST(unpacked.none());
}

BOOST_AUTO_TEST_CASE(dynamic_bitset_small_test)
{
   fc::dynamic_bitset bs1;
   bs1.resize(21);

   bs1.set(2);
   bs1.set(7);

   auto packed = fc::raw::pack(bs1);
   auto unpacked = fc::raw::unpack<fc::dynamic_bitset>(packed);

   BOOST_TEST(unpacked.at(2));
   BOOST_TEST(unpacked.at(7));
   unpacked.flip(2);
   unpacked.flip(7);
   BOOST_TEST(unpacked.none());
}

BOOST_AUTO_TEST_SUITE_END()
