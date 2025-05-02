#include <cstdint>
#include "eosio.system.hpp"
#include "peer_keys.hpp"

#include <eosio/eosio.hpp>
#include <eosio/print.hpp>

namespace eosiosystem {

void peer_keys::regpeerkey(const name& proposer_finalizer_name, const public_key& key) {
   require_auth(proposer_finalizer_name);
   peer_keys_table peer_keys_table(get_self(), get_self().value);
   check(!std::holds_alternative<eosio::webauthn_public_key>(key), "webauthn keys not allowed in regpeerkey action");

   auto peers_itr = peer_keys_table.find(proposer_finalizer_name.value);
   if (peers_itr == peer_keys_table.end()) {
      peer_keys_table.emplace(proposer_finalizer_name, [&](auto& row) {
         row.init_row(proposer_finalizer_name);
         row.set_public_key(key);
      });
   } else {
      const auto& prev_key = peers_itr->get_public_key();
      check(!prev_key || *prev_key != key, "Provided key is the same as currently stored one");
      peer_keys_table.modify(peers_itr, eosio::same_payer, [&](auto& row) {
         row.update_row();
         row.set_public_key(key);
      });
   }
}

void peer_keys::delpeerkey(const name& proposer_finalizer_name, const public_key& key) {
   require_auth(proposer_finalizer_name);
   peer_keys_table peer_keys_table(get_self(), get_self().value);

   // not updating the version here. deleted keys will persist in the memory hashmap
   auto peers_itr = peer_keys_table.find(proposer_finalizer_name.value);
   check(peers_itr != peer_keys_table.end(), "Key not present for name: " + proposer_finalizer_name.to_string());
   const auto& prev_key = peers_itr->get_public_key();
   check(prev_key && *prev_key == key, "Current key does not match the provided one");
   peer_keys_table.erase(peers_itr);
}

peer_keys::getpeerkeys_res_t peer_keys::getpeerkeys() {
   peer_keys_table  peer_keys_table(get_self(), get_self().value);

   getpeerkeys_res_t resp;

   // this is a simpler implementation than the one in `eos-system-contracts`.
   // the one in `eos-system-contracts` iterates over both ends of the "prototalvote"_n index
   // (to take into account non-active producers)
   // Most integration tests use bios contract to setprods, just return complete list for tests
   // since prototalvote will not be populated.
   for (auto it = peer_keys_table.begin(); it != peer_keys_table.end(); ++it) {
      resp.push_back(peerkeys_t{it->account, it->get_public_key()});
   }
   return resp;
}

} // namespace eosiosystem
