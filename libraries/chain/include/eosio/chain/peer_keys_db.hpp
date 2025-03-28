#pragma once

#include <eosio/chain/controller.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <fc/mutex.hpp>

namespace eosio::chain {

/**
 * This class caches the on-chain public keys that BP use to sign the `gossip_bp_peers`
 * network message. These public keys are populated using the actions regpeerkey and
 * delpeerkey of eos-system-contracts.
 */
class peer_keys_db_t {
public:
   // vector, sorted by rank, of the top-60 producer names and their peer key
   // if populated on-chain.
   // -----------------------------------------------------------------------
   using  getpeerkeys_res_t = std::vector<std::pair<name, std::optional<public_key_type>>>;

   using peer_key_map_t = boost::unordered_flat_map<name, peer_info_t, std::hash<name>>;
   using new_peers_t    = flat_set<name>;

   peer_keys_db_t();

   void set_active(bool b) { _active = b; }

   // must be called from main thread
   // return the new peers either:
   //  - added to the top-60
   //  - removed from the top-60
   //  - whose key changed
   // since the last call to update_peer_keys
   // ---------------------------------------
   new_peers_t update_peer_keys(getpeerkeys_res_t v);

   // safe to be called from any thread
   // peers no longer in top-60 will have a rank of std::numeric_limits<uint32_t>::max()
   // ----------------------------------------------------------------------------------
   peer_info_t get_peer_info(name n) const;

private:
   std::optional<uint64_t> _get_version(const chainbase::database& db);

   bool               _active = false;       // if not active (the default), no update occurs
   mutable fc::mutex  _m;
   peer_key_map_t     _peer_info_map GUARDED_BY(_m);
};

} // namespace eosio::chain

