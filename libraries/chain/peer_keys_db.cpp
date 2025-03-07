#include <eosio/chain/peer_keys_db.hpp>
#include <eosio/chain/contract_table_objects.hpp>

namespace eosio::chain {

peer_keys_db_t::peer_keys_db_t()
   : _peer_key_map(boost::make_shared<peer_key_map_t>()) {
}

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
      if (!cb_version || *cb_version == _version)
         return num_updated;                    // nothing to do
      assert(*cb_version > _version);

      const auto* t_id =
         db.find<table_id_object, by_code_scope_table>(boost::make_tuple("eosio"_n, "eosio"_n, "peerkeys"_n));
      assert(t_id != nullptr);
      const auto& secidx = db.get_index<index64_index, by_secondary>();

      auto lower = secidx.lower_bound(std::make_tuple(t_id->id._id, _version, std::numeric_limits<uint64_t>::lowest()));
      auto upper = secidx.upper_bound(std::make_tuple(t_id->id._id, *cb_version, std::numeric_limits<uint64_t>::max()));

      auto new_map = boost::make_shared<peer_key_map_t>(*get_peer_key_map());

      for (auto itr = lower; itr != upper; ++itr) {
         const auto* itr2 = db.find<key_value_object, by_scope_primary>(boost::make_tuple(t_id->id, itr->primary_key));

         name            row_name;
         uint64_t        row_version;
         public_key_type row_key;

         const auto& obj = *itr2;
         fc::datastream<const char*> ds(obj.value.data(), obj.value.size());
         // must match `struct peer_key;` in eosio.system.hpp
         // -------------------------------------------------
         fc::raw::unpack(ds, row_name);
         fc::raw::unpack(ds, row_version);
         fc::raw::unpack(ds, row_key);
         assert(row_version > _version && row_version <= *cb_version);

         (*new_map)[row_name] = row_key;
         ++num_updated;
      }

      _peer_key_map = new_map;
      _version      = *cb_version;
   } FC_LOG_AND_DROP(("Error when updating peer_keys_db"));
   return num_updated;
}

} // namespace eosio::chain