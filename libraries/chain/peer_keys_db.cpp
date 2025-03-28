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
flat_set<name> peer_keys_db_t::update_peer_keys(const controller& chain, uint32_t lib_number) {
   flat_set<name> result;
   if (!_active || lib_number <= _block_num)
      return result;                      // nothing to do

   try {
      const auto& db = chain.db();
      const auto  table_ref = boost::make_tuple("eosio"_n, "eosio"_n, "peerkeys"_n);
      const auto* t_id      = db.find<table_id_object, by_code_scope_table>(table_ref);
      EOS_ASSERT(t_id != nullptr, misc_exception, "cannot retrieve `peerkeys` table");

      const auto& secidx = db.get_index<index64_index, by_secondary>();

      const auto lower = secidx.lower_bound(std::make_tuple(t_id->id._id, static_cast<uint64_t>(_block_num + 1)));
      const auto upper = secidx.upper_bound(std::make_tuple(t_id->id._id, static_cast<uint64_t>(lib_number)));

      if (upper == lower) {
         // no new keys registered
         _block_num = lib_number;
         return result;
      }

      fc::lock_guard g(_m); // we only need to protect access to _peer_key_map

      for (auto itr = lower; itr != upper; ++itr) {
         try {
            const auto* itr2 =
               db.find<key_value_object, by_scope_primary>(boost::make_tuple(t_id->id, itr->primary_key));

            name                  row_name;
            uint32_t              row_block_num;
            uint8_t               row_version;
            std::variant<v0_data> row_variant;

            const auto&                 obj = *itr2;
            fc::datastream<const char*> ds(obj.value.data(), obj.value.size());
            // must match `struct peer_key;` in eosio.system.hpp
            // -------------------------------------------------
            fc::raw::unpack(ds, row_name);
            EOS_ASSERT(row_name.good(), misc_exception, "deserialized invalid name from `peerkeys`");

            fc::raw::unpack(ds, row_block_num);
            EOS_ASSERT(row_block_num > static_cast<uint64_t>(_block_num), misc_exception,
                       "deserialized invalid version from `peerkeys`");

            fc::raw::unpack(ds, row_version);
            if (row_version != 0)
               continue;

            fc::raw::unpack(ds, row_variant);
            EOS_ASSERT(std::holds_alternative<v0_data>(row_variant), misc_exception, "deserialized invalid data from `peerkeys`");
            auto& data = std::get<v0_data>(row_variant);
            if (data.pubkey) {
               EOS_ASSERT(data.pubkey->valid(), misc_exception, "deserialized invalid public key from `peerkeys`");

               _peer_key_map[row_name] = *data.pubkey;
               result.insert(row_name);
            }
         }
         FC_LOG_AND_DROP(("skipping invalid record deserialized from `peerkeys`"));
      }

      _block_num = lib_number;                   // mark that we have updated up to lib_number
   } FC_LOG_AND_DROP(("Error when updating peer_keys_db"));
   return result;
}

} // namespace eosio::chain