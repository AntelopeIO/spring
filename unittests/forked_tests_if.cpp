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

// ---------------------------- fork_with_bad_block -------------------------------------
// - split the network (so finality doesn't advance) and create 3 forks on a node,
//   each fork containing 3 blocks, each having a different block corrupted (first
//   second or third block of the fork).
//
// - blocks are corrupted by changing action_mroot, which allows them to be inserted
//   in fork_db, but they won't validate.
//
// - make sure that the first two blocks of each fork have a timestamp earlier that the
//   blocks of node0's fork, and that the last block of each fork has a timestamp later
//   than the blocks of _nodes[0]'s fork (so the fork swith happens when the last block
//   of the fork is pushed, according to Savanna's fork choice rules).
//
// - push forks to other nodes, most corrupted fork first (causing multiple fork switches).
//   Verify that we get an exception when the last block of the fork is pushed.
//
// - produce blocks and verify that finality still advances.
// ---------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(fork_with_bad_block_if, savanna_cluster::cluster_t) try {
   struct fork_tracker {
      vector<signed_block_ptr> blocks;
   };

   _nodes[0].produce_block();

   const vector<account_name> producers {"a"_n, "b"_n, "c"_n, "d"_n, "e"_n};
   _nodes[0].create_accounts(producers);
   auto prod = set_producers(0, producers);   // set new producers and produce blocks until the switch is pending

   auto sb = _nodes[0].produce_block();       // now the next block can be produced on any node (here _nodes[0])
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
   auto pk = _nodes[3].get_private_key(producers[prod], "active");

   // Create 3 forks of 3 blocks, each with a corrupted block.
   // We will create the last block of each fork with a higher timestamp than the blocks of _nodes[0],
   // so that when blocks are pushed from _nodes[3] to _nodes[0], the fork_switch will happen only when
   // the last block is pushed, according to the Savanna fork-choice rules.
   // (see `fork_database::by_best_branch_if_t`).
   // So we need a lambda to produce (and possibly corrupt) a block on _nodes[3] with a specified offset.
   // -----------------------------------------------------------------------------------------------
   auto produce_and_store_block_on_node3_forks = [&](size_t i, int offset) {
      auto b = _nodes[3].produce_block(fc::milliseconds(offset * config::block_interval_ms));
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

   // First produce forks of 2 blocks on _nodes[3], so the fork switch will happen when we produce the
   // third block which will have a newer timestamp than the last block of _nodes[0]'s branch.
   // Finality progress is halted as the network is split, so the timestamp criteria decides the best fork.
   //
   // Skip producer prod with time delay (13 blocks)
   // -----------------------------------------------------------------------------------------------------
   for (size_t i = 0; i < num_forks-1; ++i) {
      produce_and_store_block_on_node3_forks(i, 1);
   }

   // then produce 3 blocks on _nodes[0]. This will be the default branch before we attempt
   // to push the forks from _nodes[3].
   // -------------------------------------------------------------------------------------
   for (size_t i = 0; i < num_forks; ++i) {
      auto sb = _nodes[0].produce_block(fc::milliseconds(config::block_interval_ms * (i==0 ? num_forks : 1)));
      BOOST_REQUIRE_EQUAL(sb->producer, producers[prod]); // produced by the producer returned by `set_producers`
   }

   // Produce the last block of _nodes[3]'s forks, with a later timestamp than all 3 blocks of _nodes[0].
   // When pushed to _nodes[0], It will cause a fork switch as it will be more recent than _nodes[0]'s head.
   // ------------------------------------------------------------------------------------------------------
   produce_and_store_block_on_node3_forks(num_forks-1, num_forks * 2);

   // Now we push each fork (going from most corrupted fork to least) from _nodes[3] to _nodes[0].
   // Blocks are correct enough to be pushed and inserted into fork_db, but will fail validation
   // (when apply_block is called on the corrupted block). This will happen when the fork switch occurs,
   // and all blocks from the forks are validated, which is why we expect an exception when the last
   // block of the fork is pushed.
   // -------------------------------------------------------------------------------------------------
   auto node0_head = _nodes[0].control->head_block_id();
   for (size_t i = 0; i < forks.size(); i++) {
      BOOST_TEST_CONTEXT("Testing Fork: " << i) {
         const auto& fork = forks.at(i);
         // push the fork to the original node
         for (size_t fidx = 0; fidx < fork.blocks.size() - 1; fidx++) {
            const auto& b = fork.blocks.at(fidx);
            // push the block only if its not known already
            if (!_nodes[0].control->fetch_block_by_id(b->calculate_id())) {
               _nodes[0].push_block(b);
            }
         }

         // push the block which should attempt the corrupted fork and fail
         BOOST_REQUIRE_EXCEPTION(_nodes[0].push_block(fork.blocks.back()), fc::exception,
                                 fc_exception_message_starts_with( "finality_mroot does not match"));
         BOOST_REQUIRE_EQUAL(_nodes[0].control->head_block_id(), node0_head);
      }
   }

   // make sure we can still produce blocks until irreversibility moves
   // -----------------------------------------------------------------
   set_partition({});
   propagate_heads();
   // produce an even more recent block on _nodes[0] so that it will be the uncontested head
   sb = _nodes[0].produce_block();
   BOOST_REQUIRE_EQUAL(_nodes[0].head().id(), _nodes[2].head().id());
   BOOST_REQUIRE_EQUAL(_nodes[0].head().id(), _nodes[3].head().id());

   auto lib = _nodes[0].lib_block->block_num();
   size_t tries = 0;
   while (_nodes[0].lib_block->block_num() <= lib + 3 && ++tries < 10) {
      _nodes[0].produce_block();
   }
   BOOST_REQUIRE_GT(_nodes[0].lib_block->block_num(), lib + 3);
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()