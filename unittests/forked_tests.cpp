#include <eosio/chain/abi_serializer.hpp>
#include <eosio/testing/tester.hpp>

#include <eosio/chain/fork_database.hpp>

#include <boost/test/unit_test.hpp>

#include "fork_test_utilities.hpp"

using namespace eosio::chain;
using namespace eosio::testing;

// ---------------------------------------------------
// Following tests in this file are for Legacy only:
//    - fork_with_bad_block
//    - forking
//    - prune_remove_branch
//    - irreversible_mode
//    - push_block_returns_forked_transactions
//
// Similar Savanna tests are in: `forked_tests_if.cpp`
// ---------------------------------------------------

BOOST_AUTO_TEST_SUITE(forked_tests)

// ---------------------------- fork_tracker ----------------------------------------
struct fork_tracker {
   vector<signed_block_ptr>           blocks;
   incremental_merkle_tree_legacy     block_merkle;
};

// ---------------------------- fork_with_bad_block ---------------------------------
BOOST_AUTO_TEST_CASE( fork_with_bad_block ) try {
   legacy_tester bios;
   bios.produce_block();
   bios.produce_block();
   bios.create_accounts( {"a"_n,"b"_n,"c"_n,"d"_n,"e"_n} );

   bios.produce_block();
   auto res = bios.set_producers( {"a"_n,"b"_n,"c"_n,"d"_n,"e"_n} );

   // run until the producers are installed and its the start of "a's" round
   BOOST_REQUIRE( produce_until_transition( bios, "e"_n, "a"_n ) );

   // sync remote node
   legacy_tester remote(setup_policy::none);
   push_blocks(bios, remote);

   // produce 6 blocks on bios
   for (int i = 0; i < 6; i ++) {
      bios.produce_block();
      BOOST_REQUIRE_EQUAL( bios.head().block()->producer.to_string(), "a" );
   }

   vector<fork_tracker> forks(7);
   // enough to skip A's blocks
   auto offset = fc::milliseconds(config::block_interval_ms * 13);

   // skip a's blocks on remote
   // create 7 forks of 7 blocks so this fork is longer where the ith block is corrupted
   for (size_t i = 0; i < 7; i ++) {
      auto b = remote.produce_block(offset);
      BOOST_REQUIRE_EQUAL( b->producer.to_string(), "b" );

      for (size_t j = 0; j < 7; j ++) {
         auto& fork = forks.at(j);

         if (j <= i) {
            auto copy_b = b->clone();
            if (j == i) {
               // corrupt this block
               fork.block_merkle = remote.control->head_block_state_legacy()->blockroot_merkle;
               copy_b->action_mroot._hash[0] ^= 0x1ULL;
            } else if (j < i) {
               // link to a corrupted chain
               copy_b->previous = fork.blocks.back()->calculate_id();
            }

            // re-sign the block
            auto header_bmroot = digest_type::hash( std::make_pair( copy_b->digest(), fork.block_merkle.get_root() ) );
            auto sig_digest = digest_type::hash( std::make_pair(header_bmroot, remote.control->head_block_state_legacy()->pending_schedule.schedule_hash) );
            copy_b->producer_signature = remote.get_private_key("b"_n, "active").sign(sig_digest);

            // add this new block to our corrupted block merkle
            auto signed_copy_b = signed_block::create_signed_block(std::move(copy_b));
            fork.block_merkle.append(signed_copy_b->calculate_id());
            fork.blocks.emplace_back(signed_copy_b);
         } else {
            fork.blocks.emplace_back(b);
         }
      }

      offset = fc::milliseconds(config::block_interval_ms);
   }

   // fork_db is sorted on block id which can cause fork switch on the second to last block or last block depending
   // on block id. Allow exception on either one so that test is not sensitive to block id hash.
   auto push_last_two = [&](const fork_tracker& fork) {
      if (fork.blocks.size() > 1) {
         const auto& b = fork.blocks.at(fork.blocks.size() - 2);
         if (!bios.fetch_block_by_id(b->calculate_id())) {
            bios.push_block(b);
         }
      }
      bios.push_block(fork.blocks.back());
   };

   // go from most corrupted fork to least
   for (size_t i = 0; i < forks.size(); i++) {
      BOOST_TEST_CONTEXT("Testing Fork: " << i) {
         const auto& fork = forks.at(i);
         // push the fork to the original node
         if (fork.blocks.size() > 1) {
            for (size_t fidx = 0; fidx < fork.blocks.size() - 2; fidx++) {
               const auto& b = fork.blocks.at(fidx);
               // push the block only if its not known already
               if (!bios.fetch_block_by_id(b->calculate_id())) {
                  bios.push_block(b);
               }
            }
         }

         // push the block which should attempt the corrupted fork and fail
         BOOST_REQUIRE_EXCEPTION( push_last_two(fork), fc::exception,
                                  fc_exception_message_starts_with( "Block ID does not match" )
         );
      }
   }

   // make sure we can still produce blocks until irreversibility moves
   auto lib = bios.lib_block->block_num();
   size_t tries = 0;
   while (bios.lib_block->block_num() == lib && ++tries < 10000) {
      bios.produce_block();
   }
   BOOST_REQUIRE_GT(bios.lib_block->block_num(), lib);

} FC_LOG_AND_RETHROW();

// ---------------------------- forking ---------------------------------
BOOST_AUTO_TEST_CASE( forking ) try {
   legacy_tester c;
   while (c.head().block_num() < 3) {
      c.produce_block();
   }
   auto r = c.create_accounts( {"dan"_n,"sam"_n,"pam"_n} );
   wdump((fc::json::to_pretty_string(r)));
   c.produce_block();
   auto res = c.set_producers( {"dan"_n,"sam"_n,"pam"_n} );

   wdump((fc::json::to_pretty_string(res)));
   wlog("set producer schedule to [dan,sam,pam]");
   c.produce_blocks(30); // legacy: 0..2 by eosio, 3..7 by dan, 8..19 by sam, 20..29 by pam, pam still has 2 to produce
   c.produce_blocks(10); // 0..1 by pam, 2..9 by dan, dan still has 4 to produce

   legacy_tester c2(setup_policy::none);
   wlog( "push c1 blocks to c2" );
   push_blocks(c, c2);
   wlog( "end push c1 blocks to c2" );

   wlog( "c1 blocks:" );
   signed_block_ptr b = c.produce_blocks(4);
   BOOST_REQUIRE_EQUAL( b->producer.to_string(), "dan"_n.to_string() );

   b = c.produce_block();
   BOOST_REQUIRE_EQUAL( b->producer.to_string(), "sam"_n.to_string() );
   c.produce_blocks(10);
   c.create_accounts( {"cam"_n} );
   c.set_producers( {"dan"_n,"sam"_n,"pam"_n,"cam"_n} );
   wlog("set producer schedule to [dan,sam,pam,cam]");
   c.produce_block();
   // The next block should be produced by pam.

   // Sync second chain with first chain.
   wlog( "push c1 blocks to c2" );
   push_blocks(c, c2);
   wlog( "end push c1 blocks to c2" );

   // Now sam and pam go on their own fork while dan is producing blocks by himself.

   wlog( "sam and pam go off on their own fork on c2 while dan produces blocks by himself in c1" );
   auto fork_block_num = c.head().block_num();

   wlog( "c2 blocks:" );
   c2.produce_blocks(12); // pam produces 12 blocks
   b = c2.produce_block( fc::milliseconds(config::block_interval_ms * 13) ); // sam skips over dan's blocks
   BOOST_REQUIRE_EQUAL( b->producer.to_string(), "sam"_n.to_string() );
   c2.produce_blocks(11 + 12);


   wlog( "c1 blocks:" );
   b = c.produce_block( fc::milliseconds(config::block_interval_ms * 13) ); // dan skips over pam's blocks
   BOOST_REQUIRE_EQUAL( b->producer.to_string(), "dan"_n.to_string() );
   c.produce_blocks(11);

   // dan on chain 1 now gets all of the blocks from chain 2 which should cause fork switch
   wlog( "push c2 blocks to c1" );
   for( uint32_t start = fork_block_num + 1, end = c2.head().block_num(); start <= end; ++start ) {
      wdump((start));
      auto fb = c2.fetch_block_by_number( start );
      c.push_block( fb );
   }
   wlog( "end push c2 blocks to c1" );

   wlog( "c1 blocks:" );
   c.produce_blocks(24);

   b = c.produce_block(); // Switching active schedule to version 2 happens in this block.
   BOOST_REQUIRE_EQUAL( b->producer.to_string(), "pam"_n.to_string() );

   b = c.produce_block();
//   BOOST_REQUIRE_EQUAL( b->producer.to_string(), "cam"_n.to_string() );
   c.produce_blocks(10);

   wlog( "push c1 blocks to c2" );
   push_blocks(c, c2);
   wlog( "end push c1 blocks to c2" );

   // Now with four block producers active and two identical chains (for now),
   // we can test out the case that would trigger the bug in the old fork db code:
   fork_block_num = c.head().block_num();
   wlog( "cam and dan go off on their own fork on c1 while sam and pam go off on their own fork on c2" );
   wlog( "c1 blocks:" );
   c.produce_blocks(12); // dan produces 12 blocks
   c.produce_block( fc::milliseconds(config::block_interval_ms * 25) ); // cam skips over sam and pam's blocks
   c.produce_blocks(23); // cam finishes the remaining 11 blocks then dan produces his 12 blocks
   wlog( "c2 blocks:" );
   c2.produce_block( fc::milliseconds(config::block_interval_ms * 25) ); // pam skips over dan and sam's blocks
   c2.produce_blocks(11); // pam finishes the remaining 11 blocks
   c2.produce_block( fc::milliseconds(config::block_interval_ms * 25) ); // sam skips over cam and dan's blocks
   c2.produce_blocks(11); // sam finishes the remaining 11 blocks

   wlog( "now cam and dan rejoin sam and pam on c2" );
   c2.produce_block( fc::milliseconds(config::block_interval_ms * 13) ); // cam skips over pam's blocks (this block triggers a block on this branch to become irreversible)
   c2.produce_blocks(11); // cam produces the remaining 11 blocks
   b = c2.produce_block(); // dan produces a block

   // a node on chain 1 now gets all but the last block from chain 2 which should cause a fork switch
   wlog( "push c2 blocks (except for the last block by dan) to c1" );
   for( uint32_t start = fork_block_num + 1, end = c2.head().block_num() - 1; start <= end; ++start ) {
      auto fb = c2.fetch_block_by_number( start );
      c.push_block( fb );
   }
   wlog( "end push c2 blocks to c1" );
   wlog( "now push dan's block to c1 but first corrupt it so it is a bad block" );
   auto bad_block = b->clone();
   bad_block->action_mroot = bad_block->previous;
   auto bad_id = bad_block->calculate_id();
   BOOST_REQUIRE_EXCEPTION(c.control->accept_block(bad_id, signed_block::create_signed_block(std::move(bad_block))),
      fc::exception, [] (const fc::exception& ex)->bool {
         return ex.to_detail_string().find("block signed by unexpected key") != std::string::npos;
      });
} FC_LOG_AND_RETHROW()


// ---------------------------- prune_remove_branch ---------------------------------
/**
 *  This test verifies that the fork-choice rule favors the branch with
 *  the highest last irreversible block over one that is longer.
 */
BOOST_AUTO_TEST_CASE( prune_remove_branch ) try {
   legacy_tester c;
   while (c.head().block_num() < 11) {
      c.produce_block();
   }
   auto r = c.create_accounts( {"dan"_n,"sam"_n,"pam"_n,"scott"_n} );
   auto res = c.set_producers( {"dan"_n,"sam"_n,"pam"_n,"scott"_n} );
   wlog("set producer schedule to [dan,sam,pam,scott]");

   // run until the producers are installed and its the start of "dan's" round
   BOOST_REQUIRE( produce_until_transition( c, "dan"_n, "sam"_n ) );
   c.produce_block(); // after `push_blocks`, both c and c2 will have seen all of dan's blocks
                      // and one block by sam, so finality will advance again when a new producer produces

   legacy_tester c2(setup_policy::none);
   wlog( "push c1 blocks to c2" );
   push_blocks(c, c2);

   // fork happen after block fork_num
   uint32_t fork_num = c.head().block_num();

   BOOST_REQUIRE_EQUAL(fork_num, c2.head().block_num());

   auto nextproducer = [](legacy_tester &c, int skip_interval) ->account_name {
      auto head_time = c.head().block_time();
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

   BOOST_REQUIRE_EQUAL(fork_num + 24u, c.head().block_num());  // dan and sam each produced 12 blocks
   BOOST_REQUIRE_EQUAL(fork_num + 12u, c2.head().block_num()); // only scott produced its 12 blocks

   // push fork from c2 => c
   size_t p = fork_num;

   while ( p < c2.head().block_num()) {
      auto fb = c2.fetch_block_by_number(++p);
      c.push_block(fb);
   }

   BOOST_REQUIRE_EQUAL(fork_num + 12u, c.head().block_num());

} FC_LOG_AND_RETHROW()


// ---------------------------- validator_accepts_valid_blocks ---------------------------------
/**
 *  Tests that a validating node does not accept a block which is considered invalid by another node.
 */
template <class TESTER>
void test_validator_accepts_valid_blocks() try {

   TESTER n1;
   TESTER n2;
   TESTER n3;

   n1.produce_block();

   auto id = n1.head().id();

   signed_block_ptr first_block;
   block_id_type first_id;
   signed_block_header first_header;

   auto c = n2.control->accepted_block().connect( [&]( block_signal_params t ) {
      const auto& [ block, id ] = t;
      first_block = block;
      first_id = id;
      first_header = static_cast<signed_block_header>(*block);
   } );

   push_blocks( n1, n2 );

   BOOST_CHECK_EQUAL( n2.head().id(), id );

   BOOST_REQUIRE( first_block );
   const auto& first_bp = n2.fetch_block_by_id(first_id);
   BOOST_CHECK_EQUAL( first_bp->calculate_id(), first_block->calculate_id() );
   BOOST_CHECK( first_bp->producer_signature == first_block->producer_signature );

   c.disconnect();

   n3.push_block( first_block );

   BOOST_CHECK_EQUAL( n3.head().id(), id );
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE( validator_accepts_valid_blocks ) {
   test_validator_accepts_valid_blocks<legacy_tester>();
   test_validator_accepts_valid_blocks<tester>();
}

// ---------------------------- read_modes ---------------------------------
template <class TESTER>
void test_read_modes() try {
   TESTER c;
   c.produce_block();
   c.produce_block();
   auto r = c.create_accounts( {"dan"_n,"sam"_n,"pam"_n} );
   c.produce_block();
   auto res = c.set_producers( {"dan"_n,"sam"_n,"pam"_n} );
   c.produce_blocks(200);
   auto head_block_num = c.head().block_num();
   auto last_irreversible_block_num = c.last_irreversible_block_num();

   TESTER head(setup_policy::none, db_read_mode::HEAD);
   push_blocks(c, head);
   BOOST_CHECK_EQUAL(head_block_num, head.fork_db_head().block_num());
   BOOST_CHECK_EQUAL(head_block_num, head.head().block_num());

   TESTER irreversible(setup_policy::none, db_read_mode::IRREVERSIBLE);
   push_blocks(c, irreversible);
   BOOST_CHECK_EQUAL(head_block_num, irreversible.fork_db_head().block_num());
   BOOST_CHECK_EQUAL(last_irreversible_block_num, irreversible.head().block_num());

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE( read_modes ) {
   test_read_modes<legacy_tester>();
   test_read_modes<tester>();
}

// ---------------------------- irreversible_mode ---------------------------------
BOOST_AUTO_TEST_CASE( irreversible_mode ) try {
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
   auto hbn1 = main.head().block_num();
   auto lib1 = main.last_irreversible_block_num();

   BOOST_REQUIRE( produce_until_transition( main, "producer2"_n, "producer1"_n, 11) );

   auto hbn2 = main.head().block_num();
   auto lib2 = main.last_irreversible_block_num();

   BOOST_REQUIRE( lib2 < hbn1 );

   legacy_tester other(setup_policy::none);

   push_blocks( main, other );
   BOOST_CHECK_EQUAL( other.head().block_num(), hbn2 );

   BOOST_REQUIRE( produce_until_transition( main, "producer1"_n, "producer2"_n, 12) );
   BOOST_REQUIRE( produce_until_transition( main, "producer2"_n, "producer1"_n, 12) );

   auto hbn3 = main.head().block_num();
   auto lib3 = main.last_irreversible_block_num();

   BOOST_REQUIRE( lib3 >= hbn1 );

   BOOST_CHECK_EQUAL( does_account_exist( main, "alice"_n ), true );

   // other forks away from main after hbn2
   BOOST_REQUIRE_EQUAL( other.head().producer().to_string(), "producer2" );

   other.produce_block( fc::milliseconds( 13 * config::block_interval_ms ) ); // skip over producer1's round
   BOOST_REQUIRE_EQUAL( other.head().producer().to_string(), "producer2" );
   auto fork_first_block_id = other.head().id();
   wlog( "{w}", ("w", fork_first_block_id));

   BOOST_REQUIRE( produce_until_transition( other, "producer2"_n, "producer1"_n, 11) ); // finish producer2's round
   BOOST_REQUIRE_EQUAL( other.control->pending_block_producer().to_string(), "producer1" );

   // Repeat two more times to ensure other has a longer chain than main
   other.produce_block( fc::milliseconds( 13 * config::block_interval_ms ) ); // skip over producer1's round
   BOOST_REQUIRE( produce_until_transition( other, "producer2"_n, "producer1"_n, 11) ); // finish producer2's round

   other.produce_block( fc::milliseconds( 13 * config::block_interval_ms ) ); // skip over producer1's round
   BOOST_REQUIRE( produce_until_transition( other, "producer2"_n, "producer1"_n, 11) ); // finish producer2's round

   auto hbn4 = other.head().block_num();
   auto lib4 = other.last_irreversible_block_num();

   BOOST_REQUIRE( hbn4 > hbn3 );
   BOOST_REQUIRE( lib4 < hbn1 );

   legacy_tester irreversible(setup_policy::none, db_read_mode::IRREVERSIBLE);

   push_blocks( main, irreversible, hbn1 );

   BOOST_CHECK_EQUAL( irreversible.fork_db_head().block_num(), hbn1 );
   BOOST_CHECK_EQUAL( irreversible.head().block_num(), lib1 );
   BOOST_CHECK_EQUAL( does_account_exist( irreversible, "alice"_n ), false );

   push_blocks( other, irreversible, hbn4 );

   BOOST_CHECK_EQUAL( irreversible.fork_db_head().block_num(), hbn4 );
   BOOST_CHECK_EQUAL( irreversible.head().block_num(), lib4 );
   BOOST_CHECK_EQUAL( does_account_exist( irreversible, "alice"_n ), false );

   // force push blocks from main to irreversible creating a new branch in irreversible's fork database
   for( uint32_t n = hbn2 + 1; n <= hbn3; ++n ) {
      auto fb = main.fetch_block_by_number( n );
      irreversible.push_block( fb );
   }

   BOOST_CHECK_EQUAL( irreversible.fork_db_head().block_num(), hbn3 );
   BOOST_CHECK_EQUAL( irreversible.head().block_num(), lib3 );
   BOOST_CHECK_EQUAL( does_account_exist( irreversible, "alice"_n ), true );

   {
      auto b = irreversible.fetch_block_by_id( fork_first_block_id );
      BOOST_REQUIRE( b && b->calculate_id() == fork_first_block_id );
      BOOST_TEST( irreversible.block_exists(fork_first_block_id) );
   }

   main.produce_block();
   auto hbn5 = main.head().block_num();
   auto lib5 = main.last_irreversible_block_num();

   BOOST_REQUIRE( lib5 > lib3 );

   push_blocks( main, irreversible, hbn5 );

   {
      auto b = irreversible.fetch_block_by_id( fork_first_block_id );
      BOOST_REQUIRE( !b );
      BOOST_TEST( !irreversible.block_exists(fork_first_block_id) );
   }

} FC_LOG_AND_RETHROW()

// ---------------------------- reopen_fork_db ---------------------------------
template <class TESTER>
void test_reopen_fork_db() try {
   TESTER c1;

   c1.create_accounts( {"alice"_n,"bob"_n,"carol"_n} );
   c1.produce_block();

   auto res = c1.set_producers( {"alice"_n,"bob"_n,"carol"_n} );

   c1.produce_blocks(2);

   produce_until_transition( c1, "carol"_n, "alice"_n );
   c1.produce_block();
   produce_until_transition( c1, "carol"_n, "alice"_n );

   TESTER c2(setup_policy::none);

   if constexpr (std::is_same_v<TESTER, tester>) {
      c1.do_check_for_votes(false);
      c2.do_check_for_votes(false);
   }

   push_blocks( c1, c2 );

   auto fork1_lib_before = c1.last_irreversible_block_num();

   // alice produces a block on fork 1 causing LIB to advance
   c1.produce_block();

   auto fork1_head_block_id = c1.head().id();

   auto fork1_lib_after = c1.last_irreversible_block_num();
   BOOST_REQUIRE( fork1_lib_after > fork1_lib_before );

   auto fork2_lib_before = c2.last_irreversible_block_num();
   BOOST_REQUIRE_EQUAL( fork1_lib_before, fork2_lib_before );

   // carol produces a block on fork 2 skipping over the slots of alice and bob
   c2.produce_block( fc::milliseconds(config::block_interval_ms * 25) );
   auto fork2_start_block = c2.head().block_num();
   c2.produce_block();

   auto fork2_lib_after = c2.last_irreversible_block_num();
   BOOST_REQUIRE_EQUAL( fork2_lib_before, fork2_lib_after );

   for( uint32_t block_num = fork2_start_block; block_num < c2.head().block_num(); ++block_num ) {
      auto fb = c2.fetch_block_by_number( block_num );
      c1.push_block( fb );
   }

   BOOST_REQUIRE( fork1_head_block_id == c1.head().id() ); // new blocks should not cause fork switch

   c1.close();

   c1.open();

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE( reopen_fork_db ) {
   test_reopen_fork_db<legacy_tester>();
   test_reopen_fork_db<tester>();
}

// ---------------------------- push_block_returns_forked_transactions ---------------------------------
BOOST_AUTO_TEST_CASE( push_block_returns_forked_transactions ) try {
   legacy_tester c1;
   while (c1.head().block_num() < 3) {
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
   auto fork_block_num = c1.head().block_num();

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
      trx.expiration = fc::time_point_sec{c1.head().block_time() + fc::seconds( 60 )};
      trx.set_reference_block( cb->calculate_id() );
      trx.sign( get_private_key( config::system_account_name, "active" ), c1.get_chain_id()  );
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
      trx.expiration = fc::time_point_sec{c1.head().block_time() + fc::seconds( 60 )};
      trx.set_reference_block( cb->calculate_id() );
      trx.sign( get_private_key( config::system_account_name, "active" ), c1.get_chain_id()  );
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
      trx.expiration = fc::time_point_sec{c1.head().block_time() + fc::seconds( 60 )};
      trx.set_reference_block( cb->calculate_id() );
      trx.sign( get_private_key( config::system_account_name, "active" ), c1.get_chain_id()  );
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
      trx.expiration = fc::time_point_sec{c1.head().block_time() + fc::seconds( 60 )};
      trx.set_reference_block( b->calculate_id() ); // tapos to dan's block should be rejected on fork switch
      trx.sign( get_private_key( config::system_account_name, "active" ), c1.get_chain_id()  );
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
   for( uint32_t start = fork_block_num + 1, end = c2.head().block_num(); start <= end; ++start ) {
      auto fb = c2.fetch_block_by_number( start );
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

   BOOST_REQUIRE_EXCEPTION(c1.get_account( "test1"_n ), fc::exception,
                           [a="test1"_n] (const fc::exception& e)->bool {
                              return std::string( e.what() ).find( a.to_string() ) != std::string::npos;
                           }) ;
   BOOST_REQUIRE_EXCEPTION(c1.get_account( "test2"_n ), fc::exception,
                           [a="test2"_n] (const fc::exception& e)->bool {
                              return std::string( e.what() ).find( a.to_string() ) != std::string::npos;
                           }) ;
   BOOST_REQUIRE_EXCEPTION(c1.get_account( "test3"_n ), fc::exception,
                           [a="test3"_n] (const fc::exception& e)->bool {
                              return std::string( e.what() ).find( a.to_string() ) != std::string::npos;
                           }) ;
   BOOST_REQUIRE_EXCEPTION(c1.get_account( "test4"_n ), fc::exception,
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
   BOOST_REQUIRE_EQUAL( c1.get_account( "test1"_n ).name,  "test1"_n );
   BOOST_REQUIRE_EQUAL( c1.get_account( "test2"_n ).name,  "test2"_n );
   BOOST_REQUIRE_EQUAL( c1.get_account( "test3"_n ).name,  "test3"_n );

   // failed because of tapos to forked out block
   BOOST_REQUIRE_EXCEPTION(c1.get_account( "test4"_n ), fc::exception,
                           [a="test4"_n] (const fc::exception& e)->bool {
                              return std::string( e.what() ).find( a.to_string() ) != std::string::npos;
                           }) ;

} FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_SUITE_END()
