#include "savanna_cluster.hpp"

using namespace eosio::chain;
using namespace eosio::testing;

// auto& A=_nodes[0]; auto&  B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];

BOOST_AUTO_TEST_SUITE(savanna_disaster_recovery)

// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(nodes_go_down, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2];

   C.close();
   require_lib_advancing_by(4, [&]() { A.produce_blocks(4); }); // lib still advances with 3 finalizers
   C.open();
   A.push_blocks_to(C);
   require_lib_advancing_by(4, [&]() { A.produce_blocks(4); }); // all 4 finalizers should be back voting
   BOOST_REQUIRE(!A.is_head_missing_finalizer_votes());         // let's make sure of that
} FC_LOG_AND_RETHROW()


// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(recover_killed_node_with_old_fsi, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2];

   auto fsi = C.save_fsi();
   require_lib_advancing_by(2, [&]() { A.produce_blocks(2); });
   auto snapshot = C.snapshot();
   require_lib_advancing_by(2, [&]() { A.produce_blocks(2); });
   C.close();
   require_lib_advancing_by(2, [&]() { A.produce_blocks(2); }); // lib still advances with 3 finalizers
   C.remove_state();
   C.overwrite_fsi(fsi);
   C.open_from_snapshot(snapshot);
   A.push_blocks_to(C);
   require_lib_advancing_by(2, [&]() { A.produce_blocks(2); }); // all 4 finalizers should be back voting
   BOOST_REQUIRE(!A.is_head_missing_finalizer_votes());         // let's make sure of that
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(recover_killed_node_with_deleted_fsi, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2];

   require_lib_advancing_by(2, [&]() { A.produce_blocks(2); });
   auto snapshot = C.snapshot();
   require_lib_advancing_by(2, [&]() { A.produce_blocks(2); });
   C.close();
   require_lib_advancing_by(2, [&]() { A.produce_blocks(2); }); // lib still advances with 3 finalizers
   C.remove_state();
   C.remove_fsi();
   C.open_from_snapshot(snapshot);
   A.push_blocks_to(C);
   require_lib_advancing_by(2, [&]() { A.produce_blocks(2); }); // all 4 finalizers should be back voting
   BOOST_REQUIRE(!A.is_head_missing_finalizer_votes());         // let's make sure of that
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(recover_killed_node_while_retaining_fsi, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2];

   require_lib_advancing_by(2, [&]() { A.produce_blocks(2); });
   auto snapshot = C.snapshot();
   require_lib_advancing_by(2, [&]() { A.produce_blocks(2); });
   C.close();
   require_lib_advancing_by(2, [&]() { A.produce_blocks(2); }); // lib still advances with 3 finalizers
   C.remove_state();
   C.open_from_snapshot(snapshot);
   A.push_blocks_to(C);
   require_lib_advancing_by(2, [&]() { A.produce_blocks(2); }); // all 4 finalizers should be back voting
   BOOST_REQUIRE(!A.is_head_missing_finalizer_votes());         // let's make sure of that
} FC_LOG_AND_RETHROW()



BOOST_AUTO_TEST_SUITE_END()