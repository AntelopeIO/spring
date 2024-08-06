#include "savanna_cluster.hpp"

using namespace eosio::chain;
using namespace eosio::testing;

BOOST_AUTO_TEST_SUITE(savanna_transition)

// ---------------------------------------------------------------------------------------------------
//                               Single finalizer goes down
// ---------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(straightforward_transition, savanna_cluster::pre_transition_cluster_t) try {
   auto& A=_nodes[0];

   // set one finalizer per node
   // --------------------------
   for (size_t i = 0; i < _nodes.size(); ++i)
      _nodes[i].set_node_finalizers({&_fin_keys[i], 1});

   // set finalizer policy and transition to Savanna
   // ----------------------------------------------
   A.transition_to_savanna({&_fin_keys[0], num_nodes()});

   // A produces blocks, verify lib advances
   BOOST_REQUIRE_EQUAL(3, A.lib_advances_by([&]() { A.produce_blocks(3); }));
} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_SUITE_END()