#include <eosio/chain/fork_database.hpp>
#include <eosio/chain/finalizer.hpp>

#include <boost/test/unit_test.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/testing/bls_utils.hpp>
#include <fc/bitutil.hpp>

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;

using bs            = eosio::chain::block_state;
using bsp           = eosio::chain::block_state_ptr;
using bhs           = eosio::chain::block_header_state;
using bhsp          = eosio::chain::block_header_state_ptr;
using vote_decision = finalizer::vote_decision;
using vote_result   = finalizer::vote_result;
using tstamp        = block_timestamp_type;
using fsi_t         = finalizer_safety_information;

// ---------------------------------------------------------------------------------------
// Used to access privates of block_state
namespace eosio::chain {
   struct test_block_state_accessor {
      static void set_valid(block_state_ptr& bsp, bool v) {
         bsp->set_valid(v);
      }

      static bool is_valid(const block_state_ptr& bsp) {
         return bsp->is_valid();
      }
   };
}

// ---------------------------------------------------------------------------------------
struct bls_keys_t {
   bls_private_key privkey;
   bls_public_key  pubkey;
   std::string     privkey_str;
   std::string     pubkey_str;

   bls_keys_t(name n) {
      bls_signature pop;
      std::tie(privkey, pubkey, pop)    = eosio::testing::get_bls_key(n);
      std::tie(privkey_str, pubkey_str) = std::pair{ privkey.to_string(), pubkey.to_string() };
   }
};

// ---------------------------------------------------------------------------------------
inline block_id_type calc_id(block_id_type id, uint32_t block_number) {
   id._hash[0] &= 0xffffffff00000000;
   id._hash[0] += fc::endian_reverse_u32(block_number);
   return id;
}

// ---------------------------------------------------------------------------------------
struct proposal_t {
   uint32_t             block_number;
   std::string          proposer_name;
   block_timestamp_type block_timestamp;

   proposal_t(uint32_t block_number, const char* proposer, std::optional<uint32_t> timestamp = {}) :
      block_number(block_number), proposer_name(proposer), block_timestamp(timestamp ? *timestamp : block_number)
   {}

   const std::string&   proposer()  const { return proposer_name; }
   block_timestamp_type timestamp() const { return block_timestamp; }
   uint32_t             block_num() const { return block_number; }

   block_id_type calculate_id() const
   {
      std::string   id_str = proposer_name + std::to_string(block_number);
      return calc_id(fc::sha256::hash(id_str.c_str()), block_number);
   }

   explicit operator block_ref() const {
      auto id = calculate_id();
      // we use bogus generation numbers in `block_ref` constructor, but these are unused in the test
      return block_ref{id, timestamp(), id, 1, 0}; // reuse id for the finality_digest which is not used in this test
   }
};

// ---------------------------------------------------------------------------------------
bsp make_bsp(const proposal_t& p, const bsp& previous, finalizer_policy_ptr finpol,
             std::optional<qc_claim_t> claim = {}) {
   auto makeit = [](bhs &&h) {
      bs new_bs;
      dynamic_cast<bhs&>(new_bs) = std::move(h);
      return std::make_shared<bs>(std::move(new_bs));
   };

   if (p.block_num() == 0) {
      // special case of genesis block
      auto id = calc_id(fc::sha256::hash("genesis"), 0);
      auto tstamp = block_timestamp_type{0};
      bhs new_bhs { {}, id, block_header{tstamp}, {}, finality_core::create_core_for_genesis_block(id, tstamp),
                    std::move(finpol), std::make_shared<proposer_policy>() };
      return makeit(std::move(new_bhs));
   }

   assert(claim);
   block_ref ref = previous ? previous->make_block_ref() : block_ref{};
   bhs new_bhs { {}, p.calculate_id(), block_header{p.block_timestamp, {}, {}, previous->id()}, {}, previous->core.next(ref, *claim),
      std::move(finpol), std::make_shared<proposer_policy>() }; // proposer_policy needed for make_block_ref
   return makeit(std::move(new_bhs));
}

// ---------------------------------------------------------------------------------------
// simulates one finalizer voting on its own proposals "n0", and other proposals received
// from the network.
struct simulator_t {
   using core = finality_core;

   bls_keys_t           keys;
   finalizer            my_finalizer;
   fork_database_if_t   fork_db;
   finalizer_policy_ptr finpol;
   std::vector<bsp>     bsp_vec;

   struct result {
      bsp         new_bsp;
      vote_result vote;

      qc_claim_t new_claim() const {
         if (vote.decision == vote_decision::no_vote)
            return new_bsp->core.latest_qc_claim();
         return { new_bsp->block_num(), vote.decision == vote_decision::strong_vote };
      }
   };

   simulator_t() :
      keys("alice"_n),
      my_finalizer(keys.privkey, finalizer_safety_information{}) {

      finalizer_policy fin_policy;
      fin_policy.threshold = 1;
      fin_policy.generation = 1;
      fin_policy.finalizers.push_back({"n0", 1, keys.pubkey});
      finpol = std::make_shared<finalizer_policy>(fin_policy);

      auto genesis = make_bsp(proposal_t{0, "n0"}, bsp(), finpol);
      bsp_vec.push_back(genesis);
      fork_db.reset_root(genesis);

      block_ref genesis_ref(genesis->id(), genesis->timestamp(), genesis->id(), 1, 0);
      my_finalizer.fsi = fsi_t{genesis_ref, genesis_ref, {}};
   }

   vote_result vote(const bsp& p) {
      auto vote_res = my_finalizer.decide_vote(p);
      return vote_res;
   }

   vote_result propose(const proposal_t& p, std::optional<qc_claim_t> _claim = {}) {
      bsp h = fork_db.head(include_root_t::yes);
      qc_claim_t old_claim = _claim ? *_claim : h->core.latest_qc_claim();
      bsp new_bsp = make_bsp(p, h, finpol, old_claim);
      bsp_vec.push_back(new_bsp);
      auto v = vote(new_bsp);
      return v;
   }

   result add(const proposal_t& p, std::optional<qc_claim_t> _claim = {}, const bsp& parent = {}) {
      bsp h = parent ? parent : fork_db.head(include_root_t::yes);
      qc_claim_t old_claim = _claim ? *_claim : h->core.latest_qc_claim();
      bsp new_bsp = make_bsp(p, h, finpol, old_claim);
      bsp_vec.push_back(new_bsp);
      test_block_state_accessor::set_valid(new_bsp, true);
      fork_db.add(new_bsp, ignore_duplicate_t::no);

      auto v = vote(new_bsp);
      return { new_bsp, v };
   }
};

BOOST_AUTO_TEST_SUITE(finalizer_vote_tests)

// ---------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE( decide_vote_basic ) try {
   simulator_t sim;
   // this proposal verifies all properties and extends genesis => expect strong vote
   auto res = sim.add({1, "n0"});
   BOOST_CHECK(res.vote.decision == vote_decision::strong_vote);
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE( decide_vote_no_vote_if_finalizer_safety_lock_empty ) try {
   simulator_t sim;
   sim.my_finalizer.fsi.lock = {};    // force lock empty... finalizer should not vote
   auto res = sim.add({1, "n0"});
   BOOST_CHECK(res.vote.decision == vote_decision::no_vote);
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE( decide_vote_normal_vote_sequence ) try {
   simulator_t sim;
   qc_claim_t new_claim { 0, true };
   for (uint32_t i=1; i<10; ++i) {
      auto res = sim.add({i, "n0"}, new_claim);
      BOOST_CHECK(res.vote.decision == vote_decision::strong_vote);
      BOOST_CHECK_EQUAL(new_claim, res.new_bsp->core.latest_qc_claim());
      new_claim = { res.new_bsp->block_num(), res.vote.decision == vote_decision::strong_vote };

      auto lib { res.new_bsp->core.last_final_block_num() };
      BOOST_CHECK_EQUAL(lib, i <= num_chains_to_final - 1 ? 0 : i - num_chains_to_final);
   }
} FC_LOG_AND_RETHROW()

// ---------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE( decide_vote_monotony_check ) try {
   simulator_t sim;

   auto res = sim.add({1, "n0", 1});
   BOOST_CHECK(res.vote.decision == vote_decision::strong_vote);

   auto res2 = sim.add({2, "n0", 1});
   BOOST_CHECK_EQUAL(res2.vote.monotony_check, false);
   BOOST_CHECK(res2.vote.decision == vote_decision::no_vote); // use same timestamp as previous proposal => should not vote

} FC_LOG_AND_RETHROW()


// ---------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE( decide_vote_liveness_and_safety_check ) try {
   simulator_t sim;
   qc_claim_t new_claim { 0, true };
   for (uint32_t i=1; i<10; ++i) {
      auto res = sim.add({i, "n0", i}, new_claim);
      BOOST_CHECK(res.vote.decision == vote_decision::strong_vote);
      BOOST_CHECK_EQUAL(new_claim, res.new_bsp->core.latest_qc_claim());
      new_claim = res.new_claim();

      auto lib { res.new_bsp->core.last_final_block_num() };
      BOOST_CHECK_EQUAL(lib, i <= num_chains_to_final - 1 ? 0 : i - num_chains_to_final);

      if (i > (num_chains_to_final - 1))
         BOOST_CHECK_EQUAL(sim.my_finalizer.fsi.lock.block_id, sim.bsp_vec[i-(num_chains_to_final-1)]->id());
   }

   // we just issued proposal #9. Verify we are locked on proposal #8 and our last_vote is #9
   BOOST_CHECK_EQUAL(sim.my_finalizer.fsi.lock.block_id, sim.bsp_vec[8]->id());
   BOOST_CHECK_EQUAL(block_header::num_from_id(sim.my_finalizer.fsi.last_vote.block_id), 9u);

   // proposal #6 from "n0" is final (although "n1" may not know it yet).
   // proposal #7 would be final if it receives a strong QC

   /// let's have "n1" build on proposal #7. Default will use timestamp(8) so we will fail the monotony check
   auto res = sim.add({8, "n1"}, {}, sim.bsp_vec[7]);
   BOOST_CHECK(res.vote.decision == vote_decision::no_vote);
   BOOST_CHECK_EQUAL(res.vote.monotony_check, false);

   // let's vote for a couple more proposals, and finally when we'll reach timestamp 10 the
   // monotony check will pass (both liveness and safety check should still fail)
   // ------------------------------------------------------------------------------------
   res = sim.add({9, "n1"}, {}, res.new_bsp);
   BOOST_CHECK_EQUAL(res.vote.monotony_check, false);

   res = sim.add({10, "n1"}, {}, res.new_bsp);
   BOOST_CHECK_EQUAL(res.vote.monotony_check, true);

   res = sim.add({11, "n1"}, {}, res.new_bsp);
   BOOST_CHECK(res.vote.decision == vote_decision::no_vote);
   BOOST_CHECK_EQUAL(res.vote.monotony_check, true);
   BOOST_CHECK_EQUAL(res.vote.liveness_check, false);
   BOOST_CHECK_EQUAL(res.vote.safety_check, false);

   // No matter how long we keep voting on this branch without a new qc claim, we will never achieve
   // liveness or safety again
   // ----------------------------------------------------------------------------------------------
   for (uint32_t i=12; i<20; ++i) {
      res = sim.add({i, "n1"}, {}, res.new_bsp);

      BOOST_CHECK(res.vote.decision == vote_decision::no_vote);
      BOOST_CHECK_EQUAL(res.vote.monotony_check, true);
      BOOST_CHECK_EQUAL(res.vote.liveness_check, false);
      BOOST_CHECK_EQUAL(res.vote.safety_check,   false);
   }

   // Now suppose we receive a qc in a block that was created in the "n0" branch, for example the qc from
   // proposal 8. We can get it from sim.bsp_vec[9]->core.latest_qc_claim().
   // liveness should be restored, because core.latest_qc_block_timestamp() > fsi.lock.timestamp
   // ---------------------------------------------------------------------------------------------------
   BOOST_CHECK_EQUAL(block_header::num_from_id(sim.my_finalizer.fsi.last_vote.block_id), 9u);
   new_claim = sim.bsp_vec[9]->core.latest_qc_claim();
   res = sim.add({20, "n1"}, qc_claim_t{9, true}, res.new_bsp);

   BOOST_CHECK(res.vote.decision == vote_decision::strong_vote);
   BOOST_CHECK_EQUAL(block_header::num_from_id(sim.my_finalizer.fsi.last_vote.block_id), 20u);
   BOOST_CHECK_EQUAL(res.vote.monotony_check, true);
   BOOST_CHECK_EQUAL(res.vote.liveness_check, true);
   BOOST_CHECK_EQUAL(res.vote.safety_check, false); // because liveness_check is true, safety is not checked.

   new_claim = res.new_claim();
   res = sim.add({21, "n1"}, new_claim, res.new_bsp);
   BOOST_CHECK(res.vote.decision == vote_decision::strong_vote); // because core.extends(fsi.last_vote.block_id);
   BOOST_CHECK_EQUAL(block_header::num_from_id(sim.my_finalizer.fsi.last_vote.block_id), 21u);
   BOOST_CHECK_EQUAL(res.vote.monotony_check, true);
   BOOST_CHECK_EQUAL(res.vote.liveness_check, true);
   BOOST_CHECK_EQUAL(res.vote.safety_check, false); // because liveness_check is true, safety is not checked.

   BOOST_CHECK_EQUAL(sim.my_finalizer.fsi.lock.block_id, sim.bsp_vec[22]->id());

   // this new strong vote will finally advance the last_final_block_num thanks to the chain
   // 20 - 21 - 22 (meaning that if we get a strong QC on 22, 20 becomes final)
   // -----------------------------------------------------------------------------------------------
   new_claim = res.new_claim();
   res = sim.add({22, "n1"}, new_claim, res.new_bsp);
   BOOST_CHECK(res.vote.decision == vote_decision::strong_vote);
   BOOST_CHECK_EQUAL(block_header::num_from_id(sim.my_finalizer.fsi.last_vote.block_id), 22u);
   BOOST_CHECK_EQUAL(res.vote.monotony_check, true);
   BOOST_CHECK_EQUAL(res.vote.liveness_check, true);
   BOOST_CHECK_EQUAL(res.vote.safety_check, false); // because liveness_check is true, safety is not checked.
   BOOST_CHECK_EQUAL(res.new_bsp->core.last_final_block_num(), 20u);

   // OK, add one proposal + strong vote. This should finally move lib to 20
   // ----------------------------------------------------------------------
   new_claim = res.new_claim();
   res = sim.add({23, "n1"}, new_claim, res.new_bsp);
   BOOST_CHECK(res.vote.decision == vote_decision::strong_vote);
   BOOST_CHECK_EQUAL(block_header::num_from_id(sim.my_finalizer.fsi.last_vote.block_id), 23u);
   BOOST_CHECK_EQUAL(res.vote.monotony_check, true);
   BOOST_CHECK_EQUAL(res.vote.liveness_check, true);
   BOOST_CHECK_EQUAL(res.vote.safety_check, false); // because liveness_check is true, safety is not checked.
   BOOST_CHECK_EQUAL(res.new_bsp->core.last_final_block_num(), 21u);

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_SUITE_END()
