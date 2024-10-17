#pragma once
#include <fc/uint128.hpp>
#include <fc/array.hpp>

#include <eosio/chain/types.hpp>
#include <eosio/chain/block_timestamp.hpp>
#include <eosio/chain/chain_config.hpp>
#include <eosio/chain/chain_snapshot.hpp>
#include <eosio/chain/kv_config.hpp>
#include <eosio/chain/wasm_config.hpp>
#include <eosio/chain/producer_schedule.hpp>
#include <eosio/chain/finalizer_policy.hpp>
#include <eosio/chain/snapshot.hpp>
#include <eosio/chain/genesis_state.hpp>
#include <chainbase/chainbase.hpp>
#include "multi_index_includes.hpp"

namespace eosio::chain {

   /**
    * a fc::raw::unpack compatible version of the old global_property_object structure stored in
    * version 2 snapshots and before
    */
   namespace legacy {
      struct snapshot_global_property_object_v2 {
         static constexpr uint32_t minimum_version = 0;
         static constexpr uint32_t maximum_version = 2;
         static_assert(chain_snapshot_header::minimum_compatible_version <= maximum_version,
                       "snapshot_global_property_object_v2 is no longer needed");

         std::optional<block_num_type>    proposed_schedule_block_num;
         producer_schedule_type           proposed_schedule;
         chain_config_v0                  configuration;
      };
      struct snapshot_global_property_object_v3 {
         static constexpr uint32_t minimum_version = 3;
         static constexpr uint32_t maximum_version = 3;
         static_assert(chain_snapshot_header::minimum_compatible_version <= maximum_version,
                       "snapshot_global_property_object_v3 is no longer needed");

         std::optional<block_num_type>       proposed_schedule_block_num;
         producer_authority_schedule         proposed_schedule;
         chain_config_v0                     configuration;
         chain_id_type                       chain_id;
      };
      struct snapshot_global_property_object_v4 {
         static constexpr uint32_t minimum_version = 4;
         static constexpr uint32_t maximum_version = 4;
         static_assert(chain_snapshot_header::minimum_compatible_version <= maximum_version,
                       "snapshot_global_property_object_v4 is no longer needed");

         std::optional<block_num_type>       proposed_schedule_block_num;
         producer_authority_schedule         proposed_schedule;
         chain_config_v0                     configuration;
         chain_id_type                       chain_id;
         kv_database_config                  kv_configuration;
         wasm_config                         wasm_configuration;
      };
      struct snapshot_global_property_object_v5 {
         // minimum_version and maximum_version refer to chain_snapshot_header
         // version. snapshot_global_property_object_v5 applies to version 5 & 6
         static constexpr uint32_t minimum_version = 5;
         static constexpr uint32_t maximum_version = 6;
         static_assert(chain_snapshot_header::minimum_compatible_version <= maximum_version,
                       "snapshot_global_property_object_v5 is no longer needed");

         std::optional<block_num_type>       proposed_schedule_block_num;
         producer_authority_schedule         proposed_schedule;
         chain_config                        configuration;
         chain_id_type                       chain_id;
         kv_database_config                  kv_configuration;
         wasm_config                         wasm_configuration;
      };
   }

   /**
    * @class global_property_object
    * @brief Maintains global state information about block producer schedules and chain configuration parameters
    * @ingroup object
    * @ingroup implementation
    */
   class global_property_object : public chainbase::object<global_property_object_type, global_property_object>
   {
      OBJECT_CTOR(global_property_object, (proposed_schedule))

   public:
      id_type                             id;
      std::optional<block_num_type>       proposed_schedule_block_num;
      shared_producer_authority_schedule  proposed_schedule;
      chain_config                        configuration;
      chain_id_type                       chain_id;
      wasm_config                         wasm_configuration;

      // For snapshot_global_property_object_v2 and initialize_from( const T& legacy )
      template<typename T>
      void initialize_from( const T& legacy, const chain_id_type& chain_id_val) {
         proposed_schedule_block_num = legacy.proposed_schedule_block_num;

         if constexpr (std::is_same_v<T, legacy::snapshot_global_property_object_v2>) {
            proposed_schedule = producer_authority_schedule(legacy.proposed_schedule);
         } else {
            proposed_schedule = legacy.proposed_schedule;
         }

         configuration = legacy.configuration;
         chain_id = chain_id_val;

         if constexpr (std::is_same_v<T, legacy::snapshot_global_property_object_v2> ||
                       std::is_same_v<T, legacy::snapshot_global_property_object_v3>) {
            wasm_configuration = genesis_state::default_initial_wasm_configuration;
         } else {
            wasm_configuration = legacy.wasm_configuration;
         }
      }

      // For snapshot_global_property_object v3, v4, and v5
      template<typename T>
      void initialize_from( const T& legacy ) {
         initialize_from(legacy, legacy.chain_id);
      }
   };

   using global_property_multi_index = chainbase::shared_multi_index_container<
      global_property_object,
      indexed_by<
         ordered_unique<tag<by_id>,
            BOOST_MULTI_INDEX_MEMBER(global_property_object, global_property_object::id_type, id)
         >
      >
   >;

   struct snapshot_global_property_object {
      std::optional<block_num_type>       proposed_schedule_block_num;
      producer_authority_schedule         proposed_schedule;
      chain_config                        configuration;
      chain_id_type                       chain_id;
      wasm_config                         wasm_configuration;
   };

   namespace detail {
      template<>
      struct snapshot_row_traits<global_property_object> {
         using value_type = global_property_object;
         using snapshot_type = snapshot_global_property_object;

         static snapshot_global_property_object to_snapshot_row( const global_property_object& value, const chainbase::database& ) {
            return {value.proposed_schedule_block_num, producer_authority_schedule::from_shared(value.proposed_schedule),
                    value.configuration, value.chain_id, value.wasm_configuration};
         }

         static void from_snapshot_row( snapshot_global_property_object&& row, global_property_object& value, chainbase::database& ) {
            value.proposed_schedule_block_num = row.proposed_schedule_block_num;
            value.proposed_schedule = row.proposed_schedule;
            value.configuration = row.configuration;
            value.chain_id = row.chain_id;
            value.wasm_configuration = row.wasm_configuration;
         }
      };
   }

   /**
    * @class dynamic_global_property_object
    * @brief Maintains global state information that frequently change
    * @ingroup object
    * @ingroup implementation
    */
   class dynamic_global_property_object : public chainbase::object<dynamic_global_property_object_type, dynamic_global_property_object>
   {
        OBJECT_CTOR(dynamic_global_property_object)

        id_type    id;
        uint64_t   global_action_sequence = 0;
   };

   using dynamic_global_property_multi_index = chainbase::shared_multi_index_container<
      dynamic_global_property_object,
      indexed_by<
         ordered_unique<tag<by_id>,
            BOOST_MULTI_INDEX_MEMBER(dynamic_global_property_object, dynamic_global_property_object::id_type, id)
         >
      >
   >;

}

CHAINBASE_SET_INDEX_TYPE(eosio::chain::global_property_object, eosio::chain::global_property_multi_index)
CHAINBASE_SET_INDEX_TYPE(eosio::chain::dynamic_global_property_object,
                         eosio::chain::dynamic_global_property_multi_index)

FC_REFLECT(eosio::chain::global_property_object,
            (proposed_schedule_block_num)(proposed_schedule)(configuration)(chain_id)(wasm_configuration)
          )

FC_REFLECT(eosio::chain::legacy::snapshot_global_property_object_v2,
            (proposed_schedule_block_num)(proposed_schedule)(configuration)
          )

FC_REFLECT(eosio::chain::legacy::snapshot_global_property_object_v3,
            (proposed_schedule_block_num)(proposed_schedule)(configuration)(chain_id)
          )

FC_REFLECT(eosio::chain::legacy::snapshot_global_property_object_v4,
            (proposed_schedule_block_num)(proposed_schedule)(configuration)(chain_id)(kv_configuration)(wasm_configuration)
          )

FC_REFLECT(eosio::chain::legacy::snapshot_global_property_object_v5,
            (proposed_schedule_block_num)(proposed_schedule)(configuration)(chain_id)(kv_configuration)(wasm_configuration)
          )

FC_REFLECT(eosio::chain::snapshot_global_property_object,
            (proposed_schedule_block_num)(proposed_schedule)(configuration)(chain_id)(wasm_configuration)
          )

FC_REFLECT(eosio::chain::dynamic_global_property_object,
            (global_action_sequence)
          )
