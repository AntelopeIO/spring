#include <fc/log/logger.hpp>
#include <fc/log/log_message.hpp>
#include <fc/log/appender.hpp>
#include <fc/exception/exception.hpp>
#include <fc/filesystem.hpp>
#include <fc/log/logger_config.hpp>
#include <unordered_map>

namespace fc {

    inline static logger the_default_logger;

    class logger::impl {
      public:
         impl()
         :_parent(nullptr),_enabled(true),_level(log_level::warn){}
         std::string      _name;
         logger           _parent;
         bool             _enabled;
         log_level        _level;

         std::vector<appender::ptr> _appenders;
    };


    logger::logger()
    :my( new impl() ){}

    logger::logger(nullptr_t){}

    logger::logger( const std::string& name, const logger& parent )
    :my( new impl() )
    {
       my->_name = name;
       my->_parent = parent;
    }


    logger::logger( const logger& l )
    :my(l.my){}

    logger::logger( logger&& l ) noexcept
    :my(std::move(l.my)){}

    logger::~logger(){}

    logger& logger::operator=( const logger& l ){
       my = l.my;
       return *this;
    }
    logger& logger::operator=( logger&& l ) noexcept {
       fc_swap(my,l.my);
       return *this;
    }
    bool operator==( const logger& l, std::nullptr_t ) { return !l.my; }
    bool operator!=( const logger& l, std::nullptr_t ) { return !!l.my;  }

    void logger::set_enabled( bool e ) {
       my->_enabled = e;
    }
    bool logger::is_enabled()const {
       return my->_enabled;
    }
    bool logger::is_enabled( log_level e )const {
       return my->_enabled && e >= my->_level;
    }

    void logger::log( log_message m ) {
       std::unique_lock g( log_config::get().log_mutex );
       m.get_context().append_context( my->_name );

       if (!my->_appenders.empty()) {
          for( auto itr = my->_appenders.begin(); itr != my->_appenders.end(); ++itr ) {
             try {
                (*itr)->log( m );
             } catch( fc::exception& er ) {
                std::cerr << "ERROR: logger::log fc::exception: " << er.to_detail_string() << std::endl;
             } catch( const std::exception& e ) {
                std::cerr << "ERROR: logger::log std::exception: " << e.what() << std::endl;
             } catch( ... ) {
                std::cerr << "ERROR: logger::log unknown exception: " << std::endl;
             }
          }
       } else if (my->_parent != nullptr) {
          logger parent = my->_parent;
          g.unlock();
          parent.log( m );
       }
    }

    void logger::set_name( const std::string& n ) { my->_name = n; }
    std::string logger::get_name()const { return my->_name; }

    logger logger::get( const std::string& s ) {
       return log_config::get_logger( s );
    }

    logger& logger::default_logger() {
       return the_default_logger;
    }

    void logger::update( const std::string& name, logger& log ) {
       log_config::update_logger( name, log );
    }

    logger  logger::get_parent()const { return my->_parent; }
    logger& logger::set_parent(const logger& p) { my->_parent = p; return *this; }

    log_level logger::get_log_level()const { return my->_level; }
    logger& logger::set_log_level(log_level ll) { my->_level = ll; return *this; }

    void logger::add_appender( const std::shared_ptr<appender>& a ) {
       my->_appenders.push_back(a);
    }

   bool configure_logging( const logging_config& cfg );
   bool do_default_config      = configure_logging( logging_config::default_config() );

} // namespace fc
