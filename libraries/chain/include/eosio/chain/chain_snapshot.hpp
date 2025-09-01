#pragma once

#include <eosio/chain/exceptions.hpp>

namespace eosio::chain {

struct chain_snapshot_header {
   /**
    * Version history
    *   1: initial version
    *   2: Updated chain snapshot for v1.8.0 initial protocol features release:
    *         - Incompatible with version 1.
    *         - Adds new indices for: protocol_state_object and account_ram_correction_object
    *   3: Updated for v2.0.0 protocol features:
    *         - forwards compatible with version 2
    *         - WebAuthn keys
    *         - wtmsig block siganatures: the block header state changed to include producer authorities and additional signatures
    *         - removed genesis_state and added chain ID to global_property_object
    *   4: Updated for v3.0.0 protocol features:
    *         - forwards compatible with versions 2 and 3
    *         - kv database
    *         - Configurable wasm limits
    *   5: Updated for v3.0.0 eos features:
    *         - chain_config update
    *   6: Updated for v3.1.0 release
    *   7: Updated for Spring v1.0.0 release:
    *         - Savanna consensus support
    *         - Each chainbase contract table placed in individual snapshot section instead of commingled "contract_tables" section
    *   8: Updated for Spring v1.0.1 release:
    *         - new member `latest_qc_claim_block_active_finalizer_policy` in `block_header_state`
    *         - 2 new members (`pending` and `active` policy generations in every `block_ref` of the `finality_core`)
    *         - Spring v1.0.1 is incompatible with v7 format, but can read previous formats
    *   9: Updated for Spring v2.0.0 release:
    *         - chain_config_v2 update for new members `max_sync_call_depth` and `max_sync_call_data_size`
    *         - Event support: new event_state table & chain_config_v2 new_event_epoch_log_size_threshold
    */

   static constexpr uint32_t minimum_compatible_version = 2;
   static constexpr uint32_t current_version = 9;

   static constexpr uint32_t first_version_with_split_table_sections = 7;

   uint32_t version = current_version;

   void validate() const {
      auto min = minimum_compatible_version;
      auto max = current_version;
      EOS_ASSERT(version >= min && version <= max,
              snapshot_validation_exception,
              "Unsupported version of chain snapshot: ${version}. Supported version must be between ${min} and ${max} inclusive.",
              ("version",version)("min",min)("max",max));
   }
};

}

FC_REFLECT(eosio::chain::chain_snapshot_header,(version))
