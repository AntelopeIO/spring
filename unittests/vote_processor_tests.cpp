#include <boost/test/unit_test.hpp>

#include <eosio/chain/controller.hpp>
#include <eosio/chain/vote_processor.hpp>
#include <eosio/testing/tester.hpp>
#include <fc/bitutil.hpp>
#include <boost/signals2/signal.hpp>

namespace std {
std::ostream& operator<<(std::ostream& os, const eosio::chain::vote_message& v) {
   os << "vote_message{" << v.block_id << std::endl;
   return os;
}
std::ostream& operator<<(std::ostream& os, const eosio::chain::vote_result_t& v) {
   os << fc::reflector<eosio::chain::vote_result_t>::to_string(v) << std::endl;
   return os;
}
}

namespace {

using namespace eosio;
using namespace eosio::chain;

block_id_type make_block_id(uint32_t block_num) {
   block_id_type block_id;
   block_id._hash[0] &= 0xffffffff00000000;
   block_id._hash[0] += fc::endian_reverse_u32(block_num);
   return block_id;
}

bls_private_key bls_priv_key_0 = bls_private_key::generate();
bls_private_key bls_priv_key_1 = bls_private_key::generate();
bls_private_key bls_priv_key_2 = bls_private_key::generate();
std::vector<bls_private_key> bls_priv_keys{bls_priv_key_0, bls_priv_key_1, bls_priv_key_2};

auto create_genesis_block_state() { // block 2
   auto block = signed_block::create_mutable_block({});

   block->producer = eosio::chain::config::system_account_name;
   auto pub_key = eosio::testing::base_tester::get_public_key( block->producer, "active" );

   std::vector<finalizer_authority> finalizers;
   finalizers.push_back(finalizer_authority{.description = "first", .weight = 1, .public_key = bls_priv_keys.at(0).get_public_key()});
   finalizers.push_back(finalizer_authority{.description = "second", .weight = 1, .public_key = bls_priv_keys.at(1).get_public_key()});
   finalizers.push_back(finalizer_authority{.description = "third", .weight = 1, .public_key = bls_priv_keys.at(2).get_public_key()});
   finalizer_policy new_finalizer_policy{.finalizers = finalizers};
   finalizer_policy_diff new_finalizer_policy_diff = finalizer_policy{}.create_diff(new_finalizer_policy);
   qc_claim_t initial_if_claim { .block_num = 2,
                                 .is_strong_qc = false };
   emplace_extension(block->header_extensions, finality_extension::extension_id(),
                     fc::raw::pack(finality_extension{ initial_if_claim, new_finalizer_policy_diff, {} }));

   producer_authority_schedule schedule = { 0, { producer_authority{block->producer, block_signing_authority_v0{ 1, {{pub_key, 1}} } } } };
   auto genesis = std::make_shared<block_state>();
   block->previous = make_block_id(1);
   genesis->block = signed_block::create_signed_block(std::move(block));
   genesis->activated_protocol_features = std::make_shared<protocol_feature_activation_set>();
   genesis->active_finalizer_policy = std::make_shared<finalizer_policy>(new_finalizer_policy);
   genesis->active_proposer_policy = std::make_shared<proposer_policy>(proposer_policy{.proposer_schedule = schedule});
   genesis->core = finality_core::create_core_for_genesis_block(genesis->block_id, genesis->header.timestamp);
   genesis->block_id = genesis->block->calculate_id();
   return genesis;
}

auto create_test_block_state(const block_state_ptr& prev) {
   static block_timestamp_type timestamp;
   timestamp = timestamp.next(); // each test block state will be unique
   auto block = prev->block->clone();
   block->producer = eosio::chain::config::system_account_name;
   block->previous = prev->id();
   block->timestamp = timestamp;

   auto priv_key = eosio::testing::base_tester::get_private_key( block->producer, "active" );
   auto pub_key  = eosio::testing::base_tester::get_public_key( block->producer, "active" );

   auto sig_digest = digest_type::hash("something");
   block->producer_signature = priv_key.sign( sig_digest );

   vector<private_key_type> signing_keys;
   signing_keys.emplace_back( std::move( priv_key ) );

   auto signer = [&]( digest_type d ) {
      std::vector<signature_type> result;
      result.reserve(signing_keys.size());
      for (const auto& k: signing_keys)
         result.emplace_back(k.sign(d));
      return result;
   };
   block_header_state bhs = *prev;
   bhs.header = *block;
   bhs.header.timestamp = timestamp;
   bhs.header.previous = prev->id();
   bhs.header.schedule_version = block_header::proper_svnn_schedule_version;
   bhs.block_id = block->calculate_id();

   auto bsp = std::make_shared<block_state>(bhs,
      deque<transaction_metadata_ptr>{},
      deque<transaction_receipt>{},
      std::optional<valid_t>{},
      std::optional<qc_t>{},
      signer,
      block_signing_authority_v0{ 1, {{pub_key, 1}} },
      digest_type{});

   return bsp;
}

vote_message_ptr make_empty_message(const block_id_type& id) {
   vote_message_ptr vm = std::make_shared<vote_message>();
   vm->block_id = id;
   return vm;
}

vote_message_ptr make_vote_message(const block_state_ptr& bsp) {
   vote_message_ptr vm = std::make_shared<vote_message>();
   vm->block_id = bsp->id();
   vm->strong = true;
   size_t i = bsp->block_num() % bls_priv_keys.size();
   vm->finalizer_key = bls_priv_keys.at(i).get_public_key();
   vm->sig = bls_priv_keys.at(i).sign({(uint8_t*)bsp->strong_digest.data(), (uint8_t*)bsp->strong_digest.data() + bsp->strong_digest.data_size()});
   return vm;
}

BOOST_AUTO_TEST_SUITE(vote_processor_tests)

BOOST_AUTO_TEST_CASE( vote_processor_test ) {
   vote_signal_t voted_block;

   uint32_t received_connection_id = 0;
   vote_result_t received_vote_status = vote_result_t::unknown_block;
   vote_message_ptr received_vote_message{};

   std::atomic<size_t> signaled = 0;
   std::mutex                               fork_db_mtx;
   std::map<block_id_type, block_state_ptr> fork_db;
   auto add_to_fork_db = [&](const block_state_ptr& bsp) {
      std::lock_guard g(fork_db_mtx);
      fork_db[bsp->id()] = bsp;
   };

   voted_block.connect( [&]( const vote_signal_params& vote_signal ) {
      received_connection_id = std::get<0>(vote_signal);
      received_vote_status = std::get<1>(vote_signal);
      received_vote_message = std::get<2>(vote_signal);
      ++signaled;
   } );

   vote_processor_t vp{[&](const vote_signal_params& p) { voted_block(p); },
                       [&](const block_id_type& id) -> block_state_ptr {
                          std::lock_guard g(fork_db_mtx);
                          return fork_db[id];
                       }};
   vp.start(2, [](const fc::exception& e) {
      edump((e));
      BOOST_REQUIRE(false);
   });

   { // empty fork db, block never found, never signaled
      vote_message_ptr vm1 = make_empty_message(make_block_id(1));
      signaled = 0;
      vp.process_vote_message(1, vm1, async_t::yes);
      for (size_t i = 0; i < 50 && vp.index_size() < 1; ++i) {
         std::this_thread::sleep_for(std::chrono::milliseconds{5});
      }
      BOOST_TEST(vp.index_size() == 1u);
      // move lib past block
      vp.notify_lib(2);
      vp.notify_new_block(async_t::yes);
      for (size_t i = 0; i < 50 && vp.index_size() > 0; ++i) {
         std::this_thread::sleep_for(std::chrono::milliseconds{5});
      }
      BOOST_TEST(vp.index_size() == 0u);
   }
   { // process a valid vote
      signaled = 0;
      auto gensis = create_genesis_block_state();
      auto bsp = create_test_block_state(gensis);
      BOOST_CHECK_EQUAL(bsp->block_num(), 3u);
      vote_message_ptr m1 = make_vote_message(bsp);
      add_to_fork_db(bsp);
      vp.process_vote_message(1, m1, async_t::yes);
      // duplicate ignored
      vp.process_vote_message(1, m1, async_t::yes);
      for (size_t i = 0; i < 50 && signaled.load() < 1; ++i) {
         std::this_thread::sleep_for(std::chrono::milliseconds{5});
      }
      BOOST_TEST(signaled.load() == 1u);
      BOOST_TEST(1u == received_connection_id);
      BOOST_TEST(vote_result_t::success == received_vote_status);
      BOOST_TEST(m1 == received_vote_message);
   }
   { // process an invalid signature vote
      signaled = 0;
      auto gensis = create_genesis_block_state();
      auto bsp = create_test_block_state(gensis);
      BOOST_CHECK_EQUAL(bsp->block_num(), 3u);
      vote_message_ptr m1 = make_vote_message(bsp);
      m1->strong = false; // signed with strong_digest
      add_to_fork_db(bsp);
      vp.process_vote_message(1, m1, async_t::yes);
      for (size_t i = 0; i < 50 && signaled.load() < 1; ++i) {
         std::this_thread::sleep_for(std::chrono::milliseconds{5});
      }
      BOOST_TEST(signaled.load() == 1u);
      BOOST_TEST(1u == received_connection_id);
      BOOST_TEST(vote_result_t::invalid_signature == received_vote_status);
      BOOST_TEST(m1 == received_vote_message);
   }
   { // process two diff block votes
      signaled = 0;
      auto gensis = create_genesis_block_state();
      auto bsp = create_test_block_state(gensis);
      auto bsp2 = create_test_block_state(bsp);
      vote_message_ptr m1 = make_vote_message(bsp);
      vote_message_ptr m2 = make_vote_message(bsp2);
      vp.process_vote_message(2, m1, async_t::yes);
      vp.process_vote_message(3, m2, async_t::yes);
      for (size_t i = 0; i < 5; ++i) {
         if (vp.index_size() == 2) break;
         std::this_thread::sleep_for(std::chrono::milliseconds{5});
      }
      BOOST_TEST(vp.index_size() == 2u);
      std::this_thread::sleep_for(std::chrono::milliseconds{5}); // no votes for awhile
      BOOST_TEST(signaled.load() == 0u);
      add_to_fork_db(bsp);
      vp.notify_new_block(async_t::yes);
      for (size_t i = 0; i < 50 && signaled.load() < 2; ++i) {
         std::this_thread::sleep_for(std::chrono::milliseconds{5});
      }
      BOOST_TEST(signaled.load() == 1u);
      BOOST_TEST(2u == received_connection_id);
      BOOST_TEST(vote_result_t::success == received_vote_status);
      BOOST_CHECK(m1 == received_vote_message);

      add_to_fork_db(bsp2);
      vp.notify_new_block(async_t::yes);
      for (size_t i = 0; i < 50 && signaled.load() < 2; ++i) {
         std::this_thread::sleep_for(std::chrono::milliseconds{5});
      }
      BOOST_TEST(signaled.load() == 2u);
      BOOST_TEST(3u == received_connection_id);
      BOOST_TEST(vote_result_t::success == received_vote_status);
      BOOST_CHECK(m2 == received_vote_message);
   }
}

BOOST_AUTO_TEST_SUITE_END()

}
