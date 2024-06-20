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
   auto prod = set_producers(0, producers);   // set new producers and produce blocks until the switch is pending

   auto sb = node0.produce_block();           // now the next block produced on any node
   BOOST_REQUIRE_EQUAL(sb->producer,
                       producers[prod]);      // should be produced by the producer returned by `set_producers`

                                              // split the network. Finality will stop advancing as
                                              // votes and blocks are not propagated.
   const std::vector<size_t> partition {2, 3};
   set_partition(partition);                  // simulate 2 disconnected partitions:  nodes {0, 1} and nodes {2, 3}

                                              // at this point, each node has a QC to include into
                                              // the next block it produces which will advance lib.

   const size_t num_forks {3};                // shouldn't be greater than 5 otherwise production will span
                                              // more than 1 producer
   vector<fork_tracker> forks(num_forks);
   auto pk = node3.get_private_key(producers[prod], "active");

   // Create 3 forks of 3 blocks, each with a corrupted block.
   // We will create the last block of each fork with a higher timestamp than the blocks of node0,
   // so that when blocks are pushed from node3 to node0, the fork_switch will happen only when
   // the last block is pushed, according to the Savanna fork-choice rules.
   // (see `fork_database::by_best_branch_if_t`).
   // So we need a lambda to produce (and possibly corrupt) a block on node3 with a specified offset.
   // -----------------------------------------------------------------------------------------------
   auto produce_and_store_block_on_node3_forks = [&](size_t i, int offset) {
      auto b = node3.produce_block(fc::milliseconds(offset * config::block_interval_ms));
      BOOST_REQUIRE_EQUAL(sb->producer, producers[prod]);

      for (size_t j = 0; j < num_forks; j ++) {
         auto& fork = forks.at(j);

         if (j <= i) {
            auto copy_b = std::make_shared<signed_block>(b->clone());
            if (j == i) {
               // corrupt this block (forks[j].blocks[j] is corrupted)
               copy_b->action_mroot._hash[0] ^= 0x1ULL;
            } else if (j < i) {
               // link to a corrupted chain (fork.blocks[j] was corrupted)
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
   };

   // First produce forks of 2 blocks on node3, so the fork switch will happen when we produce the
   // third block which will have a newer timestamp than the last block of node0's branch.
   // Finality progress is halted as the network is split, so the timestamp criteria decides the best fork.
   //
   // Skip producer prod with time delay (13 blocks)
   // -----------------------------------------------------------------------------------------------------
   for (size_t i = 0; i < num_forks-1; ++i) {
      produce_and_store_block_on_node3_forks(i, 1);
   }

   // then produce 3 blocks on node0. This will be the default branch before we attempt to push the
   // forks from node3.
   // ---------------------------------------------------------------------------------------------
   for (size_t i = 0; i < num_forks; ++i) {
      auto sb = node0.produce_block(fc::milliseconds(config::block_interval_ms * (i==0 ? num_forks : 1)));
      BOOST_REQUIRE_EQUAL(sb->producer, producers[prod]); // produced by the producer returned by `set_producers`
   }

   // Produce the last block of node3's forks, with a later timestamp than all 3 blocks of node0.
   // When pushed to node0, It will cause a fork switch as it will be more recent than node0's head.
   // -------------------------------------------------------
   produce_and_store_block_on_node3_forks(num_forks-1, num_forks * 2);

   // Now we push each fork (going from most corrupted fork to least) from node3 to node0.
   // Blocks are correct enough to be pushed and inserted into fork_db, but will fail validation
   // (when apply_block is called on the corrupted block). This will happen when the fork switch occurs,
   // and all blocks from the forks are validated, which is why we expect an exception when the last
   // block of the fork is pushed.
   // -------------------------------------------------------------------------------------------------
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
                                  fc_exception_message_starts_with( "finality_mroot does not match" )
         );
      }
   }

   // make sure we can still produce blocks until irreversibility moves
   // -----------------------------------------------------------------
   auto lib = node0.lib_block->block_num();
   size_t tries = 0;
   while (node0.lib_block->block_num() == lib && ++tries < 10) {
      node0.produce_block();
   }

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()