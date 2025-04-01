#pragma once

#include <eosio/chain/types.hpp>

namespace eosio { namespace chain {
   enum class sync_call_return_code : int32_t {
      receiver_not_support_sync_call = -1,  // no sync_call entry point or its signature is invalid
      success                        = 0
   };

   enum class sync_call_flags {
      read_only  = 1ull<<0,
      last       = read_only
   };

   struct sync_call_context {
      sync_call_context(account_name sender, account_name receiver, uint64_t flags, std::span<const char>  data);
      bool is_read_only()const;

      // input
      account_name           sender{};
      account_name           receiver{};
      uint64_t               flags = 0;
      std::span<const char>  data{}; // includes function name, arguments, and other information

      // output
      std::vector<char>      return_value{};

      bool receiver_supports_sync_call = false; // the receiver contract has valid sync_call entry point
   };
} } /// namespace eosio::chain

FC_REFLECT(eosio::chain::sync_call_context, (sender)(receiver)(flags)(data)(return_value)(receiver_supports_sync_call))
