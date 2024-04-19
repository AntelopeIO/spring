#include "finality_test_cluster.hpp"

using vote_status = eosio::chain::vote_status;
using vote_message_ptr = eosio::chain::vote_message_ptr;

static auto num_from_id(const eosio::chain::block_id_type &id) {
   return eosio::chain::block_header::num_from_id(id);
}

// Construct a test network and activate IF.
finality_test_cluster::finality_test_cluster() {
   using namespace eosio::testing;

   node0.setup("node0"_n);
   node1.setup("node1"_n);
   node2.setup("node2"_n);

   produce_and_push_block(); // make setfinalizer irreversible

   // node0's votes
   node0.control->voted_block().connect( [&]( const eosio::chain::vote_signal_params& v ) {
      last_vote_status = std::get<1>(v);
      last_connection_vote = std::get<0>(v);
   });
   // collect node1's votes
   node1.control->voted_block().connect( [&]( const eosio::chain::vote_signal_params& v ) {
      std::lock_guard g(node1.votes_mtx);
      node1.votes.emplace_back(std::get<2>(v));
   });
   // collect node2's votes
   node2.control->voted_block().connect( [&]( const eosio::chain::vote_signal_params& v ) {
      std::lock_guard g(node2.votes_mtx);
      node2.votes.emplace_back(std::get<2>(v));
   });

   // form a 3-chain to make LIB advacing on node0
   // node0's vote (internal voting) and node1's vote make the quorum
   for (auto i = 0; i < 3; ++i) {
      produce_and_push_block();
      node1.process_vote(*this);
   }
   FC_ASSERT(node0.lib_advancing(), "LIB has not advanced on node0");

   // QC extension in the block sent to node1 and node2 makes them LIB advancing
   produce_and_push_block();
   node1.process_vote(*this);
   FC_ASSERT(node1.lib_advancing(), "LIB has not advanced on node1");
   FC_ASSERT(node2.lib_advancing(), "LIB has not advanced on node2");

   // clean up processed votes
   for (auto& n : nodes) {
      std::lock_guard g(n.votes_mtx);
      n.votes.clear();
      n.prev_lib_num = n.control->if_irreversible_block_num();
   }
}

vote_status finality_test_cluster::wait_on_vote(uint32_t connection_id, bool duplicate) {
   // wait for this node's vote to be processed
   // duplicates are not signaled
   size_t retrys = 200;
   while ( (last_connection_vote != connection_id) && --retrys) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }
   if (!duplicate && last_connection_vote != connection_id) {
      FC_ASSERT(false, "Never received vote");
   } else if (duplicate && last_connection_vote == connection_id) {
      FC_ASSERT(false, "Duplicate should not have been signaled");
   }
   return duplicate ? vote_status::duplicate : last_vote_status.load();
}

// node0 produces a block and pushes it to node1 and node2
void finality_test_cluster::produce_and_push_block() {
   auto b = node0.produce_block();
   node1.push_block(b);
   node2.push_block(b);
}

// Produces a number of blocks and returns true if LIB is advancing.
// This function can be only used at the end of a test as it clears
// node1_votes and node2_votes when starting.
bool finality_test_cluster::produce_blocks_and_verify_lib_advancing() {
   // start from fresh
   {
      std::scoped_lock g(node1.votes_mtx, node2.votes_mtx);
      node1.votes.clear();
      node2.votes.clear();
   }

   for (auto i = 0; i < 3; ++i) {
      produce_and_push_block();
      node1.process_vote(*this);
      produce_and_push_block();
      if (!node0.lib_advancing() || !node1.lib_advancing() || !node2.lib_advancing()) {
         return false;
      }
   }

   return true;
}

void finality_test_cluster::node_t::corrupt_vote_block_id() {
   std::lock_guard g(votes_mtx);
   orig_vote = votes[0];

   if( votes[0]->block_id.data()[0] == 'a' ) {
      votes[0]->block_id.data()[0] = 'b';
   } else {
      votes[0]->block_id.data()[0] = 'a';
   }
}

void finality_test_cluster::node_t::corrupt_vote_finalizer_key() {
   std::lock_guard g(votes_mtx);
   orig_vote = votes[0];

   // corrupt the finalizer_key (manipulate so it is different)
   auto g1 = votes[0]->finalizer_key.jacobian_montgomery_le();
   g1 = bls12_381::aggregate_public_keys(std::array{g1, g1});
   auto affine = g1.toAffineBytesLE(bls12_381::from_mont::yes);
   votes[0]->finalizer_key = fc::crypto::blslib::bls_public_key(affine);
}

void finality_test_cluster::node_t::corrupt_vote_signature() {
   std::lock_guard g(votes_mtx);
   orig_vote = votes[0];

   // corrupt the signature
   auto g2 = votes[0]->sig.jacobian_montgomery_le();
   g2 = bls12_381::aggregate_signatures(std::array{g2, g2});
   auto affine = g2.toAffineBytesLE(bls12_381::from_mont::yes);
   votes[0]->sig = fc::crypto::blslib::bls_signature(affine);
}

void finality_test_cluster::node_t::restore_to_original_vote() {
   std::lock_guard g(votes_mtx);
   votes[0] = orig_vote;
}

bool finality_test_cluster::node_t::lib_advancing() {
   auto curr_lib_num = control->if_irreversible_block_num();
   auto advancing = curr_lib_num > prev_lib_num;
   // update pre_lib_num for next time check
   prev_lib_num = curr_lib_num;
   return advancing;
}

// private methods follow
void finality_test_cluster::node_t::setup(eosio::chain::account_name local_finalizer) {
   using namespace eosio::testing;

   produce_block();
   produce_block();

   // activate savanna
   eosio::testing::base_tester::finalizer_policy_input policy_input = {
      .finalizers       = { {.name = "node0"_n, .weight = 1},
                            {.name = "node1"_n, .weight = 1},
                            {.name = "node2"_n, .weight = 1}},
      .threshold        = 2,
      .local_finalizers = {local_finalizer}
   };

   auto [trace_ptr, priv_keys] = set_finalizers(policy_input);
   FC_ASSERT( priv_keys.size() == 1, "number of private keys should be 1" );
   priv_key = priv_keys[0];  // we only have one private key

   auto block = produce_block();

   // this block contains the header extension for the instant finality
   std::optional<eosio::chain::block_header_extension> ext =
      block->extract_header_extension(eosio::chain::instant_finality_extension::extension_id());
   BOOST_TEST(!!ext);
   std::optional<eosio::chain::finalizer_policy> fin_policy =
      std::get<eosio::chain::instant_finality_extension>(*ext).new_finalizer_policy;
   BOOST_TEST(!!fin_policy);
   BOOST_TEST(fin_policy->finalizers.size() == 3);
   BOOST_TEST(fin_policy->generation == 1);
}

// Update "vote_index" vote on node according to `mode` parameter
vote_status finality_test_cluster::node_t::process_vote(finality_test_cluster& cluster, size_t vote_index,
                                                        vote_mode mode, bool duplicate) {
   std::unique_lock g(votes_mtx);
   if (vote_index == (size_t)-1)
      vote_index = votes.size() - 1;

   FC_ASSERT( vote_index < votes.size(), "out of bound index in process_vote" );
   auto& vote = votes[vote_index];
   if( mode == vote_mode::strong ) {
      vote->strong = true;
   } else {
      vote->strong = false;

      // fetch the strong digest
      auto strong_digest = control->get_strong_digest_by_id(vote->block_id);
      // convert the strong digest to weak and sign it
      vote->sig = priv_key.sign(eosio::chain::create_weak_digest(strong_digest));
   }

   return cluster.process_vote(vote, duplicate);
}

// Update "vote_index" vote on node according to `mode` parameter, and send the vote message to
// node0 which is the producer (and Savanna leader)
vote_status finality_test_cluster::process_vote(vote_message_ptr& vote, bool duplicate) {
   static uint32_t connection_id = 0;
   node0.control->process_vote_message( ++connection_id, vote );
   if (num_from_id(vote->block_id) > node0.lib())
      return wait_on_vote(connection_id, duplicate);
   return vote_status::unknown_block;
}

