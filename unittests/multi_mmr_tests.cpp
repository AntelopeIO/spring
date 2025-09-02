#include <boost/test/unit_test.hpp>
#include <eosio/chain/multi_mmr.hpp>

using namespace eosio::chain;
using namespace fc;

BOOST_AUTO_TEST_SUITE(multi_mmr_tests)

BOOST_AUTO_TEST_CASE(basic_single) {
   multi_mmr<1> mmr;

   const sha256 a = sha256::hash("a");
   const sha256 b = sha256::hash("b");
   const sha256 c = sha256::hash("c");
   const sha256 d = sha256::hash("d");
   const sha256 e = sha256::hash("e");
   const sha256 f = sha256::hash("f");
   const sha256 g = sha256::hash("g");
   const sha256 h = sha256::hash("h");
   const sha256 i = sha256::hash("i");

   BOOST_CHECK_EQUAL(mmr.root(), sha256());

   mmr.append(a);
   BOOST_CHECK_EQUAL(mmr.root(), a); //p0

   mmr.append(b);
   BOOST_CHECK_EQUAL(mmr.root(), sha256::packhash(a, b)); //p1

   mmr.append(c);
   BOOST_CHECK_EQUAL(mmr.root(), sha256::packhash(
                                   sha256::packhash(a, b),  //p0
                                   c                        //p1
                                 ));

   mmr.append(d);
   BOOST_CHECK_EQUAL(mmr.root(), sha256::packhash(
                                   sha256::packhash(a, b), sha256::packhash(c, d) //p2
                                 ));

   mmr.append(e);
   BOOST_CHECK_EQUAL(mmr.root(), sha256::packhash(
                                    sha256::packhash(sha256::packhash(a, b), sha256::packhash(c, d)), //p2
                                    e                                                                 //p0
                                 ));

   mmr.append(f);
   BOOST_CHECK_EQUAL(mmr.root(), sha256::packhash(
                                    sha256::packhash(sha256::packhash(a, b), sha256::packhash(c, d)), //p2
                                    sha256::packhash(e, f)                                            //p1
                                 ));

   mmr.append(g);
   BOOST_CHECK_EQUAL(mmr.root(), sha256::packhash(
                                    sha256::packhash(
                                       sha256::packhash(sha256::packhash(a, b), sha256::packhash(c, d)), //p2
                                       sha256::packhash(e, f)                                            //p1
                                    ),
                                    g                                                                    //p0
                                 ));

   mmr.append(h);
   BOOST_CHECK_EQUAL(mmr.root(), sha256::packhash(
                                       sha256::packhash(sha256::packhash(a, b), sha256::packhash(c, d)), sha256::packhash(sha256::packhash(e, f), sha256::packhash(g, h)) //p3
                                 ));

   mmr.append(i);
   BOOST_CHECK_EQUAL(mmr.root(), sha256::packhash(
                                    sha256::packhash(
                                       sha256::packhash(sha256::packhash(a, b), sha256::packhash(c, d)), sha256::packhash(sha256::packhash(e, f), sha256::packhash(g, h)) //p3
                                    ),
                                    i                                                                                                                                     //p0
                                 ));
}

BOOST_AUTO_TEST_CASE(single_copy) {
   multi_mmr<1> mmr;

   const sha256 a = sha256::hash("a");
   const sha256 b = sha256::hash("b");
   const sha256 c = sha256::hash("c");
   const sha256 d = sha256::hash("d");

   {
      auto copy = mmr;
      BOOST_CHECK_EQUAL(copy.root(), sha256());
   }
   {
      auto copy = mmr;
      copy.append(a);
      BOOST_CHECK_EQUAL(copy.root(), a);
      BOOST_CHECK_EQUAL(mmr.root(), sha256());
   }
   BOOST_CHECK_EQUAL(mmr.root(), sha256());

   {
      auto copy = mmr;
      copy.append(a);
      copy.append(b);
      mmr.append(c);

      BOOST_CHECK_EQUAL(copy.root(), sha256::packhash(a, b));
      BOOST_CHECK_EQUAL(mmr.root(), c);
   }
   BOOST_CHECK_EQUAL(mmr.root(), c);

   {
      auto copy = mmr;
      auto copy2 = mmr;

      copy.append(a);
      copy2.append(b);
      mmr.append(d);

      BOOST_CHECK_EQUAL(copy.root(), sha256::packhash(c, a));
      BOOST_CHECK_EQUAL(copy2.root(), sha256::packhash(c, b));
      BOOST_CHECK_EQUAL(mmr.root(), sha256::packhash(c, d));
   }
   BOOST_CHECK_EQUAL(mmr.root(), sha256::packhash(c, d));
}

BOOST_AUTO_TEST_SUITE_END()