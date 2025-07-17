#include <eosio/chain/code_object.hpp>
#include <eosio/chain/wasm_interface.hpp>

namespace eosio { namespace chain {

void code_object::reflector_init()
{
   sync_call_supported = wasm_interface::is_sync_call_supported(code.data(), code.size());
}

}}
