#pragma once

#include <eosio/net_plugin/protocol.hpp>
#include <eosio/net_plugin/gossip_bps_index.hpp>
#include <eosio/net_plugin/net_logger.hpp>
#include <fc/io/raw.hpp>

#include <memory>
#include <vector>

namespace eosio {

   using send_buffer_type = std::unique_ptr<std::vector<char>>;

   struct buffer_factory {

      /// caches result for subsequent calls, only provide same net_message instance for each invocation
      const send_buffer_type& get_send_buffer( const net_message& m ) {
         if( !send_buffer ) {
            send_buffer = create_send_buffer( m );
         }
         return send_buffer;
      }

   protected:
      send_buffer_type send_buffer;

   protected:
      static send_buffer_type create_send_buffer( const net_message& m ) {
         const uint32_t payload_size = fc::raw::pack_size( m );

         const char* const header = reinterpret_cast<const char* const>(&payload_size); // avoid variable size encoding of uint32_t
         const size_t buffer_size = message_header_size + payload_size;

         auto send_buffer = std::make_unique<vector<char>>(buffer_size);
         fc::datastream<char*> ds( send_buffer->data(), buffer_size);
         ds.write( header, message_header_size );
         fc::raw::pack( ds, m );

         return send_buffer;
      }

      template< typename T>
      static send_buffer_type create_send_buffer( uint32_t which, const T& v ) {
         // match net_message static_variant pack
         const uint32_t which_size = fc::raw::pack_size( unsigned_int( which ) );
         const uint32_t payload_size = which_size + fc::raw::pack_size( v );

         const char* const header = reinterpret_cast<const char* const>(&payload_size); // avoid variable size encoding of uint32_t
         const size_t buffer_size = message_header_size + payload_size;

         auto send_buffer = std::make_unique<vector<char>>( buffer_size );
         fc::datastream<char*> ds( send_buffer->data(), buffer_size );
         ds.write( header, message_header_size );
         fc::raw::pack( ds, unsigned_int( which ) );
         fc::raw::pack( ds, v );

         return send_buffer;
      }

      static send_buffer_type create_send_buffer_from_serialized_block( const std::vector<char>& v ) {
         constexpr uint32_t signed_block_which = to_index(msg_type_t::signed_block);

         // match net_message static_variant pack
         const uint32_t which_size = fc::raw::pack_size( unsigned_int( signed_block_which ) );
         const uint32_t payload_size = which_size + v.size();

         const char* const header = reinterpret_cast<const char* const>(&payload_size); // avoid variable size encoding of uint32_t
         const size_t buffer_size = message_header_size + payload_size;

         auto send_buffer = std::make_unique<vector<char>>( buffer_size );
         fc::datastream<char*> ds( send_buffer->data(), buffer_size );
         ds.write( header, message_header_size );
         fc::raw::pack( ds, unsigned_int( signed_block_which ) );
         ds.write( v.data(), v.size() );

         return send_buffer;
      }

   };

   struct block_buffer_factory : public buffer_factory {

      /// caches result for subsequent calls, only provide same signed_block_ptr instance for each invocation.
      const send_buffer_type& get_send_buffer( const signed_block_ptr& sb ) {
         if( !send_buffer ) {
            send_buffer = create_send_buffer( sb );
         }
         return send_buffer;
      }

      const send_buffer_type& get_send_buffer( const std::vector<char>& sb ) {
         if( !send_buffer ) {
            send_buffer = create_send_buffer( sb );
         }
         return send_buffer;
      }

   private:

      static send_buffer_type create_send_buffer( const signed_block_ptr& sb ) {
         constexpr uint32_t signed_block_which = to_index(msg_type_t::signed_block);

         // this implementation is to avoid copy of signed_block to net_message
         // matches which of net_message for signed_block
         fc_dlog( logger, "sending block ${bn}", ("bn", sb->block_num()) );
         return buffer_factory::create_send_buffer( signed_block_which, *sb );
      }

      static send_buffer_type create_send_buffer( const std::vector<char>& ssb ) { // ssb: serialized signed block
         // this implementation is to avoid copy of signed_block to net_message
         // matches which of net_message for signed_block
         return buffer_factory::create_send_buffer_from_serialized_block( ssb );
      }
   };

   struct trx_buffer_factory : public buffer_factory {

      /// caches result for subsequent calls, only provide same packed_transaction_ptr instance for each invocation.
      const send_buffer_type& get_send_buffer( const packed_transaction_ptr& trx ) {
         if( !send_buffer ) {
            send_buffer = create_send_buffer( trx );
         }
         return send_buffer;
      }

   private:

      static send_buffer_type create_send_buffer( const packed_transaction_ptr& trx ) {
         constexpr uint32_t packed_transaction_which = to_index(msg_type_t::packed_transaction);

         // this implementation is to avoid copy of packed_transaction to net_message
         // matches which of net_message for packed_transaction
         return buffer_factory::create_send_buffer( packed_transaction_which, *trx );
      }
   };

   struct gossip_buffer_factory : public buffer_factory {

      /// caches result for subsequent calls
      const send_buffer_type& get_send_buffer(const gossip_bp_index_t& gossip_bp_peers) {
         if( !send_buffer ) {
            send_buffer = create_send_buffer(gossip_bp_peers);
         }
         return send_buffer;
      }

   private:

      static send_buffer_type create_send_buffer(const gossip_bp_index_t& gossip_bp_peers) {
         constexpr uint32_t which = to_index(msg_type_t::gossip_bp_peers_message);

         fc::lock_guard g(gossip_bp_peers.mtx);

         // match net_message static_variant pack
         const uint32_t which_size = fc::raw::pack_size( unsigned_int( which ) );
         // content size
         size_t s = fc::raw::pack_size( unsigned_int((uint32_t)gossip_bp_peers.index.size()) ); // match vector pack
         for (const auto& peer : gossip_bp_peers.index.get<by_producer>()) {
            s += fc::raw::pack_size( peer );
         }
         const uint32_t payload_size = which_size + s;

         const char* const header = reinterpret_cast<const char* const>(&payload_size); // avoid variable size encoding of uint32_t
         const size_t buffer_size = message_header_size + payload_size;

         auto send_buffer = std::make_unique<vector<char>>( buffer_size );
         fc::datastream<char*> ds( send_buffer->data(), buffer_size );
         ds.write( header, message_header_size );
         fc::raw::pack( ds, unsigned_int( which ) );
         fc::raw::pack( ds, unsigned_int((uint32_t)gossip_bp_peers.index.size()) );
         for (const auto& peer : gossip_bp_peers.index.get<by_producer>()) {
            fc::raw::pack( ds, peer );
         }

         return send_buffer;
      }
   };

   struct gossip_buffer_initial_factory : public buffer_factory {

      // called on startup
      void set_initial_send_buffer(const gossip_bp_peers_message::bp_peer& signed_empty) {
         send_buffer = create_initial_send_buffer(signed_empty);
      }

      /// requires set_initial_send_buffer to be called first
      const send_buffer_type& get_initial_send_buffer() {
         assert(send_buffer);
         return send_buffer;
      }

   private:

      static send_buffer_type create_initial_send_buffer(const gossip_bp_peers_message::bp_peer& signed_empty) {
         constexpr uint32_t which = to_index(msg_type_t::gossip_bp_peers_message);

         // match net_message static_variant pack
         const uint32_t which_size = fc::raw::pack_size( unsigned_int( which ) );
         // content size
         size_t s = fc::raw::pack_size( unsigned_int((uint32_t)1) ); // match vector pack
         s += fc::raw::pack_size(signed_empty);
         const uint32_t payload_size = which_size + s;

         const char* const header = reinterpret_cast<const char* const>(&payload_size); // avoid variable size encoding of uint32_t
         const size_t buffer_size = message_header_size + payload_size;

         auto send_buffer = std::make_unique<vector<char>>( buffer_size );
         fc::datastream<char*> ds( send_buffer->data(), buffer_size );
         ds.write( header, message_header_size );
         fc::raw::pack( ds, unsigned_int( which ) );
         fc::raw::pack( ds, unsigned_int((uint32_t)1) );
         fc::raw::pack( ds, signed_empty );

         return send_buffer;
      }
   };

} // namespace eosio
