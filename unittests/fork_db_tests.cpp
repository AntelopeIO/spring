#include <eosio/chain/types.hpp>
#include <eosio/chain/fork_database.hpp>
#include <eosio/testing/tester.hpp>
#include <fc/bitutil.hpp>
#include <boost/test/unit_test.hpp>


namespace eosio::chain {

inline block_id_type make_block_id(block_num_type block_num) {
   static uint32_t nonce = 0;
   ++nonce;
   block_id_type id = fc::sha256::hash(std::to_string(block_num) + "-" + std::to_string(nonce));
   id._hash[0] &= 0xffffffff00000000;
   id._hash[0] += fc::endian_reverse_u32(block_num); // store the block num in the ID, 160 bits is plenty for the hash
   return id;
}

// Used to access privates of block_state
struct test_block_state_accessor {
   static auto make_genesis_block_state() {
      block_state_ptr root = std::make_shared<block_state>();
      block_id_type genesis_id = make_block_id(10);
      root->block_id = genesis_id;
      root->header.timestamp = block_timestamp_type{10};
      root->core = finality_core::create_core_for_genesis_block(block_ref{ root->block_id, root->header.timestamp });
      return root;
   }

   // use block_num > 10
   static auto make_unique_block_state(block_num_type block_num, const block_state_ptr& prev) {
      block_state_ptr bsp = std::make_shared<block_state>();
      bsp->block_id = make_block_id(block_num);
      bsp->header.timestamp.slot = prev->header.timestamp.slot + 1;
      bsp->header.previous = prev->id();
      block_ref prev_block {
         .block_id  = prev->id(),
         .timestamp = prev->timestamp()
      };
      bsp->core = prev->core.next(prev_block, prev->core.latest_qc_claim());
      return bsp;
   }

   static void reset_valid(block_state_ptr& bsp) {
      bsp->set_valid(false);
   }

   static bool is_valid(const block_state_ptr& bsp) {
      return bsp->is_valid();
   }
};

} // namespace eosio::chain

using namespace eosio::chain;

BOOST_AUTO_TEST_SUITE(fork_database_tests)

BOOST_AUTO_TEST_CASE(add_remove_test) try {
   fork_database_if_t forkdb;

   // Setup fork database with blocks based on a root of block 10
   // Add a number of forks in the fork database
   auto root = test_block_state_accessor::make_genesis_block_state();
   auto   bsp11a = test_block_state_accessor::make_unique_block_state(11, root);
   auto     bsp12a = test_block_state_accessor::make_unique_block_state(12, bsp11a);
   auto       bsp13a = test_block_state_accessor::make_unique_block_state(13, bsp12a);
   auto   bsp11b = test_block_state_accessor::make_unique_block_state(11, root);
   auto     bsp12b = test_block_state_accessor::make_unique_block_state(12, bsp11b);
   auto       bsp13b = test_block_state_accessor::make_unique_block_state(13, bsp12b);
   auto         bsp14b = test_block_state_accessor::make_unique_block_state(14, bsp13b);
   auto     bsp12bb = test_block_state_accessor::make_unique_block_state(12, bsp11b);
   auto       bsp13bb = test_block_state_accessor::make_unique_block_state(13, bsp12bb);
   auto       bsp13bbb = test_block_state_accessor::make_unique_block_state(13, bsp12bb);
   auto     bsp12bbb = test_block_state_accessor::make_unique_block_state(12, bsp11b);
   auto   bsp11c = test_block_state_accessor::make_unique_block_state(11, root);
   auto     bsp12c = test_block_state_accessor::make_unique_block_state(12, bsp11c);
   auto       bsp13c = test_block_state_accessor::make_unique_block_state(13, bsp12c);

   // keep track of all those added for easy verification
   std::vector<block_state_ptr> all { bsp11a, bsp12a, bsp13a, bsp11b, bsp12b, bsp12bb, bsp12bbb, bsp13b, bsp13bb, bsp13bbb, bsp14b, bsp11c, bsp12c, bsp13c };

   auto reset = [&]() {
      forkdb.reset_root(root);
      forkdb.add(bsp11a, ignore_duplicate_t::no);
      forkdb.add(bsp11b, ignore_duplicate_t::no);
      forkdb.add(bsp11c, ignore_duplicate_t::no);
      forkdb.add(bsp12a, ignore_duplicate_t::no);
      forkdb.add(bsp13a, ignore_duplicate_t::no);
      forkdb.add(bsp12b, ignore_duplicate_t::no);
      forkdb.add(bsp12bb, ignore_duplicate_t::no);
      forkdb.add(bsp12bbb, ignore_duplicate_t::no);
      forkdb.add(bsp12c, ignore_duplicate_t::no);
      forkdb.add(bsp13b, ignore_duplicate_t::no);
      forkdb.add(bsp13bb, ignore_duplicate_t::no);
      forkdb.add(bsp13bbb, ignore_duplicate_t::no);
      forkdb.add(bsp14b, ignore_duplicate_t::no);
      forkdb.add(bsp13c, ignore_duplicate_t::no);
   };
   reset();

   // test get_block
   for (auto& i : all) {
      BOOST_TEST(forkdb.get_block(i->id()) == i);
   }

   // test remove, should remove descendants
   forkdb.remove(bsp12b->id());
   BOOST_TEST(!forkdb.get_block(bsp12b->id()));
   BOOST_TEST(!forkdb.get_block(bsp13b->id()));
   BOOST_TEST(!forkdb.get_block(bsp14b->id()));
   forkdb.add(bsp12b, ignore_duplicate_t::no); // will throw if already exists
   forkdb.add(bsp13b, ignore_duplicate_t::no); // will throw if already exists
   forkdb.add(bsp14b, ignore_duplicate_t::no); // will throw if already exists

   // test search
   BOOST_TEST(forkdb.search_on_branch( bsp13bb->id(), 11) == bsp11b);
   BOOST_TEST(forkdb.search_on_branch( bsp13bb->id(), 9) == block_state_ptr{});

   // test fetch branch
   auto branch = forkdb.fetch_branch( bsp13b->id(), 12);
   BOOST_REQUIRE(branch.size() == 2);
   BOOST_TEST(branch[0] == bsp12b);
   BOOST_TEST(branch[1] == bsp11b);
   branch = forkdb.fetch_branch( bsp13bbb->id(), 13);
   BOOST_REQUIRE(branch.size() == 3);
   BOOST_TEST(branch[0] == bsp13bbb);
   BOOST_TEST(branch[1] == bsp12bb);
   BOOST_TEST(branch[2] == bsp11b);

   // test fetch branch providing head and lib
   branch = forkdb.fetch_branch(bsp13a->id(), bsp11c->id());
   BOOST_TEST(branch.empty()); // bsp11c not on bsp13a branch
   branch = forkdb.fetch_branch(bsp13a->id(), bsp12a->id());
   BOOST_REQUIRE(branch.size() == 2);
   BOOST_TEST(branch[0] == bsp12a);
   BOOST_TEST(branch[1] == bsp11a);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
