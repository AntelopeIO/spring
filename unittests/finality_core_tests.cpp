#include <eosio/chain/finality_core.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/bitutil.hpp>
#include <boost/test/unit_test.hpp>

using namespace eosio::chain;

// ---------------------------------------------------------------------------------------
inline block_id_type calc_id(block_id_type id, uint32_t block_number) {
   id._hash[0] &= 0xffffffff00000000;
   id._hash[0] += fc::endian_reverse_u32(block_number);
   return id;
}

// ---------------------------------------------------------------------------------------
struct test_core {
   finality_core   core;
   block_time_type timestamp;

   test_core() {
      core = finality_core::create_core_for_genesis_block(
         calc_id(fc::sha256::hash("genesis"), 0), block_timestamp_type{0});

      // current block 0, next block 1
      next(qc_claim_t{.block_num = 0, .is_strong_qc = true});
      check_conditions(0); // block 1 -- last_final_block_num: 0

      // current block 1, next block 2
      next(qc_claim_t{.block_num = 1, .is_strong_qc = true});
      check_conditions(0); // block 2 -- last_final_block_num: 0 

      // current block 2, next block 3
      next(qc_claim_t{.block_num = 2, .is_strong_qc = true});
      check_conditions(1); // block 3 -- last_final_block_num: 1
   }

   void next(qc_claim_t qc_claim) {
      auto prev_block_num = core.current_block_num();
      timestamp = timestamp.next();
      auto id = id_from_num(prev_block_num);
      core = core.next(block_ref {id, timestamp, id, 1, 0}, qc_claim); // bogus generation numbers, but unused
      // next block num is previous  block number + 1, qc_claim becomes latest_qc_claim
      BOOST_REQUIRE_EQUAL(core.current_block_num(), prev_block_num + 1);
      BOOST_REQUIRE(core.latest_qc_claim() == qc_claim);
   }

   void check_conditions( block_num_type expected_last_final_block_num) {
      BOOST_REQUIRE_EQUAL(core.last_final_block_num(), expected_last_final_block_num);
   }

   // This function is intentionally simplified for tests here only.
   block_id_type id_from_num(block_num_type block_num) {
      block_id_type result;
      result._hash[0] &= 0xffffffff00000000;
      result._hash[0] += fc::endian_reverse_u32(block_num);
      return result;
   }
};

BOOST_AUTO_TEST_SUITE(finality_core_tests)

// Verify post conditions of IF genesis block core
BOOST_AUTO_TEST_CASE(create_core_for_genesis_block_test) { try {
   finality_core core = finality_core::create_core_for_genesis_block(
      calc_id(fc::sha256::hash("genesis"), 0), block_timestamp_type{0});

   BOOST_REQUIRE_EQUAL(core.current_block_num(), 0u);
   qc_claim_t qc_claim{.block_num=0, .is_strong_qc=false};
   BOOST_REQUIRE(core.latest_qc_claim() == qc_claim);
   BOOST_REQUIRE_EQUAL(core.last_final_block_num(), 0u);
} FC_LOG_AND_RETHROW() }

// a sequence of strong QCs work
BOOST_AUTO_TEST_CASE(strong_qc_claim_test) { try {
   {  // same QC claim
      test_core core;
      // current conditions of core:
      // current_block_num() == 3,
      // last_final_block_num() == 1,
      // latest qc_claim == {"block_num":2,"is_strong_qc":true}

      // Make the same strong QC claim as the latest qc_claim; nothing changes.
      core.next(qc_claim_t{.block_num = 2, .is_strong_qc = true });
      core.check_conditions(1);
   }
   { // new QC
      test_core core;

      // current_block_num() == 3
      // A strong QC claim on block 3 will advance LIB to 2
      core.next(qc_claim_t{.block_num = 3, .is_strong_qc = true });
      core.check_conditions(2);

      // A strong QC claim on block 4 will advance LIB to 3
      core.next(qc_claim_t{.block_num = 4, .is_strong_qc = true });
      core.check_conditions(3);
   }
} FC_LOG_AND_RETHROW() }

// verify repeated same strong QCs do not advance last_final_block_num
BOOST_AUTO_TEST_CASE(same_strong_qc_claim_test_1) { try {
   test_core core;
   // current conditions of core:
   // current_block_num() == 3,
   // last_final_block_num() == 1,
   // latest qc_claim == {"block_num":2,"is_strong_qc":true}
   // new chain: 2 <-- 3

   core.next(qc_claim_t{.block_num = 2, .is_strong_qc = true });
   // same QC claim on block 2 will not advance last_final_block_num
   // new chain: 2 <-- 4
   core.check_conditions(1);

   core.next(qc_claim_t{.block_num = 2, .is_strong_qc = true });
   // same QC claim on block 2 will not advance last_final_block_num
   // new chain: 2 <-- 5
   core.check_conditions(1);

   core.next(qc_claim_t{.block_num = 3, .is_strong_qc = true });
   // strong QC claim on block 3.
   // new chain: 3 <-- 6, two-chain: 2 <-- 3 <-- 6
   core.check_conditions(2);

   core.next(qc_claim_t{.block_num = 5, .is_strong_qc = true });
   // new chain: 5 <-- 7, two-chain: 2 <-- 5 <-- 7
   core.check_conditions(2);

   core.next(qc_claim_t{.block_num = 6, .is_strong_qc = true });
   // new chain: 6 <-- 8, two-chain: 3 <-- 6 <-- 8
   core.check_conditions(3);

   core.next(qc_claim_t{.block_num = 7, .is_strong_qc = true });
   // new chain: 7 <-- 9, two-chain: 5 <-- 7 <-- 9
   core.check_conditions(5);
} FC_LOG_AND_RETHROW() }

// A block is skipped from QC
BOOST_AUTO_TEST_CASE(same_strong_qc_claim_test_2) { try {
   test_core core;
   // current conditions of core:
   // current_block_num() == 3,
   // last_final_block_num() == 1,
   // latest qc_claim == {"block_num":2,"is_strong_qc":true}
   // new chain: 2 <-- 3

   // same QC claim on block 2 will not advance last_final_block_num
   core.next(qc_claim_t{.block_num = 2, .is_strong_qc = true });
   // new chain: 2 <-- 4
   core.check_conditions(1);

   // same QC claim on block 2 will not advance last_final_block_num
   core.next(qc_claim_t{.block_num = 2, .is_strong_qc = true });
   // new chain: 2 <-- 5
   core.check_conditions(1);

   // Skip qc claim on block 3. Make a strong QC claim on block 4.
   core.next(qc_claim_t{.block_num = 4, .is_strong_qc = true });
   // new chain: 4 <-- 6, two-chain: 2 <-- 4 <-- 6
   core.check_conditions(2);

   // A new qc claim advances last_final_block_num
   core.next(qc_claim_t{.block_num = 6, .is_strong_qc = true });
   // new chain: 6 <-- 7, two-chain: 4 <-- 6 <-- 7
   core.check_conditions(4);
} FC_LOG_AND_RETHROW() }

// A block is skipped from QC
BOOST_AUTO_TEST_CASE(same_strong_qc_claim_test_3) { try {
   test_core core;
   // current conditions of core:
   // current_block_num() == 3,
   // last_final_block_num() == 1,
   // latest qc_claim == {"block_num":2,"is_strong_qc":true}
   // new chain: 2 <-- 3

   // same QC claim on block 2 will not advance last_final_block_num
   core.next(qc_claim_t{.block_num = 2, .is_strong_qc = true });
   // new chain: 2 <-- 4
   core.check_conditions(1);

   // same QC claim on block 2 will not advance last_final_block_num
   core.next(qc_claim_t{.block_num = 2, .is_strong_qc = true });
   // new chain: 2 <-- 5
   core.check_conditions(1);

   // Skip qc claim on block 4. Make a strong QC claim on block 5.
   core.next(qc_claim_t{.block_num = 5, .is_strong_qc = true });
   // new chain: 5 <-- 6, two-chain: 2 <-- 5 <-- 6
   core.check_conditions(2);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(same_weak_qc_claim_test_1) { try {
   test_core core;
   // current conditions of core:
   // current_block_num() == 3,
   // latest qc_claim == {"block_num":2,"is_strong_qc":true}
   // new chain: 2 <-- 3

   // weak QC claim on block 3; nothing changes
   core.next(qc_claim_t{.block_num = 3, .is_strong_qc = false });
   // new chain: 3 <--w 4 (w indicates weak)
   core.check_conditions(1);

   // same weak QC claim on block 3; nothing changes
   core.next(qc_claim_t{.block_num = 3, .is_strong_qc = false });
   // new chain: 3 <--w 5
   core.check_conditions(1);

   // same weak QC claim on block 3; nothing changes
   core.next(qc_claim_t{.block_num = 3, .is_strong_qc = false });
   // new chain: 3 <--w 6
   core.check_conditions(1);

   // strong QC claim on block 3
   core.next(qc_claim_t{.block_num = 3, .is_strong_qc = true });
   // new chain: 3 <-- 7, two-chain: 2 <-- 3 <-- 7
   core.check_conditions(2);

   core.next(qc_claim_t{.block_num = 4, .is_strong_qc = true });
   // new chain: 4 <-- 8, two-chain: 3 <-- 4 <-- 8
   core.check_conditions(3);

   core.next(qc_claim_t{.block_num = 6, .is_strong_qc = true });
   // new chain: 6 <-- 9, two-chain: 3 <-- 6 <-- 9
   core.check_conditions(3);

   core.next(qc_claim_t{.block_num = 7, .is_strong_qc = true });
   // new chain: 7 <-- 10, two-chain: 3 <-- 7 <-- 10
   core.check_conditions(3);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(same_weak_qc_claim_test_2) { try {
   test_core core;
   // current conditions of core:
   // current_block_num() == 3,
   // latest qc_claim == {"block_num":2,"is_strong_qc":true}
   // new chain: 2 <-- 3

   // weak QC claim on block 3; nothing changes
   core.next(qc_claim_t{.block_num = 3, .is_strong_qc = false });
   // new chain: 3 <--w 4 (w indicates weak)
   core.check_conditions(1);

   // same weak QC claim on block 3; nothing changes
   core.next(qc_claim_t{.block_num = 3, .is_strong_qc = false });
   // new chain: 3 <--w 5
   core.check_conditions(1);

   // same weak QC claim on block 3; nothing changes
   core.next(qc_claim_t{.block_num = 3, .is_strong_qc = false });
   // new chain: 3 <--w 6
   core.check_conditions(1);

   // strong QC claim on block 4
   core.next(qc_claim_t{.block_num = 4, .is_strong_qc = true });
   // new chain: 4 <-- 7, two-chain: 3 <-- 4 <-- 7
   core.check_conditions(3);

   core.next(qc_claim_t{.block_num = 5, .is_strong_qc = true });
   // new chain: 3 <-- 8, two-chain: 3 <-- 5 <-- 8
   core.check_conditions(3);

   core.next(qc_claim_t{.block_num = 6, .is_strong_qc = true });
   // new chain: 6 <-- 9, two-chain: 3 <-- 6 <-- 9
   core.check_conditions(3);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(same_weak_qc_claim_test_3) { try {
   test_core core;
   // current conditions of core:
   // current_block_num() == 3,
   // latest qc_claim == {"block_num":2,"is_strong_qc":true}
   // new chain: 2 <-- 3

   // weak QC claim on block 3; nothing changes
   core.next(qc_claim_t{.block_num = 3, .is_strong_qc = false });
   // new chain: 3 <--w 4 (w indicates weak)
   core.check_conditions(1);

   // same weak QC claim on block 3; nothing changes
   core.next(qc_claim_t{.block_num = 3, .is_strong_qc = false });
   // new chain: 3 <--w 5
   core.check_conditions(1);

   // same weak QC claim on block 3; nothing changes
   core.next(qc_claim_t{.block_num = 3, .is_strong_qc = false });
   // new chain: 3 <--w 6
   core.check_conditions(1);

   // strong QC claim on block 5
   core.next(qc_claim_t{.block_num = 5, .is_strong_qc = true });
   // new chain: 5 <-- 7, two-chain: 3 <-- 5 <-- 7
   core.check_conditions(3);

   core.next(qc_claim_t{.block_num = 6, .is_strong_qc = true });
   // new chain: 6 <-- 8, two-chain: 3 <-- 6 <-- 8
   core.check_conditions(3);

   core.next(qc_claim_t{.block_num = 7, .is_strong_qc = true });
   // new chain: 7 <-- 9, two-chain: 5 <-- 7 <-- 9
   core.check_conditions(5);

   core.next(qc_claim_t{.block_num = 8, .is_strong_qc = true });
   // new chain: 8 <-- 10, two-chain: 6 <-- 8 <-- 10
   core.check_conditions(6);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(interwined_strong_and_weak_test) { try {
   test_core core;
   // current conditions of core:
   // current_block_num() == 3,
   // latest qc_claim == {"block_num":2,"is_strong_qc":true}
   // new chain: 2 <-- 3

   // weak QC claim on block 3, finality does not change
   core.next(qc_claim_t{.block_num = 3, .is_strong_qc = false });
   // new chain: 3 <--w 4 (w indicates weak)
   core.check_conditions(1);

   // strong QC claim on block 4
   core.next(qc_claim_t{.block_num = 4, .is_strong_qc = true });
   // new chain: 4 <-- 5, , two-chain: 3 <--w 4 <-- 5
   core.check_conditions(3);

   // weak QC claim on block 5, finality does not change
   core.next(qc_claim_t{.block_num = 5, .is_strong_qc = false });
   // new chain: 5 <--w 6
   core.check_conditions(3);

   // strong QC claim on block 6
   core.next(qc_claim_t{.block_num = 6, .is_strong_qc = true });
   // new chain: 6 <-- 7, two-chain: 5 <--w 6 <-- 7
   core.check_conditions(5);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
