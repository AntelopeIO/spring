#pragma once

#include <eosio/chain/block.hpp>
#include <eosio/chain/hotstuff/finalizer_authority.hpp>
#include <fc/crypto/bls_private_key.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include <boost/test/unit_test.hpp>
#pragma GCC diagnostic pop
#include <eosio/testing/tester.hpp>

// Set up a test network which consists of 3 nodes:
//   * node0 produces blocks and pushes them to node1 and node2;
//     node0 votes the blocks it produces internally.
//   * node1 votes on the proposal sent by node0
//   * node2 votes on the proposal sent by node0
// Each node has one finalizer: node0 -- "node0"_n, node1 -- "node1"_n, node2 -- "node2"_n.
// Quorum is set to 2.
// At start up, head is at the IF Genesis block

using namespace eosio::chain;

class svnn_ibc_test_cluster {
public:

   enum class vote_mode {
      strong,
      weak,
   };

   struct node_info {
      eosio::testing::tester                  node;
      uint32_t                                prev_lib_num{0};
      std::vector<eosio::chain::vote_message> votes;
      fc::crypto::blslib::bls_private_key     priv_key;
   };

   // Construct a test network and set head to IF Genesis for all nodes
   svnn_ibc_test_cluster() {
      using namespace eosio::testing;

      setup_node(node0, "node0"_n);
      setup_node(node1, "node1"_n);
      setup_node(node2, "node2"_n);

      // collect node1's votes
      node1.node.control->voted_block().connect( [&]( const eosio::chain::vote_message& vote ) {
         node1.votes.emplace_back(vote);
      });
      // collect node2's votes
      node2.node.control->voted_block().connect( [&]( const eosio::chain::vote_message& vote ) {
         node2.votes.emplace_back(vote);
      });

   }

   // send node1's vote identified by "vote_index" in the collected votes
   eosio::chain::vote_status process_node1_vote(uint32_t vote_index, vote_mode mode = vote_mode::strong) {
      return process_vote( node1, vote_index, mode );
   }

   // send node1's latest vote
   eosio::chain::vote_status process_node1_vote(vote_mode mode = vote_mode::strong) {
      return process_vote( node1, mode );
   }

   // send node2's vote identified by "vote_index" in the collected votes
   eosio::chain::vote_status process_node2_vote(uint32_t vote_index, vote_mode mode = vote_mode::strong) {
      return process_vote( node2, vote_index, mode );
   }

   // send node2's latest vote
   eosio::chain::vote_status process_node2_vote(vote_mode mode = vote_mode::strong) {
      return process_vote( node2, mode );
   }

   // node0 produces a block and pushes it to node1 and node2
   signed_block_ptr produce_and_push_block() {
      signed_block_ptr b = node0.node.produce_block();
      node1.node.push_block(b);
      node2.node.push_block(b);
      return b;
   }

   std::array<node_info, 3> nodes;
   node_info& node0 = nodes[0];
   node_info& node1 = nodes[1];
   node_info& node2 = nodes[2];

   eosio::chain::vote_message node1_orig_vote;

   // sets up "node_index" node
   void setup_node(node_info& node, eosio::chain::account_name local_finalizer) {
      using namespace eosio::testing;

      //pre-IF

      auto block_1 = node.node.produce_block();
      auto block_2 = node.node.produce_block();

      // activate IF
      eosio::testing::base_tester::finalizer_policy_input policy_input = {
         .finalizers       = { {.name = "node0"_n, .weight = 1},
                               {.name = "node1"_n, .weight = 1},
                               {.name = "node2"_n, .weight = 1}},
         .threshold        = 2,
         .local_finalizers = {local_finalizer}
      };

      auto [trace_ptr, priv_keys] = node.node.set_finalizers(policy_input);
      FC_ASSERT( priv_keys.size() == 1, "number of private keys should be 1" );
      node.priv_key = priv_keys[0];  // we only have one private key

   }

   // send a vote to node0
   eosio::chain::vote_status process_vote(node_info& node, size_t vote_index, vote_mode mode) {
      FC_ASSERT( vote_index < node.votes.size(), "out of bound index in process_vote" );
      auto& vote = node.votes[vote_index];
      if( mode == vote_mode::strong ) {
         vote.strong = true;
      } else {
         vote.strong = false;

         // fetch the strong digest
         auto strong_digest = node.node.control->get_strong_digest_by_id(vote.block_id);
         // convert the strong digest to weak and sign it
         vote.sig = node.priv_key.sign(eosio::chain::create_weak_digest(strong_digest));
      }

      return node0.node.control->process_vote_message( vote );
   }

   eosio::chain::vote_status process_vote(node_info& node, vote_mode mode) {
      auto vote_index = node.votes.size() - 1;
      return process_vote( node, vote_index, mode );
   }

};
