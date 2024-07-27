#include "savanna_cluster.hpp"

using namespace eosio::chain;
using namespace eosio::testing;

// auto& A=_nodes[0]; auto&  B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];

BOOST_AUTO_TEST_SUITE(savanna_disaster_recovery)

// ---------------------------------------------------------------------------------------------------
//                               Single finalizer goes down
// ---------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(node_goes_down, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2];

   C.close();
   A.require_lib_advancing_by(4, [&]() { A.produce_blocks(4); }); // lib still advances with 3 finalizers
   C.open();
   A.push_blocks_to(C);
   A.require_lib_advancing_by(4, [&]() { A.produce_blocks(4); }); // all 4 finalizers should be back voting
   BOOST_REQUIRE(!C.is_head_missing_finalizer_votes());           // let's make sure of that
} FC_LOG_AND_RETHROW()


// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(recover_killed_node_with_old_fsi, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2];

   auto fsi = C.save_fsi();
   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); });
   auto snapshot = C.snapshot();
   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); });
   C.close();
   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); }); // lib still advances with 3 finalizers
   C.remove_state();
   C.overwrite_fsi(fsi);
   C.open_from_snapshot(snapshot);
   A.push_blocks_to(C);
   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); }); // all 4 finalizers should be back voting
   BOOST_REQUIRE(!C.is_head_missing_finalizer_votes());           // let's make sure of that
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(recover_killed_node_with_deleted_fsi, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2];

   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); });
   auto snapshot = C.snapshot();
   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); });
   C.close();
   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); }); // lib still advances with 3 finalizers
   C.remove_state();
   C.remove_fsi();
   C.open_from_snapshot(snapshot);
   A.push_blocks_to(C);
   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); }); // all 4 finalizers should be back voting
   BOOST_REQUIRE(!C.is_head_missing_finalizer_votes());           // let's make sure of that
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(recover_killed_node_while_retaining_fsi, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2];

   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); });
   auto snapshot = C.snapshot();
   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); });
   C.close();
   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); }); // lib still advances with 3 finalizers
   C.remove_state();
   C.open_from_snapshot(snapshot);
   A.push_blocks_to(C);
   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); }); // all 4 finalizers should be back voting
   BOOST_REQUIRE(!C.is_head_missing_finalizer_votes());           // let's make sure of that
} FC_LOG_AND_RETHROW()


// ---------------------------------------------------------------------------------------------------
//                               All but one finalizers go down
// ---------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(nodes_go_down, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];
   std::array<savanna_cluster::node_t*, 3> failing_nodes { &B, &C, &D };

   for (auto& N : failing_nodes) N->close();
   A.require_lib_advancing_by(1, [&]() { A.produce_blocks(4); }); // lib stalls 3 finalizers down
   for (auto& N : failing_nodes) N->open();
   for (auto& N : failing_nodes) A.push_blocks_to(*N);
   A.require_lib_advancing_by(7, [&]() { A.produce_blocks(4); }); // all 4 finalizers should be back voting
   for (auto& N : failing_nodes) BOOST_REQUIRE(!N->is_head_missing_finalizer_votes());
} FC_LOG_AND_RETHROW()


// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(recover_killed_nodes_with_old_fsi, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];
   std::array<savanna_cluster::node_t*, 3> failing_nodes { &B, &C, &D };

   std::vector<std::vector<uint8_t>> fsis;
   std::vector<std::string> snapshots;

   for (auto& N : failing_nodes) fsis.push_back(N->save_fsi());
   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); });
   for (auto& N : failing_nodes) snapshots.push_back(N->snapshot());
   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); });
   for (auto& N : failing_nodes) N->close();
   A.require_lib_advancing_by(1, [&]() { A.produce_blocks(2); }); // lib stalls 3 finalizers down
   size_t i = 0;
   for (auto& N : failing_nodes) {
      N->remove_state();
      N->overwrite_fsi(fsis[i]);
      N->open_from_snapshot(snapshots[i]);
      A.push_blocks_to(*N);
      ++i;
   }
   A.require_lib_advancing_by(3, [&]() { A.produce_blocks(2); }); // all 4 finalizers should be back voting
   for (auto& N : failing_nodes) BOOST_REQUIRE(!N->is_head_missing_finalizer_votes());
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(recover_killed_nodes_with_deleted_fsi, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2];

   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); });
   auto snapshot = C.snapshot();
   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); });
   C.close();
   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); }); // lib still advances with 3 finalizers
   C.remove_state();
   C.remove_fsi();
   C.open_from_snapshot(snapshot);
   A.push_blocks_to(C);
   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); }); // all 4 finalizers should be back voting
   BOOST_REQUIRE(!A.is_head_missing_finalizer_votes());           // let's make sure of that
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(recover_killed_nodes_while_retaining_fsi, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2];

   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); });
   auto snapshot = C.snapshot();
   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); });
   C.close();
   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); }); // lib still advances with 3 finalizers
   C.remove_state();
   C.open_from_snapshot(snapshot);
   A.push_blocks_to(C);
   A.require_lib_advancing_by(2, [&]() { A.produce_blocks(2); }); // all 4 finalizers should be back voting
   BOOST_REQUIRE(!A.is_head_missing_finalizer_votes());           // let's make sure of that
} FC_LOG_AND_RETHROW()





BOOST_AUTO_TEST_SUITE_END()