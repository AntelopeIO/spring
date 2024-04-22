#pragma once

#include <eosio/chain/finality/finalizer_authority.hpp>
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
// After starup up, IF are activated on both nodes.
//
// APIs are provided to modify/delay/reoder/remove votes from node1 and node2 to node0.
class finality_test_cluster {
public:
   using vote_message_ptr = eosio::chain::vote_message_ptr;
   using vote_status = eosio::chain::vote_status;

   enum class vote_mode {
      strong,
      weak,
   };

   // Construct a test network and activate IF.
   finality_test_cluster();

   // node0 produces a block and pushes it to node1 and node2
   void produce_and_push_block();

   // Produces a number of blocks and returns true if LIB is advancing.
   // This function can be only used at the end of a test as it clears
   // node1_votes and node2_votes when starting.
   bool produce_blocks_and_verify_lib_advancing();

   struct node_t : public eosio::testing::tester {
      uint32_t                                prev_lib_num{0};
      std::mutex                              votes_mtx;
      std::vector<vote_message_ptr>           votes;
      fc::crypto::blslib::bls_private_key     priv_key;
      eosio::chain::vote_message_ptr          orig_vote;
      //eosio::testing::finalizer_keys          finkeys;

      //node_t() : finkeys(*this, 50, 3) {}

      size_t last_vote_index() const { return votes.size() - 1; }

      void setup(eosio::chain::account_name local_finalizer);

      // returns true if LIB advances on "node_index" node
      bool lib_advancing();

      uint32_t lib() const { return control->last_irreversible_block_num(); }

      // Intentionally corrupt node's vote's block_id and save the original vote
      void corrupt_vote_block_id();

      // Intentionally corrupt node's vote's finalizer_key and save the original vote
      void corrupt_vote_finalizer_key();

      // Intentionally corrupt node's vote's signature and save the original vote
      void corrupt_vote_signature();

      // Restore node's original vote
      void restore_to_original_vote();

      // Update "vote_index" vote on node according to `mode` parameter, and send and process the vote
      // on  node0 which is the producer (and Savanna leader)
      vote_status process_vote(finality_test_cluster& cluster, size_t vote_index = (size_t)-1,
                               vote_mode mode = vote_mode::strong, bool duplicate = false);

   };

private:
   std::atomic<uint32_t>      last_connection_vote{0};
   std::atomic<vote_status>   last_vote_status{};

   std::array<node_t, 3> nodes;

public:
   node_t& node0 = nodes[0];
   node_t& node1 = nodes[1];
   node_t& node2 = nodes[2];

private:
   // sets up "node_index" node
   void setup_node(node_t& node, eosio::chain::account_name local_finalizer);

   // send the vote message to node0 which is the producer (and Savanna leader), and wait till processed
   vote_status process_vote(vote_message_ptr& vote, bool duplicate);

   vote_status wait_on_vote(uint32_t connection_id, bool duplicate);
};
