#include <eosio/chain/peer_keys_db.hpp>
#include <eosio/chain/contract_table_objects.hpp>

namespace eosio::chain {

peer_keys_db_t::peer_keys_db_t() : _active(false) {}

peer_info_t peer_keys_db_t::get_peer_info(name n) const {
   fc::lock_guard g(_m);
   assert(_active);
   if (auto it = _peer_info_map.find(n); it != _peer_info_map.end())
      return it->second;
   return peer_info_t{};
}

peer_keys_db_t::new_peers_t peer_keys_db_t::update_peer_keys(const getpeerkeys_res_t& v) {
   if (!_active || v.empty())
      return {};
   
   // create hash_map of current top-50
   // ---------------------------------
   peer_key_map_t current;
   for (size_t i=0; i<v.size(); ++i)
      current[v[i].producer_name] = peer_info_t{static_cast<uint32_t>(i), v[i].peer_key};

   fc::lock_guard g(_m);
   new_peers_t res;

   // update ranking of those removed from top-50
   // -------------------------------------------
   for (auto& pi : _peer_info_map) {
      if (!current.contains(pi.first)) {
         pi.second.rank = std::numeric_limits<uint32_t>::max();
         res.insert(pi.first);
      }
   }

   // add new ones to _peer_info_map and updated modified ones
   // --------------------------------------------------------
   for (auto& pi : current) {
      if (!_peer_info_map.contains(pi.first) || _peer_info_map[pi.first] != pi.second) {
         _peer_info_map[pi.first] = pi.second;
         res.insert(pi.first);
      }
   }
   
   return res;
}

} // namespace eosio::chain