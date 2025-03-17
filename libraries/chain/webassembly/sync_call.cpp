#include <eosio/chain/webassembly/interface.hpp>
#include <eosio/chain/apply_context.hpp>

namespace eosio { namespace chain { namespace webassembly {
   void interface::call(name receiver, uint64_t flags, std::span<const char> data) {
      context.execute_sync_call(receiver, flags, data);
   }

   uint32_t interface::get_call_return_value(span<char> /* memory */) const {
      return 0;
   }

   uint32_t interface::get_call_data(span<char> memory) const {
      return context.get_call_data(memory);
   }

   void interface::set_call_return_value(span<const char> /* value */) {
      ;
   }
}}} // ns eosio::chain::webassembly
