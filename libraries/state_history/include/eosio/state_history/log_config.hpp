#pragma once

#include <filesystem>
#include <variant>
#include <optional>
#include <stdint.h>

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

}