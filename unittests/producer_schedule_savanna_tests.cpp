#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/authorization_manager.hpp>
#include <eosio/testing/tester.hpp>

#include <boost/test/unit_test.hpp>

using namespace eosio::testing;
using namespace eosio::chain;
using mvo = fc::mutable_variant_object;

BOOST_AUTO_TEST_SUITE(producer_schedule_savanna_tests)

namespace {

// Calculate expected producer given the schedule and slot number
inline account_name get_expected_producer(const vector<producer_authority>& schedule, block_timestamp_type t) {
   const auto& index = (t.slot % (schedule.size() * config::producer_repetitions)) / config::producer_repetitions;
   return schedule.at(index).producer_name;
};

} // anonymous namespace

// Use legacy_validating_tester because it transitions to savanna as part of the test.
BOOST_FIXTURE_TEST_CASE( verify_producer_schedule_after_savanna_activation, legacy_validating_tester ) try {

   // Utility function to ensure that producer schedule work as expected
   const auto& confirm_schedule_correctness = [&](const vector<producer_authority>& new_prod_schd, uint32_t expected_schd_ver, uint32_t expected_block_num = 0)  {
      const uint32_t check_duration = 100; // number of blocks
      bool scheduled_changed_to_new = false;
      for (uint32_t i = 0; i < check_duration; ++i) {
         const auto current_schedule = control->active_producers().producers;
         if (new_prod_schd == current_schedule) {
            scheduled_changed_to_new = true;
            if (expected_block_num != 0)
               BOOST_TEST(head().block_num() == expected_block_num);

            // verify eosio.prods updated
            const name usr = config::producers_account_name;
            const name active_permission = config::active_name;
            const auto* perm = control->db().template find<permission_object, by_owner>(boost::make_tuple(usr, active_permission));
            for (auto account : perm->auth.accounts) {
               auto act = account.permission.actor;
               auto itr = std::find_if( current_schedule.begin(), current_schedule.end(), [&](const auto& p) {
                  return p.producer_name == act;
               });
               bool found = itr != current_schedule.end();
               BOOST_TEST(found);
            }
         }

         auto b = produce_block();
         BOOST_TEST( b->confirmed == 0); // must be 0 after instant finality is enabled

         // Check if the producer is the same as what we expect
         const auto block_time = head().block_time();
         const auto& expected_producer = get_expected_producer(current_schedule, block_time);
         BOOST_TEST(head().producer() == expected_producer);

         if (scheduled_changed_to_new)
            break;
      }

      BOOST_TEST(scheduled_changed_to_new);
   };

   // Create producer accounts
   vector<account_name> producers = {
           "inita"_n, "initb"_n, "initc"_n, "initd"_n, "inite"_n, "initf"_n, "initg"_n,
           "inith"_n, "initi"_n, "initj"_n, "initk"_n, "initl"_n, "initm"_n, "initn"_n,
           "inito"_n, "initp"_n, "initq"_n, "initr"_n, "inits"_n, "initt"_n, "initu"_n
   };
   create_accounts(producers);

   // enable savanna
   set_finalizers(producers);
   auto setfin_block = produce_block(); // this block contains the header extension of the finalizer set

   for (block_num_type active_block_num = setfin_block->block_num(); active_block_num > lib_block->block_num(); produce_block()) {
      set_producers({"initc"_n, "inite"_n}); // should be ignored since in transition
      (void)active_block_num; // avoid warning
   };

   // ---- Test first set of producers ----
   // Send set prods action and confirm schedule correctness
   auto trace = set_producers(producers);
   const auto first_prod_schd = get_producer_authorities(producers);
   // called in first round so complete it, skip one round of 12 and start on next round, so block 24
   confirm_schedule_correctness(first_prod_schd, 1, 24);

   // ---- Test second set of producers ----
   vector<account_name> second_set_of_producer = {
           producers[3], producers[6], producers[9], producers[12], producers[15], producers[18], producers[20]
   };
   // Send set prods action and confirm schedule correctness
   set_producers(second_set_of_producer);
   const auto second_prod_schd = get_producer_authorities(second_set_of_producer);
   // called after block 24, so next,next is 48
   confirm_schedule_correctness(second_prod_schd, 2, 48);

   // ---- Test deliberately miss some blocks ----
   const int64_t num_of_missed_blocks = 5000;
   produce_block(fc::microseconds(500 * 1000 * num_of_missed_blocks));
   // Ensure schedule is still correct
   confirm_schedule_correctness(second_prod_schd, 2);
   produce_block();

   // ---- Test third set of producers ----
   vector<account_name> third_set_of_producer = {
           producers[2], producers[5], producers[8], producers[11], producers[14], producers[17], producers[20],
           producers[0], producers[3], producers[6], producers[9], producers[12], producers[15], producers[18],
           producers[1], producers[4], producers[7], producers[10], producers[13], producers[16], producers[19]
   };
   // Send set prods action and confirm schedule correctness
   set_producers(third_set_of_producer);
   const auto third_prod_schd = get_producer_authorities(third_set_of_producer);
   confirm_schedule_correctness(third_prod_schd, 3);

} FC_LOG_AND_RETHROW()

bool compare_schedules( const vector<producer_authority>& a, const producer_authority_schedule& b ) {
      return std::equal( a.begin(), a.end(), b.producers.begin(), b.producers.end() );
};

BOOST_FIXTURE_TEST_CASE( proposer_policy_progression_test, legacy_validating_tester ) try {
   create_accounts( {"alice"_n,"bob"_n,"carol"_n} );

   // set_producers in same block, do it the explicit way to use a diff expiration and avoid duplicate trx
   auto set_producers_force = [&](const std::vector<account_name>& producers) {
      static int unique = 0; // used to force uniqueness of transaction
      auto schedule = get_producer_authorities( producers );
      fc::variants schedule_variant;
      schedule_variant.reserve(schedule.size());
      for( const auto& e: schedule ) {
         schedule_variant.emplace_back(e.get_abi_variant());
      }
      push_action( config::system_account_name, "setprods"_n, config::system_account_name,
                  fc::mutable_variant_object()("schedule", schedule_variant), DEFAULT_EXPIRATION_DELTA + (++unique));
   };

   auto verify_block_finality_ext_producer = [](const signed_block_ptr& block, uint32_t version, account_name new_producer) {
      std::optional<block_header_extension> ext = block->extract_header_extension(finality_extension::extension_id());
      BOOST_TEST(!!ext);
      std::optional<proposer_policy_diff> policy_diff = std::get<finality_extension>(*ext).new_proposer_policy_diff;
      BOOST_TEST_REQUIRE(!!policy_diff);
      BOOST_TEST(policy_diff->version == version);
      bool new_producer_in_insert = std::ranges::find_if(policy_diff->producer_auth_diff.insert_indexes,
                                                         [&](const auto& e) {
                                                            return e.second.producer_name == new_producer;
                                                         }) != policy_diff->producer_auth_diff.insert_indexes.end();
      BOOST_TEST(new_producer_in_insert);
   };

   while (head().block_num() < 3) {
      produce_block();
   }

   // activate savanna
   set_finalizers({"alice"_n,"bob"_n,"carol"_n});
   produce_block(); // this block contains the header extension of the finalizer set
   produce_block(); // one producer, lib here

   // current proposer schedule stays the same as the one prior to IF transition
   vector<producer_authority> prev_sch = {
                                 producer_authority{"eosio"_n, block_signing_authority_v0{1, {{get_public_key("eosio"_n, "active"), 1}}}}};
   BOOST_CHECK_EQUAL( true, compare_schedules( prev_sch, control->active_producers() ) );
   BOOST_CHECK_EQUAL( 0u, control->active_producers().version );

   // set a new proposer policy sch1
   set_producers( {"alice"_n} );
   auto b = produce_block();
   verify_block_finality_ext_producer(b, 1, "alice"_n);
   vector<producer_authority> alice_sch = {
                                 producer_authority{"alice"_n, block_signing_authority_v0{1, {{get_public_key("alice"_n, "active"), 1}}}}
                               };

   // start a round of production
   produce_blocks(config::producer_repetitions-1, true);

   // sch1 cannot become active before one round of production
   BOOST_CHECK_EQUAL( 0u, control->active_producers().version );
   BOOST_CHECK_EQUAL( true, compare_schedules( prev_sch, control->active_producers() ) );

   // set another ploicy to have multiple pending different active time policies
   set_producers( {"bob"_n,"carol"_n} );
   vector<producer_authority> bob_carol_sch = {
                                 producer_authority{"bob"_n,   block_signing_authority_v0{ 1, {{get_public_key("bob"_n,   "active"),1}}}},
                                 producer_authority{"carol"_n, block_signing_authority_v0{ 1, {{get_public_key("carol"_n, "active"),1}}}}
                               };
   b = produce_block();
   verify_block_finality_ext_producer(b, 2u, "bob"_n);

   // set another ploicy should replace sch2
   set_producers( {"bob"_n,"alice"_n} );
   vector<producer_authority> bob_alice_sch = {
      producer_authority{"bob"_n,   block_signing_authority_v0{ 1, {{get_public_key("bob"_n,   "active"),1}}}},
      producer_authority{"alice"_n, block_signing_authority_v0{ 1, {{get_public_key("alice"_n, "active"),1}}}}
   };
   b = produce_block();
   verify_block_finality_ext_producer(b, 3u, "alice"_n);

   // another round
   produce_blocks(config::producer_repetitions-2, true); // -2, already produced tow of the round above

   // sch1  must become active no later than 2 rounds but sch2 cannot become active yet
   BOOST_CHECK_EQUAL( control->active_producers().version, 1u );
   BOOST_CHECK_EQUAL( true, compare_schedules( alice_sch, control->active_producers() ) );

   produce_blocks(config::producer_repetitions, true);

   // sch3 becomes active, version should be 3 even though sch2 was replaced by sch3
   BOOST_CHECK_EQUAL( 3u, control->active_producers().version );
   BOOST_CHECK_EQUAL( true, compare_schedules( bob_alice_sch, control->active_producers() ) );

   // get to next producer round
   auto prod = produce_block()->producer;
   for (auto b = produce_block(); b->producer == prod; b = produce_block());

   // test no change to active schedule
   set_producers( {"bob"_n,"alice"_n} ); // same as before, so no change
   b = produce_block();
   std::optional<block_header_extension> ext = b->extract_header_extension(finality_extension::extension_id());
   BOOST_TEST(!!ext);
   std::optional<proposer_policy_diff> policy_diff = std::get<finality_extension>(*ext).new_proposer_policy_diff;
   BOOST_TEST_REQUIRE(!policy_diff); // no diff

   produce_blocks(config::producer_repetitions-1, true);
   produce_blocks(config::producer_repetitions, true);
   BOOST_CHECK_EQUAL( 3u, control->active_producers().version ); // should be 3 as not different so no change
   BOOST_CHECK_EQUAL( true, compare_schedules( bob_alice_sch, control->active_producers() ) );

   // test no change to proposed schedule, only the first one will take affect
   for (size_t i = 0; i < config::producer_repetitions*2-1; ++i) {
      BOOST_CHECK_EQUAL( 3u, control->active_producers().version ); // should be 3 as not taken affect yet
      BOOST_CHECK_EQUAL( true, compare_schedules( bob_alice_sch, control->active_producers() ) );
      set_producers( {"bob"_n,"carol"_n} );
      set_producers_force({"bob"_n,"carol"_n} );
      set_producers_force({"bob"_n,"carol"_n} );
      produce_block();
   }
   produce_block();
   BOOST_CHECK_EQUAL( 4u, control->active_producers().version ); // should be 4 now as bob,carol now active
   BOOST_CHECK_EQUAL( true, compare_schedules( bob_carol_sch, control->active_producers() ) );

   // get to next producer round
   prod = produce_block()->producer;
   for (auto b = produce_block(); b->producer == prod; b = produce_block());

   // test change in same block where there is an existing proposed that is the same
   set_producers( {"bob"_n,"alice"_n} );
   b = produce_block();
   verify_block_finality_ext_producer(b, 5u, "alice"_n);
   set_producers( {"bob"_n,"carol"_n} );
   set_producers_force({"bob"_n,"carol"_n} );
   b = produce_block();
   verify_block_finality_ext_producer(b, 6u, "carol"_n);
   produce_blocks(config::producer_repetitions-2, true);
   produce_blocks(config::producer_repetitions, true);
   BOOST_CHECK_EQUAL( 6u, control->active_producers().version ); // should be 6 now as bob,carol now active
   BOOST_CHECK_EQUAL( true, compare_schedules( bob_carol_sch, control->active_producers() ) );

   // test change in same block where there is no change
   set_producers( {"bob"_n,"alice"_n} );
   set_producers_force({"bob"_n,"carol"_n} ); // put back, no change expected
   produce_block();
   produce_blocks(config::producer_repetitions-1, true);
   produce_blocks(config::producer_repetitions, true);
   BOOST_CHECK_EQUAL( 6u, control->active_producers().version ); // should be 6 now as bob,carol is still active
   BOOST_CHECK_EQUAL( true, compare_schedules( bob_carol_sch, control->active_producers() ) );

   // get to next producer round
   prod = produce_block()->producer;
   for (auto b = produce_block(); b->producer == prod; b = produce_block());

   // test two in-flight
   //    round A [1,2,..12], next_round B [1,2,..12], next_next_round C [1,2,..12], D [1,2,..12]
   //      propose P1 in A2, active in C1
   //      propose P2 in B2, active in D1
   //      propose P3 in B3, active in D1, replaces P2
   produce_block();
   set_producers({"alice"_n}); // A2, P1
   produce_block();
   produce_blocks(config::producer_repetitions-2, true); // A12
   produce_block();
   set_producers({"bob"_n,"carol"_n}); // B2
   b = produce_block();
   verify_block_finality_ext_producer(b, 8u, "bob"_n);
   set_producers({"bob"_n, "alice"_n} ); // P3
   b = produce_block();
   verify_block_finality_ext_producer(b, 9u, "alice"_n);
   produce_blocks(config::producer_repetitions-3, true); // B12
   produce_block(); // C1
   BOOST_CHECK_EQUAL( 7u, control->active_producers().version );
   BOOST_CHECK_EQUAL( true, compare_schedules( alice_sch, control->active_producers() ) );
   produce_blocks(config::producer_repetitions, true); // D1
   BOOST_CHECK_EQUAL( 9u, control->active_producers().version );
   BOOST_CHECK_EQUAL( true, compare_schedules( bob_alice_sch, control->active_producers() ) );

   // get to next producer round
   prod = produce_block()->producer;
   for (auto b = produce_block(); b->producer == prod; b = produce_block());

   // test two in-flight, P1 == P3, so no change
   //    round A [1,2,..12], next_round B [1,2,..12], next_next_round C [1,2,..12], D [1,2,..12]
   //      propose P1 in A2, active in C1
   //      propose P2 in B2, active in D1
   //      propose P3 in B3, active in D1, replaces P2
   produce_block();
   set_producers({"bob"_n,"carol"_n}); // A2, P1
   b = produce_block();
   verify_block_finality_ext_producer(b, 10u, "carol"_n);
   produce_blocks(config::producer_repetitions-2, true); // A12
   produce_block();
   set_producers({"alice"_n}); // B2
   b = produce_block();
   verify_block_finality_ext_producer(b, 11u, "alice"_n);
   set_producers({"bob"_n,"carol"_n}); // P3 == P1
   b = produce_block();
   verify_block_finality_ext_producer(b, 12u, "bob"_n);
   produce_blocks(config::producer_repetitions-3, true); // B12
   produce_block(); // C1
   BOOST_CHECK_EQUAL( 10u, control->active_producers().version );
   BOOST_CHECK_EQUAL( true, compare_schedules( bob_carol_sch, control->active_producers() ) );
   produce_blocks(config::producer_repetitions, true); // D1
   BOOST_CHECK_EQUAL( 12u, control->active_producers().version );
   BOOST_CHECK_EQUAL( true, compare_schedules( bob_carol_sch, control->active_producers() ) );

   // get to next producer round
   prod = produce_block()->producer;
   for (auto b = produce_block(); b->producer == prod; b = produce_block());

   // test two in-flight, ultimately no change
   produce_block(); // 1
   set_producers({"bob"_n,"carol"_n});
   produce_block(); // 2
   set_producers({"alice"_n});
   b = produce_block(); // 3
   verify_block_finality_ext_producer(b, 13u, "alice"_n);
   set_producers({"carol"_n,"alice"_n});
   b = produce_block(); // 4
   verify_block_finality_ext_producer(b, 14u, "carol"_n);
   set_producers({"carol"_n});
   produce_block(); // 5
   set_producers({"alice"_n});
   b = produce_block(); // 6
   verify_block_finality_ext_producer(b, 16u, "alice"_n);
   set_producers({"bob"_n,"carol"_n});
   b = produce_block();
   verify_block_finality_ext_producer(b, 17u, "bob"_n);
   produce_blocks(config::producer_repetitions-7, true);
   set_producers({"bob"_n});
   produce_block(); // 2
   set_producers({"bob"_n,"carol"_n});
   b = produce_block(); // 3
   verify_block_finality_ext_producer(b, 19u, "carol"_n);
   set_producers({"carol"_n,"bob"_n});
   produce_block(); // 4
   set_producers({"alice"_n} );
   b = produce_block(); // 5
   verify_block_finality_ext_producer(b, 21u, "alice"_n);
   set_producers({"bob"_n,"carol"_n});
   b = produce_block();
   verify_block_finality_ext_producer(b, 22u, "bob"_n);
   produce_blocks(config::producer_repetitions-6, true); // B12
   BOOST_CHECK_EQUAL( 17u, control->active_producers().version );
   BOOST_CHECK_EQUAL( true, compare_schedules( bob_carol_sch, control->active_producers() ) );
   produce_blocks(config::producer_repetitions, true);
   BOOST_CHECK_EQUAL( 22u, control->active_producers().version );
   BOOST_CHECK_EQUAL( true, compare_schedules( bob_carol_sch, control->active_producers() ) );

} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( proposer_policy_misc_tests, legacy_validating_tester ) try {
   create_accounts( {"alice"_n,"bob"_n} );

   while (head().block_num() < 3) {
      produce_block();
   }

   // activate savanna
   set_finalizers({"alice"_n,"bob"_n});
   produce_block(); // this block contains the header extension of the finalizer set
   produce_block(); // one producer, lib here

   { // set multiple policies in the same block. The last one will be chosen
      set_producers( {"alice"_n} );
      set_producers( {"bob"_n} );

      auto b = produce_block();
      auto index = b->timestamp.slot % config::producer_repetitions;
      produce_blocks(config::producer_repetitions - index - 1); // until the last block of round 1

      produce_blocks(config::producer_repetitions); // round 2

      produce_block(); // round 3
      vector<producer_authority> sch = {
         producer_authority{"bob"_n, block_signing_authority_v0{1, {{get_public_key("bob"_n, "active"), 1}}}}
                               };
      BOOST_CHECK_EQUAL( control->active_producers().version, 1u );
      BOOST_CHECK_EQUAL( true, compare_schedules( sch, control->active_producers() ) );
   }

   { // unknown account in proposer policy
      BOOST_CHECK_THROW( set_producers({"carol"_n}), wasm_execution_error );
   }

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE( switch_producers_test ) try {
   legacy_validating_tester chain;

   const std::vector<account_name> accounts = { "aliceaccount"_n, "bobbyaccount"_n, "carolaccount"_n, "emilyaccount"_n };
   chain.create_accounts( accounts );
   chain.produce_block();

   // activate savanna
   chain.set_finalizers(accounts);
   chain.set_producers( accounts );
   chain.produce_block();

   // looping less than 20 did not reproduce the `producer_double_confirm: Producer is double confirming known range` error
   for (size_t i = 0; i < 20; ++i) {
      chain.set_producers( { "aliceaccount"_n, "bobbyaccount"_n } );
      chain.produce_block();

      chain.set_producers( { "bobbyaccount"_n, "aliceaccount"_n } );
      chain.produce_block();
      chain.produce_block( fc::hours(1) );

      chain.set_producers( accounts );
      chain.produce_block();
      chain.produce_block( fc::hours(1) );

      chain.set_producers( { "carolaccount"_n } );
      chain.produce_block();
      chain.produce_block( fc::hours(1) );
   }

} FC_LOG_AND_RETHROW()

// Two policies are proposed in the same round
BOOST_FIXTURE_TEST_CASE( proposed_and_pending_in_same_round_test, validating_tester ) try {
   // With regular validating_tester, we have already transitioned into Savanna

   create_accounts( {"alice"_n, "bob"_n} );
   auto b = produce_block();
   auto index = b->timestamp.slot % config::producer_repetitions; // current index in current round
   // if no rooms in current round for 2 blocks, produce until the last block
   if (index >= config::producer_repetitions - 2) {
      produce_blocks(config::producer_repetitions -1);
   }

   // round 1: propose 2 policies
   set_producers( {"alice"_n} );
   produce_block();
   set_producers( {"bob"_n} );
   b = produce_block();
   index = b->timestamp.slot % config::producer_repetitions;
   produce_blocks(config::producer_repetitions - index - 1); // until the last block of round 1

   // round 2
   produce_blocks(config::producer_repetitions - 1);
   b = produce_block();
   BOOST_CHECK_EQUAL(b->producer, "eosio"_n); // producer still original `eosio`

   // round 3: the latest proposed policy (bob) becomes active because it was already proposed
   // 2 rounds before. Alice policy was skipped.
   b = produce_block();
   vector<producer_authority> bob_sch = {
      producer_authority{
         "bob"_n,
         block_signing_authority_v0{
            1,
            {{get_public_key("bob"_n, "active"), 1}}}}
   };
   BOOST_CHECK_EQUAL(b->producer, "bob"_n);
   BOOST_CHECK_EQUAL(control->active_producers().version, 2u);
   BOOST_CHECK_EQUAL(compare_schedules(bob_sch, control->active_producers()), true);
} FC_LOG_AND_RETHROW()

// Two policies are proposed in two different rounds
BOOST_FIXTURE_TEST_CASE( proposed_and_pending_in_different_rounds_test, validating_tester ) try {
   create_accounts( {"alice"_n, "bob"_n} );
   produce_block();

   // round 1: propose alice policy
   set_producers( {"alice"_n} );
   produce_blocks(config::producer_repetitions); // into somewhere in round 2

   // round 2: propose bob policy
   set_producers( {"bob"_n} );
   auto b = produce_block();
   auto index = b->timestamp.slot % config::producer_repetitions;
   produce_blocks(config::producer_repetitions - index - 1); // until the last block of round 1

   // round 3: the latest pending policy (alice) becomes active because it was already proposed
   // 2 rounds before.
   b = produce_block();
   vector<producer_authority> alice_sch = {
      producer_authority{
         "alice"_n,
         block_signing_authority_v0{
            1,
            {{get_public_key("alice"_n, "active"), 1}}}}
   };
   BOOST_CHECK_EQUAL(b->producer, "alice"_n);
   BOOST_CHECK_EQUAL(control->active_producers().version, 1u);
   BOOST_CHECK_EQUAL(compare_schedules(alice_sch, control->active_producers()), true);
   produce_blocks(config::producer_repetitions - 1); // until the last block of round 2

   // round 4: the latest proposed policy (bob) becomes active because it was already proposed
   // 2 rounds before.
   b = produce_block();
   vector<producer_authority> bob_sch = {
      producer_authority{
         "bob"_n,
         block_signing_authority_v0{
            1,
            {{get_public_key("bob"_n, "active"), 1}}}}
   };
   BOOST_CHECK_EQUAL(b->producer, "bob"_n);
   BOOST_CHECK_EQUAL(control->active_producers().version, 2u);
   BOOST_CHECK_EQUAL(compare_schedules(bob_sch, control->active_producers()), true);
} FC_LOG_AND_RETHROW()

// Large gap after a policy is proposed
BOOST_FIXTURE_TEST_CASE( large_gap_test, validating_tester ) try {
   create_accounts( {"alice"_n, "bob"_n} );
   auto b = produce_block();

   // round 1
   set_producers( {"alice"_n} );
   produce_blocks(config::producer_repetitions); // make sure to next round

   // round 2
   set_producers( {"bob"_n} );
   produce_block();
   produce_block(fc::hours(10));

   // far in the future, the latest proposed policy (bob) becomes active
   b = produce_block();
   vector<producer_authority> alice_sch = {
      producer_authority{
         "alice"_n,
         block_signing_authority_v0{
            1,
            {{get_public_key("alice"_n, "active"), 1}}}}
   };
   BOOST_CHECK_EQUAL(b->producer, "alice"_n);
   BOOST_CHECK_EQUAL(control->active_producers().version, 1u);
   BOOST_CHECK_EQUAL(compare_schedules(alice_sch, control->active_producers()), true);
} FC_LOG_AND_RETHROW()

// This is to verify the bug reported by https://github.com/AntelopeIO/spring/issues/454
// is fixed.
BOOST_FIXTURE_TEST_CASE( policy_transition_corner_case_test, validating_tester ) try {
   // In round 1, a block proposes a proposer policy.
   create_accounts( {"alice"_n} );
   set_producers( {"alice"_n} );
   auto b = produce_block();
   auto index = b->timestamp.slot % config::producer_repetitions; // current index in current round
   produce_blocks(config::producer_repetitions - index - 1); // to end of the round

   // In round 2, the block in the last time slot of the round is not present.
   produce_blocks(config::producer_repetitions - 1);

   // In round 3, there exists at least one block.
   // We need 2*config::block_interval_ms: one to skip to the last block
   // of round 2, and another to skip to the first block of round 3
   const auto time_to_skip = fc::milliseconds(2*config::block_interval_ms);
   b = produce_block(time_to_skip);
   
   vector<producer_authority> alice_sch = {
      producer_authority{
         "alice"_n,
         block_signing_authority_v0{
            1,
            {{get_public_key("alice"_n, "active"), 1}}}}
   };

   // Now alice's schedule should become active.
   // Make sure the first block of round 3 was produced by alice.
   BOOST_CHECK_EQUAL(b->producer, "alice"_n);
   BOOST_CHECK_EQUAL(control->active_producers().version, 1u);
   BOOST_CHECK_EQUAL(compare_schedules(alice_sch, control->active_producers()), true);
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
