#include <fc/log/logger_config.hpp>
#include <fc/log/appender.hpp>
#include <fc/io/json.hpp>
#include <fc/filesystem.hpp>
#include <unordered_map>
#include <string>
#include <fc/log/console_appender.hpp>
#include <fc/log/gelf_appender.hpp>
#include <fc/log/dmlog_appender.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/exception/exception.hpp>

#define BOOST_DLL_USE_STD_FS
#include <boost/dll/runtime_symbol_info.hpp>

namespace fc {

   log_config& log_config::get() {
      // allocate dynamically which will leak on exit but allow loggers to be used until the very end of execution
      static log_config* the = new log_config;
      return *the;
   }

   bool log_config::register_appender( const std::string& type, const appender_factory::ptr& f )
   {
      std::lock_guard g( log_config::get().log_mutex );
      log_config::get().appender_factory_map[type] = f;
      return true;
   }

   logger log_config::get_logger( const std::string& name ) {
      std::lock_guard g( log_config::get().log_mutex );
      return log_config::get().logger_map[name];
   }

   void log_config::update_logger( const std::string& name, logger& log ) {
      update_logger_with_default(name, log, DEFAULT_LOGGER);
   }

   void log_config::update_logger_with_default( const std::string& name, logger& log, const std::string& default_name ) {
      std::lock_guard g( log_config::get().log_mutex );
      if( log_config::get().logger_map.find( name ) != log_config::get().logger_map.end() ) {
         log = log_config::get().logger_map[name];
      } else {
         // no entry for logger, so setup with default logger if it exists, otherwise do nothing since default logger not configured
         if( log_config::get().logger_map.find( default_name ) != log_config::get().logger_map.end() ) {
            log = log_config::get().logger_map[default_name];
            log_config::get().logger_map.emplace( name, log );
         }
      }
   }

   void log_config::initialize_appenders() {
      std::lock_guard g( log_config::get().log_mutex );
      for( auto& iter : log_config::get().appender_map )
         iter.second->initialize();
   }

   void configure_logging( const std::filesystem::path& lc ) {
      configure_logging( fc::json::from_file<logging_config>(lc) );
   }
   bool configure_logging( const logging_config& cfg ) {
      return log_config::configure_logging( cfg );
   }

   bool log_config::configure_logging( const logging_config& cfg ) {
      try {
      static bool reg_console_appender = log_config::register_appender<console_appender>( "console" );
      static bool reg_gelf_appender = log_config::register_appender<gelf_appender>( "gelf" );
      static bool reg_dmlog_appender = log_config::register_appender<dmlog_appender>( "dmlog" );

      std::lock_guard g( log_config::get().log_mutex );
      log_config::get().logger_map.clear();
      log_config::get().appender_map.clear();

      logger::default_logger() = log_config::get().logger_map[DEFAULT_LOGGER];
      logger& default_logger = logger::default_logger();

      for( size_t i = 0; i < cfg.appenders.size(); ++i ) {
         // create appender
         auto fact_itr = log_config::get().appender_factory_map.find( cfg.appenders[i].type );
         if( fact_itr == log_config::get().appender_factory_map.end() ) {
            //wlog( "Unknown appender type '%s'", type.c_str() );
            continue;
         }
         auto ap = fact_itr->second->create( cfg.appenders[i].args );
         log_config::get().appender_map[cfg.appenders[i].name] = ap;
      }
      for (bool first_pass = true; ; first_pass = false) { // process default first
         for( size_t i = 0; i < cfg.loggers.size(); ++i ) {
            auto lgr = log_config::get().logger_map[cfg.loggers[i].name];
            if (first_pass && cfg.loggers[i].name != DEFAULT_LOGGER)
               continue;
            if (!first_pass && cfg.loggers[i].name == DEFAULT_LOGGER)
               continue;

            lgr.set_name(cfg.loggers[i].name);
            if (lgr.get_name() != DEFAULT_LOGGER) {
               lgr.set_parent(default_logger);
            }
            if( cfg.loggers[i].enabled ) {
               lgr.set_enabled( *cfg.loggers[i].enabled );
            } else {
               lgr.set_enabled( default_logger.is_enabled() );
            }
            if( cfg.loggers[i].level ) {
               lgr.set_log_level( *cfg.loggers[i].level );
            } else {
               lgr.set_log_level( default_logger.get_log_level() );
            }

            for( auto a = cfg.loggers[i].appenders.begin(); a != cfg.loggers[i].appenders.end(); ++a ){
               auto ap_it = log_config::get().appender_map.find(*a);
               if( ap_it != log_config::get().appender_map.end() ) {
                  lgr.add_appender( ap_it->second );
               }
            }
         }
         if (!first_pass)
            break;
      }
      return reg_console_appender || reg_gelf_appender || reg_dmlog_appender;
      } catch ( exception& e )
      {
         std::cerr<<e.to_detail_string()<<"\n";
      }
      return false;
   }

   logging_config logging_config::default_config() {
      //slog( "default cfg" );
      logging_config cfg;

     variants  c;
               c.push_back(  mutable_variant_object( "level","debug")("color", "green") );
               c.push_back(  mutable_variant_object( "level","warn")("color", "brown") );
               c.push_back(  mutable_variant_object( "level","error")("color", "red") );

      cfg.appenders.push_back(
             appender_config( "stderr", "console",
                 mutable_variant_object()
                     ( "stream","std_error")
                     ( "level_colors", c )
                 ) );
      cfg.appenders.push_back(
             appender_config( "stdout", "console",
                 mutable_variant_object()
                     ( "stream","std_out")
                     ( "level_colors", c )
                 ) );

      logger_config dlc;
      dlc.name = DEFAULT_LOGGER;
      dlc.level = log_level::info;
      dlc.appenders.push_back("stderr");
      cfg.loggers.push_back( dlc );
      return cfg;
   }

   static thread_local std::string thread_name;

   void set_thread_name( const std::string& name ) {
      thread_name = name;
#if defined(__linux__) || defined(__FreeBSD__)
      pthread_setname_np( pthread_self(), name.c_str() );
#elif defined(__APPLE__)
      pthread_setname_np( name.c_str() );
#endif
   }
   const std::string& get_thread_name() {
      if(thread_name.empty()) {
         try {
            thread_name = boost::dll::program_location().filename().generic_string();
         } catch (...) {
            thread_name = "unknown";
         }
      }
      return thread_name;
   }
}
