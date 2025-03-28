#pragma once

#include <eosio/chain/types.hpp>

namespace eosio { namespace chain {
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
   };
} } /// namespace eosio::chain

FC_REFLECT(eosio::chain::sync_call_context, (sender)(receiver)(flags)(data)(return_value))
