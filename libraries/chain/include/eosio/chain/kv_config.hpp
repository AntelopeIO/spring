#pragma once

#include <fc/reflect/reflect.hpp>
#include <cstdint>

namespace eosio { namespace chain {

   /**
    * @brief limits for a kv database: NOT IN USE for spring
    *
    * Each database (ram or disk, currently) has its own limits for these parameters.
    * The key and value limits apply when adding or modifying elements.  They may be reduced
    * below existing database entries.
    * 
    * Current uses of kv_database_config were removed in Spring 1.0.
    * Do not use it in the future.
    *
    * The file is kept for backward compatibilities as kv_database_config was used
    * in snapshots containing legacy::snapshot_global_property_object_v4 and
    * legacy::snapshot_global_property_object_v5.
    */
   struct kv_database_config {
      std::uint32_t max_key_size   = 0; ///< the maximum size in bytes of a key
      std::uint32_t max_value_size = 0; ///< the maximum size in bytes of a value
      std::uint32_t max_iterators  = 0; ///< the maximum number of iterators that a contract can have simultaneously.
   };
   inline bool operator==(const kv_database_config& lhs, const kv_database_config& rhs) {
      return std::tie(lhs.max_key_size, lhs.max_value_size, lhs.max_iterators)
          == std::tie(rhs.max_key_size, rhs.max_value_size, rhs.max_iterators);
   }
   inline bool operator!=(const kv_database_config& lhs, const kv_database_config& rhs) {
      return !(lhs == rhs);
   }
}}

FC_REFLECT(eosio::chain::kv_database_config, (max_key_size)(max_value_size)(max_iterators))

