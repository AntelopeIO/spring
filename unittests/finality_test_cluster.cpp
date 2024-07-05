#include "finality_test_cluster.hpp"

using namespace eosio::chain;

// Construct a test network and activate IF.

void finality_node_t::corrupt_vote_block_id() {
   auto& last_vote = votes.back();
   orig_vote = std::make_shared<vote_message>(*last_vote);
   last_vote->block_id.data()[0] ^= 1; // flip one bit
}

void finality_node_t::corrupt_vote_finalizer_key() {
   auto& last_vote = votes.back();
   orig_vote = std::make_shared<vote_message>(*last_vote);

   // corrupt the finalizer_key (manipulate so it is different)
   auto g1 = last_vote->finalizer_key.jacobian_montgomery_le();
   g1 = bls12_381::aggregate_public_keys(std::array{g1, g1});
   auto affine = g1.toAffineBytesLE(bls12_381::from_mont::yes);
   last_vote->finalizer_key = fc::crypto::blslib::bls_public_key(affine);
}

void finality_node_t::corrupt_vote_signature() {
   auto& last_vote = votes.back();
   orig_vote = std::make_shared<vote_message>(*last_vote);

   // corrupt the signature
   auto g2 = last_vote->sig.jacobian_montgomery_le();
   g2 = bls12_381::aggregate_signatures(std::array{g2, g2});
   auto affine = g2.toAffineBytesLE(bls12_381::from_mont::yes);
   last_vote->sig = fc::crypto::blslib::bls_signature(affine);
}

bool finality_node_t::lib_advancing() {
   if (lib_num() > prev_lib_num) {
      prev_lib_num = lib_num();
      return true;
   }
   assert(lib_num() == prev_lib_num);
   return false;
}

// private methods follow
void finality_node_t::setup(size_t first_node_key, size_t num_node_keys) {
   using namespace eosio::testing;

   cur_key = first_node_key;
   finkeys.set_node_finalizers(first_node_key, num_node_keys);

   control->voted_block().connect( [&]( const eosio::chain::vote_signal_params& v ) {
      votes.emplace_back(std::get<2>(v));
   });
}

// Update "vote_index" vote on node according to `mode` parameter
vote_message_ptr finality_node_t::get_vote(size_t vote_index, vote_mode mode) {
   if (votes.empty())
      return {};

   if (vote_index == (size_t)-1)
      vote_index = votes.size() - 1;

   assert(vote_index < votes.size());
   auto vote = votes[vote_index];
   if( mode == vote_mode::strong ) {
      vote->strong = true;
   } else {
      vote->strong = false;

      // fetch the strong digest
      auto strong_digest = control->get_strong_digest_by_id(vote->block_id);
      // convert the strong digest to weak and sign it
      vote->sig = finkeys.privkeys.at(cur_key).sign(eosio::chain::create_weak_digest(strong_digest));
   }

   return vote;
}
