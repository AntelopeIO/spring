#include "finality_test_cluster.hpp"

using namespace eosio::chain;

static auto num_from_id(const eosio::chain::block_id_type &id) {
   return eosio::chain::block_header::num_from_id(id);
}

// Construct a test network and activate IF.
finality_test_cluster::finality_test_cluster(size_t num_keys, size_t fin_policy_size)
{
   using namespace eosio::testing;
   size_t num_finalizers = nodes.size();

   // -----------------------------------------------------------------------------------
   // each node gets an equal range of keys to be used as local finalizer.
   // for example, the default parameters  are `num_keys = 80` and `fin_policy_size = 4`,
   // which means that for 4 nodes, we'll make each node capable on voting with 20
   // different keys (node0 will use keys 0 .. 19, node1 will use keys 20 .. 39, etc...
   // (see set_node_finalizers() call).
   //
   // The first finalizer_policy (see set_finalizer_policy below) will activate
   // keys 0, 20, 40 and 60
   // -----------------------------------------------------------------------------------
   size_t split = num_keys / num_finalizers;

   // set initial finalizer key for each node
   // ---------------------------------------
   for (size_t i=0; i<nodes.size(); ++i) {
      nodes[i].finkeys.init_keys(num_keys, fin_policy_size);
      nodes[i].setup(i * split, split);
   }

   // node0's votes
   node0.control->voted_block().connect( [&]( const eosio::chain::vote_signal_params& v ) {
      last_vote_status = std::get<1>(v);
      last_connection_vote = std::get<0>(v);
   });

   // set initial finalizer policy
   // ----------------------------
   std::array<size_t, num_nodes> initial_policy;
   for (size_t i=0; i<nodes.size(); ++i)
      initial_policy[i] = i * split;
   node0.finkeys.set_finalizer_policy(initial_policy);

   // transition to Savanna
   // ---------------------
   node0.finkeys.transition_to_Savanna([&](const signed_block_ptr& b) {
      for (size_t i=1; i<nodes.size(); ++i) {
         nodes[i].push_block(b);
         nodes[i].process_vote(*this);
      }
   });

   // at this point, node0 has a QC to include in next block.
   // Produce that block and push it, but don't process votes so that
   // we don't start with an existing QC
   // ---------------------------------------------------------------
   produce_and_push_block();

   // reset votes and saved lib, so that each test starts in a clean slate
   // --------------------------------------------------------------------
   clear_votes_and_reset_lib();
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

// node0 produces a block and pushes it to other nodes
signed_block_ptr finality_test_cluster::produce_and_push_block() {
   auto b = node0.produce_block();
   for (size_t i=1; i<nodes.size(); ++i)
      nodes[i].push_block(b);
   return b;
}

size_t finality_test_cluster::lib_advancing() {
   size_t num_advancing = 0;
   for (auto& n : nodes)
      if (n.lib_advancing())
         ++num_advancing;
   return num_advancing;
}

// Produces a number of blocks and returns true if LIB is advancing.
// This function can be only used at the end of a test as it clears
// votes from all nodes
bool finality_test_cluster::produce_blocks_and_verify_lib_advancing() {
   // start from fresh
   clear_votes_and_reset_lib();

   produce_and_push_block();
   for (auto i = 0; i < 3; ++i) {
      process_votes(1, num_needed_for_quorum);
      produce_and_push_block();
      if (lib_advancing() < num_nodes)
         return false;
   }

   return true;
}

void finality_test_cluster::node_t::corrupt_vote_block_id() {
   std::lock_guard g(votes_mtx);
   auto& last_vote = votes.back();
   orig_vote = last_vote;

   if( last_vote->block_id.data()[0] == 'a' ) {
      last_vote->block_id.data()[0] = 'b';
   } else {
      last_vote->block_id.data()[0] = 'a';
   }
}

void finality_test_cluster::node_t::corrupt_vote_finalizer_key() {
   std::lock_guard g(votes_mtx);
   auto& last_vote = votes.back();
   orig_vote = last_vote;

   // corrupt the finalizer_key (manipulate so it is different)
   auto g1 = last_vote->finalizer_key.jacobian_montgomery_le();
   g1 = bls12_381::aggregate_public_keys(std::array{g1, g1});
   auto affine = g1.toAffineBytesLE(bls12_381::from_mont::yes);
   last_vote->finalizer_key = fc::crypto::blslib::bls_public_key(affine);
}

void finality_test_cluster::node_t::corrupt_vote_signature() {
   std::lock_guard g(votes_mtx);
   auto& last_vote = votes.back();
   orig_vote = last_vote;

   // corrupt the signature
   auto g2 = last_vote->sig.jacobian_montgomery_le();
   g2 = bls12_381::aggregate_signatures(std::array{g2, g2});
   auto affine = g2.toAffineBytesLE(bls12_381::from_mont::yes);
   last_vote->sig = fc::crypto::blslib::bls_signature(affine);
}

void finality_test_cluster::node_t::restore_to_original_vote() {
   std::lock_guard g(votes_mtx);
   votes.back() = orig_vote;
}

bool finality_test_cluster::node_t::lib_advancing() {
   //std::cout << "curr_lib_num = " << lib_num() << ", prev_lib_num = " << prev_lib_num << '\n';
   if (lib_num() > prev_lib_num) {
      prev_lib_num = lib_num();
      return true;
   }
   assert(lib_num() == prev_lib_num);
   return false;
}

// private methods follow
void finality_test_cluster::node_t::setup(size_t first_node_key, size_t num_node_keys) {
   using namespace eosio::testing;

   cur_key = first_node_key;
   finkeys.set_node_finalizers(first_node_key, num_node_keys);

   control->voted_block().connect( [&]( const eosio::chain::vote_signal_params& v ) {
      std::lock_guard g(votes_mtx);
      votes.emplace_back(std::get<2>(v));
   });
}

// Update "vote_index" vote on node according to `mode` parameter
vote_status finality_test_cluster::node_t::process_vote(finality_test_cluster& cluster, size_t vote_index,
                                                        vote_mode mode, bool duplicate) {
   std::lock_guard g(votes_mtx);
   if (votes.empty())
      return vote_status::unknown_block;

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
      vote->sig = finkeys.privkeys[cur_key].sign(eosio::chain::create_weak_digest(strong_digest));
   }

   return cluster.process_vote(vote, duplicate);
}

// Update "vote_index" vote on node according to `mode` parameter, and send the vote message to
// node0 which is the producer (and Savanna leader)
vote_status finality_test_cluster::process_vote(vote_message_ptr& vote, bool duplicate) {
   static uint32_t connection_id = 0;
   node0.control->process_vote_message( ++connection_id, vote );
   if (num_from_id(vote->block_id) > node0.lib_num())
      return wait_on_vote(connection_id, duplicate);
   return vote_status::unknown_block;
}

