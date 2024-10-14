#pragma once

#include <fc/crypto/bls_public_key.hpp>
#include <string>

namespace eosio::chain {

   struct finalizer_authority {

      std::string  description;
      uint64_t     weight = 0; // weight that this finalizer's vote has for meeting fthreshold
      fc::crypto::blslib::bls_public_key  public_key;

      auto operator<=>(const finalizer_authority&) const = default;
   };

   using finalizer_authority_ptr = std::shared_ptr<const finalizer_authority>;

   // This is used by SHiP and Deepmind which require public keys in string format.
   struct finalizer_authority_with_string_key {
      std::string  description;
      uint64_t     weight = 0;
      std::string  public_key;

      finalizer_authority_with_string_key() = default;
      explicit finalizer_authority_with_string_key(const finalizer_authority& input)
         : description(input.description)
         , weight(input.weight)
         , public_key(input.public_key.to_string()) {}
   };
} /// eosio::chain

FC_REFLECT( eosio::chain::finalizer_authority, (description)(weight)(public_key) )
FC_REFLECT( eosio::chain::finalizer_authority_with_string_key, (description)(weight)(public_key) )
