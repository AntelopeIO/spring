#include <eosio/chain/host_context.hpp>
#include <eosio/chain/sync_call_context.hpp>
#include <eosio/chain/account_object.hpp>
#include <eosio/chain/global_property_object.hpp>

namespace eosio::chain {

host_context::host_context(controller& con, transaction_context& trx_ctx)
   : control(con)
   , db(con.mutable_db())
   , trx_context(trx_ctx)
   , idx64(*this)
   , idx128(*this)
   , idx256(*this)
   , idx_double(*this)
   , idx_long_double(*this)
{
}

host_context::~host_context() = default;

// called from apply_context or sync_call_context
uint32_t host_context::execute_sync_call(name call_receiver, uint64_t flags, std::span<const char> data) {
   auto* code = control.db().find<account_object, by_name>(call_receiver);
   EOS_ASSERT(code != nullptr, sync_call_validate_exception,
              "sync call's receiver account ${r} does not exist", ("r", call_receiver));
   const auto max_sync_call_data_size = control.get_global_properties().configuration.max_sync_call_data_size;
   EOS_ASSERT(data.size() <= max_sync_call_data_size, sync_call_call_data_exception,
              "sync call call data size must be less or equal to ${s} bytes", ("s", max_sync_call_data_size));
   EOS_ASSERT((flags & ~0b11) == 0, sync_call_validate_exception,  // all but last 2 bits are 0s
              "only two least significant bits of sync call's flags (${flags}) can be set", ("flags", flags)); // bit 0 for read-only, bit 1 for no-op if no sync call entry point exists

   auto handle_exception = [&](const auto& e)
   {
      // TBD need to store the exception in the sync call trace
      throw;
   };

   last_sync_call_return_value = std::nullopt; // reset for current sync call
   uint32_t return_value_size  = 0;

   try {
      try {
         const account_metadata_object* receiver_account = &db.get<account_metadata_object, by_name>( call_receiver);
         if (receiver_account->code_hash.empty()) {
            // TBD store the info in sync call trace
            ilog("receiver_account->code_hash empty");
            return return_value_size;
         }

         try {
            // use a new sync_call_context for next sync call
            sync_call_context call_ctx(control, trx_context, get_sender(), call_receiver, flags, data);
            control.get_wasm_interface().do_sync_call(receiver_account->code_hash, receiver_account->vm_type, receiver_account->vm_version, call_ctx);

            // store return value
            last_sync_call_return_value = std::move(call_ctx.return_value);
            return_value_size = last_sync_call_return_value->size();
         } catch( const wasm_exit&) {}
      } FC_RETHROW_EXCEPTIONS(warn, "sync call exception on ${receiver}", ("receiver", call_receiver))
   } catch (const std::bad_alloc&) {
      throw;
   } catch (const boost::interprocess::bad_alloc&) {
      throw;
   } catch(const fc::exception& e) {
      handle_exception(e);
   } catch (const std::exception& e) {
      auto wrapper = fc::std_exception_wrapper::from_current_exception(e);
      handle_exception(wrapper);
   }
   trx_context.checktime(); // protect against the case where during the removal of the callback, the timer expires.
   return return_value_size;
}

// called from apply_context or sync_call_context
uint32_t host_context::get_call_return_value(std::span<char> memory) const {
   if (!last_sync_call_return_value.has_value()) {
      return 0;
   }

   const auto data_size = last_sync_call_return_value->size();
   const auto copy_size = std::min(memory.size(), data_size);

   if (copy_size == 0) {
      return data_size;
   }

   // Copy up to the length of memory of data to memory
   std::memcpy(memory.data(), last_sync_call_return_value->data(), copy_size);

   // Return the number of bytes of the data that can be retrieved
   return data_size;
}

bool host_context::is_account( const account_name& account )const {
   return nullptr != db.find<account_object,by_name>( account );
}

void host_context::get_code_hash(
   account_name account, uint64_t& code_sequence, fc::sha256& code_hash, uint8_t& vm_type, uint8_t& vm_version) const {

   auto obj = db.find<account_metadata_object,by_name>(account);
   if(!obj || obj->code_hash == fc::sha256{}) {
      if(obj)
         code_sequence = obj->code_sequence;
      else
         code_sequence = 0;
      code_hash = {};
      vm_type = 0;
      vm_version = 0;
   } else {
      code_sequence = obj->code_sequence;
      code_hash = obj->code_hash;
      vm_type = obj->vm_type;
      vm_version = obj->vm_version;
   }
}

const table_id_object* host_context::find_table( name code, name scope, name table ) {
   return db.find<table_id_object, by_code_scope_table>(boost::make_tuple(code, scope, table));
}

const table_id_object& host_context::find_or_create_table( name code, name scope, name table, const account_name &payer ) {
   const auto* existing_tid =  db.find<table_id_object, by_code_scope_table>(boost::make_tuple(code, scope, table));
   if (existing_tid != nullptr) {
      return *existing_tid;
   }

   if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient())) {
      std::string event_id = RAM_EVENT_ID("${code}:${scope}:${table}",
         ("code", code)
         ("scope", scope)
         ("table", table)
      );
      dm_logger->on_ram_trace(std::move(event_id), "table", "add", "create_table");
   }

   update_db_usage(payer, config::billable_size_v<table_id_object>);

   return db.create<table_id_object>([&](table_id_object &t_id){
      t_id.code = code;
      t_id.scope = scope;
      t_id.table = table;
      t_id.payer = payer;

      if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient())) {
         dm_logger->on_create_table(t_id);
      }
   });
}

void host_context::remove_table( const table_id_object& tid ) {
   if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient())) {
      std::string event_id = RAM_EVENT_ID("${code}:${scope}:${table}",
         ("code", tid.code)
         ("scope", tid.scope)
         ("table", tid.table)
      );
      dm_logger->on_ram_trace(std::move(event_id), "table", "remove", "remove_table");
   }

   update_db_usage(tid.payer, - config::billable_size_v<table_id_object>);

   if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient())) {
      dm_logger->on_remove_table(tid);
   }

   db.remove(tid);
}

vector<account_name> host_context::get_active_producers() const {
   const auto& ap = control.active_producers();
   vector<account_name> accounts; accounts.reserve( ap.producers.size() );

   for(const auto& producer : ap.producers )
      accounts.push_back(producer.producer_name);

   return accounts;
}

int host_context::db_store_i64( name scope, name table, const account_name& payer, uint64_t id, const char* buffer, size_t buffer_size ) {
   EOS_ASSERT( !trx_context.is_read_only(), table_operation_not_permitted, "cannot store a db record when executing a readonly transaction" );
   return db_store_i64( receiver, scope, table, payer, id, buffer, buffer_size);
}

int host_context::db_store_i64( name code, name scope, name table, const account_name& payer, uint64_t id, const char* buffer, size_t buffer_size ) {
//   require_write_lock( scope );
   EOS_ASSERT( !trx_context.is_read_only(), table_operation_not_permitted, "cannot store a db record when executing a readonly transaction" );
   const auto& tab = find_or_create_table( code, scope, table, payer );
   auto tableid = tab.id;

   EOS_ASSERT( payer != account_name(), invalid_table_payer, "must specify a valid account to pay for new record" );

   const auto& obj = db.create<key_value_object>( [&]( auto& o ) {
      o.t_id        = tableid;
      o.primary_key = id;
      o.value.assign( buffer, buffer_size );
      o.payer       = payer;
   });

   db.modify( tab, [&]( auto& t ) {
     ++t.count;
   });

   int64_t billable_size = (int64_t)(buffer_size + config::billable_size_v<key_value_object>);

   if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient())) {
      std::string event_id = RAM_EVENT_ID("${table_code}:${scope}:${table_name}:${primkey}",
         ("table_code", tab.code)
         ("scope", tab.scope)
         ("table_name", tab.table)
         ("primkey", name(obj.primary_key))
      );
      dm_logger->on_ram_trace(std::move(event_id), "table_row", "add", "primary_index_add");
   }

   update_db_usage( payer, billable_size);

   if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient())) {
      dm_logger->on_db_store_i64(tab, obj);
   }

   keyval_cache.cache_table( tab );
   return keyval_cache.add( obj );
}

void host_context::db_update_i64( int iterator, account_name payer, const char* buffer, size_t buffer_size ) {
   EOS_ASSERT( !trx_context.is_read_only(), table_operation_not_permitted, "cannot update a db record when executing a readonly transaction" );
   const key_value_object& obj = keyval_cache.get( iterator );

   const auto& table_obj = keyval_cache.get_table( obj.t_id );
   EOS_ASSERT( table_obj.code == receiver, table_access_violation, "db access violation" );

//   require_write_lock( table_obj.scope );

   const int64_t overhead = config::billable_size_v<key_value_object>;
   int64_t old_size = (int64_t)(obj.value.size() + overhead);
   int64_t new_size = (int64_t)(buffer_size + overhead);

   if( payer == account_name() ) payer = obj.payer;

   std::string event_id;
   if (control.get_deep_mind_logger(trx_context.is_transient()) != nullptr) {
      event_id = RAM_EVENT_ID("${table_code}:${scope}:${table_name}:${primkey}",
         ("table_code", table_obj.code)
         ("scope", table_obj.scope)
         ("table_name", table_obj.table)
         ("primkey", name(obj.primary_key))
      );
   }

   if( account_name(obj.payer) != payer ) {
      // refund the existing payer
      if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient()))
      {
         dm_logger->on_ram_trace(std::string(event_id), "table_row", "remove", "primary_index_update_remove_old_payer");
      }
      update_db_usage( obj.payer,  -(old_size) );
      // charge the new payer
      if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient()))
      {
         dm_logger->on_ram_trace(std::move(event_id), "table_row", "add", "primary_index_update_add_new_payer");
      }
      update_db_usage( payer,  (new_size));
   } else if(old_size != new_size) {
      // charge/refund the existing payer the difference
      if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient()))
      {
         dm_logger->on_ram_trace(std::move(event_id) , "table_row", "update", "primary_index_update");
      }
      update_db_usage( obj.payer, new_size - old_size);
   }

   if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient())) {
      dm_logger->on_db_update_i64(table_obj, obj, payer, buffer, buffer_size);
   }

   db.modify( obj, [&]( auto& o ) {
     o.value.assign( buffer, buffer_size );
     o.payer = payer;
   });
}

void host_context::db_remove_i64( int iterator ) {
   EOS_ASSERT( !trx_context.is_read_only(), table_operation_not_permitted, "cannot remove a db record when executing a readonly transaction" );
   const key_value_object& obj = keyval_cache.get( iterator );

   const auto& table_obj = keyval_cache.get_table( obj.t_id );
   EOS_ASSERT( table_obj.code == receiver, table_access_violation, "db access violation" );

//   require_write_lock( table_obj.scope );

   if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient())) {
      std::string event_id = RAM_EVENT_ID("${table_code}:${scope}:${table_name}:${primkey}",
         ("table_code", table_obj.code)
         ("scope", table_obj.scope)
         ("table_name", table_obj.table)
         ("primkey", name(obj.primary_key))
      );
      dm_logger->on_ram_trace(std::move(event_id), "table_row", "remove", "primary_index_remove");
   }

   update_db_usage( obj.payer,  -(obj.value.size() + config::billable_size_v<key_value_object>) );

   if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient())) {
      dm_logger->on_db_remove_i64(table_obj, obj);
   }

   db.modify( table_obj, [&]( auto& t ) {
      --t.count;
   });
   db.remove( obj );

   if (table_obj.count == 0) {
      remove_table(table_obj);
   }

   keyval_cache.remove( iterator );
}

int host_context::db_get_i64( int iterator, char* buffer, size_t buffer_size ) {
   const key_value_object& obj = keyval_cache.get( iterator );

   auto s = obj.value.size();
   if( buffer_size == 0 ) return s;

   auto copy_size = std::min( buffer_size, s );
   memcpy( buffer, obj.value.data(), copy_size );

   return copy_size;
}

int host_context::db_next_i64( int iterator, uint64_t& primary ) {
   if( iterator < -1 ) return -1; // cannot increment past end iterator of table

   const auto& obj = keyval_cache.get( iterator ); // Check for iterator != -1 happens in this call
   const auto& idx = db.get_index<key_value_index, by_scope_primary>();

   auto itr = idx.iterator_to( obj );
   ++itr;

   if( itr == idx.end() || itr->t_id != obj.t_id ) return keyval_cache.get_end_iterator_by_table_id(obj.t_id);

   primary = itr->primary_key;
   return keyval_cache.add( *itr );
}

int host_context::db_previous_i64( int iterator, uint64_t& primary ) {
   const auto& idx = db.get_index<key_value_index, by_scope_primary>();

   if( iterator < -1 ) // is end iterator
   {
      auto tab = keyval_cache.find_table_by_end_iterator(iterator);
      EOS_ASSERT( tab, invalid_table_iterator, "not a valid end iterator" );

      auto itr = idx.upper_bound(tab->id);
      if( idx.begin() == idx.end() || itr == idx.begin() ) return -1; // Empty table

      --itr;

      if( itr->t_id != tab->id ) return -1; // Empty table

      primary = itr->primary_key;
      return keyval_cache.add(*itr);
   }

   const auto& obj = keyval_cache.get(iterator); // Check for iterator != -1 happens in this call

   auto itr = idx.iterator_to(obj);
   if( itr == idx.begin() ) return -1; // cannot decrement past beginning iterator of table

   --itr;

   if( itr->t_id != obj.t_id ) return -1; // cannot decrement past beginning iterator of table

   primary = itr->primary_key;
   return keyval_cache.add(*itr);
}

int host_context::db_find_i64( name code, name scope, name table, uint64_t id ) {
   //require_read_lock( code, scope ); // redundant?

   const auto* tab = find_table( code, scope, table );
   if( !tab ) return -1;

   auto table_end_itr = keyval_cache.cache_table( *tab );

   const key_value_object* obj = db.find<key_value_object, by_scope_primary>( boost::make_tuple( tab->id, id ) );
   if( !obj ) return table_end_itr;

   return keyval_cache.add( *obj );
}

int host_context::db_lowerbound_i64( name code, name scope, name table, uint64_t id ) {
   //require_read_lock( code, scope ); // redundant?

   const auto* tab = find_table( code, scope, table );
   if( !tab ) return -1;

   auto table_end_itr = keyval_cache.cache_table( *tab );

   const auto& idx = db.get_index<key_value_index, by_scope_primary>();
   auto itr = idx.lower_bound( boost::make_tuple( tab->id, id ) );
   if( itr == idx.end() ) return table_end_itr;
   if( itr->t_id != tab->id ) return table_end_itr;

   return keyval_cache.add( *itr );
}

int host_context::db_upperbound_i64( name code, name scope, name table, uint64_t id ) {
   //require_read_lock( code, scope ); // redundant?

   const auto* tab = find_table( code, scope, table );
   if( !tab ) return -1;

   auto table_end_itr = keyval_cache.cache_table( *tab );

   const auto& idx = db.get_index<key_value_index, by_scope_primary>();
   auto itr = idx.upper_bound( boost::make_tuple( tab->id, id ) );
   if( itr == idx.end() ) return table_end_itr;
   if( itr->t_id != tab->id ) return table_end_itr;

   return keyval_cache.add( *itr );
}

int host_context::db_end_i64( name code, name scope, name table ) {
   //require_read_lock( code, scope ); // redundant?

   const auto* tab = find_table( code, scope, table );
   if( !tab ) return -1;

   return keyval_cache.cache_table( *tab );
}

} /// eosio::chain
