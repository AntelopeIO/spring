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

BOOST_AUTO_TEST_SUITE(forked_tests_if)

// ---------------------------- fork_with_bad_block ---------------------------------
BOOST_FIXTURE_TEST_CASE(fork_with_bad_block_if, savanna_cluster::cluster_t) try {
   struct fork_tracker {
      vector<signed_block_ptr>           blocks;
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
   auto                 offset    = fc::milliseconds(config::block_interval_ms * 13);
   auto                 prod2_idx = prod < producers.size() - 1 ? prod + 1 : 0;
   auto                 prod2     = producers[prod2_idx];
   auto                 pk        = node3.get_private_key(prod2, "active");

   for (size_t i = 0; i < 7; i ++) {
      auto b = node3.produce_block(offset);
      BOOST_REQUIRE_EQUAL(b->producer, prod2);

      for (size_t j = 0; j < 7; j ++) {
         auto& fork = forks.at(j);

         if (j <= i) {
            auto copy_b = std::make_shared<signed_block>(b->clone());
            if (j == i) {
               // corrupt this block
               copy_b->action_mroot._hash[0] ^= 0x1ULL;
            } else if (j < i) {
               // link to a corrupted chain
               copy_b->previous = fork.blocks.back()->calculate_id();
            }

            // re-sign the block
            copy_b->producer_signature = pk.sign(copy_b->calculate_id());

            // add this new block to our corrupted block merkle
            fork.blocks.emplace_back(copy_b);
         } else {
            fork.blocks.emplace_back(b);
         }
      }

      offset = fc::milliseconds(config::block_interval_ms);
   }

   // go from most corrupted fork to least
   for (size_t i = 0; i < forks.size(); i++) {
      BOOST_TEST_CONTEXT("Testing Fork: " << i) {
         const auto& fork = forks.at(i);
         // push the fork to the original node
         for (size_t fidx = 0; fidx < fork.blocks.size() - 1; fidx++) {
            const auto& b = fork.blocks.at(fidx);
            // push the block only if its not known already
            if (!node0.control->fetch_block_by_id(b->calculate_id())) {
               node0.push_block(b);
            }
         }

         // push the block which should attempt the corrupted fork and fail
         BOOST_REQUIRE_EXCEPTION( node0.push_block(fork.blocks.back()), fc::exception,
                                  fc_exception_message_starts_with( "Block ID does not match" )
         );
      }
   }

   // make sure we can still produce blocks until irreversibility moves
   auto lib = node0.lib_block->block_num();
   size_t tries = 0;
   while (node0.lib_block->block_num() == lib && ++tries < 10) {
      node0.produce_block();
   }

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()