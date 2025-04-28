#include <eosio/chain/code_object.hpp>
#include <eosio/chain/wasm_interface.hpp>

namespace eosio { namespace chain {

void code_object::reflector_init()
{
   static_assert(fc::raw::has_feature_reflector_init_on_unpacked_reflected_types,
                 "code_object expects FC to support reflector_init");

   sync_call_supported = wasm_interface::is_sync_call_supported(code.data(), code.size());
}

}}
