#pragma once

#include <filesystem>
#include <variant>
#include <optional>
#include <stdint.h>
#include <eosio/chain/types.hpp>

namespace eosio::state_history {

struct prune_config {
   uint32_t                prune_blocks;                  //when pruning, the number of most recent blocks to remain available in the log
   size_t                  prune_threshold = 4*1024*1024; //(approximately) how many bytes need to be added before a prune is performed
   std::optional<size_t>   vacuum_on_close;               //when set, a vacuum is performed on dtor if log contains less than this many bytes
};

struct partition_config {
   std::filesystem::path retained_dir       = "retained";
   std::filesystem::path archive_dir        = "archive";
   uint32_t              stride             = 1000000;
   uint32_t              max_retained_files = UINT32_MAX;
};

using state_history_log_config = std::variant<std::monostate, prune_config, partition_config>;

std::ostream& boost_test_print_type(std::ostream& os, const state_history_log_config& conf) {
   std::visit(chain::overloaded {
      [&os](const std::monostate&) {
         os << "flat";
      },
      [&os](const prune_config& pc) {
         os << "prune:" << pc.prune_blocks << "," << pc.prune_threshold << "," << (pc.vacuum_on_close.has_value() ? std::to_string(*pc.vacuum_on_close) : "no");
      },
      [&os](const partition_config& pc) {
         os << "split:" << pc.retained_dir << "," << pc.archive_dir << "," << pc.stride << "," << pc.max_retained_files;
      }
   }, conf);
   return os;
}

}