#include <eosio/chain/sync_call_context.hpp>
#include <eosio/chain/account_object.hpp>
#include <eosio/chain/code_object.hpp>
#include <eosio/chain/global_property_object.hpp>

namespace eosio::chain {

sync_call_context::sync_call_context(controller& con, transaction_context& trx_ctx, account_name sender, account_name receiver, uint64_t flags, std::span<const char>data)
   : host_context(con, trx_ctx)
   , sender(sender)
   , receiver(receiver)
   , flags(flags)
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

bool sync_call_context::is_read_only()const {
   return flags & static_cast<uint64_t>(sync_call_flags::read_only);
}

bool sync_call_context::no_op_if_receiver_not_support_sync_call()const {
   return flags & static_cast<uint64_t>(sync_call_flags::no_op_if_receiver_not_support_sync_call);
}

// Returns the sender of any sync call initiated by this apply_context or sync_call_ctx
action_name sync_call_context::get_sender() const {
   // The sync call is initiated by this apply_context or its sync_call_ctx.
   // That's why the context's receiver is the sender of the sync call.
   return receiver;
}

// EOS_ASSERTs and tests will be added for the following methods in next PR
void sync_call_context::require_authorization(const account_name& account) {
}
bool sync_call_context::has_authorization(const account_name& account) const {
   return false;
}
void sync_call_context::require_authorization(const account_name& account, const permission_name& permission) {
}
void sync_call_context::require_recipient(account_name account) {
}
bool sync_call_context::has_recipient(account_name account)const {
   return false;
}
void sync_call_context::update_db_usage( const account_name& payer, int64_t delta ) {
}
int sync_call_context::get_action( uint32_t type, uint32_t index, char* buffer, size_t buffer_size)const {
   return 0;
}
int sync_call_context::get_context_free_data( uint32_t index, char* buffer, size_t buffer_size )const {
   return 0;
}
bool sync_call_context::is_context_free()const {
   return false;
}
bool sync_call_context::is_privileged()const {
   return false;
}
action_name sync_call_context::get_receiver()const {
   return {};
}
const action& sync_call_context::get_action()const {
   static action t;
   return t;
}
const action* sync_call_context::get_action_ptr()const {
   static action t;
   return &t;
}
void sync_call_context::exec() {
}
void sync_call_context::execute_inline( action&& a ) {
}
void sync_call_context::execute_context_free_inline( action&& a ) {
}
void sync_call_context::schedule_deferred_transaction( const uint128_t& sender_id, account_name payer, transaction&& trx, bool replace_existing ) {
}
bool sync_call_context::cancel_deferred_transaction( const uint128_t& sender_id, account_name sender ) {
   return false;
}
bool sync_call_context::cancel_deferred_transaction( const uint128_t& sender_id) {
   return false;
}

} /// eosio::chain
