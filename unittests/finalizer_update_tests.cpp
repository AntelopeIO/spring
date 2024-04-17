#pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wsign-compare"
    #include <boost/test/unit_test.hpp>
#pragma GCC diagnostic pop

#include <eosio/testing/tester.hpp>

using namespace eosio::chain::literals;
using namespace eosio::testing;
using namespace eosio::chain;

// ---------------------------------------------------------------------
// Given a newly created `validating_tester`, trigger the transition to
// Savanna, and produce blocks until the transition is completed.
// ---------------------------------------------------------------------
static std::pair<std::vector<account_name>, std::vector<bls_public_key>>
transition_to_Savanna(validating_tester& t, size_t num_local_finalizers, size_t finset_size) {
   uint32_t lib = 0;
   signed_block_ptr lib_block;
   auto c = t.control->irreversible_block().connect([&](const block_signal_params& t) {
      const auto& [ block, id ] = t;
      lib = block->block_num();
      lib_block = block;
   });

   t.produce_block();

   // Create finalizer accounts
   vector<account_name> finalizers;
   finalizers.reserve(num_local_finalizers);
   for (size_t i=0; i<num_local_finalizers; ++i)
      finalizers.emplace_back(std::string("init") + (char)('a' + i/26) + (char)('a' + i%26));

   t.create_accounts(finalizers);
   t.produce_block();

   // activate savanna'
   t.set_node_finalizers({&finalizers[0], finalizers.size()});

   // activate savanna by running the `set_finalizers` host function
   auto pubkeys0 = t.set_active_finalizers({&finalizers[0], finset_size});

   // `genesis_block` is the first block where set_finalizers() was executed.
   // It is the genesis block.
   // It will include the first header extension for the instant finality.
   // -----------------------------------------------------------------------
   auto genesis_block = t.produce_block();

   // wait till the genesis_block becomes irreversible.
   // The critical block is the block that makes the genesis_block irreversible
   // -------------------------------------------------------------------------
   signed_block_ptr critical_block = nullptr;  // last value of this var is the critical block
   while(genesis_block->block_num() > lib)
      critical_block = t.produce_block();

   // Blocks after the critical block are proper IF blocks.
   // -----------------------------------------------------
   auto first_proper_block = t.produce_block();
   BOOST_REQUIRE(first_proper_block->is_proper_svnn_block());

   // wait till the first proper block becomes irreversible. Transition will be done then
   // -----------------------------------------------------------------------------------
   signed_block_ptr pt_block  = nullptr;  // last value of this var is the first post-transition block
   while(first_proper_block->block_num() > lib) {
      pt_block = t.produce_block();
      BOOST_REQUIRE(pt_block->is_proper_svnn_block());
   }

   // lib must advance after 3 blocks
   // -------------------------------
   t.produce_blocks(3);
   BOOST_REQUIRE_EQUAL(lib, pt_block->block_num());

   c.disconnect();
   return { finalizers, pubkeys0 };
}

/*
 * register test suite `finalizer_update_tests`
 */
BOOST_AUTO_TEST_SUITE(finalizer_update_tests)

// ---------------------------------------------------------------------
// checks that the active finalizer_policy for `block` matches the
// passed `generation` and `keys_span`.
// ---------------------------------------------------------------------
static void check_finalizer_policy(validating_tester& t,
                                   const signed_block_ptr& block,
                                   uint32_t generation,
                                   std::span<const bls_public_key> keys_span) {
   auto finpol = t.active_finalizer_policy(block->calculate_id());
   BOOST_REQUIRE(!!finpol);
   BOOST_REQUIRE_EQUAL(finpol->generation, generation); // new policy should not be active
                                                        // until after two 3-chains
   BOOST_REQUIRE_EQUAL(keys_span.size(), finpol->finalizers.size());
   std::vector<bls_public_key> keys {keys_span.begin(), keys_span.end() };
   std::sort(keys.begin(), keys.end());

   std::vector<bls_public_key> active_keys;
   for (const auto& auth : finpol->finalizers)
      active_keys.push_back(auth.public_key);
   std::sort(active_keys.begin(), active_keys.end());
   for (size_t i=0; i<keys.size(); ++i)
      BOOST_REQUIRE_EQUAL(keys[i], active_keys[i]);
}

// -----------------------------------------------------------------------
// produce one block, and verify that the active finalizer_policy for this
// newly produced `block` matches the passed `generation` and `keys_span`.
// -----------------------------------------------------------------------
static void ensure_next_block_finalizer_policy(validating_tester& t,
                                               uint32_t generation,
                                               std::span<const bls_public_key> keys_span) {
   auto b = t.produce_block();
   check_finalizer_policy(t, b, generation, keys_span);
}

// ---------------------------------------------------------------------
// verify that finalizer policy change via set_finalizer take 2 3-chains
// to take effect.
// ---------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(savanna_set_finalizer_single_test) { try {
   validating_tester t;
   size_t num_local_finalizers = 50;
   size_t finset_size          = 21;

   auto [finalizers, pubkeys0] = transition_to_Savanna(t, num_local_finalizers, finset_size);
   assert(finalizers.size() == num_local_finalizers && pubkeys0.size() == finset_size);

   // run set_finalizers(), verify it becomes active after exactly two 3-chains
   // -------------------------------------------------------------------------
   auto pubkeys1 = t.set_active_finalizers({&finalizers[1], finset_size});
   auto b0 = t.produce_block();
   check_finalizer_policy(t, b0, 1, pubkeys0); // new policy should only be active until after two 3-chains

   t.produce_blocks(2);
   auto b3 = t.produce_block();
   check_finalizer_policy(t, b3, 1, pubkeys0); // one 3-chain - new policy still should not be active

   t.produce_blocks(1);
   auto b5 = t.produce_block();
   check_finalizer_policy(t, b5, 1, pubkeys0); // one 3-chain + 2 blocks - new policy still should not be active

   auto b6 = t.produce_block();
   check_finalizer_policy(t, b6, 2, pubkeys1); // two 3-chain - new policy *should* be active

} FC_LOG_AND_RETHROW() }

// ---------------------------------------------------------------------------
// Test correct behavior when multiple finalizer policy changes are in-flight
// at the same time.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(savanna_set_finalizer_multiple_test) { try {
   validating_tester t;
   size_t num_local_finalizers = 50;
   size_t finset_size          = 21;

   auto [finalizers, pubkeys0] = transition_to_Savanna(t, num_local_finalizers, finset_size);

   // run set_finalizers() twice in same block, verify only latest one becomes active
   // -------------------------------------------------------------------------------
   auto pubkeys1 = t.set_active_finalizers({&finalizers[1], finset_size});
   auto pubkeys2 = t.set_active_finalizers({&finalizers[2], finset_size});
   auto b0 = t.produce_block();
   check_finalizer_policy(t, b0, 1, pubkeys0); // new policy should only be active until after two 3-chains
   t.produce_blocks(4);
   auto b5 = t.produce_block();
   check_finalizer_policy(t, b5, 1, pubkeys0); // new policy should only be active until after two 3-chains
   auto b6 = t.produce_block();
   check_finalizer_policy(t, b6, 2, pubkeys2); // two 3-chain - new policy pubkeys2 *should* be active

   // run a test with multiple set_finlizers in-flight during the two 3-chains they
   // take to become active
   // -----------------------------------------------------------------------------
   auto pubkeys3 = t.set_active_finalizers({&finalizers[3], finset_size});
   b0 = t.produce_block();
   auto pubkeys4 = t.set_active_finalizers({&finalizers[4], finset_size});
   auto b1 = t.produce_block();
   auto b2 = t.produce_block();
   auto pubkeys5 = t.set_active_finalizers({&finalizers[5], finset_size});
   t.produce_blocks(2);
   b5 = t.produce_block();
   check_finalizer_policy(t, b5, 2, pubkeys2); // 5 blocks after pubkeys3 (b5 - b0), pubkeys2 should still be active
   b6 = t.produce_block();
   check_finalizer_policy(t, b6, 3, pubkeys3); // 6 blocks after pubkeys3 (b6 - b0), pubkeys3 should be active
   auto b7 = t.produce_block();
   check_finalizer_policy(t, b7, 4, pubkeys4); // 6 blocks after pubkeys4 (b7 - b1), pubkeys4 should be active

   auto b8 = t.produce_block();
   check_finalizer_policy(t, b8, 4, pubkeys4); // 7 blocks after pubkeys4, pubkeys4 should still be active
   auto b9 = t.produce_block();
   check_finalizer_policy(t, b9, 5, pubkeys5); // 6 blocks after pubkeys5 (b9 - b3), pubkeys5 should be active

   // and no further change
   ensure_next_block_finalizer_policy(t, 5, pubkeys5);
   ensure_next_block_finalizer_policy(t, 5, pubkeys5);
   ensure_next_block_finalizer_policy(t, 5, pubkeys5);
   ensure_next_block_finalizer_policy(t, 5, pubkeys5);
   ensure_next_block_finalizer_policy(t, 5, pubkeys5);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()