#include <eosio/chain/sync_call_context.hpp>
#include <eosio/chain/account_object.hpp>
#include <eosio/chain/code_object.hpp>
#include <eosio/chain/global_property_object.hpp>

namespace eosio::chain {

sync_call_context::sync_call_context(controller&           con,
                                     transaction_context&  trx_ctx,
                                     uint32_t              ordinal,
                                     action_trace&         current_action_trace,
                                     account_name          sender,
                                     account_name          receiver,
                                     bool                  privileged,
                                     uint32_t              sync_call_depth,
                                     uint64_t              flags,
                                     std::span<const char> data)
   : host_context(con, trx_ctx, receiver, privileged, sync_call_depth)
   , ordinal(ordinal)
   , current_action_trace(current_action_trace)
   , sender(sender)
   , read_only(has_field(flags, sync_call_flags::force_read_only))
   , data(data)
{
}

uint32_t sync_call_context::get_call_data(std::span<char> memory) const {
   auto        data_size = data.size();
   auto        copy_size = std::min(memory.size(), data_size);

   if (copy_size == 0) {
      return data_size;
   }

   // Copy up to the length of memory of data to memory
   std::memcpy(memory.data(), data.data(), copy_size);

   // Return the number of bytes of the data that can be retrieved
   return data_size;
}

void sync_call_context::set_call_return_value(std::span<const char> rv) {
   const auto max_sync_call_data_size = control.get_global_properties().configuration.max_sync_call_data_size;
   EOS_ASSERT(rv.size() <= max_sync_call_data_size, sync_call_return_value_exception,
              "sync call return value size must be less or equal to ${s} bytes", ("s", max_sync_call_data_size));

   return_value.assign(rv.data(), rv.data() + rv.size());
}

// Returns the sender of any sync call initiated by this apply_context or sync_call_ctx
action_name sync_call_context::get_sender() const {
   // The sync call is initiated by this apply_context or its sync_call_ctx.
   // That's why the context's receiver is the sender of the sync call.
   return receiver;
}

void sync_call_context::console_append(std::string_view val) {
   // save into call trace's console directly
   call_trace& trace = get_call_trace(ordinal);
   trace.console += val;
}

void sync_call_context::store_console_marker() {
   // Only do this when console log is enabled; otherwise we will end up with  a non-empty
   // console markers vector with an empty console string.
   if (!control.contracts_console()) {
      return;
   }

   // Mark the starting point of upcoming sync call's console log
   // when constructing coonsole log hierarchy in pretty printing
   call_trace& trace = get_call_trace(ordinal);
   trace.console_markers.emplace_back(trace.console.size());
}

// Always return false in sync calls
bool sync_call_context::has_recipient(account_name account)const {
   return false;
}
bool sync_call_context::is_context_free()const {
   return false;
}

// This needs to be investigated further
void sync_call_context::update_db_usage( const account_name& payer, int64_t delta ) {}

} /// eosio::chain
