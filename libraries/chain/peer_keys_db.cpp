#include <eosio/chain/peer_keys_db.hpp>
#include <eosio/chain/contract_table_objects.hpp>

namespace eosio::chain {

peer_keys_db_t::peer_keys_db_t() : _active(false) {}

std::optional<public_key_type> peer_keys_db_t::get_peer_key(name n) const {
   fc::lock_guard g(_m);
   if (auto it = _peer_key_map.find(n); it != _peer_key_map.end())
      return std::optional<public_key_type>(it->second);
   return std::optional<public_key_type>{};
}

size_t peer_keys_db_t::size() const {
   fc::lock_guard g(_m);
   return _peer_key_map.size();
}

// we update the keys that were registered up to lib_number (inclusive)
// --------------------------------------------------------------------
size_t peer_keys_db_t::update_peer_keys(const controller& chain, uint32_t lib_number) {
   size_t num_updated = 0;
   if (!_active || lib_number <= _block_num)
      return num_updated;                      // nothing to do

   try {
      const auto& db = chain.db();
      const auto  table_ref = boost::make_tuple("eosio"_n, "eosio"_n, "peerkeys"_n);
      const auto* t_id      = db.find<table_id_object, by_code_scope_table>(table_ref);
      EOS_ASSERT(t_id != nullptr, plugin_exception, "cannot retrieve `peerkeys` table");

      const auto& secidx = db.get_index<index64_index, by_secondary>();

      const auto lower = secidx.lower_bound(std::make_tuple(t_id->id._id, static_cast<uint64_t>(_block_num + 1)));
      const auto upper = secidx.upper_bound(std::make_tuple(t_id->id._id, static_cast<uint64_t>(lib_number)));

      if (upper == lower) {
         // no new keys registered
         _block_num = lib_number;
         return num_updated;
      }

      fc::lock_guard g(_m); // we only need to protect access to _peer_key_map

      for (auto itr = lower; itr != upper; ++itr) {
         try {
            const auto* itr2 =
               db.find<key_value_object, by_scope_primary>(boost::make_tuple(t_id->id, itr->primary_key));

            name            row_name;
            uint64_t        row_block_num;
            public_key_type row_key;

            const auto&                 obj = *itr2;
            fc::datastream<const char*> ds(obj.value.data(), obj.value.size());
            // must match `struct peer_key;` in eosio.system.hpp
            // -------------------------------------------------
            fc::raw::unpack(ds, row_name);
            EOS_ASSERT(row_name.good(), plugin_exception, "deserialized invalid name from `peerkeys`");

            fc::raw::unpack(ds, row_block_num);
            EOS_ASSERT(row_block_num > static_cast<uint64_t>(_block_num), plugin_exception,
                       "deserialized invalid version from `peerkeys`");

            fc::raw::unpack(ds, row_key);
            EOS_ASSERT(row_key.valid(), plugin_exception, "deserialized invalid public key from `peerkeys`");

            _peer_key_map[row_name] = row_key;
            ++num_updated;
         }
         FC_LOG_AND_DROP(("skipping invalid record deserialized from `peerkeys`"));
      }

      _block_num = lib_number;                   // mark that we have updated up to lib_number
   } FC_LOG_AND_DROP(("Error when updating peer_keys_db"));
   return num_updated;
}

} // namespace eosio::chain