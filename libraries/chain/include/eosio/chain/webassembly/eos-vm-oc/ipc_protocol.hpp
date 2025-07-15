#pragma once

#include <eosio/chain/webassembly/eos-vm-oc/config.hpp>
#include <eosio/chain/webassembly/eos-vm-oc/eos-vm-oc.hpp>
#include <eosio/chain/types.hpp>

namespace eosio { namespace chain { namespace eosvmoc {

struct initialize_message {
   //Two sent fds: 1) communication socket for this instance  2) the cache file 
};

struct initalize_response_message {
   std::optional<std::string> error_message; //no error message? everything groovy
};

struct code_tuple {
   eosio::chain::digest_type code_id;
   uint8_t vm_version;
   bool operator==(const code_tuple& o) const {return o.code_id == code_id && o.vm_version == vm_version;}
};

struct compile_wasm_message {
   code_tuple code;
   fc::time_point queued_time;      // when compilation was queued to begin
   std::optional<eosvmoc::subjective_compile_limits> limits;
   //Two sent fd: 1) communication socket for result, 2) the wasm to compile
};

struct evict_wasms_message {
   std::vector<code_descriptor> codes;
};

struct code_compilation_result_message {
   eosvmoc_optional_offset_or_import_t start;
   unsigned apply_offset;
   std::optional<unsigned> call_offset;  // sync call entry point
   int starting_memory_pages;
   unsigned initdata_prologue_size;
   fc::time_point queued_time;      // when compilation was queued to begin
   //Two sent fds: 1) wasm code, 2) initial memory snapshot
};


struct compilation_result_unknownfailure {};
struct compilation_result_toofull {};

using wasm_compilation_result = std::variant<code_descriptor,  //a successful compile
                                             compilation_result_unknownfailure,
                                             compilation_result_toofull>;

struct wasm_compilation_result_message {
   code_tuple code;
   wasm_compilation_result result;
   size_t cache_free_bytes;
   fc::time_point queued_time;      // when compilation was queued to begin, copied from compile_wasm_message
};

using eosvmoc_message = std::variant<initialize_message,
                                     initalize_response_message,
                                     compile_wasm_message,
                                     evict_wasms_message,
                                     code_compilation_result_message,
                                     wasm_compilation_result_message>;
}}}

FC_REFLECT(eosio::chain::eosvmoc::initialize_message, )
FC_REFLECT(eosio::chain::eosvmoc::initalize_response_message, (error_message))
FC_REFLECT(eosio::chain::eosvmoc::code_tuple, (code_id)(vm_version))
FC_REFLECT(eosio::chain::eosvmoc::compile_wasm_message, (code)(queued_time)(limits))
FC_REFLECT(eosio::chain::eosvmoc::evict_wasms_message, (codes))
FC_REFLECT(eosio::chain::eosvmoc::code_compilation_result_message, (start)(apply_offset)(call_offset)(starting_memory_pages)(initdata_prologue_size)(queued_time))
FC_REFLECT(eosio::chain::eosvmoc::compilation_result_unknownfailure, )
FC_REFLECT(eosio::chain::eosvmoc::compilation_result_toofull, )
FC_REFLECT(eosio::chain::eosvmoc::wasm_compilation_result_message, (code)(result)(cache_free_bytes)(queued_time))
