#include <eosio/chain/sync_call_context.hpp>

namespace eosio::chain {

sync_call_context::sync_call_context(account_name sender, account_name receiver, uint64_t flags, std::span<const char>data)
   : sender(sender)
   , receiver(receiver)
   , flags(flags)
   , data(data)
{
}

bool sync_call_context::is_read_only()const {
   return flags & static_cast<uint64_t>(sync_call_flags::read_only);
}

bool sync_call_context::no_op_if_receiver_not_support_sync_call()const {
   return flags & static_cast<uint64_t>(sync_call_flags::no_op_if_receiver_not_support_sync_call);
}

} /// eosio::chain
