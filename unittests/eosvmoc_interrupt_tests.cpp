#include <eosio/testing/tester.hpp>
#include <test_contracts.hpp>
#include <test_utils.hpp>
#include <boost/test/unit_test.hpp>

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;
using namespace test_utils;
using mvo = fc::mutable_variant_object;

BOOST_AUTO_TEST_SUITE(eosvmoc_interrupt_tests)

BOOST_AUTO_TEST_CASE( wasm_interrupt_test ) { try {
#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
   fc::temp_directory tempdir;
   constexpr bool use_genesis = true;
   savanna_validating_tester t(
      tempdir,
      [&](controller::config& cfg) {
         cfg.eos_vm_oc_whitelist_suffixes.insert("testapi"_n);
         if (cfg.wasm_runtime != wasm_interface::vm_type::eos_vm_oc)
            cfg.eosvmoc_tierup = chain::wasm_interface::vm_oc_enable::oc_auto;
      },
      use_genesis
   );
   if( t.get_config().wasm_runtime == wasm_interface::vm_type::eos_vm_oc ) {
      // eos_vm_oc wasm_runtime does not tier-up and completes compile before continuing execution.
      // A completely different test with different constraints would be needed to test with eos_vm_oc.
      // Since non-tier-up is not a normal valid nodeos runtime, just skip this test for eos_vm_oc.
      return;
   }
   t.execute_setup_policy( setup_policy::full );
   t.produce_block();

   t.create_account( "testapi"_n );
   t.set_code( "testapi"_n, test_contracts::test_api_wasm() );
   t.produce_block();

   auto pre_count = t.control->get_wasm_interface().get_eos_vm_oc_compile_interrupt_count();

   // Use an infinite executing action. When oc compile completes it will kill the action and restart it under
   // eosvmoc. That action will then fail when it hits the 5000ms deadline.
   // 5000ms has to be long enough for oc compile to complete and kill the non-oc executing transaction
   BOOST_CHECK_THROW( push_trx( t, test_api_action<WASM_TEST_ACTION("test_checktime", "checktime_failure")>{},
                                0, 150, 5000, true, fc::raw::pack(10000000000000000000ULL) ),
                      deadline_exception );

   auto post_count = t.control->get_wasm_interface().get_eos_vm_oc_compile_interrupt_count();

   // if post_count == pre_count, then likely that 5000ms above was not long enough for oc compile to complete
   BOOST_TEST(post_count == pre_count + 1);

   BOOST_REQUIRE_EQUAL( t.validate(), true );
#endif
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
