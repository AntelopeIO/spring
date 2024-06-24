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
// - blocks are corrupted by changing action_mroot, which allows them to be inserted
//   in fork_db, but they won't validate.
// - make sure that the first two blocks of each fork have a timestamp earlier that the
//   blocks of node0's fork, and that the last block of each fork has a timestamp later
//   than the blocks of node0's fork (so the fork swith happens when the last block of the
//   fork is pushed, according to Savanna's fork choice rules).
// - push forks to other node, most corrupted fork first (causing multiple fork switches).
//   Verify that we get an exception when the last block of the fork is pushed.
// - produce blocks and verify that finality still advances.
// ---------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE(fork_with_bad_block_if, savanna_cluster::cluster_t) try {
   struct fork_tracker {
      vector<signed_block_ptr>           blocks;
   };

   node0.produce_block();

   const vector<account_name> producers {"a"_n, "b"_n, "c"_n, "d"_n, "e"_n};
   auto prod = set_producers(0, producers);   // set new producers and produce blocks until the switch is pending

   auto sb = node0.produce_block();           // now the next block can be produced on any node (here node0)
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

// ---------------------------- forking ---------------------------------------------------------
// - on a network of 4 nodes, set a producer schedule { "dan"_n, "sam"_n, "pam"_n }
// - split the network into two partitions P0 and P1
// - produce 10 blocks on P0 and verify lib doesn't advance on either partition
// - and on partition P0 update the schedule to { "dan"_n, "sam"_n, "pam"_n, "cam"_n }
// - on P1, produce a block with a later timestamp than the last P0 block and push it to P0.
// - verify that the fork switch happens on P0 because of the later timestamp.
// - produce more blocks on P1, push them on P0, verify fork switch happens and head blocks match.
//
// -----------------------------------------------------------------------------------------------
BOOST_FIXTURE_TEST_CASE( forking_if, savanna_cluster::cluster_t ) try {
   while (node0.control->head_block_num() < 3) {
      node0.produce_block();
   }
   const vector<account_name> producers { "dan"_n, "sam"_n, "pam"_n };
   auto prod = set_producers(0, producers);   // set new producers and produce blocks until the switch is pending

   auto sb = node0.produce_block();
   BOOST_REQUIRE_EQUAL(sb->producer, producers[prod]); // first block produced by producers[prod]

   const std::vector<size_t> partition {2, 3};
   set_partition(partition);                  // simulate 2 disconnected partitions:  nodes {0, 1} and nodes {2, 3}
                                              // at this point, each node has a QC to include into
                                              // the next block it produces which will advance lib.

   // process in-flight QC and reset lib
   node0.produce_block();
   node3.produce_block();
   reset_lib();

   // now that the network is split, produce 9 blocks on node0
   sb = node0.produce_blocks(9);
   BOOST_REQUIRE_EQUAL(sb->producer, producers[prod]); // 11th block produced by producers[prod]

   // verify that lib doesn't advance
   BOOST_REQUIRE_EQUAL(num_lib_advancing(), 0);

   // set new producers and produce blocks until the switch is pending
   node0.create_accounts( {"cam"_n} );
   const vector<account_name> new_producers { "dan"_n, "sam"_n, "pam"_n, "cam"_n };
   auto new_prod = set_producers(0, new_producers, false);      // set new producers and produce blocks until the switch is pending

   sb = node0.produce_block();
   BOOST_REQUIRE_EQUAL(sb->producer, new_producers[new_prod]);  // new_prod will be "sam"
   BOOST_REQUIRE_GT(new_prod, prod);
   BOOST_REQUIRE_EQUAL(new_prod, 1);

   node0.produce_blocks(3);                                     // sam produces 3 more blocks

   // start producing on node3, skipping ahead by 23 block_interval_ms so that these block timestamps
   // will be ahead of those of node0.
   //
   // node3 is still having just produced the 2nd block by "sam", and with the `producers` schedule.
   // skip 23 blocks in the future so that "pam" produces
   auto node3_head = node3.produce_block(fc::milliseconds(22 * config::block_interval_ms));
   BOOST_REQUIRE_EQUAL(node3_head->producer, producers[1]);    // should be sam's last block
   push_block(0, node3_head);
   BOOST_REQUIRE_EQUAL(node3_head, node0.head());              // fork switch on 1st block because of later timestamp
   BOOST_REQUIRE_EQUAL(node3_head, node1.head());              // push_block() propagated on peer which also fork switched

   sb = node3.produce_block();
   BOOST_REQUIRE_EQUAL(sb->producer, producers[2]);            // just switched to "pam"
   sb = node3.produce_blocks(12);                              // after 12 blocks, should have switched to "dan"
   BOOST_REQUIRE_EQUAL(sb->producer, producers[0]);            // chack that this is the case

   push_blocks(3, 0, node3_head->block_num() + 1);             // push the last 13 produced blocks to node0
   BOOST_REQUIRE_EQUAL(node0.head(), node3.head());            // node0 caught up
   BOOST_REQUIRE_EQUAL(node1.head(), node3.head());            // node0 peer was updated as well
} FC_LOG_AND_RETHROW()


#if 0

// ---------------------------- prune_remove_branch ---------------------------------
/**
 *  This test verifies that the fork-choice rule favors the branch with
 *  the highest last irreversible block over one that is longer.
 */
BOOST_AUTO_TEST_CASE( prune_remove_branch_if ) try {
   legacy_tester c;
   while (c.control->head_block_num() < 11) {
      c.produce_block();
   }
   auto r = c.create_accounts( {"dan"_n,"sam"_n,"pam"_n,"scott"_n} );
   auto res = c.set_producers( {"dan"_n,"sam"_n,"pam"_n,"scott"_n} );
   wlog("set producer schedule to [dan,sam,pam,scott]");

   // run until the producers are installed and its the start of "dan's" round
   BOOST_REQUIRE( produce_until_transition( c, "scott"_n, "dan"_n ) );

   legacy_tester c2(setup_policy::none);
   wlog( "push c1 blocks to c2" );
   push_blocks(c, c2);

   // fork happen after block fork_num
   uint32_t fork_num = c.control->head_block_num();
   BOOST_REQUIRE_EQUAL(fork_num, c2.control->head_block_num());

   auto nextproducer = [](legacy_tester &c, int skip_interval) ->account_name {
      auto head_time = c.control->head_block_time();
      auto next_time = head_time + fc::milliseconds(config::block_interval_ms * skip_interval);
      return c.control->active_producers().get_scheduled_producer(next_time).producer_name;
   };

   // fork c: 2 producers: dan, sam
   // fork c2: 1 producer: scott
   int skip1 = 1, skip2 = 1;
   for (int i = 0; i < 48; ++i) {
      account_name next1 = nextproducer(c, skip1);
      if (next1 == "dan"_n || next1 == "sam"_n) {
         c.produce_block(fc::milliseconds(config::block_interval_ms * skip1)); skip1 = 1;
      }
      else ++skip1;
      account_name next2 = nextproducer(c2, skip2);
      if (next2 == "scott"_n) {
         c2.produce_block(fc::milliseconds(config::block_interval_ms * skip2)); skip2 = 1;
      }
      else ++skip2;
   }

   BOOST_REQUIRE_EQUAL(fork_num + 24u, c.control->head_block_num());  // dan and sam each produced 12 blocks
   BOOST_REQUIRE_EQUAL(fork_num + 12u, c2.control->head_block_num()); // only scott produced its 12 blocks

   // push fork from c2 => c
   size_t p = fork_num;

   while ( p < c2.control->head_block_num()) {
      auto fb = c2.control->fetch_block_by_number(++p);
      c.push_block(fb);
   }

   BOOST_REQUIRE_EQUAL(fork_num + 24u, c.control->head_block_num());

} FC_LOG_AND_RETHROW()

// ---------------------------- irreversible_mode ---------------------------------
BOOST_AUTO_TEST_CASE( irreversible_mode_if ) try {
   auto does_account_exist = []( const tester& t, account_name n ) {
      const auto& db = t.control->db();
      return (db.find<account_object, by_name>( n ) != nullptr);
   };

   legacy_tester main;

   main.create_accounts( {"producer1"_n, "producer2"_n} );
   main.produce_block();
   main.set_producers( {"producer1"_n, "producer2"_n} );
   main.produce_block();
   BOOST_REQUIRE( produce_until_transition( main, "producer1"_n, "producer2"_n, 26) );

   main.create_accounts( {"alice"_n} );
   main.produce_block();
   auto hbn1 = main.control->head_block_num();
   auto lib1 = main.control->last_irreversible_block_num();

   BOOST_REQUIRE( produce_until_transition( main, "producer2"_n, "producer1"_n, 11) );

   auto hbn2 = main.control->head_block_num();
   auto lib2 = main.control->last_irreversible_block_num();

   BOOST_REQUIRE( lib2 < hbn1 );

   legacy_tester other(setup_policy::none);

   push_blocks( main, other );
   BOOST_CHECK_EQUAL( other.control->head_block_num(), hbn2 );

   BOOST_REQUIRE( produce_until_transition( main, "producer1"_n, "producer2"_n, 12) );
   BOOST_REQUIRE( produce_until_transition( main, "producer2"_n, "producer1"_n, 12) );

   auto hbn3 = main.control->head_block_num();
   auto lib3 = main.control->last_irreversible_block_num();

   BOOST_REQUIRE( lib3 >= hbn1 );

   BOOST_CHECK_EQUAL( does_account_exist( main, "alice"_n ), true );

   // other forks away from main after hbn2
   BOOST_REQUIRE_EQUAL( other.control->head_block_producer().to_string(), "producer2" );

   other.produce_block( fc::milliseconds( 13 * config::block_interval_ms ) ); // skip over producer1's round
   BOOST_REQUIRE_EQUAL( other.control->head_block_producer().to_string(), "producer2" );
   auto fork_first_block_id = other.control->head_block_id();
   wlog( "{w}", ("w", fork_first_block_id));

   BOOST_REQUIRE( produce_until_transition( other, "producer2"_n, "producer1"_n, 11) ); // finish producer2's round
   BOOST_REQUIRE_EQUAL( other.control->pending_block_producer().to_string(), "producer1" );

   // Repeat two more times to ensure other has a longer chain than main
   other.produce_block( fc::milliseconds( 13 * config::block_interval_ms ) ); // skip over producer1's round
   BOOST_REQUIRE( produce_until_transition( other, "producer2"_n, "producer1"_n, 11) ); // finish producer2's round

   other.produce_block( fc::milliseconds( 13 * config::block_interval_ms ) ); // skip over producer1's round
   BOOST_REQUIRE( produce_until_transition( other, "producer2"_n, "producer1"_n, 11) ); // finish producer2's round

   auto hbn4 = other.control->head_block_num();
   auto lib4 = other.control->last_irreversible_block_num();

   BOOST_REQUIRE( hbn4 > hbn3 );
   BOOST_REQUIRE( lib4 < hbn1 );

   legacy_tester irreversible(setup_policy::none, db_read_mode::IRREVERSIBLE);

   push_blocks( main, irreversible, hbn1 );

   BOOST_CHECK_EQUAL( irreversible.control->fork_db_head_block_num(), hbn1 );
   BOOST_CHECK_EQUAL( irreversible.control->head_block_num(), lib1 );
   BOOST_CHECK_EQUAL( does_account_exist( irreversible, "alice"_n ), false );

   push_blocks( other, irreversible, hbn4 );

   BOOST_CHECK_EQUAL( irreversible.control->fork_db_head_block_num(), hbn4 );
   BOOST_CHECK_EQUAL( irreversible.control->head_block_num(), lib4 );
   BOOST_CHECK_EQUAL( does_account_exist( irreversible, "alice"_n ), false );

   // force push blocks from main to irreversible creating a new branch in irreversible's fork database
   for( uint32_t n = hbn2 + 1; n <= hbn3; ++n ) {
      auto fb = main.control->fetch_block_by_number( n );
      irreversible.push_block( fb );
   }

   BOOST_CHECK_EQUAL( irreversible.control->fork_db_head_block_num(), hbn3 );
   BOOST_CHECK_EQUAL( irreversible.control->head_block_num(), lib3 );
   BOOST_CHECK_EQUAL( does_account_exist( irreversible, "alice"_n ), true );

   {
      auto b = irreversible.control->fetch_block_by_id( fork_first_block_id );
      BOOST_REQUIRE( b && b->calculate_id() == fork_first_block_id );
      BOOST_TEST( irreversible.control->block_exists(fork_first_block_id) );
   }

   main.produce_block();
   auto hbn5 = main.control->head_block_num();
   auto lib5 = main.control->last_irreversible_block_num();

   BOOST_REQUIRE( lib5 > lib3 );

   push_blocks( main, irreversible, hbn5 );

   {
      auto b = irreversible.control->fetch_block_by_id( fork_first_block_id );
      BOOST_REQUIRE( !b );
      BOOST_TEST( !irreversible.control->block_exists(fork_first_block_id) );
   }

} FC_LOG_AND_RETHROW()

// ---------------------------- push_block_returns_forked_transactions ---------------------------------
BOOST_AUTO_TEST_CASE( push_block_returns_forked_transactions_if ) try {
   legacy_tester c1;
   while (c1.control->head_block_num() < 3) {
      c1.produce_block();
   }
   auto r = c1.create_accounts( {"dan"_n,"sam"_n,"pam"_n} );
   c1.produce_block();
   auto res = c1.set_producers( {"dan"_n,"sam"_n,"pam"_n} );
   wlog("set producer schedule to [dan,sam,pam]");
   BOOST_REQUIRE( produce_until_transition( c1, "dan"_n, "sam"_n ) );
   c1.produce_blocks(32);

   legacy_tester c2(setup_policy::none);
   wlog( "push c1 blocks to c2" );
   push_blocks(c1, c2);

   wlog( "c1 blocks:" );
   signed_block_ptr cb;
   c1.produce_blocks(3);
   signed_block_ptr b;
   cb = b = c1.produce_block();
   BOOST_REQUIRE_EQUAL( b->producer.to_string(), "dan"_n.to_string() );

   b = c1.produce_block();
   BOOST_REQUIRE_EQUAL( b->producer.to_string(), "sam"_n.to_string() );
   c1.produce_blocks(10);
   c1.create_accounts( {"cam"_n} );
   c1.set_producers( {"dan"_n,"sam"_n,"pam"_n,"cam"_n} );
   wlog("set producer schedule to [dan,sam,pam,cam]");
   c1.produce_block();
   // The next block should be produced by pam.

   // Sync second chain with first chain.
   wlog( "push c1 blocks to c2" );
   push_blocks(c1, c2);
   wlog( "end push c1 blocks to c2" );

   // Now sam and pam go on their own fork while dan is producing blocks by himself.

   wlog( "sam and pam go off on their own fork on c2 while dan produces blocks by himself in c1" );
   auto fork_block_num = c1.control->head_block_num();

   signed_block_ptr c2b;
   wlog( "c2 blocks:" );
   // pam produces 12 blocks
   for (size_t i=0; i<12; ++i) {
      b = c2.produce_block();
      BOOST_REQUIRE_EQUAL( b->producer.to_string(), "pam"_n.to_string() );
   }
   b = c2b = c2.produce_block( fc::milliseconds(config::block_interval_ms * 13) ); // sam skips over dan's blocks
   BOOST_REQUIRE_EQUAL( b->producer.to_string(), "sam"_n.to_string() );
   // save blocks for verification of forking later
   std::vector<signed_block_ptr> c2blocks;
   for( size_t i = 0; i < 11 + 12; ++i ) {
      c2blocks.emplace_back( c2.produce_block() );
   }


   wlog( "c1 blocks:" );
   b = c1.produce_block( fc::milliseconds(config::block_interval_ms * 13) ); // dan skips over pam's blocks
   BOOST_REQUIRE_EQUAL( b->producer.to_string(), "dan"_n.to_string() );
   // create accounts on c1 which will be forked out
   c1.produce_block();

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
      trx.expiration = fc::time_point_sec{c1.control->head_block_time() + fc::seconds( 60 )};
      trx.set_reference_block( cb->calculate_id() );
      trx.sign( get_private_key( config::system_account_name, "active" ), c1.control->get_chain_id()  );
      trace1 = c1.push_transaction( trx );
   }
   c1.produce_block();
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
      trx.expiration = fc::time_point_sec{c1.control->head_block_time() + fc::seconds( 60 )};
      trx.set_reference_block( cb->calculate_id() );
      trx.sign( get_private_key( config::system_account_name, "active" ), c1.control->get_chain_id()  );
      trace2 = c1.push_transaction( trx );
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
      trx.expiration = fc::time_point_sec{c1.control->head_block_time() + fc::seconds( 60 )};
      trx.set_reference_block( cb->calculate_id() );
      trx.sign( get_private_key( config::system_account_name, "active" ), c1.control->get_chain_id()  );
      trace3 = c1.push_transaction( trx );
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
      trx.expiration = fc::time_point_sec{c1.control->head_block_time() + fc::seconds( 60 )};
      trx.set_reference_block( b->calculate_id() ); // tapos to dan's block should be rejected on fork switch
      trx.sign( get_private_key( config::system_account_name, "active" ), c1.control->get_chain_id()  );
      trace4 = c1.push_transaction( trx );
      BOOST_CHECK( trace4->receipt->status == transaction_receipt_header::executed );
   }
   c1.produce_block();
   c1.produce_blocks(9);

   // test forked blocks signal accepted_block in order, required by trace_api_plugin
   std::vector<signed_block_ptr> accepted_blocks;
   auto conn = c1.control->accepted_block().connect( [&]( block_signal_params t ) {
      const auto& [ block, id ] = t;
      accepted_blocks.emplace_back( block );
   } );

   // dan on chain 1 now gets all of the blocks from chain 2 which should cause fork switch
   wlog( "push c2 blocks to c1" );
   for( uint32_t start = fork_block_num + 1, end = c2.control->head_block_num(); start <= end; ++start ) {
      auto fb = c2.control->fetch_block_by_number( start );
      c1.push_block( fb );
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
   BOOST_REQUIRE_EQUAL( 4u, c1.get_unapplied_transaction_queue().size() );
   BOOST_REQUIRE_EQUAL( trace1->id, c1.get_unapplied_transaction_queue().begin()->id() );
   BOOST_REQUIRE_EQUAL( trace2->id, (++c1.get_unapplied_transaction_queue().begin())->id() );
   BOOST_REQUIRE_EQUAL( trace3->id, (++(++c1.get_unapplied_transaction_queue().begin()))->id() );
   BOOST_REQUIRE_EQUAL( trace4->id, (++(++(++c1.get_unapplied_transaction_queue().begin())))->id() );

   BOOST_REQUIRE_EXCEPTION(c1.control->get_account( "test1"_n ), fc::exception,
                           [a="test1"_n] (const fc::exception& e)->bool {
                              return std::string( e.what() ).find( a.to_string() ) != std::string::npos;
                           }) ;
   BOOST_REQUIRE_EXCEPTION(c1.control->get_account( "test2"_n ), fc::exception,
                           [a="test2"_n] (const fc::exception& e)->bool {
                              return std::string( e.what() ).find( a.to_string() ) != std::string::npos;
                           }) ;
   BOOST_REQUIRE_EXCEPTION(c1.control->get_account( "test3"_n ), fc::exception,
                           [a="test3"_n] (const fc::exception& e)->bool {
                              return std::string( e.what() ).find( a.to_string() ) != std::string::npos;
                           }) ;
   BOOST_REQUIRE_EXCEPTION(c1.control->get_account( "test4"_n ), fc::exception,
                           [a="test4"_n] (const fc::exception& e)->bool {
                              return std::string( e.what() ).find( a.to_string() ) != std::string::npos;
                           }) ;

   // produce block which will apply the unapplied transactions
   produce_block_result_t produce_block_result = c1.produce_block_ex(fc::milliseconds(config::block_interval_ms), true);
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
   BOOST_REQUIRE_EQUAL( c1.control->get_account( "test1"_n ).name,  "test1"_n );
   BOOST_REQUIRE_EQUAL( c1.control->get_account( "test2"_n ).name,  "test2"_n );
   BOOST_REQUIRE_EQUAL( c1.control->get_account( "test3"_n ).name,  "test3"_n );

   // failed because of tapos to forked out block
   BOOST_REQUIRE_EXCEPTION(c1.control->get_account( "test4"_n ), fc::exception,
                           [a="test4"_n] (const fc::exception& e)->bool {
                              return std::string( e.what() ).find( a.to_string() ) != std::string::npos;
                           }) ;

} FC_LOG_AND_RETHROW()

#endif

BOOST_AUTO_TEST_SUITE_END()