#pragma once

#include <eosio/chain/controller.hpp>
#include <boost/smart_ptr/atomic_shared_ptr.hpp>

namespace eosio::chain {

/**
 * This class caches the on-chain public keys that BP use to sign the `gossip_bp_peers`
 * network message. These public keys are populated using the actions regpeerkey and
 * delpeerkey of eos-system-contracts.
 */

class peer_keys_db_t {
public:
   using peer_key_map_t = std::map<name, public_key_type>;

   peer_keys_db_t();

   void on_accepted_block(const controller& chain);        // update the map every N blocks, a small delay is fine

   boost::shared_ptr<peer_key_map_t> get_peer_key_map() { return _peer_key_map.load(); }

private:
   boost::atomic_shared_ptr<peer_key_map_t> _peer_key_map;
};

} // namespace eosio::chain
