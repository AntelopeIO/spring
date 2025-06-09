#include <fc/log/console_appender.hpp>
#include <fc/log/log_message.hpp>
#include <fc/string.hpp>
#include <fc/variant.hpp>
#include <fc/reflect/variant.hpp>
#ifndef WIN32
#include <unistd.h>
#endif
#define COLOR_CONSOLE 1
#include "console_defines.h"
#include <fc/exception/exception.hpp>
#include <iomanip>
#include <mutex>
#include <sstream>


namespace fc {

   class console_appender::impl {
   public:
     config                      cfg;
     color::type                 lc[log_level::off+1];
     bool                        use_syslog_header{getenv("JOURNAL_STREAM") != nullptr};
#ifdef WIN32
     HANDLE                      console_handle;
#endif
   };

   console_appender::console_appender( const variant& args )
   :my(new impl)
   {
      configure( args.as<config>() );
   }

   console_appender::console_appender( const config& cfg )
   :my(new impl)
   {
      configure( cfg );
   }
   console_appender::console_appender()
   :my(new impl){}


   void console_appender::configure( const config& console_appender_config )
   { try {
#ifdef WIN32
      my->console_handle = INVALID_HANDLE_VALUE;
#endif
      my->cfg = console_appender_config;
#ifdef WIN32
         if (my->cfg.stream == stream::std_error)
            my->console_handle = GetStdHandle(STD_ERROR_HANDLE);
         else if (my->cfg.stream == stream::std_out)
            my->console_handle = GetStdHandle(STD_OUTPUT_HANDLE);
#endif

         for( int i = 0; i < log_level::off+1; ++i )
            my->lc[i] = color::console_default;
         for( auto itr = my->cfg.level_colors.begin(); itr != my->cfg.level_colors.end(); ++itr )
            my->lc[itr->level] = itr->color;
   } FC_CAPTURE_AND_RETHROW( (console_appender_config) ) }

   console_appender::~console_appender() {}

   #ifdef WIN32
   static WORD
   #else
   static const char*
   #endif
   get_console_color(console_appender::color::type t ) {
      switch( t ) {
         case console_appender::color::red: return CONSOLE_RED;
         case console_appender::color::green: return CONSOLE_GREEN;
         case console_appender::color::brown: return CONSOLE_BROWN;
         case console_appender::color::blue: return CONSOLE_BLUE;
         case console_appender::color::magenta: return CONSOLE_MAGENTA;
         case console_appender::color::cyan: return CONSOLE_CYAN;
         case console_appender::color::white: return CONSOLE_WHITE;
         case console_appender::color::console_default:
         default:
            return CONSOLE_DEFAULT;
      }
   }

   void append_fixed_size( std::string& line, size_t s, std::string_view str ) {
      line += str.substr(0, std::min(s, str.size()));
      if (s > str.size())
         line.append(s - str.size(), ' ');
   }

   void console_appender::log( const log_message& m ) {
      const log_context context = m.get_context();
      std::string file_line = context.get_file().substr( 0, 22 );
      file_line += ':';
      append_fixed_size(file_line, 6, std::to_string( context.get_line_number() ));

      std::string line;
      line.reserve( 384 ); // Received block line is typically > 300 characters
      if(my->use_syslog_header) {
         switch(m.get_context().get_log_level()) {
            case log_level::error:
               line += "<3>";
               break;
            case log_level::warn:
               line += "<4>";
               break;
            case log_level::info:
               line += "<6>";
               break;
            case log_level::debug:
               line += "<7>";
               break;
         }
      }
      append_fixed_size(line, 5, context.get_log_level().to_string() ); line += ' ';
      // use timestamp of when log message created, note this could cause times on log entries to not be consecutive
      line += context.get_timestamp().to_iso_string(); line += ' ';
      append_fixed_size(line, 9, context.get_thread_name() ); line += ' ';
      append_fixed_size(line, 29, file_line ); line += ' ';

      std::string method = context.get_method();
      std::string_view me = method;
      // strip all leading scopes...
      if( !me.empty() ) {
         auto c = me.find_last_of( ':' );
         std::string::size_type p = c != std::string::npos ? ++c : 0;
         append_fixed_size(line, 20, me.substr( p, 20 ) ); line += ' ';
      }
      line += "] ";
      line += fc::format_string( m.get_format(), m.get_data() );

      print( line, my->lc[context.get_log_level()] );
   }

   void console_appender::print( const std::string& text, color::type text_color )
   {
      FILE* out = my->cfg.stream == stream::std_error ? stderr : stdout;

      #ifdef WIN32
         if (my->console_handle != INVALID_HANDLE_VALUE)
           SetConsoleTextAttribute(my->console_handle, get_console_color(text_color));
      #else
         if(isatty(fileno(out))) fprintf( out, "%s", get_console_color( text_color ) );
      #endif

      if( text.size() )
         fprintf( out, "%s", text.c_str() );

      #ifdef WIN32
      if (my->console_handle != INVALID_HANDLE_VALUE)
        SetConsoleTextAttribute(my->console_handle, CONSOLE_DEFAULT);
      #else
      if(isatty(fileno(out))) fprintf( out, "%s", CONSOLE_DEFAULT );
      #endif

      fprintf( out, "\n" );

      if( my->cfg.flush ) fflush( out );
   }

}
