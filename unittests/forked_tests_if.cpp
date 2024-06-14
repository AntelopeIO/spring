#include "savanna_cluster.hpp"

#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/fork_database.hpp>

using namespace eosio::chain;
using namespace eosio::testing;

// ---------------------------------------------------
// Following tests in this file are for Savanna only:
//    - fork_with_bad_block
//    - forking
//    - prune_remove_branch
//    - irreversible_mode
//    - push_block_returns_forked_transactions
//
// Similar Legacy tests are in: `forked_tests.cpp`
// ---------------------------------------------------

static digest_type hash_pair(const digest_type& a, const digest_type& b) {
   return digest_type::hash(std::pair<const digest_type&, const digest_type&>(a, b));
}

BOOST_AUTO_TEST_SUITE(forked_tests_if)

// ---------------------------- fork_with_bad_block ---------------------------------
BOOST_FIXTURE_TEST_CASE(fork_with_bad_block_if, savanna_cluster::cluster_t) try {
   struct fork_tracker {
      vector<signed_block_ptr>           blocks;
      incremental_merkle_tree_legacy     block_merkle;
   };

   node0.produce_block();

   const vector<account_name> producers {"a"_n, "b"_n, "c"_n, "d"_n, "e"_n};
   auto prod = set_producers(0, producers);              // set new producers and produce blocks until the switch is pending

   auto sb = node0.produce_block();                      // now the next block produced on any node
   BOOST_REQUIRE_EQUAL(sb->producer, producers[prod]);   // should be produced by the producer returned by `set_producers`

                                                         // split the network. Finality will stop advancing as
                                                         // votes and blocks are not propagated.
   const std::vector<size_t> partition {2, 3};
   set_partition(partition);                             // simulate 2 disconnected partitions:  nodes {0, 1} and nodes {2, 3}

                                                         // at this point, each node has a QC to include into
                                                         // the next block it produces which will advance lib.

   // First produce 6 blocks on node0
   // -------------------------------
   for (size_t i=0; i<6; ++i) {
      sb = node0.produce_block();
      BOOST_REQUIRE_EQUAL(sb->producer, producers[prod]); // these will be produced by the producer returned by `set_producers`
   }

   // Produce forks on node3
   // skip producer prod with time delay (13 blocks)
   // create 7 forks of 7 blocks, each with a corrupred block
   // -------------------------------------------------------
   vector<fork_tracker> forks(7);
   auto offset = fc::milliseconds(config::block_interval_ms * 13);
   auto prod2 = prod < producers.size() - 1 ? prod+1 : 0;

   for (size_t i = 0; i < 7; i ++) {
      auto b = node3.produce_block(offset);
      BOOST_REQUIRE_EQUAL(b->producer, producers[prod2]);

      for (size_t j = 0; j < 7; j ++) {
         auto& fork = forks.at(j);

         if (j <= i) {
            auto copy_b = std::make_shared<signed_block>(b->clone());
            if (j == i) {
               // corrupt this block
               fork.block_merkle = node3.control->head_block_state_legacy()->blockroot_merkle;
               copy_b->action_mroot._hash[0] ^= 0x1ULL;
            } else if (j < i) {
               // link to a corrupted chain
               copy_b->previous = fork.blocks.back()->calculate_id();
            }

            // re-sign the block
            auto header_bmroot = hash_pair(copy_b->digest(), fork.block_merkle.get_root()));
            auto sig_digest = hash_pair(header_bmroot, node3.control->head_block_state_legacy()->pending_schedule.schedule_hash);
            copy_b->producer_signature = node3.get_private_key("b"_n, "active").sign(sig_digest);

            // add this new block to our corrupted block merkle
            fork.block_merkle.append(copy_b->calculate_id());
            fork.blocks.emplace_back(copy_b);
         } else {
            fork.blocks.emplace_back(b);
         }
      }

      offset = fc::milliseconds(config::block_interval_ms);
   }



} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()