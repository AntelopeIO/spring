#pragma once

#include <eosio/chain/types.hpp>

#include <fc/mutex.hpp>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/container/flat_set.hpp>

namespace eosio::chain {

// vector, sorted by rank, of the top-50 producers by `total_votes` (whether
// active or not) and their peer key if populated on-chain.
// -------------------------------------------------------------------------
struct peerkeys_t {
   name                           producer_name;
   std::optional<public_key_type> peer_key;
};
using getpeerkeys_res_t = std::vector<peerkeys_t>;

struct peer_info_t {
   // rank by `total_votes` of all producers, active or not, may not match schedule rank
   uint32_t                       rank{std::numeric_limits<uint32_t>::max()};
   std::optional<public_key_type> key;

   bool operator==(const peer_info_t&) const = default;
};

using peer_key_map_t = boost::unordered_flat_map<name, peer_info_t, std::hash<name>>;
using name_set_t = flat_set<name>;

/**
 * This class caches the on-chain public keys that BP use to sign the `gossip_bp_peers`
 * network message. These public keys are populated using the actions regpeerkey and
 * delpeerkey of eos-system-contracts.
 */
class peer_keys_db_t {
public:
   peer_keys_db_t() = default;

   // called on startup with configured bp peers of the node
   void set_active(name_set_t configured_bp_peers) {
      _configured_bp_peers = std::move(configured_bp_peers);
      _active = true;
   }

   // safe to be called from any thread, _active only modified on startup
   bool is_active() const { return _active; }

   // must be called from the main thread
   // return true if update_peer_keys should be called with new map of peer keys
   bool should_update(block_num_type block_num) { return _active && (_last_block_num == 0 || block_num % 120 == 0); }

   // must be called from main thread, only call if should_update() returns true
   void update_peer_keys(block_num_type block_num, const getpeerkeys_res_t& v);

   // safe to be called from any thread
   std::optional<peer_info_t> get_peer_info(name n) const;

   // safe to be called from any thread
   // returns true if configured bp peers modified, also resets flag so next call returns false until updated again
   bool configured_peer_keys_updated();

private:
   bool               _active = false;       // if not active (the default), no update occurs
   block_num_type     _last_block_num = 0;
   name_set_t         _configured_bp_peers;  // no updates occurs
   std::atomic<bool>  _configured_bp_peers_updated{false};

   mutable fc::mutex  _m;
   peer_key_map_t     _peer_info_map GUARDED_BY(_m);
};

} // namespace eosio::chain

