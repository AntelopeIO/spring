#pragma once
#include <fc/log/appender.hpp>
#include <fc/log/logger.hpp>
#include <vector>

namespace fc
{
   class console_appender final : public appender
   {
       public:
            struct color
            {
                enum type {
                   red,
                   green,
                   brown,
                   blue,
                   magenta,
                   cyan,
                   white,
                   console_default,
                };
            };

            struct stream { enum type { std_out, std_error }; };

            struct level_color
            {
               level_color( log_level l=log_level::all,
                            color::type c=color::console_default )
               :level(l),color(c){}

               log_level                         level;
               console_appender::color::type     color;
            };

            struct config
            {
               config()
               :stream(console_appender::stream::std_error),flush(true){}

               console_appender::stream::type     stream;
               std::vector<level_color>           level_colors;
               bool                               flush;
            };


            explicit console_appender( const variant& args );
            explicit console_appender( const config& cfg );
            console_appender();

            ~console_appender();
            void initialize() override {}
            virtual void log( const log_message& m ) override;

            void print( const std::string& text_to_print,
                        color::type text_color = color::console_default );

            void configure( const config& cfg );

       private:
            class impl;
            std::unique_ptr<impl> my;
   };
} // namespace fc

#include <fc/reflect/reflect.hpp>
FC_REFLECT_ENUM( fc::console_appender::stream::type, (std_out)(std_error) )
FC_REFLECT_ENUM( fc::console_appender::color::type, (red)(green)(brown)(blue)(magenta)(cyan)(white)(console_default) )
FC_REFLECT( fc::console_appender::level_color, (level)(color) )
FC_REFLECT( fc::console_appender::config, (stream)(level_colors)(flush) )
