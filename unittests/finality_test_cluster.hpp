#pragma once

#include <eosio/chain/finality/finalizer_authority.hpp>
#include <fc/crypto/bls_private_key.hpp>

#pragma GCC diagnostic push
   #pragma GCC diagnostic ignored "-Wsign-compare"
   #include <boost/test/unit_test.hpp>
#pragma GCC diagnostic pop

#include <eosio/testing/tester.hpp>


// ----------------------------------------------------------------------------
struct finality_node_t : public eosio::testing::tester {
   using vote_message_ptr = eosio::chain::vote_message_ptr;
   using vote_status      = eosio::chain::vote_status;

   enum class vote_mode {
      strong,
      weak,
   };

   uint32_t                                prev_lib_num{0};
   std::mutex                              votes_mtx;
   std::vector<vote_message_ptr>           votes;
   eosio::chain::vote_message_ptr          orig_vote;
   eosio::testing::finalizer_keys<tester>  finkeys;
   size_t                                  cur_key{0}; // index of key used in current policy

   finality_node_t() : finkeys(*this) {}

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

   // Update "vote_index" vote on node according to `mode` parameter, and returns
   // the updated vote.
   vote_message_ptr get_vote(size_t vote_index = (size_t)-1, vote_mode mode = vote_mode::strong,
                             bool duplicate = false);

};

struct finality_cluster_config_t {
   bool   transition_to_savanna;
};

// ------------------------------------------------------------------------------------
// finality_test_cluster
// ---------------------
//
// Set up a test network which consists of NUM_NODES nodes (one producer,
// NUM_NODES finalizers)
//
//   * node0 produces blocks and pushes them to [node1, node2, node3, ...];
//     node0 votes on the blocks it produces internally.
//
//   * [node1, node2, node3, ...] vote on proposals sent by node0, votes are sent
//     to node0 when `process_vote` is called
//
// Each node has one finalizer, quorum is computed using the same formula as in the
// system contracts.
//
// After startup up, IF is activated on node0.
//
// APIs are provided to modify/delay/alter/re-order/remove votes
// from [node1, node2, node3, ...] to node0.
//  ------------------------------------------------------------------------------------
template<size_t NUM_NODES> requires (NUM_NODES > 3)
class finality_test_cluster {
public:
   using vote_message_ptr = eosio::chain::vote_message_ptr;
   using vote_status      = eosio::chain::vote_status;
   using signed_block_ptr = eosio::chain::signed_block_ptr;
   using tester           = eosio::testing::tester;
   using vote_mode        = finality_node_t::vote_mode;
   using bls_public_key   = fc::crypto::blslib::bls_public_key;

   static constexpr size_t num_nodes = NUM_NODES;
   static constexpr size_t keys_per_node = 10;

   // actual quorum - 1 since node0 processes its own votes
   static constexpr size_t num_needed_for_quorum = (num_nodes * 2) / 3;

   static_assert(num_needed_for_quorum < num_nodes,
                 "this is needed for some tests (conflicting_votes_strong_first for ex)");

   // Construct a test network and activate IF.
   finality_test_cluster(finality_cluster_config_t config = {.transition_to_savanna = true}) {
      using namespace eosio::testing;
      size_t num_finalizers = nodes.size();

      // -----------------------------------------------------------------------------------
      // each node gets an equal range of keys to be used as local finalizer.
      // for example, the default parameters  are `num_keys = 40` and `fin_policy_size = 4`,
      // which means that for 4 nodes, we'll make each node capable on voting with 10
      // different keys (node0 will use keys 0 .. 9, node1 will use keys 10 .. 19, etc...
      // (see set_node_finalizers() call).
      //
      // The first finalizer_policy (see set_finalizer_policy below) will activate
      // keys 0, 10, 20 and 30
      // -----------------------------------------------------------------------------------
      size_t split = finality_test_cluster::keys_per_node;

      // set initial finalizer key for each node
      // ---------------------------------------
      for (size_t i=0; i<nodes.size(); ++i) {
         nodes[i].finkeys.init_keys(split * num_finalizers, num_finalizers);
         nodes[i].setup(i * split, split);
      }

      // node0's votes
      node0.control->voted_block().connect( [&]( const eosio::chain::vote_signal_params& v ) {
         last_vote_status = std::get<1>(v);
         last_connection_vote = std::get<0>(v);
      });


      // set initial finalizer policy
      // ----------------------------
      for (size_t i=0; i<nodes.size(); ++i)
         fin_policy_indices_0[i] = i * split;
      fin_policy_pubkeys_0 = node0.finkeys.set_finalizer_policy(fin_policy_indices_0);

      if (config.transition_to_savanna) {
         // transition to Savanna
         // ---------------------
         fin_policy_0 = node0.finkeys.transition_to_savanna([&](const signed_block_ptr& b) {
            for (size_t i=1; i<nodes.size(); ++i)
               nodes[i].push_block(b);
            process_votes(1, num_nodes - 1);
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
   }

   // node0 produces a block and pushes it to node1 and node2
   signed_block_ptr produce_and_push_block() {
      auto b = node0.produce_block();
      for (size_t i=1; i<nodes.size(); ++i)
         nodes[i].push_block(b);
      return b;
   }

   // Produces and propagate finality votes for `block_count` blocks.
   signed_block_ptr produce_blocks(uint32_t blocks_count)  {
      signed_block_ptr b;
      for (uint32_t i=0; i<blocks_count; ++i) {
         b = produce_and_push_block();
         process_votes(1, num_nodes - 1);
      }
      return b;
   }

   // Produces a number of blocks and returns true if LIB is advancing.
   // This function can be only used at the end of a test as it clears
   // node1_votes and node2_votes when starting.
   bool produce_blocks_and_verify_lib_advancing() {
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

   // returns true if lib advanced on all nodes since we last checked
   size_t lib_advancing() {
      size_t num_advancing = 0;
      for (auto& n : nodes)
         if (n.lib_advancing())
            ++num_advancing;
      return num_advancing;
   }

   vote_status process_vote(size_t node_idx, size_t vote_index = (size_t)-1,
                            vote_mode mode = vote_mode::strong, bool duplicate = false) {
      auto vote = nodes[node_idx].get_vote(vote_index, mode, duplicate);
      return vote ? process_vote(vote, duplicate) : vote_status::unknown_block;;
   }

   // returns first node to not vote
   size_t process_votes(size_t start_idx, size_t num_voting_nodes, size_t vote_index = (size_t)-1,
                        vote_mode mode = vote_mode::strong, bool duplicate = false) {
      assert(num_voting_nodes > 0 && (num_voting_nodes + start_idx <= num_nodes));
      size_t i = start_idx;
      for (; i<num_voting_nodes+start_idx; ++i)
         process_vote(i, vote_index, mode, duplicate);
      return i;
   }

   void clear_votes_and_reset_lib() {
      for (auto& n : nodes)
         n.clear_votes_and_reset_lib();
   }

private:
   std::atomic<uint32_t>      last_connection_vote{0};
   std::atomic<vote_status>   last_vote_status{};

public:
   std::array<finality_node_t, num_nodes>                 nodes;

   // Used for transition to Savanna
   std::optional<eosio::chain::finalizer_policy> fin_policy_0;         // policy used to transition to Savanna
   std::array<size_t, num_nodes>                 fin_policy_indices_0; // set of key indices used for transition
   std::vector<bls_public_key>                   fin_policy_pubkeys_0; // set of public keys used for transition

   finality_node_t& node0 = nodes[0];
   finality_node_t& node1 = nodes[1];

private:
   // sets up "node_index" node
   void setup_node(finality_node_t& node, eosio::chain::account_name local_finalizer);

   // send the vote message to node0 which is the producer (and Savanna leader), and wait till processed
   vote_status process_vote(vote_message_ptr& vote, bool duplicate) {
      static uint32_t connection_id = 0;
      node0.control->process_vote_message( ++connection_id, vote );
      if (eosio::chain::block_header::num_from_id(vote->block_id) > node0.lib_num())
         return wait_on_vote(connection_id, duplicate);
      return vote_status::unknown_block;
   }

   vote_status wait_on_vote(uint32_t connection_id, bool duplicate)  {
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
};
