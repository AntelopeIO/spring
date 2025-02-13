#include "savanna_cluster.hpp"

#include "fork_test_utilities.hpp"

using namespace eosio::chain;
using namespace eosio::testing;

// ---------------------------- access some finality_core data ---------------------------------
namespace eosio::chain {
   struct core_info_t {
      uint32_t             last_final_block_num {0};
      uint32_t             last_qc_block_num {0};
      block_timestamp_type timestamp;
   };

   struct block_handle_accessor {
      static std::optional<core_info_t> core_info(const block_handle& h)  {
         return std::visit(
            overloaded{[](const block_state_legacy_ptr&) -> std::optional<core_info_t> { return {}; },
                       [](const block_state_ptr& bsp) -> std::optional<core_info_t> {
                          return core_info_t{bsp->last_final_block_num(), bsp->latest_qc_block_num(), bsp->timestamp()};
                       }},
            h.internal());
      }
   };

   void print_core(const block_handle& h) {
      auto core = block_handle_accessor::core_info(h);
      ilog("block ${bn} finality_core: last_final=${lfbn}, last_qc=${lqc}, timestamp=${t}\n",
           ("bn",h.block_num())("lfbn",core->last_final_block_num)
           ("lqc",core->last_qc_block_num)("t",core->timestamp));
   }

}

static bool does_account_exist( const tester& t, account_name n ) {
   const auto& db = t.control->db();
   return (db.find<account_object, by_name>( n ) != nullptr);
};

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

BOOST_AUTO_TEST_SUITE(forked_tests_savanna)

// ---------------------------- fork_with_bad_block -------------------------------------
// - split the network (so finality doesn't advance) and create 3 forks on a node,
//   each fork containing 3 blocks, each having a different block corrupted (first
//   second or third block of the fork).
//
// - blocks are corrupted by changing action_mroot, which allows them to be inserted
//   in fork_db, but they won't validate.
//
// - make sure that the first two blocks of each fork have a timestamp earlier that the
//   blocks of _nodes[0]'s fork, and that the last block of each fork has a timestamp later
//   than the blocks of _nodes[0]'s fork (so the fork swith happens when the last block
//   of the fork is pushed, according to Savanna's fork choice rules).
//
// - push forks to other nodes, most corrupted fork first (causing multiple fork switches).
//   Verify that we get an exception when the last block of the fork is pushed.
//
// - produce blocks and verify that finality still advances.
// ---------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(fork_with_bad_block_savanna, savanna_cluster::cluster_t) try {
   auto& C=_nodes[2]; auto& D=_nodes[3];
   struct fork_tracker {
      vector<signed_block_ptr> blocks;
   };

   _nodes[0].produce_block();

   const vector<account_name> producers {"a"_n, "b"_n, "c"_n, "d"_n, "e"_n};
   _nodes[0].create_accounts(producers);
   auto prod = _nodes[0].set_producers(producers); // set new producers and produce blocks until the switch is pending

   auto sb = _nodes[0].produce_block();       // now the next block can be produced on any node (here _nodes[0])
   BOOST_REQUIRE_EQUAL(sb->producer,
                       producers[prod]);      // should be produced by the producer returned by `set_producers`

                                              // split the network. Finality will stop advancing as
                                              // votes and blocks are not propagated.

   set_partition( {&C, &D} );                 // simulate 2 disconnected partitions:  nodes {0, 1} and nodes {2, 3}

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
      auto b = _nodes[3].produce_block(_block_interval_us * offset);
      BOOST_REQUIRE_EQUAL(sb->producer, producers[prod]);

      for (size_t j = 0; j < num_forks; j ++) {
         auto& fork = forks.at(j);

         if (j <= i) {
            auto copy_b = b->clone();
            if (j == i) {
               // Corrupt this block (forks[j].blocks[j] is corrupted).
               // Do not corrupt the block by modifying action_mroot, as action_mroot is checked
               // by block header validation, _nodes[0].push_block(b) would fail.
               copy_b->confirmed++;
            } else if (j < i) {
               // link to a corrupted chain (fork.blocks[j] was corrupted)
               copy_b->previous = fork.blocks.back()->calculate_id();
            }

            // re-sign the block
            copy_b->producer_signature = pk.sign(copy_b->calculate_id());

            // add this new block to our corrupted block merkle
            fork.blocks.emplace_back(signed_block::create_signed_block(std::move(copy_b)));
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
      auto sb = _nodes[0].produce_block(_block_interval_us * (i==0 ? num_forks : 1));
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
   auto node0_head = _nodes[0].head().id();
   for (size_t i = 0; i < forks.size(); i++) {
      BOOST_TEST_CONTEXT("Testing Fork: " << i) {
         const auto& fork = forks.at(i);
         // push the fork to the original node
         for (size_t fidx = 0; fidx < fork.blocks.size() - 1; fidx++) {
            const auto& b = fork.blocks.at(fidx);
            // push the block only if its not known already
            if (!_nodes[0].fetch_block_by_id(b->calculate_id())) {
               _nodes[0].push_block(b);
            }
         }

         // push the block which should attempt the corrupted fork and fail
         BOOST_REQUIRE_EXCEPTION(_nodes[0].push_block(fork.blocks.back()), fc::exception,
                                 fc_exception_message_starts_with( "Block ID does not match"));
         BOOST_REQUIRE_EQUAL(_nodes[0].head().id(), node0_head);
      }
   }

   // make sure we can still produce blocks until irreversibility moves
   // -----------------------------------------------------------------
   set_partition({});
   propagate_heads();

   sb = _nodes[0].produce_block();  // produce an even more recent block on _nodes[0] so that it will be the uncontested head
   BOOST_REQUIRE_EQUAL(_nodes[0].head().id(), _nodes[2].head().id());
   BOOST_REQUIRE_EQUAL(_nodes[0].head().id(), _nodes[3].head().id());

   verify_lib_advances();
} FC_LOG_AND_RETHROW();

// ---------------------------- forking ---------------------------------------------------------
// - on a network of 4 nodes, set a producer schedule { "dan"_n, "sam"_n, "pam"_n }
// - split the network into two partitions P0 and P1
// - produce 10 blocks on P0 and verify lib doesn't advance on either partition
// - and on partition P0 update the schedule to { "dan"_n, "sam"_n, "pam"_n, "cam"_n }
// - on P1, produce a block with a later timestamp than the last P0 block and push it to P0.
// - verify that the fork switch happens on P0 because of the later timestamp.
// - produce more blocks on P1, push them on P0, verify fork switch happens and head blocks match.
// - unsplit the network, produce blocks on _nodes[0] and verify lib advances.
// -----------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE( forking_savanna, savanna_cluster::cluster_t ) try {
   auto& C=_nodes[2]; auto& D=_nodes[3];
   _nodes[0].produce_blocks(2); // produce two extra blocks at the beginning so that producer schedules align
   
   const vector<account_name> producers { "dan"_n, "sam"_n, "pam"_n };
   _nodes[0].create_accounts(producers);
   auto prod = _nodes[0].set_producers(producers);   // set new producers and produce blocks until the switch is pending

   auto sb = _nodes[0].produce_block();
   BOOST_REQUIRE_EQUAL(sb->producer, producers[prod]); // first block produced by producers[prod]

   set_partition( {&C, &D} );                 // simulate 2 disconnected partitions:  nodes {0, 1} and nodes {2, 3}
                                              // at this point, each node has a QC to include into
                                              // the next block it produces which will advance lib.

   // process in-flight QC and reset lib
   _nodes[0].produce_block();
   _nodes[3].produce_block();

   BOOST_REQUIRE_EQUAL(0u, num_lib_advancing([&]() {
      // now that the network is split, produce 9 blocks on _nodes[0]
      sb = _nodes[0].produce_blocks(9);
      BOOST_REQUIRE_EQUAL(sb->producer, producers[prod]); // 11th block produced by producers[prod]
   }));

   // set new producers and produce blocks until the switch is pending
   _nodes[0].create_accounts( {"cam"_n} );
   const vector<account_name> new_producers { "dan"_n, "sam"_n, "pam"_n, "cam"_n };
   auto new_prod = _nodes[0].set_producers(new_producers);

   sb = _nodes[0].produce_block();
   BOOST_REQUIRE_EQUAL(sb->producer, new_producers[new_prod]);  // new_prod will be "sam"
   BOOST_REQUIRE_GT(new_prod, prod);
   BOOST_REQUIRE_EQUAL(new_prod, 1u);

   _nodes[0].produce_blocks(3);                                 // sam produces 3 more blocks

   // start producing on _nodes[3], skipping ahead by 23 block_interval_ms so that these block timestamps
   // will be ahead of those of _nodes[0].
   //
   // _nodes[3] is still having just produced the 2nd block by "sam", and with the `producers` schedule.
   // skip 22 blocks in the future so that "pam" produces
   auto node3_head = _nodes[3].produce_block(_block_interval_us * 22);
   BOOST_REQUIRE_EQUAL(node3_head->producer, producers[1]);    // should be sam's last block
   push_block(0, node3_head);
   BOOST_REQUIRE_EQUAL(_nodes[3].head().id(), _nodes[0].head().id());  // fork switch on 1st block because of later timestamp
   BOOST_REQUIRE_EQUAL(_nodes[3].head().id(), _nodes[1].head().id());  // push_block() propagated on peer which also fork switched

   sb = _nodes[3].produce_block();
   BOOST_REQUIRE_EQUAL(sb->producer, producers[2]);            // just switched to "pam"
   sb = _nodes[3].produce_blocks(12);                          // after 12 blocks, should have switched to "dan"
   BOOST_REQUIRE_EQUAL(sb->producer, producers[0]);            // chack that this is the case

   push_blocks(3, 0, node3_head->block_num() + 1);             // push the last 13 produced blocks to _nodes[0]
   BOOST_REQUIRE_EQUAL(_nodes[0].head().id(), _nodes[3].head().id());  // _nodes[0] caught up
   BOOST_REQUIRE_EQUAL(_nodes[1].head().id(), _nodes[3].head().id());  // _nodes[0] peer was updated as well

   // unsplit the network
   set_partition({});

   // produce an even more recent block on _nodes[0] so that it will be the uncontested head
   sb = _nodes[0].produce_block(_block_interval_us, true);     // no_throw = true because of expired transaction
   BOOST_REQUIRE_EQUAL(_nodes[0].head().id(), _nodes[2].head().id());
   BOOST_REQUIRE_EQUAL(_nodes[0].head().id(), _nodes[3].head().id());

   // and verify lib advances.
   auto lib = _nodes[0].lib_block->block_num();
   size_t tries = 0;
   while (_nodes[0].lib_block->block_num() <= lib + 3 && ++tries < 10) {
      _nodes[0].produce_block();
   }
   BOOST_REQUIRE_GT(_nodes[0].lib_block->block_num(), lib + 3);
   BOOST_REQUIRE_EQUAL(_nodes[0].lib_block->block_num(), _nodes[3].lib_block->block_num());
} FC_LOG_AND_RETHROW()

// ---------------------------- verify_savanna_fork_choice --------------------------
// Verify fork choice criteria for Savanna:
//   last_final_block_num > last_qc_block_num > timestamp
//
// - Simulate two network partitions: P0 (node {0}) and P1 (nodes {1, 2, 3}).
// - produce 2 blocks on P1. finality will advance by 2 blocks.
// - produce 2 blocks on P0. finality will advance by 1 block only (because no quorum)
//   but they'll have a later timestamp
// - push the 2 newly produced blocks from P1 to P0
// - check that we fork-switched to P1's head (despite P0's head timestamp being later)
// - Unpartition the network, veerify lib advances.
// ----------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE( verify_savanna_fork_choice, savanna_cluster::cluster_t) try {
   auto& A=_nodes[0];
   const vector<account_name> producers { "dan"_n, "sam"_n, "pam"_n };
   _nodes[0].create_accounts(producers);
   auto prod = _nodes[0].set_producers(producers);   // set new producers and produce blocks until the switch is pending

   auto sb_common = _nodes[0].produce_blocks(4);
   auto lib = _nodes[0].lib_num();
   BOOST_REQUIRE_EQUAL(sb_common->producer, producers[prod]); // first block produced by producers[prod]


   set_partition( {&A} );                     // simulate 2 disconnected partitions:
                                              // P0 (node {0}) and P1 (nodes {1, 2, 3}).
                                              // At this point, each node has a QC to include into
                                              // the next block it produces which will advance lib by one)
                                              // finality will still advance further in p1 because it has 3 finalizers

   _nodes[1].produce_blocks(2);                   // produce 2 blocks on _nodes[1] finality will advance by 2 blocks
   auto node1_head = _nodes[1].head();
   BOOST_REQUIRE_EQUAL(_nodes[1].lib_num(), lib+2);

   _nodes[0].produce_block(_block_interval_us * 12); // produce 2 blocks on _nodes[0]. finality will advance by 1 block only
   _nodes[0].produce_block();                        // but they'll have a later timestamp
   auto node0_head = _nodes[0].head();
   BOOST_REQUIRE_EQUAL(_nodes[0].lib_num(), lib+1);

   // verify assumptions (finality more advanced on _nodes[1], but timestamp less)
   auto core0 = block_handle_accessor::core_info(node0_head);
   auto core1 = block_handle_accessor::core_info(node1_head);
   BOOST_REQUIRE_GT(core1->last_final_block_num, core0->last_final_block_num);
   BOOST_REQUIRE_GT(core1->last_qc_block_num, core0->last_qc_block_num);
   BOOST_REQUIRE_LT(core1->timestamp, core0->timestamp);

   BOOST_REQUIRE_EQUAL(_nodes[0].head().id(), node0_head.id());

   push_blocks(1, 0, sb_common->block_num() + 1); // push the 2 produced blocks to _nodes[0]
   BOOST_REQUIRE_EQUAL(_nodes[0].head().id(), node1_head.id()); // and check that we fork-switched to _nodes[1]'s head

   set_partition({});
   propagate_heads();
   verify_lib_advances();

} FC_LOG_AND_RETHROW()


// ---------------------------- irreversible_mode_savanna_1 ----------------------------
//   A node not in irreversible mode captures what the LIB is as of different blocks
//   in the blockchain. Then the blocks are synced to a node running in irreversible
//   mode gradually. When syncing up to some block number, the test checks that the
//   controller head is at the block number of the corresponding LIB that was captured
//   earlier.
// -------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE( irreversible_mode_savanna_1, savanna_cluster::cluster_t ) try {
   const vector<account_name> producers {"producer1"_n, "producer2"_n};
   _nodes[0].create_accounts(producers);
   _nodes[0].set_producers(producers);   // set new producers and produce blocks until the switch is pending

   _nodes[0].create_accounts( {"alice"_n} );
   _nodes[0].produce_block();

   auto hbn1 = _nodes[0].head().block_num();
   auto lib1 = _nodes[0].last_irreversible_block_num();

   legacy_tester irreversible(setup_policy::none, db_read_mode::IRREVERSIBLE);

   _nodes[0].push_blocks_to(irreversible, hbn1);
   BOOST_CHECK_EQUAL( irreversible.fork_db_head().block_num(), hbn1 );
   BOOST_CHECK_EQUAL( irreversible.head().block_num(), lib1 );
   BOOST_CHECK_EQUAL( does_account_exist( irreversible, "alice"_n ), false );

   _nodes[0].produce_blocks(3); // produce a few blocks

   auto hbn2 = _nodes[0].head().block_num();
   auto lib2 = _nodes[0].last_irreversible_block_num();
   BOOST_CHECK_GT(lib2, lib1);

   _nodes[0].push_blocks_to(irreversible, hbn2);
   BOOST_CHECK_EQUAL( irreversible.fork_db_head().block_num(), hbn2 );
   BOOST_CHECK_EQUAL( irreversible.head().block_num(), lib2 );
   BOOST_CHECK_EQUAL( does_account_exist( irreversible, "alice"_n ), true );

   _nodes[0].produce_blocks(4); // produce a few blocks

   auto hbn3 = _nodes[0].head().block_num();
   auto lib3 = _nodes[0].last_irreversible_block_num();
   BOOST_CHECK_GT(lib3, lib2);

   _nodes[0].push_blocks_to(irreversible, hbn3);
   BOOST_CHECK_EQUAL( irreversible.fork_db_head().block_num(), hbn3 );
   BOOST_CHECK_EQUAL( irreversible.head().block_num(), lib3 );
} FC_LOG_AND_RETHROW()


// ---------------------------- irreversible_mode_savanna_2 ------------------------------
//   Two partitions not in irreversible mode are used to construct two competing branches.
//   One branch is considered the better branch compared to the other. The LIB determined
//   from the better branch is a descendant of the LIB determined from the worse branch.
//   Then a third node in irreversible mode gradually receives blocks from the two nodes.
//   It first receives the worse branch and the test checks the LIB is what is expected.
//   Then it receives the better branch and the test checks that the LIB has advanced to
//   the new block number expected which indicates that the fork DB has recognized the new
//   branch as the better branch. Also verify that a block from the worse branch that
//   was not in the better branch has been pruned out of the fork database after LIB
//   advances past the fork block.
// ---------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE( irreversible_mode_savanna_2, savanna_cluster::cluster_t ) try {
   auto& D=_nodes[3];
   const vector<account_name> producers {"producer1"_n, "producer2"_n};
   _nodes[0].create_accounts(producers);
   _nodes[0].set_producers(producers);   // set new producers and produce blocks until the switch is pending

   _nodes[0].create_accounts( {"alice"_n} );
   _nodes[0].produce_blocks(3);
   auto hbn1 = _nodes[0].head().block_num(); // common block before network partitioned
   [[maybe_unused]] auto lib1 = _nodes[0].last_irreversible_block_num();
   dlog("lib1 = ${lib1}", ("lib1", lib1)); // 36


   // partition node3. lib will not advance on node3 anymore, but will advance on the other 3 nodes
   set_partition( {&D} );       // simulate 2 disconnected partitions:  nodes {0, 1, 2} and node {3}

   // produce blocks on _nodes[3], creating account "bob"_n. Finality will not advance
   // --------------------------------------------------------------------------------
   auto fork_first_block_id = _nodes[3].produce_block(_block_interval_us * 10)->calculate_id();
   dlog( "fork_first_block_id = ${w}", ("w", fork_first_block_id));
   _nodes[3].create_accounts( {"bob"_n} );
   _nodes[3].produce_blocks(4);
   BOOST_CHECK_EQUAL( does_account_exist( _nodes[3], "bob"_n ), true );

   auto hbn3 = _nodes[3].head().block_num();
   auto lib3 = _nodes[3].last_irreversible_block_num();
   dlog("lib3 = ${lib3}", ("lib3", lib3)); // 37

   // produce blocks on _nodes[0], creating account "carol"_n. Finality will  advance
   // --------------------------------------------------------------------------------
   _nodes[0].produce_block();
   _nodes[0].create_accounts( {"carol"_n} );
   _nodes[0].produce_blocks(2);
   _nodes[0].create_accounts( {"dave"_n} );
   _nodes[0].produce_blocks(2);  // need 3 blocks after carol created for the block creating carol to become irreversible
   BOOST_CHECK_EQUAL( does_account_exist( _nodes[0], "carol"_n ), true );
   BOOST_CHECK_EQUAL( does_account_exist( _nodes[0], "dave"_n ), true );
   auto hbn0 = _nodes[0].head().block_num();
   auto lib0 = _nodes[0].last_irreversible_block_num();
   dlog("lib0 = ${lib0}", ("lib0", lib0)); // 41

   BOOST_CHECK_GT(lib0, lib3);

   legacy_tester irreversible(setup_policy::none, db_read_mode::IRREVERSIBLE);

   // push the branch where `lib` has not advanced past lib1
   // ------------------------------------------------------
   _nodes[3].push_blocks_to(irreversible, hbn3);

   BOOST_CHECK_EQUAL( irreversible.fork_db_head().block_num(), hbn3 );
   BOOST_CHECK_EQUAL( irreversible.head().block_num(), lib3 );
   BOOST_CHECK_EQUAL( does_account_exist( irreversible, "alice"_n ), true );
   BOOST_CHECK_EQUAL( does_account_exist( irreversible, "bob"_n ), false );

   {
      auto b = irreversible.fetch_block_by_id( fork_first_block_id );
      BOOST_REQUIRE( b && b->calculate_id() == fork_first_block_id );
      BOOST_TEST( irreversible.block_exists(fork_first_block_id) );
   }

   // push the branch where `lib` has advanced past lib1 (creating a new branch in
   // irreversible's fork database which will be preferred because lib advanced).
   // ----------------------------------------------------------------------------
   for( uint32_t n = hbn1 + 1; n <= hbn0; ++n ) {
      auto fb = _nodes[0].fetch_block_by_number( n );
      irreversible.push_block( fb );
   }

   BOOST_CHECK_EQUAL( irreversible.fork_db_head().block_num(), hbn0 );
   BOOST_CHECK_EQUAL( irreversible.head().block_num(), lib0 );
   BOOST_CHECK_EQUAL( does_account_exist( irreversible, "alice"_n ), true );
   BOOST_CHECK_EQUAL( does_account_exist( irreversible, "carol"_n ), true );
   BOOST_CHECK_EQUAL( does_account_exist( irreversible, "dave"_n ), false ); // block where dave created is not irreversible

   {
      // verify that a block from the worse branch that was not in the better branch
      // has been pruned out of the fork database after LIB advances past the fork block.
      auto b = irreversible.fetch_block_by_id( fork_first_block_id );
      BOOST_CHECK( !b );
      BOOST_CHECK( !irreversible.block_exists(fork_first_block_id) );
   }

} FC_LOG_AND_RETHROW()

// ------------------------------ split_and_rejoin ---------------------------------------
// demonstrates a worst-case scenario of finalizers split apart for different branches,
// then rejoin together, and need to reach consensus on one of the branches through their
// votes.
//
// - simulate 2 disconnected partitions:  P0: nodes {0, 1} and P1: node {2, 3}
// - produce 12 blocks on P0 (no quorum, finality does not advance)
// - produce 12 blocks on P1 (no quorum, finality does not advance)
// - update the network split so that {0, 1, 2} are in one partition,
//   enough for finality to start advancing again
// - and restart producing on P1, check that finality advances again
// ---------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE( split_and_rejoin, savanna_cluster::cluster_t ) try {
   auto& C=_nodes[2]; auto& D=_nodes[3];
   const vector<account_name> producers { "p1"_n, "p2"_n, "p3"_n };
   _nodes[0].create_accounts(producers);
   _nodes[0].set_producers(producers);               // set new producers and produce blocks until the switch is pending
   _nodes[0].create_accounts( {"alice"_n} );
   _nodes[0].produce_blocks(12);
   auto lib0 = _nodes[0].last_irreversible_block_num();
   dlog("lib0 = ${lib0}", ("lib0", lib0)); // 45

   // split the network
   set_partition( {&C, &D} );       // simulate 2 disconnected partitions:  nodes {0, 1} and node {2, 3}

   // produce 12 blocks on _nodes[0]'s partition
   _nodes[0].create_accounts( {"bob"_n} );
   _nodes[0].produce_blocks(12);
   BOOST_CHECK_EQUAL( _nodes[0].last_irreversible_block_num(), lib0 + 1);
   BOOST_CHECK_EQUAL( does_account_exist( _nodes[0], "alice"_n ), true );
   BOOST_CHECK_EQUAL( does_account_exist( _nodes[0], "bob"_n ),   true );

   // produce 12 blocks on _nodes[2]'s partition
   _nodes[2].produce_block(_block_interval_us * 13);
   _nodes[2].create_accounts( {"carol"_n} );
   _nodes[2].produce_blocks(11);
   BOOST_CHECK_EQUAL( _nodes[2].last_irreversible_block_num(), lib0 + 1);
   BOOST_CHECK_EQUAL( does_account_exist( _nodes[2], "alice"_n ), true );
   BOOST_CHECK_EQUAL( does_account_exist( _nodes[2], "bob"_n ),   false );
   BOOST_CHECK_EQUAL( does_account_exist( _nodes[2], "carol"_n ), true );

   // update the network split so that {0, 1, 2} are in one partition, enough for finality to start
   // advancing again
   set_partition( {&D} );       // simulate 2 disconnected partitions:  nodes {0, 1, 2} and node {3}

   propagate_heads();                           // otherwise we get unlinkable_block when newly produced blocks are pushed to node2

   // and restart producing on _nodes[0]
   _nodes[0].produce_block(_block_interval_us * 26, true);
   _nodes[0].produce_blocks(6);
   auto lib2 = _nodes[0].last_irreversible_block_num();
   BOOST_CHECK_EQUAL(lib2, lib0 + 12 + 7 );   // 12 when network was split, 7 just above (6 + 1)
   dlog("lib2 = ${lib2}", ("lib2", lib2)); // 65

} FC_LOG_AND_RETHROW()

// ---------------------------- push_block_returns_forked_transactions_savanna ---------------------------------
// Verify that a fork switch applies the blocks, and the included transactions in order.
// -------------------------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE( push_block_returns_forked_transactions_savanna, savanna_cluster::cluster_t  ) try {
   auto& C=_nodes[2]; auto& D=_nodes[3];
   const vector<account_name> producers { "p1"_n, "p2"_n, "p3"_n };
   _nodes[0].create_accounts(producers);
   _nodes[0].set_producers(producers);               // set new producers and produce blocks until the switch is pending
   _nodes[0].create_accounts( {"alice"_n} );
   _nodes[0].produce_blocks(12);
   auto lib0           = _nodes[0].last_irreversible_block_num();
   auto fork_block_num = _nodes[0].head().block_num();

   dlog("lib0 = ${lib0}, fork_block_num = ${fbn}", ("lib0", lib0)("fbn", fork_block_num));

   signed_block_ptr cb;

   // split the network
   set_partition( {&C, &D} );       // simulate 2 disconnected partitions:  nodes {0, 1} and node {2, 3}
   cb = _nodes[0].produce_block();
   _nodes[2].produce_block();

   // after this, finality will not advance anymore

   dlog( "_nodes[2] produces 36 blocks:" );
   _nodes[2].produce_blocks(12);
   auto c2b = _nodes[2].produce_block( fc::milliseconds(config::block_interval_ms * 14) ); // skip 13 blocks
   // save blocks for verification of forking later
   std::vector<signed_block_ptr> c2blocks;
   for( size_t i = 0; i < 11 + 12; ++i ) {
      c2blocks.emplace_back( _nodes[2].produce_block() );
   }


   dlog( "_nodes[0] blocks:" );
   auto b = _nodes[0].produce_block( fc::milliseconds(config::block_interval_ms * 13) ); // skip 12 blocks
   // create accounts on _nodes[0] which will be forked out
   _nodes[0].produce_block();

   transaction_trace_ptr trace1, trace2, trace3, trace4;
   { // create account the hard way so we can set reference block and expiration
      signed_transaction trx;
      authority active_auth( get_public_key( "test1"_n, "active" ) );
      authority owner_auth( get_public_key( "test1"_n, "owner" ) );
      trx.actions.emplace_back( vector<permission_level>{{config::system_account_name,config::active_name}},
                                newaccount{
                                      .creator  = config::system_account_name,
                                      .name     = "test1"_n,
                                      .owner    = owner_auth,
                                      .active   = active_auth,
                                });
      trx.expiration = fc::time_point_sec{_nodes[0].head().block_time() + fc::seconds( 60 )};
      trx.set_reference_block( cb->calculate_id() );
      trx.sign( get_private_key( config::system_account_name, "active" ), _nodes[0].get_chain_id()  );
      trace1 = _nodes[0].push_transaction( trx );
   }
   _nodes[0].produce_block();
   {
      signed_transaction trx;
      authority active_auth( get_public_key( "test2"_n, "active" ) );
      authority owner_auth( get_public_key( "test2"_n, "owner" ) );
      trx.actions.emplace_back( vector<permission_level>{{config::system_account_name,config::active_name}},
                                newaccount{
                                      .creator  = config::system_account_name,
                                      .name     = "test2"_n,
                                      .owner    = owner_auth,
                                      .active   = active_auth,
                                });
      trx.expiration = fc::time_point_sec{_nodes[0].head().block_time() + fc::seconds( 60 )};
      trx.set_reference_block( cb->calculate_id() );
      trx.sign( get_private_key( config::system_account_name, "active" ), _nodes[0].get_chain_id()  );
      trace2 = _nodes[0].push_transaction( trx );
   }
   {
      signed_transaction trx;
      authority active_auth( get_public_key( "test3"_n, "active" ) );
      authority owner_auth( get_public_key( "test3"_n, "owner" ) );
      trx.actions.emplace_back( vector<permission_level>{{config::system_account_name,config::active_name}},
                                newaccount{
                                      .creator  = config::system_account_name,
                                      .name     = "test3"_n,
                                      .owner    = owner_auth,
                                      .active   = active_auth,
                                });
      trx.expiration = fc::time_point_sec{_nodes[0].head().block_time() + fc::seconds( 60 )};
      trx.set_reference_block( cb->calculate_id() );
      trx.sign( get_private_key( config::system_account_name, "active" ), _nodes[0].get_chain_id()  );
      trace3 = _nodes[0].push_transaction( trx );
   }
   {
      signed_transaction trx;
      authority active_auth( get_public_key( "test4"_n, "active" ) );
      authority owner_auth( get_public_key( "test4"_n, "owner" ) );
      trx.actions.emplace_back( vector<permission_level>{{config::system_account_name,config::active_name}},
                                newaccount{
                                      .creator  = config::system_account_name,
                                      .name     = "test4"_n,
                                      .owner    = owner_auth,
                                      .active   = active_auth,
                                });
      trx.expiration = fc::time_point_sec{_nodes[0].head().block_time() + fc::seconds( 60 )};
      trx.set_reference_block( b->calculate_id() ); // tapos to dan's block should be rejected on fork switch
      trx.sign( get_private_key( config::system_account_name, "active" ), _nodes[0].get_chain_id()  );
      trace4 = _nodes[0].push_transaction( trx );
      BOOST_CHECK( trace4->receipt->status == transaction_receipt_header::executed );
   }
   _nodes[0].produce_block();
   _nodes[0].produce_blocks(9);

   // test forked blocks signal accepted_block in order, required by trace_api_plugin
   std::vector<signed_block_ptr> accepted_blocks;
   auto conn = _nodes[0].control->accepted_block().connect( [&]( block_signal_params t ) {
      const auto& [ block, id ] = t;
      accepted_blocks.emplace_back( block );
   } );

   // dan on chain 1 now gets all of the blocks from chain 2 which should cause fork switch
   dlog( "push _nodes[2] blocks to _nodes[0]" );
   for( uint32_t start = fork_block_num + 1, end = _nodes[2].head().block_num(); start <= end; ++start ) {
      auto fb = _nodes[2].fetch_block_by_number( start );
      push_block( 0, fb );
   }

   {  // verify forked blocks were signaled in order
      auto itr = std::find( accepted_blocks.begin(), accepted_blocks.end(), c2b );
      BOOST_CHECK( itr != accepted_blocks.end() );
      ++itr;
      BOOST_CHECK( itr != accepted_blocks.end() );
      size_t i = 0;
      for( i = 0; itr != accepted_blocks.end(); ++i, ++itr ) {
         BOOST_CHECK( c2blocks.at(i) == *itr );
      }
      BOOST_CHECK( i == 11 + 12 );
   }
   // verify transaction on fork is reported by push_block in order
   BOOST_REQUIRE_EQUAL( 4u, _nodes[0].get_unapplied_transaction_queue().size() );
   BOOST_REQUIRE_EQUAL( trace1->id, _nodes[0].get_unapplied_transaction_queue().begin()->id() );
   BOOST_REQUIRE_EQUAL( trace2->id, (++_nodes[0].get_unapplied_transaction_queue().begin())->id() );
   BOOST_REQUIRE_EQUAL( trace3->id, (++(++_nodes[0].get_unapplied_transaction_queue().begin()))->id() );
   BOOST_REQUIRE_EQUAL( trace4->id, (++(++(++_nodes[0].get_unapplied_transaction_queue().begin())))->id() );

   BOOST_REQUIRE_EXCEPTION(_nodes[0].get_account( "test1"_n ), fc::exception,
                           [a="test1"_n] (const fc::exception& e)->bool {
                              return std::string( e.what() ).find( a.to_string() ) != std::string::npos;
                           }) ;
   BOOST_REQUIRE_EXCEPTION(_nodes[0].get_account( "test2"_n ), fc::exception,
                           [a="test2"_n] (const fc::exception& e)->bool {
                              return std::string( e.what() ).find( a.to_string() ) != std::string::npos;
                           }) ;
   BOOST_REQUIRE_EXCEPTION(_nodes[0].get_account( "test3"_n ), fc::exception,
                           [a="test3"_n] (const fc::exception& e)->bool {
                              return std::string( e.what() ).find( a.to_string() ) != std::string::npos;
                           }) ;
   BOOST_REQUIRE_EXCEPTION(_nodes[0].get_account( "test4"_n ), fc::exception,
                           [a="test4"_n] (const fc::exception& e)->bool {
                              return std::string( e.what() ).find( a.to_string() ) != std::string::npos;
                           }) ;

   // produce block which will apply the unapplied transactions
   produce_block_result_t produce_block_result = _nodes[0].produce_block_ex(fc::milliseconds(config::block_interval_ms), true);
   std::vector<transaction_trace_ptr>& traces = produce_block_result.unapplied_transaction_traces;

   BOOST_REQUIRE_EQUAL( 4u, traces.size() );
   BOOST_CHECK_EQUAL( trace1->id, traces.at(0)->id );
   BOOST_CHECK_EQUAL( transaction_receipt_header::executed, traces.at(0)->receipt->status );
   BOOST_CHECK_EQUAL( trace2->id, traces.at(1)->id );
   BOOST_CHECK_EQUAL( transaction_receipt_header::executed, traces.at(1)->receipt->status );
   BOOST_CHECK_EQUAL( trace3->id, traces.at(2)->id );
   BOOST_CHECK_EQUAL( transaction_receipt_header::executed, traces.at(2)->receipt->status );
   // test4 failed because it was tapos to a forked out block
   BOOST_CHECK_EQUAL( trace4->id, traces.at(3)->id );
   BOOST_CHECK( !traces.at(3)->receipt );
   BOOST_CHECK( traces.at(3)->except );

   // verify unapplied transactions ran
   BOOST_REQUIRE_EQUAL( _nodes[0].get_account( "test1"_n ).name,  "test1"_n );
   BOOST_REQUIRE_EQUAL( _nodes[0].get_account( "test2"_n ).name,  "test2"_n );
   BOOST_REQUIRE_EQUAL( _nodes[0].get_account( "test3"_n ).name,  "test3"_n );

   // failed because of tapos to forked out block
   BOOST_REQUIRE_EXCEPTION(_nodes[0].get_account( "test4"_n ), fc::exception,
                           [a="test4"_n] (const fc::exception& e)->bool {
                              return std::string( e.what() ).find( a.to_string() ) != std::string::npos;
                           }) ;

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
