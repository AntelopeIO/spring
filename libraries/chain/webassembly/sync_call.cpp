#include <eosio/chain/webassembly/interface.hpp>

namespace eosio { namespace chain { namespace webassembly {
   void interface::call(name /* receiver */, uint64_t /* flags */, span<const char> /* data */) {
      ;
   }

   uint32_t interface::get_call_data(span<char> /* memory */) const {
      return 0;
   }

   void interface::set_call_return_value(span<const char> /* value */) {
      ;
   }

}}} // ns eosio::chain::webassembly
