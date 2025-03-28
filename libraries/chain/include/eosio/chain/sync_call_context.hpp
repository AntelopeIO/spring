#pragma once

#include <eosio/chain/types.hpp>

namespace eosio { namespace chain {
   struct sync_call_context {
      // input
      account_name           sender{};
      account_name           receiver{};
      std::span<const char>  data{}; // includes function name, arguments, and other information

      // output
      std::vector<char>      return_value{};
   };
} } /// namespace eosio::chain

FC_REFLECT(eosio::chain::sync_call_context, (sender)(receiver)(data))
