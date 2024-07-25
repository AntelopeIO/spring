#include "savanna_cluster.hpp"

using namespace eosio::chain;
using namespace eosio::testing;

// auto& A=_nodes[0]; auto&  B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];

BOOST_AUTO_TEST_SUITE(savanna_disaster_recovery)

BOOST_FIXTURE_TEST_CASE(nodes_go_down, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& C=_nodes[2];

   C.close();
   require_lib_advancing_by(4, [&]() { A.produce_blocks(4); });
   C.open();
   A.push_blocks(C);
   verify_lib_advances();
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()