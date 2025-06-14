#include <fc/exception/exception.hpp>
#include <boost/exception/all.hpp>
#include <fc/log/logger.hpp>
#include <fc/io/json.hpp>

#include <iostream>

namespace fc
{
   namespace detail
   {
      class exception_impl
      {
         public:
            exception_impl(std::string_view name_value,
                        std::string_view what_value,
                        int64_t code,
                        log_messages msgs)
            : _name(name_value)
            , _what(what_value)
            , _code(code)
            , _elog(std::move(msgs))
            {}

            std::string     _name;
            std::string     _what;
            int64_t         _code;
            log_messages    _elog;
      };
   }
   exception::exception( log_messages&& msgs,
                         int64_t code,
                         std::string_view name_value,
                         std::string_view what_value )
   :my( new detail::exception_impl{name_value, what_value, code, std::move(msgs)} )
   {
   }

   exception::exception(
      const log_messages& msgs,
      int64_t code,
      std::string_view name_value,
      std::string_view what_value )
   :my( new detail::exception_impl{name_value, what_value, code, msgs} )
   {
   }

   unhandled_exception::unhandled_exception( log_message&& m, std::exception_ptr e )
   :exception( std::move(m) )
   {
      _inner = e;
   }
   unhandled_exception::unhandled_exception( const exception& r )
   :exception(r)
   {
   }
   unhandled_exception::unhandled_exception( log_messages m )
   :exception()
   { my->_elog = std::move(m); }

   std::exception_ptr unhandled_exception::get_inner_exception()const { return _inner; }

   std::shared_ptr<exception> unhandled_exception::dynamic_copy_exception()const
   {
      auto e = std::make_shared<unhandled_exception>( *this );
      e->_inner = _inner;
      return e;
   }

   exception::exception( int64_t code,
                         std::string_view name_value,
                         std::string_view what_value )
   :my( new detail::exception_impl{name_value, what_value, code, {}} )
   {
   }

   exception::exception( log_message&& msg,
                         int64_t code,
                         std::string_view name_value,
                         std::string_view what_value )
   :my( new detail::exception_impl{name_value, what_value, code, {std::move(msg)}} )
   {
   }
   exception::exception( const exception& c )
   :my( new detail::exception_impl(*c.my) )
   { }
   exception::exception( exception&& c ) noexcept = default;

   const char*  exception::name()const throw() { return my->_name.c_str(); }
   const char*  exception::what()const noexcept { return my->_what.c_str(); }
   int64_t      exception::code()const throw() { return my->_code;         }

   exception::~exception(){}

   void to_variant( const exception& e, variant& v )
   {
      v = mutable_variant_object( "code", e.code() )
                                ( "name", e.name() )
                                ( "message", e.what() )
                                ( "stack", e.get_log() );

   }
   void          from_variant( const variant& v, exception& ll )
   {
      const auto& obj = v.get_object();
      if( obj.contains( "stack" ) )
         ll.my->_elog =  obj["stack"].as<log_messages>();
      if( obj.contains( "code" ) )
         ll.my->_code = obj["code"].as_int64();
      if( obj.contains( "name" ) )
         ll.my->_name = obj["name"].as_string();
      if( obj.contains( "message" ) )
         ll.my->_what = obj["message"].as_string();
   }

   const log_messages&   exception::get_log()const { return my->_elog; }
   void                  exception::append_log( log_message m )
   {
      my->_elog.emplace_back( std::move(m) );
   }

   /**
    *   Generates a detailed string including file, line, method,
    *   and other information that is generally only useful for
    *   developers.
    */
   std::string exception::to_detail_string( log_level ll  )const
   {
      const auto deadline = fc::time_point::now() + format_time_limit;
      std::stringstream ss;
      try {
         try {
            ss << variant( my->_code ).as_string();
         } catch( std::bad_alloc& ) {
            throw;
         } catch( ... ) {
            ss << "<- exception in to_detail_string.";
         }
         ss << " " << my->_name << ": " << my->_what << "\n";
         for( auto itr = my->_elog.begin(); itr != my->_elog.end(); ++itr ) {
            try {
               ss << itr->get_message() << "\n"; //fc::format_string( itr->get_format(), itr->get_data() ) <<"\n";
               ss << "    " << json::to_string( itr->get_data(), deadline ) << "\n";
               ss << "    " << itr->get_context().to_string() << "\n";
            } catch( std::bad_alloc& ) {
               throw;
            } catch( const fc::timeout_exception& e) {
               ss << "<- timeout exception in to_detail_string: " << e.what() << "\n";
               break;
            } catch( ... ) {
               ss << "<- exception in to_detail_string.\n";
            }
         }
      } catch( std::bad_alloc& ) {
         throw;
      } catch( ... ) {
         ss << "<- exception in to_detail_string.\n";
      }
      return ss.str();
   }

   /**
    *   Generates a user-friendly error report.
    */
   std::string exception::to_string( log_level ll   )const
   {
      const auto deadline = fc::time_point::now() + format_time_limit;
      std::stringstream ss;
      try {
         ss << my->_what;
         try {
            ss << " (" << variant( my->_code ).as_string() << ")\n";
         } catch( std::bad_alloc& ) {
            throw;
         } catch( ... ) {
            ss << "<- exception in to_string.\n";
         }
         for( auto itr = my->_elog.begin(); itr != my->_elog.end(); ++itr ) {
            try {
               FC_CHECK_DEADLINE(deadline);
               ss << fc::format_string( itr->get_format(), itr->get_data(), true) << "\n";
               //      ss << "    " << itr->get_context().to_string() <<"\n";
            } catch( std::bad_alloc& ) {
               throw;
            } catch( const fc::timeout_exception& e) {
               ss << "<- timeout exception in to_string: " << e.what();
               break;
            } catch( ... ) {
               ss << "<- exception in to_string.\n";
            }
         }
         return ss.str();
      } catch( std::bad_alloc& ) {
         throw;
      } catch( ... ) {
         ss << "<- exception in to_string.\n";
      }
      return ss.str();
   }

   /**
    *   Generates a user-friendly error report.
    */
   std::string exception::top_message( )const
   {
      for( auto itr = my->_elog.begin(); itr != my->_elog.end(); ++itr )
      {
         auto s = fc::format_string( itr->get_format(), itr->get_data() );
         if (!s.empty()) {
            return s;
         }
      }
      return std::string();
   }

   exception_ptr exception::dynamic_copy_exception()const
   {
       return std::make_shared<exception>(*this);
   }

   std::string except_str()
   {
       return boost::current_exception_diagnostic_information();
   }

   void throw_bad_enum_cast( int64_t i, const char* e )
   {
      FC_THROW_EXCEPTION( bad_cast_exception,
                          "invalid index '${key}' in enum '${enum}'",
                          ("key",i)("enum",e) );
   }
   void throw_bad_enum_cast( const char* k, const char* e )
   {
      FC_THROW_EXCEPTION( bad_cast_exception,
                          "invalid name '${key}' in enum '${enum}'",
                          ("key",k)("enum",e) );
   }

   bool assert_optional(bool is_valid )
   {
      if( !is_valid )
         throw null_optional();
      return true;
   }
   exception& exception::operator=( const exception& copy )
   {
      *my = *copy.my;
      return *this;
   }

   exception& exception::operator=( exception&& copy )
   {
      my = std::move(copy.my);
      return *this;
   }

   void record_assert_trip(
      const char* filename,
      uint32_t lineno,
      const char* expr
      )
   {
      fc::mutable_variant_object assert_trip_info =
         fc::mutable_variant_object()
         ("source_file", filename)
         ("source_lineno", lineno)
         ("expr", expr)
         ;
      /* TODO: restore this later
      std::cout
         << "FC_ASSERT triggered:  "
         << fc::json::to_string( assert_trip_info ) << "\n";
         */
      return;
   }

   bool enable_record_assert_trip = false;

   std_exception_wrapper::std_exception_wrapper( log_message&& m, std::exception_ptr e,
                                                 const std::string& name_value,
                                                 const std::string& what_value)
   :exception( std::move(m), exception_code::std_exception_code, name_value, what_value )
   {
      _inner = {std::move(e)};
   }

   std_exception_wrapper std_exception_wrapper::from_current_exception(const std::exception& e)
   {
     return std_exception_wrapper{FC_LOG_MESSAGE(warn, "rethrow ${what}: ", ("what",e.what())), 
                                  std::current_exception(), 
                                  BOOST_CORE_TYPEID(e).name(), 
                                  e.what()};
   }

   std::exception_ptr std_exception_wrapper::get_inner_exception()const { return _inner; }

   std::shared_ptr<exception> std_exception_wrapper::dynamic_copy_exception()const
   {
      auto e = std::make_shared<std_exception_wrapper>( *this );
      e->_inner = _inner;
      return e;
   }
} // fc
