#include <boost/test/unit_test.hpp>
#include <eosio/chain/snapshot.hpp>
#include <eosio/testing/tester.hpp>
#include "snapshot_suites.hpp"
#include <eosio/chain/snapshot_scheduler.hpp>
#include <eosio/chain/pending_snapshot.hpp>
#include <test_contracts.hpp>
#include <snapshots.hpp>

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;
using namespace boost::system;

namespace {
    snapshot_scheduler::snapshot_information test_snap_info;
}

BOOST_AUTO_TEST_SUITE(producer_snapshot_tests)

using next_t = pending_snapshot<snapshot_scheduler::snapshot_information>::next_t;

template<typename TESTER, typename SNAPSHOT_SUITE>
void test_snapshot_information() {
   TESTER chain;
   const std::filesystem::path parent_path = chain.get_config().blocks_dir.parent_path();

   chain.create_account("snapshot"_n);
   chain.produce_block();
   chain.set_code("snapshot"_n, test_contracts::snapshot_test_wasm());
   chain.set_abi("snapshot"_n, test_contracts::snapshot_test_abi());
   chain.produce_block();

   auto block = chain.produce_block();
   const uint32_t base_block_num = block->block_num();

   // undo the auto-pending from tester
   chain.control->abort_block();

   auto block2 = chain.produce_block();
   BOOST_REQUIRE_EQUAL(block2->block_num(), base_block_num + 1); // ensure that test setup stays consistent with original snapshot setup

   // undo the auto-pending from tester
   chain.control->abort_block();

   // write snapshot
   auto write_snapshot = [&]( const std::filesystem::path& p ) -> void {
      if ( !std::filesystem::exists( p.parent_path() ) )
         std::filesystem::create_directory( p.parent_path() );

      // create the snapshot
      auto snap_out = std::ofstream(p.generic_string(), (std::ios::out | std::ios::binary));
      auto writer = std::make_shared<ostream_snapshot_writer>(snap_out);
      (*chain.control).write_snapshot(writer);
      writer->finalize();
      snap_out.flush();
      snap_out.close();
   };

   auto final_path = pending_snapshot<snapshot_scheduler::snapshot_information>::get_final_path(block2->previous, "../snapshots/");
   auto pending_path = pending_snapshot<snapshot_scheduler::snapshot_information>::get_pending_path(block2->previous, "../snapshots/");

   write_snapshot( pending_path );
   next_t next;
   pending_snapshot pending{ block2->previous, block2->timestamp, next, pending_path.generic_string(), final_path.generic_string() };
   test_snap_info = pending.finalize(block2->previous, *chain.control);
   BOOST_REQUIRE_EQUAL(test_snap_info.head_block_num, base_block_num);
   BOOST_REQUIRE_EQUAL(test_snap_info.version, chain_snapshot_header::current_version);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(snapshot_information_test, SNAPSHOT_SUITE, snapshot_suites) {
   test_snapshot_information<legacy_tester, SNAPSHOT_SUITE>();
   test_snapshot_information<savanna_tester, SNAPSHOT_SUITE>();
}

BOOST_AUTO_TEST_SUITE_END()
