#pragma once

#include <eosio/chain/application.hpp>
#include <fc/log/logger.hpp>
#include <string>

namespace eosio {

   const std::string  logger_name("net_plugin_impl");
   inline fc::logger  logger;
   inline std::string peer_log_format;

   template <typename Strand>
   void verify_strand_in_this_thread(const Strand& strand, const char* func, int line) {
      if( !strand.running_in_this_thread() ) {
         fc_elog( logger, "wrong strand: ${f} : line ${n}, exiting", ("f", func)("n", line) );
         appbase::app().quit();
      }
   }

   // peer_[x]log must be called from thread in connection strand
#define peer_dlog( PEER, FORMAT, ... ) \
  FC_MULTILINE_MACRO_BEGIN \
   if( logger.is_enabled( fc::log_level::debug ) ) { \
      verify_strand_in_this_thread( PEER->strand, __func__, __LINE__ ); \
      logger.log( FC_LOG_MESSAGE( debug, peer_log_format + FORMAT, __VA_ARGS__ (PEER->get_logger_variant()) ) ); \
   } \
  FC_MULTILINE_MACRO_END

#define peer_ilog( PEER, FORMAT, ... ) \
  FC_MULTILINE_MACRO_BEGIN \
   if( logger.is_enabled( fc::log_level::info ) ) { \
      verify_strand_in_this_thread( PEER->strand, __func__, __LINE__ ); \
      logger.log( FC_LOG_MESSAGE( info, peer_log_format + FORMAT, __VA_ARGS__ (PEER->get_logger_variant()) ) ); \
   } \
  FC_MULTILINE_MACRO_END

#define peer_wlog( PEER, FORMAT, ... ) \
  FC_MULTILINE_MACRO_BEGIN \
   if( logger.is_enabled( fc::log_level::warn ) ) { \
      verify_strand_in_this_thread( PEER->strand, __func__, __LINE__ ); \
      logger.log( FC_LOG_MESSAGE( warn, peer_log_format + FORMAT, __VA_ARGS__ (PEER->get_logger_variant()) ) ); \
   } \
  FC_MULTILINE_MACRO_END

#define peer_elog( PEER, FORMAT, ... ) \
  FC_MULTILINE_MACRO_BEGIN \
   if( logger.is_enabled( fc::log_level::error ) ) { \
      verify_strand_in_this_thread( PEER->strand, __func__, __LINE__ ); \
      logger.log( FC_LOG_MESSAGE( error, peer_log_format + FORMAT, __VA_ARGS__ (PEER->get_logger_variant()) ) ); \
   } \
  FC_MULTILINE_MACRO_END

} // namespace eosio