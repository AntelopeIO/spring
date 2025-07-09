#include <eosio/chain/peer_keys_db.hpp>
#include <eosio/chain/contract_table_objects.hpp>

namespace eosio::chain {

std::optional<peer_info_t> peer_keys_db_t::get_peer_info(name n) const {
   fc::lock_guard g(_m);
   assert(_active);
   if (auto it = _peer_info_map.find(n); it != _peer_info_map.end())
      return std::optional<peer_info_t>(it->second);
   return std::optional<peer_info_t>{};
}

bool peer_keys_db_t::configured_peer_keys_updated() {
   return _configured_bp_peers_updated.exchange(false);
}

void peer_keys_db_t::update_peer_keys(block_num_type block_num, const getpeerkeys_res_t& v) {
   assert(_active);

   if (v.empty())
      return;
   
   _last_block_num = block_num;

   // create hash_map of current top selected producers (according to "getpeerkeys"_n in system contracts)
   // ----------------------------------------------------------------------------------------------------
   peer_key_map_t current;
   for (size_t i=0; i<v.size(); ++i)
      current[v[i].producer_name] = peer_info_t{static_cast<uint32_t>(i), v[i].peer_key};

   bool configured_bp_peers_updated = false;
   fc::lock_guard g(_m);

   // remove those that aren't among the top producers anymore
   // --------------------------------------------------------
   for (auto it = _peer_info_map.begin(); it != _peer_info_map.end(); ) {
      if (!current.contains(it->first)) {
         if (_configured_bp_peers.contains(it->first))
            configured_bp_peers_updated = true;
         it = _peer_info_map.erase(it);
      } else {
         ++it;
      }
   }

   // add new ones to _peer_info_map and updated modified ones
   // --------------------------------------------------------
   for (auto& pi : current) {
      if (!_peer_info_map.contains(pi.first) || _peer_info_map[pi.first] != pi.second) {
         _peer_info_map[pi.first] = pi.second;
         if (_configured_bp_peers.contains(pi.first))
            configured_bp_peers_updated = true;
      }
   }

   if (configured_bp_peers_updated)
      _configured_bp_peers_updated = true;
}

} // namespace eosio::chain