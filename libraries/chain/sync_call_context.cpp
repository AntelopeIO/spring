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
   return flags & 0b1; // LSB. bit index 0
}

bool sync_call_context::no_op_if_receiver_not_support_sync_call()const {
   return flags & 0b10;  // second bit from LSB. bit index 1
}

} /// eosio::chain
