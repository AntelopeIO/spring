#include <wasm_spec_tests.hpp>

const string wasm_str_memory_trap_0 = base_dir + "/memory_trap.0.wasm";
std::vector<uint8_t> wasm_memory_trap_0= read_wasm(wasm_str_memory_trap_0.c_str());

BOOST_DATA_TEST_CASE(memory_trap_0_check_throw, boost::unit_test::data::xrange(0,10), index) { try {
   TESTER tester;
   tester.produce_block();
   tester.create_account( "wasmtest"_n );
   tester.produce_block();
   tester.set_code("wasmtest"_n, wasm_memory_trap_0);
   tester.produce_block();

   action test;
   test.account = "wasmtest"_n;
   test.name = account_name((uint64_t)index);
   test.authorization = {{"wasmtest"_n, config::active_name}};

   BOOST_CHECK_THROW(push_action(tester, std::move(test), "wasmtest"_n.to_uint64_t()), wasm_execution_error);
   tester.produce_block();
} FC_LOG_AND_RETHROW() }

BOOST_DATA_TEST_CASE(memory_trap_0_pass, boost::unit_test::data::xrange(10,11), index) { try {
   TESTER tester;
   tester.produce_block();
   tester.create_account( "wasmtest"_n );
   tester.produce_block();
   tester.set_code("wasmtest"_n, wasm_memory_trap_0);
   tester.produce_block();

   action test;
   test.account = "wasmtest"_n;
   test.name = account_name((uint64_t)index);
   test.authorization = {{"wasmtest"_n, config::active_name}};

   push_action(tester, std::move(test), "wasmtest"_n.to_uint64_t());
   tester.produce_block();
   BOOST_REQUIRE_EQUAL( tester.validate(), true );
} FC_LOG_AND_RETHROW() }

const string wasm_str_memory_trap_1 = base_dir + "/memory_trap.1.wasm";
std::vector<uint8_t> wasm_memory_trap_1= read_wasm(wasm_str_memory_trap_1.c_str());

void memory_trap_1_check_throw_common(TESTER& tester, uint32_t index) {
   tester.produce_block();
   tester.create_account( "wasmtest"_n );
   tester.produce_block();
   tester.set_code("wasmtest"_n, wasm_memory_trap_1);
   tester.produce_block();

   action test;
   test.account = "wasmtest"_n;
   test.name = account_name((uint64_t)index);
   test.authorization = {{"wasmtest"_n, config::active_name}};

   BOOST_CHECK_THROW(push_action(tester, std::move(test), "wasmtest"_n.to_uint64_t()), wasm_execution_error);
   tester.produce_block();
}

BOOST_DATA_TEST_CASE(memory_trap_1_check_throw_1, boost::unit_test::data::xrange(0,78), index) { try {
   TESTER tester;
   memory_trap_1_check_throw_common(tester, index);
} FC_LOG_AND_RETHROW() }

BOOST_DATA_TEST_CASE(memory_trap_1_check_throw_2, boost::unit_test::data::xrange(78,156), index) { try {
   TESTER tester;
   memory_trap_1_check_throw_common(tester, index);
} FC_LOG_AND_RETHROW() }

BOOST_DATA_TEST_CASE(memory_trap_1_pass, boost::unit_test::data::xrange(156,157), index) { try {
   TESTER tester;
   tester.produce_block();
   tester.create_account( "wasmtest"_n );
   tester.produce_block();
   tester.set_code("wasmtest"_n, wasm_memory_trap_1);
   tester.produce_block();

   action test;
   test.account = "wasmtest"_n;
   test.name = account_name((uint64_t)index);
   test.authorization = {{"wasmtest"_n, config::active_name}};

   push_action(tester, std::move(test), "wasmtest"_n.to_uint64_t());
   tester.produce_block();
   BOOST_REQUIRE_EQUAL( tester.validate(), true );
} FC_LOG_AND_RETHROW() }
