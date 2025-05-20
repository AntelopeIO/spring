#include <fc/exception/exception.hpp>
#include <fc/bitset.hpp>
#include <fc/io/raw.hpp>

#include <boost/test/unit_test.hpp>

using namespace fc;

struct A {
   int                        x;
   const float                y;
   const std::optional<std::string> z;

   bool operator==(const A&) const = default;
};
FC_REFLECT(A, (x)(y)(z))

BOOST_AUTO_TEST_SUITE(raw_test_suite)


BOOST_AUTO_TEST_CASE(bitset_test)
{
   fc::bitset bs1 { fc::bitset("00011110") }; // bit set size 8
   
   char buff[32];
   datastream<char*> ds(buff, sizeof(buff));

   fc::raw::pack( ds, bs1 );

   fc::bitset bs2;
   bs2.resize(8);
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

BOOST_AUTO_TEST_CASE(bitset_large_test)
{
   fc::bitset bs1;
   bs1.resize(12345);

   bs1.set(42);
   bs1.set(23);
   bs1.set(12000);

   auto packed = fc::raw::pack(bs1);
   auto unpacked = fc::raw::unpack<fc::bitset>(packed);

   BOOST_TEST(unpacked[42]);
   BOOST_TEST(unpacked[23]);
   BOOST_TEST(unpacked[12000]);
   unpacked.flip(42);
   unpacked.flip(23);
   unpacked.flip(12000);
   BOOST_TEST(unpacked.none());
}

BOOST_AUTO_TEST_CASE(bitset_pack_unpack)
{
   std::vector<char> serialized; // to accumulate serialization bits from all calls to `check_pack_unpack`

   auto check_pack_unpack = [&](const fc::bitset& bs) {
      auto bytes = fc::raw::pack(bs);
      serialized.insert(serialized.end(), bytes.begin(), bytes.end());
      auto bs2 = fc::raw::unpack<fc::bitset>(bytes);
      BOOST_TEST(bs2 == bs);
   };

   check_pack_unpack(fc::bitset(""));
   check_pack_unpack(fc::bitset("0"));
   check_pack_unpack(fc::bitset("1"));
   check_pack_unpack(fc::bitset("01"));
   check_pack_unpack(fc::bitset("0101"));
   check_pack_unpack(fc::bitset("010100"));
   check_pack_unpack(fc::bitset("001010100"));
   check_pack_unpack(fc::bitset("0101010100"));
   check_pack_unpack(fc::bitset("0110011010100"));
   check_pack_unpack(fc::bitset("00110011010100"));
   check_pack_unpack(fc::bitset("0000000001010100"));
   check_pack_unpack(fc::bitset("01110011010100101"));
   check_pack_unpack(fc::bitset("0111001101010010111"));
   check_pack_unpack(fc::bitset("011100110101001011101"));
   check_pack_unpack(fc::bitset("0111001101010010111011001"));
   check_pack_unpack(fc::bitset("01110011010100101110110010"));
   check_pack_unpack(fc::bitset("01110011010100101110110010010"));
   check_pack_unpack(fc::bitset("0111001101010010111011001001011"));
   check_pack_unpack(fc::bitset("011100110101001011101100100100110"));
   check_pack_unpack(fc::bitset("01110011010100101110110010010011000"));
   check_pack_unpack(fc::bitset("01110011010100101001001001100000010000110"));
   check_pack_unpack(fc::bitset("0111001101010010111011001001001100000000000000110"));
   check_pack_unpack(fc::bitset("01110011010100101111001101100100100111111111111111111001"));

   // verify that expected serialization doesn't change. bits generated with spring 1.2.
   std::string ser_bits{"00010001010201040506140954000a54010dd40c0ed40c10540011a5e60013979a03155d6a0e19d9a5e6001ab24bcd"
                        "011d925d6a0e1f4b76a9392126d9a5e600239864979a032986c024a5e60031060026d9a5e60038f9ff9f64f35273"};
   std::vector<char> expected_serialization(ser_bits.size() / 2);
   fc::from_hex(ser_bits, expected_serialization.data(), expected_serialization.size());

   BOOST_TEST(serialized == expected_serialization);
}

BOOST_AUTO_TEST_CASE(bitset_small_test)
{
   fc::bitset bs1;
   bs1.resize(21);

   bs1.set(2);
   bs1.set(7);

   auto packed = fc::raw::pack(bs1);
   auto unpacked = fc::raw::unpack<fc::bitset>(packed);

   BOOST_TEST(unpacked[2]);
   BOOST_TEST(unpacked[7]);
   unpacked.flip(2);
   unpacked.flip(7);
   BOOST_TEST(unpacked.none());
}

BOOST_AUTO_TEST_CASE(struct_serialization) {
   char buff[512];
   datastream<char*> ds(buff, sizeof(buff));

   A a{2, 2.2, "abc"};
   fc::raw::pack(ds, a);

   A a2{0, 0};
   ds.seekp(0);
   fc::raw::unpack(ds, a2);
   bool same = a == a2;
   BOOST_TEST(same);

}

// Verify std::optional is unpacked correctly, especially an empty optional will always
// be unpacked to an empty optional even the target is not empty
BOOST_AUTO_TEST_CASE(unpacking_optional) {
   // source is empty
   char buff[8];
   datastream<char*> ds(buff, sizeof(buff));
   std::optional<uint32_t> s;  // no value
   fc::raw::pack(ds, s);

   {  // target has value. This test used to fail.
      std::optional<uint32_t> t = 10;
      ds.seekp(0);
      fc::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }

   {  // target reused for multiple unpackings. This test used to fail.
      char buff[8];
      datastream<char*> ds1(buff, sizeof(buff));
      std::optional<uint32_t> s1 = 15;
      fc::raw::pack(ds1, s1);

      std::optional<uint32_t> t;  // target is empty initially

      // Unpacking to t the first time so t has value
      ds1.seekp(0);
      fc::raw::unpack(ds1, t);
      BOOST_TEST((s1 == t));

      // Unpacking to t the second time. Afterwards, t does not have value.
      ds.seekp(0);
      fc::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }

   { // target is empty.
      std::optional<uint32_t> t;
      ds.seekp(0);
      fc::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }

   // Source has value
   s = 5;
   ds.seekp(0);
   fc::raw::pack(ds, s);

   {  // target has value.
      std::optional<uint32_t> t = 10;
      ds.seekp(0);
      fc::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }

   { // target is empty.
      std::optional<uint32_t> t;
      ds.seekp(0);
      fc::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }
}

// Verify std::shared_ptr is unpacked correctly, especially a null shared_ptr will always
// be unpacked to a null shared_ptr even if the target was not null.
BOOST_AUTO_TEST_CASE(packing_shared_ptr) {
   // source is null
   char buff[8];
   datastream<char*> ds(buff, sizeof(buff));
   std::shared_ptr<uint32_t> s;  // null_ptr
   fc::raw::pack(ds, s);

   {  // target has value. This test used to fail.
      std::shared_ptr<uint32_t> t = std::make_shared<uint32_t>(10);
      ds.seekp(0);
      fc::raw::unpack(ds, t);
      BOOST_TEST(!t);
   }

   {  // target is null.
      std::shared_ptr<uint32_t> t;
      ds.seekp(0);
      fc::raw::unpack(ds, t);
      BOOST_TEST(!t);
   }

   // source is not null
   ds.seekp(0);
   s = std::make_shared<uint32_t>(50);
   fc::raw::pack(ds, s);

   {  // target has value.
      std::shared_ptr<uint32_t> t = std::make_shared<uint32_t>(10);
      ds.seekp(0);
      fc::raw::unpack(ds, t);
      BOOST_TEST((*s == *t));
   }

   {  // target is null.
      std::shared_ptr<uint32_t> t;
      ds.seekp(0);
      fc::raw::unpack(ds, t);
      BOOST_TEST((*s == *t));
   }
}

// Verify std::set is unpacked correctly, especially an empty set will always
// be unpacked to an empty set even if the target was not empty.
BOOST_AUTO_TEST_CASE(packing_set) {
   //==== source empty
   char buff[16];
   datastream<char*> ds(buff, sizeof(buff));
   std::set<uint32_t> s;  // empty
   fc::raw::pack(ds, s);

   {  // target is not empty. This test used to fail.
      std::set<uint32_t> t {10};
      ds.seekp(0);
      fc::raw::unpack(ds, t);
      BOOST_TEST(t.empty());
   }

   {  // target is empty.
      std::set<uint32_t> t;
      ds.seekp(0);
      fc::raw::unpack(ds, t);
      BOOST_TEST(t.empty());
   }

   // Source has values
   ds.seekp(0);
   s = {1, 2};
   fc::raw::pack(ds, s);

   {  // target is not empty. This test used to fail (ending up with {1, 2, 3}).
      std::set<uint32_t> t {3};
      ds.seekp(0);
      fc::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }

   {  // target is empty.
      std::set<uint32_t> t;
      ds.seekp(0);
      fc::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }
}

// Verify std::list is unpacked correctly, especially an empty list will always
// be unpacked to an empty list even if the target was not empty.
BOOST_AUTO_TEST_CASE(packing_list) {
   //==== source empty
   char buff[16];
   datastream<char*> ds(buff, sizeof(buff));
   std::list<uint32_t> s;  // empty
   fc::raw::pack(ds, s);

   {  // target has value. This test used to fail.
      std::list<uint32_t> t {10};
      ds.seekp(0);
      fc::raw::unpack(ds, t);
      BOOST_TEST((t.size() == 0));
   }

   {  // target is empty.
      std::list<uint32_t> t;
      ds.seekp(0);
      fc::raw::unpack(ds, t);
      BOOST_TEST((t.size() == 0));
   }

   // Source has values
   ds.seekp(0);
   s = {1, 2};
   fc::raw::pack(ds, s);

   {  // target is not empty. This test used to fail (ending up with {1, 2, 3}).
      std::list<uint32_t> t {3};
      ds.seekp(0);
      fc::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }

   {  // target is empty.
      std::list<uint32_t> t;
      ds.seekp(0);
      fc::raw::unpack(ds, t);
      BOOST_TEST((s == t));
   }
}

BOOST_AUTO_TEST_SUITE_END()
