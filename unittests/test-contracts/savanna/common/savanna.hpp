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
       std::vector<uint8_t>   finalizers;
       std::string            signature;
   };
   
   struct finalizer_authority_internal {
      std::string       description;
      uint64_t          weight = 0;
      std::vector<uint8_t> public_key;
   };

   struct finalizer_policy_internal {

      uint32_t                         generation = 0; ///< sequentially incrementing version number
      uint64_t                         threshold = 0;  ///< vote weight threshold to finalize blocks
      std::vector<finalizer_authority_internal> finalizers; ///< Instant Finality voter set

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

      uint32_t                         generation = 0; ///< sequentially incrementing version number
      uint64_t                         threshold = 0;  ///< vote weight threshold to finalize blocks
      std::vector<finalizer_authority_input> finalizers; ///< Instant Finality voter set

      checksum256 digest() const {

         std::vector<finalizer_authority_internal> finalizers_i;
         for (auto f : finalizers){
            std::array<char,96> decoded_key = decode_bls_public_key_to_g1(f.public_key);
            std::vector<uint8_t> vector_key(decoded_key.begin(), decoded_key.end());
            finalizer_authority_internal fai{f.description, f.weight, vector_key};
            finalizers_i.push_back(fai);
         }
         finalizer_policy_internal internal{generation, threshold, finalizers_i};
         return internal.digest();
      }

   };

   //Compute the maximum number of layers of a merkle tree for a given number of leaves
   uint64_t calculate_max_depth(uint64_t node_count) {
      if(node_count <= 1)
         return node_count;
      return 64 - __builtin_clzll(2 << (64 - 1 - __builtin_clzll ((node_count - 1))));
   }

   uint32_t reverse_bytes(const uint32_t input){
      uint32_t output = (input>>24 & 0xff)|(input>>8 & 0xff00)|(input<<8 & 0xff0000)|(input<<24 & 0xff000000);
      return output;
   }

   checksum256 hash_pair(const std::pair<checksum256, checksum256> p){
      std::array<uint8_t, 32> arr1 = p.first.extract_as_byte_array();
      std::array<uint8_t, 32> arr2 = p.second.extract_as_byte_array();
      std::array<uint8_t, 64> result;
      std::copy (arr1.cbegin(), arr1.cend(), result.begin());
      std::copy (arr2.cbegin(), arr2.cend(), result.begin() + 32);
      checksum256 hash = sha256(reinterpret_cast<char*>(result.data()), 64);
      return hash;
   }

   time_point add_time(const time_point& time, const uint32_t seconds ){
      int64_t total_seconds = (static_cast<int64_t>(time.sec_since_epoch()) + static_cast<int64_t>(seconds));
      microseconds ms = microseconds(total_seconds * 1000000);
      time_point tp = time_point(ms);
      return tp;
   }

   //compute proof path
   std::vector<bool> _get_proof_path(const uint64_t c_leaf_index, uint64_t const c_leaf_count) {

      uint64_t leaf_index = c_leaf_index;
      uint64_t leaf_count = c_leaf_count;

      std::vector<bool> proof_path;

      uint64_t layers_depth = calculate_max_depth(c_leaf_count) -1;

      for (uint64_t i = 0; i < layers_depth; i++) {
         bool isLeft = leaf_index % 2;
         uint64_t pairIndex = isLeft ? leaf_index - 1 :
                        (leaf_index == leaf_count ? leaf_index : leaf_index + 1);


         if (pairIndex<leaf_count) proof_path.push_back(isLeft);

         leaf_count/=2;
         leaf_index/=2;
      }

      return proof_path;

   }

   //compute the merkle root of target node and vector of merkle branches
   checksum256 _compute_root(const std::vector<checksum256> proof_nodes, const checksum256& target, const uint64_t target_index, const uint64_t last_node_index){
       checksum256 hash = target;
       std::vector<bool> proof_path = _get_proof_path(target_index, last_node_index+1);

       check(proof_path.size() == proof_nodes.size(), "internal error"); //should not happen
       for (int i = 0 ; i < proof_nodes.size() ; i++){

           const checksum256 node = proof_nodes[i];
           std::array<uint8_t, 32> arr = node.extract_as_byte_array();
           if (proof_path[i] == false){
               hash = hash_pair(std::make_pair(hash, node));
           }
           else {
               hash = hash_pair(std::make_pair(node, hash));
           }
       }
       return hash;
   }

   //add two numbers from the g1 group (aggregation)
   bls_g1 _g1add(const bls_g1& op1, const bls_g1& op2) {
      bls_g1 r;
      bls_g1_add(op1, op2, r);
      return r;
   }

   void _verify(const std::string& public_key, const std::string& signature, const std::string& message){
      check(bls_signature_verify(decode_bls_public_key_to_g1(public_key), decode_bls_signature_to_g2(signature), message), "signature verification failed");
   }
   
   //verify that the quorum certificate over the finality digest is valid
   void _check_qc(const quorum_certificate& qc, const checksum256& finality_digest, const finalizer_policy_input finalizer_policy){

      auto fa_itr = finalizer_policy.finalizers.begin();
      auto fa_end_itr = finalizer_policy.finalizers.end();
      size_t finalizer_count = std::distance(fa_itr, fa_end_itr);
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
      _verify(s_agg_pub_key, qc.signature, message);
   }


   struct authseq {
      name account;
      uint64_t sequence;

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
         std::array<uint8_t, 32> arr1 = hashes[0].extract_as_byte_array();
         std::array<uint8_t, 32> arr2 = hashes[1].extract_as_byte_array();
         std::array<uint8_t, 64> result;
         std::copy (arr1.cbegin(), arr1.cend(), result.begin());
         std::copy (arr2.cbegin(), arr2.cend(), result.begin() + 32);
         checksum256 final_hash = sha256(reinterpret_cast<char*>(result.data()), 64);
         return final_hash;
      }

      EOSLIB_SERIALIZE( action, (account)(name)(authorization)(data)(return_value))

   };

   struct action_data {

      action               action; //antelope action
      name                 receiver;
      uint64_t             recv_sequence   = 0;

      checksum256          witness_hash;

      checksum256 digest() const {

         //4x uint64_t + 2x checksum256
         std::vector<char> result(8 + 8 + 8 + 8 + 32 + 32);

         const auto serialized_receiver = pack(receiver); //uint64_t
         const auto serialized_recv_sequence = pack(recv_sequence); //uint64_t
         const auto serialized_account = pack(action.account); //uint64_t
         const auto serialized_name = pack(action.name); //uint64_t
         const auto serialized_act_digest = pack(action.digest()); //checksum256
         const auto serialized_witness_hash = pack(witness_hash); //checksum256

         std::copy (serialized_receiver.cbegin(), serialized_receiver.cend(), result.begin());
         std::copy (serialized_recv_sequence.cbegin(), serialized_recv_sequence.cend(), result.begin() + 8);
         std::copy (serialized_account.cbegin(), serialized_account.cend(), result.begin() + 16);
         std::copy (serialized_name.cbegin(), serialized_name.cend(), result.begin() + 24);
         std::copy (serialized_act_digest.cbegin(), serialized_act_digest.cend(), result.begin() + 32);
         std::copy (serialized_witness_hash.cbegin(), serialized_witness_hash.cend(), result.begin() + 64);

         checksum256 final_hash = sha256(result.data(), 96);
         return final_hash;

      };

   };

   struct action_proof_of_inclusion {

      uint64_t target_node_index;
      uint64_t last_node_index;

      action_data target;

      std::vector<checksum256> merkle_branches;

      //returns the merkle root obtained by hashing target.digest() with merkle_branches
      checksum256 root() const {
         checksum256 digest = target.digest();
         checksum256 root = _compute_root(merkle_branches, digest, target_node_index, last_node_index);
         return root;
      }; 

   };

   struct dynamic_data_v0 {

      //block_num is always present
      uint32_t block_num;

      //can include any number of action_proofs and / or state_proofs pertaining to a given block
      //all action_proofs must resolve to the same action_mroot
      std::vector<action_proof_of_inclusion> action_proofs;

      //can be used instead of providing action_proofs. Useful for proving finalizer policy changes
      std::optional<checksum256> action_mroot;

      checksum256 get_action_mroot() const {
         if (action_mroot.has_value()) return action_mroot.value();
         else {
            check(action_proofs.size()>0, "must have at least one action proof");
            checksum256 root = checksum256();
            for (auto ap : action_proofs){
               if (root == checksum256()) root = ap.root();
               else check(ap.root() == root, "all action proofs must resolve to the same merkle root");
            }
            return root;
         }
      }; 
   };
         
   struct block_finality_data {
      
      //major_version for this block
      uint32_t major_version;

      //minor_version for this block
      uint32_t minor_version;

      //finalizer_policy_generation for this block
      uint32_t finalizer_policy_generation;

      std::optional<finalizer_policy_input> new_finalizer_policy;

      //if a finalizer policy is present, witness_hash should be the base_digest. Otherwise, witness_hash should be the static_data_digest
      checksum256 witness_hash;

      //final_on_qc for this block
      checksum256 finality_mroot;

      //returns hash of digest of new_finalizer_policy + witness_hash if new_finalizer_policy is present, otherwise returns witness_hash
      checksum256 resolve_witness() const {
         if (new_finalizer_policy.has_value()){
            checksum256 policy_digest = new_finalizer_policy.value().digest();
            checksum256 base_fpolicy_digest = hash_pair( std::make_pair( policy_digest, witness_hash) );
            return base_fpolicy_digest;
         }
         else {
            return witness_hash;
         }
      }; 

      //returns hash of major_version + minor_version + finalizer_policy_generation + resolve_witness() + finality_mroot
      checksum256 finality_digest() const {
         std::array<uint8_t, 76> result;
         memcpy(&result[0], (uint8_t *)&major_version, 4);
         memcpy(&result[4], (uint8_t *)&minor_version, 4);
         memcpy(&result[8], (uint8_t *)&finalizer_policy_generation, 4);
         std::array<uint8_t, 32> arr1 = finality_mroot.extract_as_byte_array();
         std::array<uint8_t, 32> arr2 = resolve_witness().extract_as_byte_array();
         std::copy (arr1.cbegin(), arr1.cend(), result.begin() + 12);
         std::copy (arr2.cbegin(), arr2.cend(), result.begin() + 44);
         checksum256 hash = sha256(reinterpret_cast<char*>(result.data()), 76);
         return hash;
      };

   };

   struct block_data {
      
      //finality data
      block_finality_data finality_data;

      //dynamic_data to be verified
      dynamic_data_v0 dynamic_data;

      //returns hash of finality_digest() and dynamic_data_digest()
      checksum256 digest() const {
         checksum256 finality_digest = finality_data.finality_digest();
         checksum256 action_mroot = dynamic_data.get_action_mroot();
         std::array<uint8_t, 76> result;
         memcpy(&result[0], (uint8_t *)&finality_data.major_version, 4);
         memcpy(&result[4], (uint8_t *)&finality_data.minor_version, 4);
         memcpy(&result[8], (uint8_t *)&dynamic_data.block_num, 4);
         std::array<uint8_t, 32> arr1 = finality_digest.extract_as_byte_array();
         std::array<uint8_t, 32> arr2 = action_mroot.extract_as_byte_array();
         std::copy (arr1.cbegin(), arr1.cend(), result.begin() + 12);
         std::copy (arr2.cbegin(), arr2.cend(), result.begin() + 44);
         checksum256 hash = sha256(reinterpret_cast<char*>(result.data()), 76);
         return hash;
      };
   };

   struct block_proof_of_inclusion {

      uint64_t target_node_index;
      uint64_t last_node_index;

      block_data target;

      std::vector<checksum256> merkle_branches;

      //returns the merkle root obtained by hashing target.digest() with merkle_branches
      checksum256 root() const {
         checksum256 digest = target.digest();
         checksum256 root = _compute_root(merkle_branches, digest, target_node_index, last_node_index);
         return root;
      }; 

   };

   struct finality_proof {

      //block finality data over which we validate a QC
      block_finality_data qc_block;

      //signature over finality_digest() of qc_block. 
      quorum_certificate qc;

   };

   struct proof {

      //valid configurations :
      //1) finality_proof for a QC block, and proof_of_inclusion of a target block within the final_on_strong_qc block represented by the finality_mroot present in header
      //2) only a proof_of_inclusion of a target block, which must be included in a merkle tree represented by a root stored in the contract's RAM
      std::optional<finality_proof> finality_proof;
      block_proof_of_inclusion target_block_proof_of_inclusion;

   };

}