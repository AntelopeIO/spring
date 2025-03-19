#pragma once

#include <eosio/chain/types.hpp>

namespace eosio { namespace chain {
   struct sync_call_context {
      account_name           sender{};
      account_name           receiver{};
      std::span<const char>  data{}; // includes function name, arguments, and other information
   };
} } /// namespace eosio::chain

FC_REFLECT(eosio::chain::sync_call_context, (sender)(receiver)(data))
