#include "savanna_cluster.hpp"

using namespace eosio::chain;
using namespace eosio::testing;

BOOST_AUTO_TEST_SUITE(savanna_finalizer_policy)

// ---------------------------------------------------------------------------------------------------
//
// ---------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(policy_change, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0]; auto& B=_nodes[1]; auto& C=_nodes[2]; auto& D=_nodes[3];

   C.close();
   BOOST_REQUIRE_EQUAL(2u, A.lib_advances_by([&]() { A.produce_blocks(2); }));

   // update finalizer_policy with a new key for B
   // --------------------------------------------
   base_tester::finalizer_policy_input input;
   for (size_t i=0; i<num_nodes(); ++i)
      input.finalizers.emplace_back(_fin_keys[i], 1);
   input.finalizers[1] = { _fin_keys[num_nodes()], 1 }; // overwrite finalizer key for B
   input.threshold =  (input.finalizers.size() * 2) / 3 + 1;
   A.set_finalizers(input);
   A.produce_block();                                   // so the block with `set_finalizers` is `head`

   // produce blocks on A, waiting for the new policy to become pending
   // -----------------------------------------------------------------
   BOOST_REQUIRE(!A.head_pending_finalizer_policy());     // we shouldn't have a pending policy
   size_t num_to_pending = 0;
   do {
      A.produce_block();
      ++num_to_pending;
   } while (!A.head_pending_finalizer_policy());
   BOOST_REQUIRE_EQUAL(num_to_pending, num_chains_to_final); // becames pending when proposed block is final

   // now that the new policy is pending, we need B to vote on it for finality to advance, as C is down.
   // --------------------------------------------------------------------------------------------------
   B.close();
   B.node_finalizers = std::vector<account_name>{ _fin_keys[1], _fin_keys[num_nodes()] }; // see node_t::node_t
   B.open();

   // produce blocks on A, waiting for transition to complete (until the updated policy is active on A's head)
   // --------------------------------------------------------------------------------------------------------
   auto finpol = A.head_active_finalizer_policy();
   auto current_gen  = finpol->generation;
   auto expected_gen = A.head_pending_finalizer_policy()->generation;
   BOOST_REQUIRE_GT(expected_gen, current_gen);

   size_t num_to_active = 0;
   do {
      A.produce_block();
      finpol = A.head_active_finalizer_policy();
      ++num_to_active;
   } while (finpol->generation != expected_gen);
   BOOST_REQUIRE_EQUAL(num_to_active, num_chains_to_final); // becames active when "pending" block is final

   // A produces blocks, verify lib advances
   // --------------------------------------
   BOOST_REQUIRE_EQUAL(3u, A.lib_advances_by([&]() { A.produce_blocks(3); }));
} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_SUITE_END()