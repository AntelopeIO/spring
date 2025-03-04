#include <eosio/net_plugin/peer_keys_db.hpp>
#include <eosio/chain/contract_table_objects.hpp>

namespace eosio::chain {

peer_keys_db_t::peer_keys_db_t()
   : _peer_key_map( boost::shared_ptr<peer_key_map_t>(new peer_key_map_t)) {
}

void peer_keys_db_t::on_accepted_block(const controller& chain) {
   try {
      const auto& head = chain.head();

      // let's update once every 20 blocks, so every 10 seconds
      if (head.is_valid() && block_header::num_from_id(head.id()) % 20 == 0) {
         auto new_map = boost::shared_ptr<peer_key_map_t>(new peer_key_map_t);

         const auto& d = chain.db();
         const auto* t_id =
            d.find<table_id_object, by_code_scope_table>(boost::make_tuple("eosio"_n, "eosio"_n, "peerkeys"_n));
         
         if (t_id != nullptr) {
            const auto&        idx = d.get_index<chain::key_value_index, chain::by_scope_primary>();
            decltype(t_id->id) next_tid(t_id->id._id + 1);
            auto               lower = idx.lower_bound(boost::make_tuple(t_id->id));
            auto               upper = idx.lower_bound(boost::make_tuple(next_tid));

            for (auto itr = lower; itr != upper; ++itr) {
               const key_value_object& obj = *itr;
               fc::datastream<const char *> ds(obj.value.data(), obj.value.size());

               peer_key_map_t::value_type row;
               fc::raw::unpack(ds, const_cast<name&>(row.first));
               fc::raw::unpack(ds, row.second);
               new_map->insert(row);
            }
         }

         auto old_map = get_peer_key_map();
         if (*new_map != *old_map)
            _peer_key_map = new_map;
      }
   } FC_LOG_AND_DROP(("Error when updating peer_keys_db"));
}

} // namespace eosio::chain