#pragma once

#include <eosio/chain/finality/finalizer_authority.hpp>
#include <fc/crypto/bls_private_key.hpp>

#pragma GCC diagnostic push
   #pragma GCC diagnostic ignored "-Wsign-compare"
   #include <boost/test/unit_test.hpp>
#pragma GCC diagnostic pop

#include <eosio/testing/tester.hpp>

// ----------------------------------------------------------------------------
// Set up a test network which consists of 4 nodes (one producer, 4 finalizers)
//
//   * node0 produces blocks and pushes them to [node1, node2, node3];
//     node0 votes on the blocks it produces internally.
//
//   * [node1, node2, node3] vote on proposals sent by node0, votes are sent
//     to node0 when `process_vote` is called
//
// Each node has one finalizer, quorum is computed to 3
// After starup up, IF is activated on node0.
//
// APIs are provided to modify/delay/alter/re-order/remove votes
// from [node1, node2, node3] to node0.
// ----------------------------------------------------------------------------
class finality_test_cluster {
public:
   using vote_message_ptr = eosio::chain::vote_message_ptr;
   using vote_status      = eosio::chain::vote_status;
   using signed_block_ptr = eosio::chain::signed_block_ptr;
   using tester           = eosio::testing::tester;
   static constexpr size_t num_nodes = 4;

   // actual quorum - 1 since node0 processes its own votes
   static constexpr size_t num_needed_for_quorum = (num_nodes * 2) / 3;

   static_assert(num_needed_for_quorum < num_nodes,
                 "this is needed for some tests (conflicting_votes_strong_first for ex)");

   enum class vote_mode {
      strong,
      weak,
   };

   // Construct a test network and activate IF.
   finality_test_cluster(size_t num_keys = 80, size_t fin_policy_size = num_nodes);

   // node0 produces a block and pushes it to node1 and node2
   signed_block_ptr produce_and_push_block();

   // Produces and propagate finality votes block_count blocks.
   signed_block_ptr produce_blocks(uint32_t blocks_count);

   // Produces a number of blocks and returns true if LIB is advancing.
   // This function can be only used at the end of a test as it clears
   // node1_votes and node2_votes when starting.
   bool produce_blocks_and_verify_lib_advancing();

   // returns true if lib advanced on all nodes since we last checked
   size_t lib_advancing();

   // returns first node to not vote
   size_t process_votes(size_t start_idx, size_t num_voting_nodes, size_t vote_index = (size_t)-1,
                        vote_mode mode = vote_mode::strong, bool duplicate = false) {
      assert(num_voting_nodes > 0 && (num_voting_nodes + start_idx <= num_nodes));
      size_t i = start_idx;
      for (; i<num_voting_nodes+start_idx; ++i)
         nodes[i].process_vote(*this, vote_index, mode, duplicate);
      return i;
   }

   void clear_votes_and_reset_lib() {
      for (auto& n : nodes)
         n.clear_votes_and_reset_lib();
   }

   struct node_t : public tester {
      uint32_t                                prev_lib_num{0};
      std::mutex                              votes_mtx;
      std::vector<vote_message_ptr>           votes;
      eosio::chain::vote_message_ptr          orig_vote;
      eosio::testing::finalizer_keys<tester>  finkeys;
      size_t                                  cur_key; // index of key used in current policy

      node_t() : finkeys(*this) {}

      size_t last_vote_index() const { return votes.size() - 1; }

      void setup(size_t first_node_key, size_t num_node_keys);

      // returns true if LIB advances on this node since we last checked
      bool lib_advancing();

      uint32_t lib_num() const { return lib_block->block_num(); }

      // Intentionally corrupt node's vote's block_id and save the original vote
      void corrupt_vote_block_id();

      // Intentionally corrupt node's vote's finalizer_key and save the original vote
      void corrupt_vote_finalizer_key();

      // Intentionally corrupt node's vote's signature and save the original vote
      void corrupt_vote_signature();

      // Restore node's original vote
      void restore_to_original_vote(size_t idx) {
         std::lock_guard g(votes_mtx);
         if (idx == (size_t)-1)
            votes.back() = orig_vote;
         else
            votes[idx] = orig_vote;
      }

      void clear_votes_and_reset_lib() {
         std::lock_guard g(votes_mtx);
         votes.clear();
         prev_lib_num = lib_num();
      }

      // Update "vote_index" vote on node according to `mode` parameter, and send and process
      // the vote on node0 which is the producer (and Savanna leader)
      vote_status process_vote(finality_test_cluster& cluster, size_t vote_index = (size_t)-1,
                               vote_mode mode = vote_mode::strong, bool duplicate = false);

   };

private:
   std::atomic<uint32_t>      last_connection_vote{0};
   std::atomic<vote_status>   last_vote_status{};

public:
   std::array<node_t, num_nodes>      nodes;

   node_t& node0 = nodes[0];
   node_t& node1 = nodes[1];

private:
   // sets up "node_index" node
   void setup_node(node_t& node, eosio::chain::account_name local_finalizer);

   // send the vote message to node0 which is the producer (and Savanna leader), and wait till processed
   vote_status process_vote(vote_message_ptr& vote, bool duplicate);

   vote_status wait_on_vote(uint32_t connection_id, bool duplicate);
};
