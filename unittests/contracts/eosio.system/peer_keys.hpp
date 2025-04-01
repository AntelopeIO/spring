#pragma once

#include <eosio/contract.hpp>
#include <eosio/action.hpp>
#include <eosio/print.hpp>
#include <eosio/privileged.hpp>

#include <string>

namespace eosiosystem {

using eosio::name;
using eosio::public_key;

struct [[eosio::contract("eosio.system")]] peer_keys : public eosio::contract {
   peer_keys(name s, name code, eosio::datastream<const char*> ds)
      : eosio::contract(s, code, ds) {}
   
   struct peerkeys_t {
      name producer_name;
      std::optional<public_key> peer_key;

      EOSLIB_SERIALIZE(peerkeys_t, (producer_name)(peer_key))
   };
   using getpeerkeys_response = std::vector<peerkeys_t>;

   [[eosio::action]]
   getpeerkeys_response getpeerkeys() {
      getpeerkeys_response resp{{{"n1"_n, {}}}};
      return resp;
   }
};

} /// eosiosystem