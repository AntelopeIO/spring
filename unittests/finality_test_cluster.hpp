#pragma once

#include <eosio/chain/finalizer_authority.hpp>
#include <fc/crypto/bls_private_key.hpp>

#pragma GCC diagnostic push
   #pragma GCC diagnostic ignored "-Wsign-compare"
   #include <boost/test/unit_test.hpp>
#pragma GCC diagnostic pop

#include <eosio/testing/tester.hpp>


// ----------------------------------------------------------------------------
struct finality_node_t : public eosio::testing::tester {
   using vote_message_ptr = eosio::chain::vote_message_ptr;
   using vote_result_t    = eosio::chain::vote_result_t;

   enum class vote_mode {
      strong,
      weak,
   };

   uint32_t                                prev_lib_num{0};
   std::vector<vote_message_ptr>           votes;
   eosio::chain::vote_message_ptr          orig_vote;
   eosio::testing::finalizer_keys<tester>  finkeys;
   size_t                                  cur_key{0}; // index of key used in current policy

   finality_node_t() : eosio::testing::tester(eosio::testing::setup_policy::full_except_do_not_transition_to_savanna),  finkeys(*this) {}

   size_t last_vote_index() const {
      assert(!votes.empty());
      return votes.size() - 1;
   }

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
      assert(!votes.empty());

      if (idx == (size_t)-1)
         votes.back() = orig_vote;
      else {
         assert(idx < votes.size());
         votes[idx] = orig_vote;
      }
   }

   void clear_votes_and_reset_lib() {
      votes.clear();
      prev_lib_num = lib_num();
   }

   // Update "vote_index" vote on node according to `mode` parameter, and returns
   // the updated vote.
   vote_message_ptr get_vote(size_t vote_index = (size_t)-1, vote_mode mode = vote_mode::strong);

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
   using vote_result_t    = eosio::chain::vote_result_t;
   using signed_block_ptr = eosio::chain::signed_block_ptr;
   using tester           = eosio::testing::tester;
   using vote_mode        = finality_node_t::vote_mode;
   using bls_public_key   = fc::crypto::blslib::bls_public_key;

   static constexpr size_t num_nodes = NUM_NODES;
   static constexpr size_t keys_per_node = 10;

   // actual quorum - 1 since node0 processes its own votes
   static constexpr size_t num_needed_for_quorum = (num_nodes * 2) / 3;

   static_assert(num_needed_for_quorum < num_nodes,
                 "this is needed for some tests (conflicting_votes_strong_first for example)");

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

      // check that node0 aggregates votes correctly, and that after receiving a vote from another
      // node, that vote is aggregated into a QC (which we check in wait_on_aggregate_vote).
      // -----------------------------------------------------------------------------------------
      node0.control->aggregated_vote().connect( [&]( const eosio::chain::vote_signal_params& v ) {
         last_vote_status = std::get<1>(v);
         last_connection_vote = std::get<0>(v);
      });

      // set initial finalizer policy
      // ----------------------------
      for (size_t i=0; i<nodes.size(); ++i)
         fin_policy_indices_0[i] = i * split;
      fin_policy_pubkeys_0 = node0.finkeys.set_finalizer_policy(fin_policy_indices_0).pubkeys;

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

   // node0 produces a block and pushes it to all other nodes from the cluster
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

   eosio::testing::produce_block_result_t produce_and_push_block_ex() {
      auto b = node0.produce_block_ex();
      for (size_t i=1; i<nodes.size(); ++i)
         nodes[i].push_block(b.block);
      return b;
   }

   // Produces a number of blocks and returns true if LIB is advancing.
   // This function can be only used at the end of a test as it clears
   // node1 to nodeN votes when starting.
   bool produce_blocks_and_verify_lib_advancing() {
      // start from fresh
      clear_votes_and_reset_lib();

      produce_and_push_block();
      for (size_t i = 0; i < eosio::testing::num_chains_to_final; ++i) {
         process_votes(1, num_needed_for_quorum);
         produce_and_push_block();
         if (num_lib_advancing() < num_nodes)
            return false;
      }

      return true;
   }

   // returns the number of nodes on which `lib` advanced since we last checked
   size_t num_lib_advancing() {
      size_t num_advancing = 0;
      for (auto& n : nodes)
         if (n.lib_advancing())
            ++num_advancing;
      return num_advancing;
   }

   vote_result_t process_vote(size_t node_idx, size_t vote_index = (size_t)-1,
                            vote_mode mode = vote_mode::strong, bool duplicate = false) {
      auto vote = nodes[node_idx].get_vote(vote_index, mode);
      return vote ? process_vote(vote, duplicate) : vote_result_t::unknown_block;;
   }

   // returns first node to not vote
   size_t process_votes(size_t start_idx, size_t num_voting_nodes, size_t vote_index = (size_t)-1,
                        vote_mode mode = vote_mode::strong, bool duplicate = false) {
      assert(num_voting_nodes > 0 && (num_voting_nodes + start_idx <= num_nodes));
      for (size_t i = start_idx; i<num_voting_nodes+start_idx; ++i)
         process_vote(i, vote_index, mode, duplicate);
      return num_voting_nodes + start_idx;
   }

   // propagate votes to node1, node2, etc. according to their ordinal position in the bool vector
   // (shifted by one to account for node0)
   void process_finalizer_votes(const std::vector<bool>& votes) {
      assert(votes.size() == num_nodes-1);
      for (size_t i = 1; i<num_nodes; i++)
         if (votes[i-1]) process_vote(i, -1, vote_mode::strong, false);
   }

   void clear_votes_and_reset_lib() {
      for (auto& n : nodes)
         n.clear_votes_and_reset_lib();
   }

private:
   std::atomic<uint32_t>      last_connection_vote{0};
   std::atomic<vote_result_t>   last_vote_status{};

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
   vote_result_t process_vote(vote_message_ptr& vote, bool duplicate) {
      static uint32_t connection_id = 0;
      node0.control->process_vote_message( ++connection_id, vote );
      if (eosio::chain::block_header::num_from_id(vote->block_id) > node0.lib_num())
         return wait_on_aggregate_vote(connection_id, duplicate);
      return vote_result_t::unknown_block;
   }

   vote_result_t wait_on_aggregate_vote(uint32_t connection_id, bool duplicate)  {
      // wait for this node's vote to be processed
      // duplicates are not signaled
      // This wait is not strictly necessary since the controller is set (via `disable_async_aggregation(true)`)
      // to aggregate votes (and emit the `aggregated_vote` signal) synchronously.
      // -------------------------------------------------------------------------------------------------------
      size_t retrys = 200;
      while ( (last_connection_vote != connection_id) && --retrys) {
         std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      if (!duplicate && last_connection_vote != connection_id) {
         FC_ASSERT(false, "Never received vote");
      } else if (duplicate && last_connection_vote == connection_id) {
         FC_ASSERT(false, "Duplicate should not have been signaled");
      }
      return duplicate ? vote_result_t::duplicate : last_vote_status.load();
   }
};
