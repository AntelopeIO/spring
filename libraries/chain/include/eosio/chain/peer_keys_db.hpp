#pragma once

#include <eosio/chain/controller.hpp>
#include <boost/smart_ptr/atomic_shared_ptr.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

namespace eosio::chain {

/**
 * This class caches the on-chain public keys that BP use to sign the `gossip_bp_peers`
 * network message. These public keys are populated using the actions regpeerkey and
 * delpeerkey of eos-system-contracts.
 */
class peer_keys_db_t {
public:
   using peer_key_map_t = boost::unordered_flat_map<name, public_key_type, std::hash<name>>;

   peer_keys_db_t();

   size_t update_peer_keys(const controller& chain, uint32_t lib_number);

   boost::shared_ptr<peer_key_map_t> get_peer_key_map() { return _peer_key_map.load(); }

private:
   std::optional<uint64_t> _get_version(const chainbase::database& db);

   uint32_t                                 _block_num = 0;    // below maps include keys registered up to _block_num (inclusive)
   boost::atomic_shared_ptr<peer_key_map_t> _peer_key_map;
   boost::shared_ptr<peer_key_map_t>        _alt_peer_key_map; // keep two maps so we con't have to copy a whole map on every update
};

} // namespace eosio::chain
