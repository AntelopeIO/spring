#include <eosio/chain/webassembly/interface.hpp>
#include <eosio/chain/apply_context.hpp>

namespace eosio { namespace chain { namespace webassembly {
   int32_t interface::call(name receiver, uint64_t flags, std::span<const char> data) {
      return context.execute_sync_call(receiver, flags, data);
   }

   uint32_t interface::get_call_return_value(span<char> memory) const {
      return context.get_call_return_value(memory);;
   }

   uint32_t interface::get_call_data(std::span<char> memory) const {
      return context.get_call_data(memory);
   }

   void interface::set_call_return_value(std::span<const char> return_value) {
      context.set_call_return_value(return_value);;
   }
}}} // ns eosio::chain::webassembly
