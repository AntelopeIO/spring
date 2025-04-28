#pragma once
#include <eosio/chain/database_utils.hpp>
#include <eosio/chain/chain_snapshot.hpp>
#include <boost/tuple/tuple_io.hpp>

#include "multi_index_includes.hpp"

namespace eosio { namespace chain {

   class code_object : public chainbase::object<code_object_type, code_object> {
      OBJECT_CTOR(code_object, (code))

      id_type      id;
      digest_type  code_hash; //< code_hash should not be changed within a chainbase modifier lambda
      shared_blob  code;
      uint64_t     code_ref_count;
      uint32_t     first_block_used;
      uint8_t      vm_type = 0; //< vm_type should not be changed within a chainbase modifier lambda
      uint8_t      vm_version = 0; //< vm_version should not be changed within a chainbase modifier lambda
      bool         sync_call_supported = false; //< true if code contains a valid sync_call entry point
   };

   struct by_code_hash;
   using code_index = chainbase::shared_multi_index_container<
      code_object,
      indexed_by<
         ordered_unique<tag<by_id>, member<code_object, code_object::id_type, &code_object::id>>,
         ordered_unique<tag<by_code_hash>,
            composite_key< code_object,
               member<code_object, digest_type, &code_object::code_hash>,
               member<code_object, uint8_t,     &code_object::vm_type>,
               member<code_object, uint8_t,     &code_object::vm_version>
            >
         >
      >
   >;

   struct snapshot_code_object_v1 {
      static constexpr uint32_t minimum_version = 0;
      static constexpr uint32_t maximum_version = 8;
      static_assert(chain_snapshot_header::minimum_compatible_version <= maximum_version,
                    "snapshot_code_object_v1 is no longer needed");

      digest_type  code_hash;
      shared_blob  code;
      uint64_t     code_ref_count;
      uint32_t     first_block_used;
      uint8_t      vm_type = 0;
      uint8_t      vm_version = 0;
   };

} } // eosio::chain

CHAINBASE_SET_INDEX_TYPE(eosio::chain::code_object, eosio::chain::code_index)

FC_REFLECT(eosio::chain::code_object, (code_hash)(code)(code_ref_count)(first_block_used)(vm_type)(vm_version)(sync_call_supported))
FC_REFLECT(eosio::chain::snapshot_code_object_v1, (code_hash)(code)(code_ref_count)(first_block_used)(vm_type)(vm_version))
