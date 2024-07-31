#include "savanna_cluster.hpp"

using namespace eosio::chain;
using namespace eosio::testing;

BOOST_AUTO_TEST_SUITE(savanna_transition)

// ---------------------------------------------------------------------------------------------------
//                               Single finalizer goes down
// ---------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(straightforward_transition, savanna_cluster::pre_transition_cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2];

   C.close();
   A.require_lib_advancing_by(4, [&]() { A.produce_blocks(4); }); // lib still advances with 3 finalizers
   C.open();
   A.push_blocks_to(C);
   A.require_lib_advancing_by(4, [&]() { A.produce_blocks(4); }); // all 4 finalizers should be back voting
   BOOST_REQUIRE(!C.is_head_missing_finalizer_votes());           // let's make sure of that
} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_SUITE_END()