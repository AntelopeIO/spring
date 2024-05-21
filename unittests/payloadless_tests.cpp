#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include <boost/test/unit_test.hpp>
#pragma GCC diagnostic pop

#include <eosio/testing/tester.hpp>
#include <eosio/chain/abi_serializer.hpp>

#include <fc/variant_object.hpp>
#include <fc/io/json.hpp>

#include <boost/algorithm/string/predicate.hpp>

#include <test_contracts.hpp>

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;
using namespace fc;

template<typename T>
class payloadless_tester : public T {

};

using payloadless_testers = boost::mpl::list<payloadless_tester<legacy_validating_tester>,
                                             payloadless_tester<savanna_validating_tester>>;

BOOST_AUTO_TEST_SUITE(payloadless_tests)

BOOST_AUTO_TEST_CASE_TEMPLATE( test_doit, T, payloadless_testers ) {
   T chain;
   
   chain.create_accounts( {"payloadless"_n} );
   chain.set_code( "payloadless"_n, test_contracts::payloadless_wasm() );
   chain.set_abi( "payloadless"_n, test_contracts::payloadless_abi() );

   auto trace = chain.push_action("payloadless"_n, "doit"_n, "payloadless"_n, mutable_variant_object());
   auto msg = trace->action_traces.front().console;
   BOOST_CHECK_EQUAL(msg == "Im a payloadless action", true);
}

// test GH#3916 - contract api action with no parameters fails when called from cleos
// abi_serializer was failing when action data was empty.
BOOST_AUTO_TEST_CASE_TEMPLATE( test_abi_serializer, T, payloadless_testers ) {
   T chain;

   chain.create_accounts( {"payloadless"_n} );
   chain.set_code( "payloadless"_n, test_contracts::payloadless_wasm() );
   chain.set_abi( "payloadless"_n, test_contracts::payloadless_abi() );

   fc::variant pretty_trx = fc::mutable_variant_object()
      ("actions", fc::variants({
         fc::mutable_variant_object()
            ("account", name("payloadless"_n))
            ("name", "doit")
            ("authorization", fc::variants({
               fc::mutable_variant_object()
                  ("actor", name("payloadless"_n))
                  ("permission", name(config::active_name))
            }))
            ("data", fc::mutable_variant_object()
            )
         })
     );

   signed_transaction trx;
   // from_variant is key to this test as abi_serializer was explicitly not allowing empty "data"
   abi_serializer::from_variant(pretty_trx, trx, chain.get_resolver(), abi_serializer::create_yield_function( chain.abi_serializer_max_time ));
   chain.set_transaction_headers(trx);

   trx.sign( chain.get_private_key( "payloadless"_n, "active" ), chain.control->get_chain_id() );
   auto trace = chain.push_transaction( trx );
   auto msg = trace->action_traces.front().console;
   BOOST_CHECK_EQUAL(msg == "Im a payloadless action", true);
}

BOOST_AUTO_TEST_SUITE_END()
