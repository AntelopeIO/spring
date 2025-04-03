#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include <boost/test/unit_test.hpp>
#pragma GCC diagnostic pop

#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/testing/tester.hpp>

#include <fc/exception/exception.hpp>
#include <fc/variant_object.hpp>

#include <contracts.hpp>
#include "eosio_system_tester.hpp"

using namespace eosio_system;

class getpeerkeys_tester : public eosio_system_tester<validating_tester> {
public:
   getpeerkeys_res_t getpeerkeys() {
      try {
         auto trace =
            base_tester::push_action(config::system_account_name, "getpeerkeys"_n, config::system_account_name, mvo());
         auto&             retval = trace->action_traces[0].return_value;
         getpeerkeys_res_t res;

         fc::datastream<const char*> ds(retval.data(), retval.size());
         fc::raw::unpack(ds, res);
         return res;

      } catch (const fc::exception& ex) {
         return {};
      }
   }
};

BOOST_AUTO_TEST_SUITE(getpeerkeys_tests)

BOOST_FIXTURE_TEST_CASE( getpeerkeys_test, getpeerkeys_tester ) { try {
      // auto res = getpeerkeys();                 // call action from tester (as regular, not readonly action)
      auto res = control->get_top_producer_keys(); // call readonly action from controller
      BOOST_REQUIRE(!res.empty());
      BOOST_REQUIRE_EQUAL(res[0].producer_name, "n1"_n); 
      
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
