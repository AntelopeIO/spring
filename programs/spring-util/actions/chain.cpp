#include "chain.hpp"
#include <memory>

#include <fc/bitutil.hpp>
#include <fc/filesystem.hpp>
#include <fc/io/cfile.hpp>
#include <fc/io/json.hpp>
#include <fc/variant.hpp>

#include <chainbase/environment.hpp>

#include <boost/exception/diagnostic_information.hpp>

#include <eosio/chain/block_log.hpp>
#include <eosio/chain/exceptions.hpp>

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <filesystem>

using namespace eosio;
using namespace eosio::chain;

void chain_actions::setup(CLI::App& app) {
   auto* sub = app.add_subcommand("chain-state", "chain utility");
   sub->add_option("--state-dir", opt->sstate_state_dir, "The location of the state directory (absolute path or relative to the current directory)")->capture_default_str();
   sub->require_subcommand();
   sub->fallthrough();

   auto* build = sub->add_subcommand("build-info", "extract build environment information as JSON");
   build->add_option("--output-file,-o", opt->build_output_file, "write into specified file")->capture_default_str();
   build->add_flag("--print,-p", opt->build_just_print, "print to console");
   build->require_option(1);

   build->callback([&]() {
      int rc = run_subcommand_build();
      // properly return err code in main
      if(rc) throw(CLI::RuntimeError(rc));
   });

  sub->add_subcommand("last-shutdown-state", "indicate whether last shutdown was clean or not")->callback([&]() {
      int rc = run_subcommand_sstate();
      // properly return err code in main
      if(rc) throw(CLI::RuntimeError(rc));
   });
}

int chain_actions::run_subcommand_build() {
   if(!opt->build_output_file.empty()) {
      std::filesystem::path p = opt->build_output_file;
      if(p.is_relative()) {
         p = std::filesystem::current_path() / p;
      }
      fc::json::save_to_file(chainbase::environment(), p, true);
      std::cout << "Saved build info JSON to '" <<  p.generic_string() << "'" << std::endl;
   }
   if(opt->build_just_print) {
      std::cout << fc::json::to_pretty_string(chainbase::environment()) << std::endl;
   }

   return 0;
}

int chain_actions::run_subcommand_sstate() {
   std::filesystem::path state_dir = "";

   // default state dir, if none specified
   if(opt->sstate_state_dir.empty()) {
      auto root = fc::app_path();
      auto default_data_dir = root / "eosio" / "nodeos" / "data" ;
      state_dir  = default_data_dir / config::default_state_dir_name;
   }
   else {
      // adjust if path relative
      state_dir = opt->sstate_state_dir;
      if(state_dir.is_relative()) {
         state_dir = std::filesystem::current_path() / state_dir;
      }
   }

   const auto shared_mem_path = state_dir / "shared_memory.bin";

   try {
      fc::random_access_file file(shared_mem_path, fc::random_access_file::read_only);
      chainbase::db_header header = file.unpack_from<chainbase::db_header>(0);
      FC_ASSERT(header.id == chainbase::header_id, "\"" + state_dir.string() + "\" database format not compatible with this version of spring-util");
      FC_ASSERT(header.dirty == false, "Database dirty flag is set, shutdown was not clean");
   }
   catch (fc::exception& e) {
      std::string msg = e.top_message();
      if(auto pos = msg.find(": "); pos != std::string_view::npos) //remove ASSERT() criteria
         msg = msg.substr(pos + 2);
      std::cerr << msg << std::endl;
      return -1;
   }

   std::cout << "Database state is clean" << std::endl;
   return 0;
}