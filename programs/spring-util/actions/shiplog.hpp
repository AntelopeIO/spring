#include "subcommand.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

/// CLI options shared by the ship-log subcommands.
struct shiplog_options {
   std::string state_history_dir = "state-history";
   std::string log_name; ///< --log: a well-known stem (trace_history, ...) or a path to a bundle
   //these two double as "not supplied" sentinels for trim (see trim()); block 0 and UINT32_MAX are
   // never valid ship block numbers, so a user can never collide with them
   uint32_t    first_block = 0;
   uint32_t    last_block  = std::numeric_limits<uint32_t>::max();
   uint32_t    block_num   = 0; ///< --block: the block a block-id lookup asks about
   std::string output_dir;
   uint32_t    stride = 0;

   // flags
   bool deep      = false;
   bool dry_run   = false;
   bool keep_tail = false;
};

/**
 * spring-util `ship-log` subcommand family: offline inspection and repair of state history (SHiP)
 * log bundles, built on eosio::state_history::log_utils. Mirrors the structure of the `block-log`
 * subcommands.
 */
class shiplog_actions : public sub_command<shiplog_options> {
public:
   shiplog_actions() : sub_command() {}
   void setup(CLI::App& app);

protected:
   /// normalize option paths (relative -> absolute) before any subcommand runs
   void initialize();

   /// resolve --log (required) to a bundle stem; accepts a name in the state-history dir or a path
   std::filesystem::path resolve_stem() const;
   /// resolve --log to one stem, or, when --log was not given, every *.log bundle in the directory
   std::vector<std::filesystem::path> resolve_stems() const;

   int info();
   int block_id();
   int smoke_test();
   int make_index();
   int trim();
   int extract();
   int repair();
   int do_vacuum(); ///< named to mirror blocklog_actions::do_vacuum (see class doc)
   int split();
   int merge();
};
