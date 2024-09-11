#include <boost/test/unit_test.hpp>
#include <eosio/producer_plugin/production_pause_vote_tracker.hpp>

BOOST_AUTO_TEST_SUITE(production_pause_vote_tracker_tests)

BOOST_AUTO_TEST_CASE(test_production_pause) {
   // Setup production pause vote tracker:
   eosio::production_pause_vote_tracker vt{fc::milliseconds(250)}; // Use block acceptance threshold of 250 milliseconds.
   vt.set_vote_timeout(fc::milliseconds(6000));

   // Setup infrastructure for tracking current time:
   fc::time_point now(fc::milliseconds(1'000'000));
   auto tick = [&now](fc::microseconds duration = fc::microseconds(1000)) { now += duration; };

   // Helper functions to record events with checks for expectations
   auto got_block = [&now, &vt](bool expect_recorded = true) {
      bool recorded = vt.record_received_block(now, now);
      BOOST_TEST(recorded == expect_recorded);
   };
   enum vote_type { other, producer };
   auto got_vote = [&now, &vt](vote_type t, bool expect_recorded = true) {
      bool recorded = false;
      if (t == other) {
         recorded = vt.record_received_other_vote(now);
      } else {
         assert(t == producer);
         recorded = vt.record_received_producer_vote(now);
      }
      BOOST_TEST(recorded == expect_recorded);
   };

   // Run test:

   got_block();
   tick();
   got_vote(other);
   got_vote(producer);
   tick();
   got_block(false); // Block is dropped since it is too soon after getting both votes.
   tick(fc::milliseconds(998));
   got_block(); // This block is recorded since enough time has passed since getting the votes.
   tick();
   got_block(false); // Block is dropped since it is not relevant.
   tick(fc::milliseconds(5'999));
   BOOST_TEST(!vt.check_pause_status(now).should_pause()); // Still not paused though right at the boundary of pausing.
   tick(fc::microseconds(1));
   BOOST_TEST(vt.check_pause_status(now).should_pause()); // now paused
   tick(fc::microseconds(999));
   got_block(false); // Block is dropped since it is still not relevant.
   BOOST_TEST(vt.check_pause_status(now).should_pause()); // Still paused
   tick();
   got_vote(producer);
   BOOST_TEST(vt.check_pause_status(now).should_pause()); // Still paused
   tick();
   got_block(false);
   // Block is dropped since it is not relevant for other vote tracking and is too soon after receiving producer vote.
   tick(fc::milliseconds(250));
   got_block();
   // Block is recorded since while it is not relevant for other vote tracking it is relevant for producer vote tracking.
   BOOST_TEST(vt.check_pause_status(now).should_pause()); // Still paused
   tick(fc::milliseconds(7));
   got_vote(other);
   BOOST_TEST(!vt.check_pause_status(now).should_pause());
   // No longer paused because the only relevant recorded block is the one recorded 7 milliseconds ago. So it is not conflicting yet.
   tick(fc::milliseconds(5'993));
   BOOST_TEST(!vt.check_pause_status(now).should_pause()); // Still not yet paused
   tick(fc::microseconds(1));
   BOOST_TEST(vt.check_pause_status(now).should_pause()); // paused
   // But after waiting sufficient time, that relevant block becomes a conflicting block that forces a pause.
   tick(fc::microseconds(6'746'999));
   BOOST_TEST(vt.check_pause_status(now).should_pause()); // Still paused
   got_vote(other);
   BOOST_TEST(vt.check_pause_status(now).should_pause());
   // Still paused since getting a more recent other vote does not resolve the block conflict due to old producer vote.
   tick();
   got_vote(producer);
   BOOST_TEST(!vt.check_pause_status(now).should_pause()); // Now unpaused due to the most recent producer vote received.
}

BOOST_AUTO_TEST_SUITE_END()