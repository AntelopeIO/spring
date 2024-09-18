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
      root->active_finalizer_policy = std::make_shared<finalizer_policy>(); // needed for make_block_ref
      root->active_proposer_policy = std::make_shared<proposer_policy>();   // needed for make_block_ref
      root->core = finality_core::create_core_for_genesis_block(root->block_id, root->header.timestamp);
      return root;
   }

   // use block_num > 10
   static auto make_unique_block_state(block_num_type block_num, const block_state_ptr& prev) {
      block_state_ptr bsp = std::make_shared<block_state>();
      bsp->block_id = make_block_id(block_num);
      bsp->header.timestamp.slot = prev->header.timestamp.slot + 1;
      bsp->header.previous = prev->id();
      bsp->active_finalizer_policy = std::make_shared<finalizer_policy>(); // needed for make_block_ref
      bsp->active_proposer_policy = std::make_shared<proposer_policy>();   // needed for make_block_ref
      bsp->core = prev->core.next(prev->make_block_ref(), prev->core.latest_qc_claim());
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

struct generate_forkdb_state {
   generate_forkdb_state() {
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
   }

   fork_database_if_t forkdb;

   // Setup fork database with blocks based on a root of block 10
   // Add a number of forks in the fork database
   block_state_ptr root = test_block_state_accessor::make_genesis_block_state();
   block_state_ptr   bsp11a = test_block_state_accessor::make_unique_block_state(11, root);
   block_state_ptr     bsp12a = test_block_state_accessor::make_unique_block_state(12, bsp11a);
   block_state_ptr       bsp13a = test_block_state_accessor::make_unique_block_state(13, bsp12a);
   block_state_ptr   bsp11b = test_block_state_accessor::make_unique_block_state(11, root);
   block_state_ptr     bsp12b = test_block_state_accessor::make_unique_block_state(12, bsp11b);
   block_state_ptr       bsp13b = test_block_state_accessor::make_unique_block_state(13, bsp12b);
   block_state_ptr         bsp14b = test_block_state_accessor::make_unique_block_state(14, bsp13b);
   block_state_ptr     bsp12bb = test_block_state_accessor::make_unique_block_state(12, bsp11b);
   block_state_ptr       bsp13bb = test_block_state_accessor::make_unique_block_state(13, bsp12bb);
   block_state_ptr       bsp13bbb = test_block_state_accessor::make_unique_block_state(13, bsp12bb);
   block_state_ptr     bsp12bbb = test_block_state_accessor::make_unique_block_state(12, bsp11b);
   block_state_ptr   bsp11c = test_block_state_accessor::make_unique_block_state(11, root);
   block_state_ptr     bsp12c = test_block_state_accessor::make_unique_block_state(12, bsp11c);
   block_state_ptr       bsp13c = test_block_state_accessor::make_unique_block_state(13, bsp12c);

   // keep track of all those added for easy verification
   std::vector<block_state_ptr> all{bsp11a, bsp12a,  bsp13a,   bsp11b, bsp12b, bsp12bb, bsp12bbb,
                                    bsp13b, bsp13bb, bsp13bbb, bsp14b, bsp11c, bsp12c,  bsp13c};
};


BOOST_AUTO_TEST_SUITE(fork_database_tests)

BOOST_FIXTURE_TEST_CASE(add_remove_test, generate_forkdb_state) try {
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


// test `fork_database_t::validated_block_exists() const` member
// -------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(validated_block_exists, generate_forkdb_state) try {

   // if a block is valid in fork_db, all its ancestors are necessarily valid.
   root->set_valid(true);
   bsp11b->set_valid(true);
   bsp12b->set_valid(true);
   bsp13b->set_valid(true);
   bsp14b->set_valid(true);

   bsp13a->set_valid(false);

   BOOST_REQUIRE_EQUAL(true,  forkdb.validated_block_exists(bsp14b->id(), bsp14b->id()));
   BOOST_REQUIRE_EQUAL(true,  forkdb.validated_block_exists(bsp14b->id(), bsp13b->id()));
   BOOST_REQUIRE_EQUAL(true,  forkdb.validated_block_exists(bsp14b->id(), bsp12b->id()));
   BOOST_REQUIRE_EQUAL(true,  forkdb.validated_block_exists(bsp14b->id(), bsp11b->id()));

   bsp14b->set_valid(false);
   BOOST_REQUIRE_EQUAL(false, forkdb.validated_block_exists(bsp14b->id(), bsp14b->id()));
   BOOST_REQUIRE_EQUAL(true,  forkdb.validated_block_exists(bsp14b->id(), bsp13b->id()));
   BOOST_REQUIRE_EQUAL(true,  forkdb.validated_block_exists(bsp14b->id(), bsp12b->id()));
   BOOST_REQUIRE_EQUAL(true,  forkdb.validated_block_exists(bsp14b->id(), bsp11b->id()));

   bsp13b->set_valid(false);
   BOOST_REQUIRE_EQUAL(false, forkdb.validated_block_exists(bsp14b->id(), bsp14b->id()));
   BOOST_REQUIRE_EQUAL(false, forkdb.validated_block_exists(bsp14b->id(), bsp13b->id()));
   BOOST_REQUIRE_EQUAL(true,  forkdb.validated_block_exists(bsp14b->id(), bsp12b->id()));
   BOOST_REQUIRE_EQUAL(true,  forkdb.validated_block_exists(bsp14b->id(), bsp11b->id()));

   bsp12b->set_valid(false);
   BOOST_REQUIRE_EQUAL(false, forkdb.validated_block_exists(bsp14b->id(), bsp14b->id()));
   BOOST_REQUIRE_EQUAL(false, forkdb.validated_block_exists(bsp14b->id(), bsp13b->id()));
   BOOST_REQUIRE_EQUAL(false, forkdb.validated_block_exists(bsp14b->id(), bsp12b->id()));
   BOOST_REQUIRE_EQUAL(true,  forkdb.validated_block_exists(bsp14b->id(), bsp11b->id()));

   bsp11b->set_valid(false);
   BOOST_REQUIRE_EQUAL(false, forkdb.validated_block_exists(bsp14b->id(), bsp14b->id()));
   BOOST_REQUIRE_EQUAL(false, forkdb.validated_block_exists(bsp14b->id(), bsp13b->id()));
   BOOST_REQUIRE_EQUAL(false, forkdb.validated_block_exists(bsp14b->id(), bsp12b->id()));
   BOOST_REQUIRE_EQUAL(false, forkdb.validated_block_exists(bsp14b->id(), bsp11b->id()));

   BOOST_REQUIRE_EQUAL(true,  forkdb.validated_block_exists(bsp14b->id(), root->id()));
   BOOST_REQUIRE_EQUAL(true,  forkdb.validated_block_exists(bsp14b->id(), block_id_type{}));

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
