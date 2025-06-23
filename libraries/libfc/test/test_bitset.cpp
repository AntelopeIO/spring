#include <fc/exception/exception.hpp>
#include <fc/bitset.hpp>

#include <boost/test/unit_test.hpp>

using namespace fc;

BOOST_AUTO_TEST_SUITE(bitset_test_suite)

BOOST_AUTO_TEST_CASE(bitset_test)
{
   auto test_bitset = [&](const std::string_view sv) {
      const fc::bitset bs(sv);
      const auto sz = bs.size();

      // check constructors and `from_string`
      // -----------------------------------
      {
         BOOST_TEST(sz == sv.size());
         for (size_t i = 0; i < sz; ++i)
            BOOST_TEST(bs[sz - i - 1] == (sv[i] == '1'));

         fc::bitset bs2(sz);
         BOOST_TEST(bs2.size() == sz);
         for (size_t i = 0; i < sz; ++i)
            BOOST_TEST(bs2[i] == false);
      }

      // check `to_string`
      // -----------------
      BOOST_TEST(bs.to_string() == sv);

      // check copy_constructor and resize
      // ---------------------------------
      {
         fc::bitset bs2(bs);
         BOOST_TEST(bs2 == bs);
         bs2.resize(sz + 10);
         BOOST_TEST(bs2.size() == sz + 10);
         for (size_t i = sz; i < sz + 10; ++i)
            BOOST_TEST(bs2[i] == false);

         if (sz >= 2) {
            bs2 = bs;
            bs2.resize(sz - 2);
            BOOST_TEST(bs2.size() == sz - 2);
            for (size_t i = 0; i < sz - 2; ++i)
               BOOST_TEST(bs2[i] == bs[i]);
         }
      }

      // check bit operations set / clear / none / all
      // ---------------------------------------------
      {
         fc::bitset bs2(bs);
         BOOST_TEST(bs2 == bs);
         for (size_t i = 0; i < sz; ++i) {
            if (bs2[i])
               bs2.clear(i);
         }
         BOOST_TEST(bs2.none());

         fc::bitset bs3(bs);
         bs3.zero_all_bits();
         BOOST_TEST(bs3 == bs2);

         bs2 = bs;
         BOOST_TEST(bs2 == bs);
         for (size_t i = 0; i < sz; ++i) {
            if (!bs2[i])
               bs2.set(i);
         }
         BOOST_TEST(bs2.all());
      }


      // check bit operations set / clear / test / flip / operator|=
      // -----------------------------------------------------------
      {
         // flip all bits using set / clear
         fc::bitset bs2(bs);
         BOOST_TEST(bs2 == bs);
         for (size_t i = 0; i < sz; ++i) {
            if (bs2.test(i)) {
               bs2.clear(i);
               BOOST_TEST(!bs2.test(i));
            } else {
               bs2.set(i);
               BOOST_TEST(bs2.test(i));
            }
         }
         fc::bitset bs3(bs);
         bs3.flip();
         BOOST_TEST(bs2 == bs3);

         // check operator|=(), bs2 and bs3 are flipped versions of bs at this point
         bs2 |= bs;
         BOOST_TEST(bs2.all());

         fc::bitset tmp = bs3;
         tmp |= bs3; // should not change anything, b3 still bs flipped
         tmp.flip();
         BOOST_TEST(bs3 == bs);


         // flip all bits using flip
         bs2 = bs;
         BOOST_TEST(bs2 == bs);
         for (size_t i = 0; i < sz; ++i) {
            if (bs2.test(i)) {
               bs2.flip(i);
               BOOST_TEST(!bs2.test(i));
            } else {
               bs2.flip(i);
               BOOST_TEST(bs2.test(i));
            }
         }
         bs3 = bs;
         bs3.flip();
         BOOST_TEST(bs2 == bs3);

         bs3.zero_all_bits();
         BOOST_TEST(bs3.none());
      }

      // check `to_string . from_string = identity`
      // ------------------------------------------
      {
         BOOST_TEST(bitset::from_string(bs.to_string()) == bs);
      }

      // check the stream versions
      {
         std::stringstream ss;
         ss << bs;
         BOOST_TEST(ss.str() == sv);

         ss.seekg(0);
         fc::bitset bs2;
         ss >> bs2;
         BOOST_TEST(bs2 == bs);
      }
   };

   test_bitset("");
   test_bitset("0");
   test_bitset("1");
   test_bitset("01");
   test_bitset("0101");
   test_bitset("010100");
   test_bitset("001010100");
   test_bitset("0101010100");
   test_bitset("0110011010100");
   test_bitset("00110011010100");
   test_bitset("0000000001010100");
   test_bitset("01110011010100101");
   test_bitset("0111001101010010111");
   test_bitset("011100110101001011101");
   test_bitset("0111001101010010111011001");
   test_bitset("01110011010100101110110010");
   test_bitset("01110011010100101110110010010");
   test_bitset("0111001101010010111011001001011");
   test_bitset("011100110101001011101100100100110");
   test_bitset("01110011010100101110110010010011000");
   test_bitset("01110011010100101001001001100000010000110");
   test_bitset("0111001101010010111011001001001100000000000000110");
   test_bitset("01110011010100101111001101100100100111111111111111111001");
}

BOOST_AUTO_TEST_SUITE_END()