#pragma once

#include <eosio/eosio.hpp>
#include <eosio/crypto.hpp>
#include <eosio/crypto_bls_ext.hpp>
#include <eosio/system.hpp>
#include <eosio/transaction.hpp>

#include "bitset.hpp"

using namespace eosio;

namespace savanna {

   struct quorum_certificate {
       //representation of a bitset, where each bit represents the ordinal finalizer position according to canonical sorting rules of the finalizer policy
       std::vector<uint8_t>   finalizers;
       //string representation of a BLS signature
       std::string            signature;
   };
   
   struct finalizer_authority_internal {
      std::string       description;
      uint64_t          weight = 0;
      std::vector<uint8_t> public_key;
   };

   struct finalizer_policy_internal {
      uint32_t                         generation = 0; // sequentially incrementing version number
      uint64_t                         threshold = 0;  // vote weight threshold to finalize blocks
      std::vector<finalizer_authority_internal> finalizers; // Instant Finality voter set

      checksum256 digest() const {
          std::vector<char> serialized = pack(*this);
          return sha256(serialized.data(), serialized.size());
      }
   };

   struct finalizer_authority_input {
      std::string       description;
      uint64_t          weight = 0;
      std::string       public_key;
   };

   struct finalizer_policy_input {
      uint32_t                         generation = 0;
      uint64_t                         threshold = 0;
      std::vector<finalizer_authority_input> finalizers;

      checksum256 digest() const {

         std::vector<finalizer_authority_internal> finalizers_i;
         for (auto f : finalizers){
            std::array<char,96> decoded_key = decode_bls_public_key_to_g1(f.public_key);
            std::vector<uint8_t> vector_key(decoded_key.begin(), decoded_key.end());
            finalizers_i.push_back({f.description, f.weight, vector_key});
         }
         finalizer_policy_internal internal{generation, threshold, std::move(finalizers_i)};
         return internal.digest();
      }
   };

   //Compute the maximum number of layers of a merkle tree for a given number of leaves
   uint64_t calculate_max_depth(uint64_t node_count) {
      if(node_count <= 1)
         return node_count;
      return 64 - __builtin_clzll(2 << (64 - 1 - __builtin_clzll ((node_count - 1)))); //instead of std::bit_ceil available in C++ 20 
   }

   uint32_t reverse_bytes(const uint32_t input){
      return __builtin_bswap32(input);
   }

   checksum256 hash_pair(const std::pair<checksum256, checksum256> p){
      auto result = eosio::pack(p);
      return sha256(result.data(), result.size());
   }

   time_point add_time(const time_point& time, const uint32_t seconds ){
      int64_t total_seconds = (static_cast<int64_t>(time.sec_since_epoch()) + static_cast<int64_t>(seconds));
      microseconds ms = microseconds(total_seconds * 1000000);
      time_point tp = time_point(ms);
      return tp;
   }

   //compute path for proof of inclusion
   std::vector<bool> _get_proof_path(uint64_t leaf_index, const uint64_t leaf_count) {
       std::vector<bool> proof_path;
       uint64_t current_leaf_count = leaf_count;
       uint64_t current_index = leaf_index;
       uint64_t layers_depth = calculate_max_depth(leaf_count) - 1;

       for (uint64_t i = 0; i < layers_depth; ++i) {
           bool is_right = current_index % 2 == 1;
           uint64_t pair_index = is_right ? current_index - 1 : current_index + 1;

           if (pair_index < current_leaf_count) {
               proof_path.push_back(is_right);
           }

           current_leaf_count = (current_leaf_count + 1) / 2;
           current_index /= 2;
       }

       return proof_path;
   }

   //compute the merkle root of target node and vector of merkle branches
   checksum256 _compute_root(const std::vector<checksum256> proof_nodes, const checksum256& target, const uint64_t target_block_index, const uint64_t final_block_index){
       checksum256 hash = target;
       std::vector<bool> proof_path = _get_proof_path(target_block_index, final_block_index+1);
       check(proof_path.size() == proof_nodes.size(), "internal error"); //should not happen
       for (int i = 0 ; i < proof_nodes.size() ; i++){
           const checksum256 node = proof_nodes[i];
           hash = hash_pair(proof_path[i] ? std::make_pair(node, hash) : std::make_pair(hash, node));
       }
       return hash;
   }

   //add two numbers from the g1 group (key aggregation)
   bls_g1 _g1add(const bls_g1& op1, const bls_g1& op2) {
      bls_g1 r;
      bls_g1_add(op1, op2, r);
      return r;
   }

   // verify signature
   bool _verify(const std::string& public_key, const std::string& signature, const std::string& message){
      return bls_signature_verify(decode_bls_public_key_to_g1(public_key), decode_bls_signature_to_g2(signature), message);
   }
   
   //verify that the quorum certificate over the finality digest is valid
   void _check_qc(const quorum_certificate& qc, const checksum256& finality_digest, const finalizer_policy_input finalizer_policy){
      auto fa_itr = finalizer_policy.finalizers.begin();
      auto fa_end_itr = finalizer_policy.finalizers.end();
      size_t finalizer_count = finalizer_policy.finalizers.size();
      savanna::bitset b(finalizer_count, qc.finalizers);

      bool first = true;

      size_t index = 0;
      uint64_t weight = 0;

      bls_g1 agg_pub_key;

      while (fa_itr != fa_end_itr){
          if (b.test(index)){
              bls_g1 pub_key = decode_bls_public_key_to_g1(fa_itr->public_key);
              if (first){
                  first=false;
                  agg_pub_key = pub_key;
              }
              else agg_pub_key = _g1add(agg_pub_key, pub_key);
              weight+=fa_itr->weight;
          }
          index++;
          fa_itr++;
      }

      //verify that we have enough vote weight to meet the quorum threshold of the target policy
      check(weight>=finalizer_policy.threshold, "insufficient signatures to reach quorum");
      std::array<uint8_t, 32> fd_data = finality_digest.extract_as_byte_array();
      std::string message(fd_data.begin(), fd_data.end());

      std::string s_agg_pub_key = encode_g1_to_bls_public_key(agg_pub_key);
      //verify signature validity
      check(_verify(s_agg_pub_key, qc.signature, message), "signature verification failed");
   }


   struct authseq {
      name account;
      uint64_t sequence = 0;
      EOSLIB_SERIALIZE( authseq, (account)(sequence) )
   };

   struct action_base {
      name             account;
      name             name;
      std::vector<permission_level> authorization;
   };

   struct action :  action_base {
      std::vector<char>    data;
      std::vector<char>    return_value;
      checksum256 digest() const {
         checksum256 hashes[2];
         const action_base* base = this;
         const auto action_input_size = pack_size(data);
         const auto return_value_size = pack_size(return_value);
         const auto rhs_size = action_input_size + return_value_size;
         const auto serialized_base = pack(*base);
         const auto serialized_data = pack(data);
         const auto serialized_output = pack(return_value);
         hashes[0] = sha256(serialized_base.data(), serialized_base.size());
         std::vector<uint8_t> h1_result(rhs_size);
         std::copy (serialized_data.cbegin(), serialized_data.cend(), h1_result.begin());
         std::copy (serialized_output.cbegin(), serialized_output.cend(), h1_result.begin() + action_input_size);
         hashes[1] = sha256(reinterpret_cast<char*>(h1_result.data()), rhs_size);
         return hash_pair(std::make_pair(hashes[0], hashes[1]));
      }
      EOSLIB_SERIALIZE( action, (account)(name)(authorization)(data)(return_value))
   };

   struct action_data {
      action               action; //antelope action
      name                 receiver;
      uint64_t             recv_sequence   = 0;
      checksum256          witness_hash;
   };


   struct action_data_internal : action_data {
      checksum256 resolved_action_digest;

      action_data_internal(const action_data& base) : action_data(base){
         resolved_action_digest = base.action.digest();
      }

      checksum256 digest() const {
         auto result = eosio::pack(*this);
         checksum256 hash = sha256(result.data(), result.size());
         return hash;
      };

      EOSLIB_SERIALIZE( action_data_internal, (receiver)(recv_sequence)(action.account)(action.name)(resolved_action_digest)(witness_hash))
   };

   struct action_proof_of_inclusion {
      uint64_t target_block_index = 0;
      uint64_t final_block_index = 0;

      action_data target;

      std::vector<checksum256> merkle_branches;

      //returns the merkle root obtained by hashing target.digest() with merkle_branches
      checksum256 root() const {
         checksum256 digest = action_data_internal(target).digest();
         checksum256 root = _compute_root(merkle_branches, digest, target_block_index, final_block_index);
         return root;
      }; 
   };

   struct level_3_commitments_t {
      checksum256 reversible_blocks_mroot{};
      uint32_t latest_qc_claim_block_num{0};
      checksum256 latest_qc_claim_finality_digest{};
      block_timestamp_type latest_qc_claim_timestamp;
      block_timestamp_type timestamp;
      checksum256 base_digest{};
   };

   // commitments used in the context of finalizer policy transitions
   struct level_2_commitments_t {
      checksum256 last_pending_fin_pol_digest{};
      block_timestamp_type last_pending_fin_pol_start_timestamp;
      checksum256 l3_commitments_digest{};
   };

   struct dynamic_data_v0 {
      //block_num is always present
      uint32_t block_num = 0;

      //can include any number of action_proofs and / or state_proofs pertaining to a given block
      //all action_proofs must resolve to the same action_mroot
      std::vector<action_proof_of_inclusion> action_proofs;

      //can be used instead of providing action_proofs. Useful for proving finalizer policy changes
      std::optional<checksum256> action_mroot;

      checksum256 get_action_mroot() const {
         if (action_mroot.has_value()) {
            check(action_mroot.value() != checksum256(), "cannot provide empty hash");
            return action_mroot.value();
         }
         else {
            check(action_proofs.size()>0, "must have at least one action proof");
            checksum256 root = checksum256();
            for (auto ap : action_proofs){
               check(ap.root() != checksum256(), "cannot provide action proof that resolves to empty hash");
               if (root == checksum256()) root = ap.root();
               else check(ap.root() == root, "all action proofs must resolve to the same merkle root");
            }
            return root;
         }
      }; 
   };

   //input representation of finality data 
   struct block_finality_data {
      //major_version for this block
      uint32_t major_version;

      //minor_version for this block
      uint32_t minor_version;

      //finalizer_policy_generation for this block
      uint32_t active_finalizer_policy_generation;
      std::optional<uint32_t> last_pending_finalizer_policy_generation;

      //Allows the contract to obtain knowledge about them and to record them in its internal state.

      std::optional<finalizer_policy_input> pending_finalizer_policy;
      std::optional<block_timestamp> last_pending_finalizer_policy_start_timestamp;

      //if finality violation info is present (not implemented yet), witness_hash should be the base digest. 
      //if finalizer policy transition info is present, witness_hash should be the level 3 commitments digest. 
      //Otherwise, witness_hash should be level 2 commitments digest
      checksum256 witness_hash;

      //finality merkle root
      checksum256 finality_mroot;
      
      //resolves witness hash if it needs to be calculated
      checksum256 resolve_witness() const {

         //todo : add support for finality violation proofs

         //finalizer policy transition proofs 

         if (pending_finalizer_policy.has_value()  
            && last_pending_finalizer_policy_start_timestamp.has_value()
            && witness_hash!=checksum256()){

            checksum256 policy_digest = pending_finalizer_policy.value().digest();
            
            auto l2_packed = eosio::pack(level_2_commitments_t{
               .last_pending_fin_pol_digest  = policy_digest, 
               .last_pending_fin_pol_start_timestamp =  last_pending_finalizer_policy_start_timestamp.value(),
               .l3_commitments_digest = witness_hash
            });

            checksum256 l2_digest = sha256(l2_packed.data(), l2_packed.size());

            return l2_digest;
         }
         else {
            //regular finality + action proofs
            check(witness_hash!=checksum256(), "witness hash cannot be empty");
            return witness_hash;
         }
      }; 
   };

   //internal representation of finality data
   struct block_finality_data_internal : block_finality_data {
      checksum256 resolved_witness_hash;

      uint32_t resolved_last_pending_finalizer_policy_generation = 0;

      block_finality_data_internal(const block_finality_data& base) : block_finality_data(base){
         resolved_witness_hash = base.resolve_witness();
         resolved_last_pending_finalizer_policy_generation = base.last_pending_finalizer_policy_generation.has_value() ? base.last_pending_finalizer_policy_generation.value() : active_finalizer_policy_generation;
      }

      checksum256 finality_digest() const {
         auto result = eosio::pack(*this);
         checksum256 hash = sha256(result.data(), result.size());
         return hash;
      }

      EOSLIB_SERIALIZE(block_finality_data_internal, (major_version)(minor_version)(active_finalizer_policy_generation)(resolved_last_pending_finalizer_policy_generation)(finality_mroot)(resolved_witness_hash))
   };

   //used in "heavy" proofs, where verification of finality digest is performed
   struct extended_block_data {
      //finality data
      block_finality_data finality_data;

      block_timestamp timestamp;
      block_timestamp parent_timestamp;

      //dynamic_data to be verified
      dynamic_data_v0 dynamic_data;
   };

   struct extended_block_data_internal : extended_block_data {
      checksum256 resolved_finality_digest;
      checksum256 resolved_action_mroot;

      block_timestamp resolved_timestamp;
      block_timestamp resolved_parent_timestamp;

      extended_block_data_internal(const extended_block_data& base) : extended_block_data(base){
         
         resolved_finality_digest = block_finality_data_internal(base.finality_data).finality_digest();
         resolved_action_mroot = base.dynamic_data.get_action_mroot();

         resolved_timestamp = base.timestamp;
         resolved_parent_timestamp = base.parent_timestamp;

      }

      checksum256 finality_leaf() const {
         auto result = eosio::pack(*this);
         checksum256 hash = sha256(result.data(), result.size());
         return hash;
      }

      EOSLIB_SERIALIZE(extended_block_data_internal, (finality_data.major_version)(finality_data.minor_version)(dynamic_data.block_num)(resolved_timestamp)(resolved_parent_timestamp)(resolved_finality_digest)(resolved_action_mroot))
   };

   //used in "light" proofs
   struct simple_block_data {
      uint32_t major_version = 0 ;
      uint32_t minor_version = 0 ;

      //todo : use 2-level structure to remove the need for passing timestamps
      block_timestamp timestamp;
      block_timestamp parent_timestamp;

      checksum256 finality_digest;

      //dynamic_data to be verified
      dynamic_data_v0 dynamic_data;
   };

   struct simple_block_data_internal : simple_block_data {
      checksum256 resolved_action_mroot;

      block_timestamp resolved_timestamp;
      block_timestamp resolved_parent_timestamp;

      simple_block_data_internal(const simple_block_data& base) : simple_block_data(base){
         
         resolved_action_mroot = base.dynamic_data.get_action_mroot();

         resolved_timestamp = base.timestamp;
         resolved_parent_timestamp = base.parent_timestamp;
      }

      checksum256 finality_leaf() const {
         auto result = eosio::pack(*this);
         checksum256 hash = sha256(result.data(), result.size());
         return hash;
      }

      EOSLIB_SERIALIZE(simple_block_data_internal, (major_version)(minor_version)(dynamic_data.block_num)(resolved_timestamp)(resolved_parent_timestamp)(finality_digest)(resolved_action_mroot))
   };

   using block_data_type = std::variant<simple_block_data, extended_block_data>;

   struct block_proof_of_inclusion {
      uint64_t target_block_index = 0;
      uint64_t final_block_index = 0;

      block_data_type target;

      std::vector<checksum256> merkle_branches;

      //returns the merkle root obtained by hashing target.finality_leaf() with merkle_branches
      checksum256 root() const {
         checksum256 finality_leaf;

         if (std::holds_alternative<extended_block_data>(target)) finality_leaf = extended_block_data_internal(std::get<extended_block_data>(target)).finality_leaf();
         else if (std::holds_alternative<simple_block_data>(target)) finality_leaf = simple_block_data_internal(std::get<simple_block_data>(target)).finality_leaf();

         checksum256 root = _compute_root(merkle_branches, finality_leaf, target_block_index, final_block_index);
         return root;
      }; 
   };

   struct finality_proof {
      //block finality data over which we validate a QC
      block_finality_data qc_block;

      //signature over finality_digest() of qc_block by active policy generation 
      quorum_certificate active_policy_qc;

      //signature over finality_digest() of qc_block by pending policy generation (required during transitions, prohibited otherwise)
      std::optional<quorum_certificate> pending_policy_qc;

   };

   struct proof {
      //valid configurations :
      //1) finality_proof for a QC block, and proof_of_inclusion of a target block within the latest_qc_claim_block_num block represented by the finality_mroot present in header
      //2) only a proof_of_inclusion of a target block, which must be included in a merkle tree represented by a root stored in the contract's RAM
      std::optional<finality_proof> finality_proof;
      block_proof_of_inclusion target_block_proof_of_inclusion;
   };

}
