#pragma once
#include <fc/log/logger.hpp>
#include <fc/log/appender.hpp>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <filesystem>

namespace fc {
   struct appender_config {
      explicit appender_config(std::string name = {},
                               std::string type = {},
                               variant args = {}) :
        name(std::move(name)),
        type(std::move(type)),
        args(std::move(args))
      {}
      std::string  name;
      std::string  type;
      fc::variant  args;
   };

   struct logger_config {
      explicit logger_config(std::string name = {}):name(std::move(name)){}
      std::string                      name;
      /// if not set, then parents level is used.
      std::optional<log_level>         level;
      /// if not set, then parents enabled is used.
      std::optional<bool>              enabled;
      // if empty, then parents appenders are used.
      std::vector<std::string>         appenders;
   };

   struct logging_config {
      static logging_config default_config();
      std::vector<std::string>     includes;
      std::vector<appender_config> appenders;
      std::vector<logger_config>   loggers;
   };

   struct log_config {

      template<typename T>
      static bool register_appender(const std::string& type) {
         return register_appender( type, std::make_shared<detail::appender_factory_impl<T>>() );
      }

      static bool register_appender( const std::string& type, const appender_factory::ptr& f );

      static logger get_logger( const std::string& name );
      static void update_logger( const std::string& name, logger& log );
      static void update_logger_with_default( const std::string& name, logger& log, const std::string& default_name );
      static void initialize_appenders();

      static bool configure_logging( const logging_config& l );

   private:
      static log_config& get();

      friend class logger;

      std::mutex                                               log_mutex;
      std::unordered_map<std::string, appender_factory::ptr>   appender_factory_map;
      std::unordered_map<std::string, appender::ptr>           appender_map;
      std::unordered_map<std::string, logger>                  logger_map;
   };

   void configure_logging( const std::filesystem::path& log_config );
   bool configure_logging( const logging_config& l );

   void set_thread_name( const std::string& name );
   const std::string& get_thread_name();
}

#include <fc/reflect/reflect.hpp>
FC_REFLECT( fc::appender_config, (name)(type)(args) )
FC_REFLECT( fc::logger_config, (name)(level)(enabled)(appenders) )
FC_REFLECT( fc::logging_config, (includes)(appenders)(loggers) )
