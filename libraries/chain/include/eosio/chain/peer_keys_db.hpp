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

   // must be called from main thread
   size_t update_peer_keys(const controller& chain, uint32_t lib_number);

   // safe to be called from any thread
   std::optional<public_key_type> operator[](name n) const;
   size_t size() const;

private:
   std::optional<uint64_t> _get_version(const chainbase::database& db);

   mutable std::mutex _m;
   uint32_t           _block_num = 0; // below map includes keys registered up to _block_num (inclusive)
   peer_key_map_t     _peer_key_map;
};

} // namespace eosio::chain
