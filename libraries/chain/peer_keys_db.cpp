#include <eosio/chain/peer_keys_db.hpp>
#include <eosio/chain/contract_table_objects.hpp>

namespace eosio::chain {

peer_keys_db_t::peer_keys_db_t()
   : _peer_key_map(boost::make_shared<peer_key_map_t>())
   , _alt_peer_key_map(boost::make_shared<peer_key_map_t>()) {}

std::optional<uint64_t> peer_keys_db_t::_get_version(const chainbase::database& db) {
   const auto* t_id =
      db.find<table_id_object, by_code_scope_table>(boost::make_tuple("eosio"_n, "eosio"_n, "peerkeysver"_n));
   if (t_id == nullptr)
      return std::optional<uint64_t>{};

   const auto& idx = db.get_index<chain::key_value_index, chain::by_scope_primary>();

   auto itr = idx.lower_bound( boost::make_tuple(t_id->id, 0));
   if (itr == idx.end() || itr->t_id != t_id->id || itr->primary_key != 0)
      return std::optional<uint64_t>{};
   
   const key_value_object& obj = *itr;
   fc::datastream<const char*> ds(obj.value.data(), obj.value.size());
   uint64_t res = 0;
   fc::raw::unpack(ds, res);
   return std::optional<uint64_t>(res);
}

size_t peer_keys_db_t::update_peer_keys(const controller& chain) {
   size_t num_updated = 0;
   try {
      const auto& db = chain.db();
      auto cb_version = _get_version(db);
      if (!cb_version || *cb_version <= _version) // possible that chainbase version regresses during a fork switch
         return num_updated;                      // nothing to do

      const table_id_object* t_id =
         db.find<table_id_object, by_code_scope_table>(boost::make_tuple("eosio"_n, "eosio"_n, "peerkeys"_n));
      assert(t_id != nullptr);
      const auto& secidx = db.get_index<index64_index, by_secondary>();

      const auto lower = secidx.lower_bound(std::make_tuple(t_id->id._id, _version + 1));
      const auto upper = secidx.upper_bound(std::make_tuple(t_id->id._id, *cb_version));

      auto _update_map = [&](peer_key_map_t& new_map) {
         size_t num_updated = 0;
         for (auto itr = lower; itr != upper; ++itr) {
            try {
               const auto* itr2 =
                  db.find<key_value_object, by_scope_primary>(boost::make_tuple(t_id->id, itr->primary_key));

               name            row_name;
               uint64_t        row_version;
               public_key_type row_key;

               const auto&                 obj = *itr2;
               fc::datastream<const char*> ds(obj.value.data(), obj.value.size());
               // must match `struct peer_key;` in eosio.system.hpp
               // -------------------------------------------------
               fc::raw::unpack(ds, row_name);
               EOS_ASSERT(row_name.good(), plugin_exception, "deserialized invalid name from `peerkeys`");

               fc::raw::unpack(ds, row_version);
               EOS_ASSERT(row_version > 0 && row_version <= *cb_version, plugin_exception,
                          "deserialized invalid version from `peerkeys`");

               fc::raw::unpack(ds, row_key);
               EOS_ASSERT(row_key.valid(), plugin_exception, "deserialized invalid public key from `peerkeys`");

               new_map[row_name] = row_key;
               ++num_updated;
            }
            FC_LOG_AND_DROP(("skipping invalid record deserialized from `peerkeys`"));
         }
         return num_updated;
      };

      num_updated = _update_map(*_alt_peer_key_map);
      _peer_key_map.exchange(_alt_peer_key_map); // atomically make the updated map visible to net_plugin

      _update_map(*_alt_peer_key_map);           // update backup map, so it is ready for next time

      _version      = *cb_version;
   } FC_LOG_AND_DROP(("Error when updating peer_keys_db"));
   return num_updated;
}

} // namespace eosio::chain