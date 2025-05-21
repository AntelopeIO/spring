#include <boost/test/unit_test.hpp>

#include <fc/exception/exception.hpp>

BOOST_AUTO_TEST_SUITE(exception)

BOOST_AUTO_TEST_CASE(rethrow) try {
   fc::exception_ptr exp;
   try {
      try {
         FC_ASSERT(false, "test ${i}", ("i", 42));
      } FC_RETHROW_EXCEPTIONS(info, "extra stuff")
   } catch (fc::exception& e) {
      exp = e.dynamic_copy_exception();
   }

   BOOST_TEST(exp->to_detail_string().find("test 42") != std::string::npos);
   BOOST_TEST(exp->to_detail_string().find("extra stuff") != std::string::npos);

   try {
      exp->rethrow();
   } catch (fc::assert_exception& e) {
      // success
   } catch (fc::exception& e) {
      BOOST_FAIL("should be: assert_exception");
   } catch (...) {
      BOOST_FAIL("should be: assert_exception");
   }

} FC_LOG_AND_RETHROW();


BOOST_AUTO_TEST_SUITE_END()