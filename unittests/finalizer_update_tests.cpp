#pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wsign-compare"
    #include <boost/test/unit_test.hpp>
#pragma GCC diagnostic pop

#include <eosio/testing/tester.hpp>

using namespace eosio::chain::literals;
using namespace eosio::testing;
using namespace eosio::chain;


/*
 * register test suite `finalizer_update_tests`
 */
BOOST_AUTO_TEST_SUITE(finalizer_update_tests)

// -----------------------------------------------------------------------
// produce one block, and verify that the active finalizer_policy for this
// newly produced `block` matches the passed `generation` and `keys_span`.
// -----------------------------------------------------------------------
static void ensure_next_block_finalizer_policy(validating_tester& t,
                                               uint32_t generation,
                                               std::span<const bls_public_key> keys_span) {
   t.produce_block();
   t.check_head_finalizer_policy(generation, keys_span);
}

// ---------------------------------------------------------------------
// verify that finalizer policy change via set_finalizer take 2 3-chains
// to take effect.
// ---------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(savanna_set_finalizer_single_test) { try {
   // Do not transition to Savanna at constrution. Transition explicitly later.
   legacy_validating_tester t;
   size_t num_keys    = 22u;
   size_t finset_size = 21u;

   // Create finalizer keys
   finalizer_keys fin_keys(t, num_keys, finset_size);

   // set finalizers on current node
   fin_keys.set_node_finalizers(0u, num_keys);

   // run initial set_finalizer_policy() and waits until transition is complete
   auto pubkeys0 = fin_keys.set_finalizer_policy(0u).pubkeys;
   fin_keys.transition_to_savanna();

   // run set_finalizers(), verify it becomes active after exactly two 3-chains
   // -------------------------------------------------------------------------
   auto pubkeys1 = fin_keys.set_finalizer_policy(1u).pubkeys;
   t.produce_block();
   t.check_head_finalizer_policy(1u, pubkeys0); // new policy should only be active until after two 3-chains

   t.produce_blocks(3);
   t.check_head_finalizer_policy(1u, pubkeys0); // one 3-chain - new policy still should not be active

   t.produce_blocks(2);
   t.check_head_finalizer_policy(1u, pubkeys0); // one 3-chain + 2 blocks - new policy still should not be active

   t.produce_block();
   t.check_head_finalizer_policy(2u, pubkeys1); // two 3-chain - new policy *should* be active

} FC_LOG_AND_RETHROW() }

// ---------------------------------------------------------------------------
// Test correct behavior when multiple finalizer policy changes are in-flight
// at the same time.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(savanna_set_finalizer_multiple_test) { try {
   // Do not transition to Savanna at construction. Transition explicitly later.
   legacy_validating_tester t;
   size_t num_keys    = 50u;
   size_t finset_size = 21u;

   auto verify_block_finality_policy_diff = [](const signed_block_ptr& block, uint32_t gen, const bls_public_key& key) {
      std::optional<block_header_extension> ext = block->extract_header_extension(instant_finality_extension::extension_id());
      BOOST_TEST(!!ext);
      std::optional<finalizer_policy_diff> fin_policy_diff = std::get<instant_finality_extension>(*ext).new_finalizer_policy_diff;
      BOOST_TEST(!!fin_policy_diff);
      BOOST_TEST(fin_policy_diff->generation == gen);
      // each set_finalizer_policy in this test removes one and adds one
      BOOST_TEST(fin_policy_diff->finalizers_diff.remove_indexes.size() == 1);
      BOOST_TEST_REQUIRE(fin_policy_diff->finalizers_diff.insert_indexes.size() == 1);
      BOOST_TEST(fin_policy_diff->finalizers_diff.insert_indexes[0].second.public_key == key);
   };

   // Create finalizer keys
   finalizer_keys fin_keys(t, num_keys, finset_size);

   // set finalizers on current node
   fin_keys.set_node_finalizers(0u, num_keys);

   // run initial set_finalizer_policy() and waits until transition is complete
   auto pubkeys0 = fin_keys.set_finalizer_policy(0u).pubkeys;
   fin_keys.transition_to_savanna();

   // run set_finalizers() twice in same block, verify only latest one becomes active
   // -------------------------------------------------------------------------------
   (void)fin_keys.set_finalizer_policy(1u);
   auto pubkeys2 = fin_keys.set_finalizer_policy(2u).pubkeys;
   t.produce_block();
   t.check_head_finalizer_policy(1u, pubkeys0); // new policy should only be active until after two 3-chains
   t.produce_blocks(3);
   t.produce_blocks(2);
   t.check_head_finalizer_policy(1u, pubkeys0); // new policy should only be active until after two 3-chains
   t.produce_block();
   t.check_head_finalizer_policy(2u, pubkeys2); // two 3-chain - new policy pubkeys2 *should* be active

   // run a test with multiple set_finalizers in-flight during the two 3-chains they
   // take to become active
   // ------------------------------------------------------------------------------
   auto pubkeys3 = fin_keys.set_finalizer_policy(3u).pubkeys;
   auto b = t.produce_block();
   verify_block_finality_policy_diff(b, 3, pubkeys3.back());
   auto pubkeys4 = fin_keys.set_finalizer_policy(4u).pubkeys;
   b = t.produce_block();
   verify_block_finality_policy_diff(b, 4, pubkeys4.back());
   t.produce_block();
   auto pubkeys5 = fin_keys.set_finalizer_policy(5u).pubkeys;
   b = t.produce_block();
   verify_block_finality_policy_diff(b, 5, pubkeys5.back());
   t.produce_block();
   auto pubkeys6 = fin_keys.set_finalizer_policy(6u).pubkeys;
   b = t.produce_block();
   verify_block_finality_policy_diff(b, 6, pubkeys6.back());
   auto pubkeys7 = fin_keys.set_finalizer_policy(7u).pubkeys;
   t.check_head_finalizer_policy(2u, pubkeys2); // 5 blocks after pubkeys3 (b5 - b0), pubkeys2 should still be active
   b = t.produce_block();
   verify_block_finality_policy_diff(b, 7, pubkeys7.back());
   auto pubkeys8 = fin_keys.set_finalizer_policy(8u).pubkeys;
   t.check_head_finalizer_policy(3u, pubkeys3); // 6 blocks after pubkeys3 (b6 - b0), pubkeys3 should be active
   b = t.produce_block();
   verify_block_finality_policy_diff(b, 8, pubkeys8.back());
   auto pubkeys9 = fin_keys.set_finalizer_policy(9u).pubkeys;
   t.check_head_finalizer_policy(4u, pubkeys4); // 6 blocks after pubkeys4 (b7 - b1), pubkeys4 should be active
   b = t.produce_block();
   verify_block_finality_policy_diff(b, 9, pubkeys9.back());
   auto pubkeys10 = fin_keys.set_finalizer_policy(10u).pubkeys;
   t.check_head_finalizer_policy(4u, pubkeys4); // 7 blocks after pubkeys4, pubkeys4 should still be active
   b = t.produce_block();
   verify_block_finality_policy_diff(b, 10, pubkeys10.back());
   auto pubkeys11 = fin_keys.set_finalizer_policy(11u).pubkeys;
   t.check_head_finalizer_policy(5u, pubkeys5); // 6 blocks after pubkeys5 (b9 - b3), pubkeys5 should be active
   b = t.produce_block();
   verify_block_finality_policy_diff(b, 11, pubkeys11.back());
   t.produce_block(); // two blocks between 5 & 6 proposals
   t.check_head_finalizer_policy(6u, pubkeys6); // the rest are all one block apart, tests pending with propsed
   auto b12 = t.produce_block();
   t.check_head_finalizer_policy(7u, pubkeys7);
   auto b13 = t.produce_block();
   t.check_head_finalizer_policy(8u, pubkeys8);
   auto b14 = t.produce_block();
   t.check_head_finalizer_policy(9u, pubkeys9);
   auto b15 = t.produce_block();
   t.check_head_finalizer_policy(10u, pubkeys10);
   auto b16 = t.produce_block();
   t.check_head_finalizer_policy(11u, pubkeys11);

   // and no further change
   // ---------------------
   for (size_t i=0; i<10; ++i)
      ensure_next_block_finalizer_policy(t, 11u, pubkeys11);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
