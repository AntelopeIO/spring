#pragma once
#include <eosio/chain/transaction.hpp>
#include <eosio/chain/types.hpp>
#include <boost/asio/io_context.hpp>
#include <future>

namespace boost { namespace asio {
   class thread_pool;
}}

namespace eosio { namespace chain {

class transaction_metadata;
using transaction_metadata_ptr = std::shared_ptr<transaction_metadata>;
using recover_keys_future = std::future<transaction_metadata_ptr>;

/**
 *  This data structure should store context-free cached data about a transaction such as
 *  packed/unpacked/compressed and recovered keys
 */
class transaction_metadata {
   public:
      enum class trx_type {
         input,
         implicit,
         scheduled,
         dry_run,
         read_only
      };

   private:
      const packed_transaction_ptr                               _packed_trx;
      const fc::microseconds                                     _sig_cpu_usage;
      const flat_set<public_key_type>                            _recovered_pub_keys;
      const trx_type                                             _trx_type;

   public:
      uint32_t                                                   elapsed_time_us = 0;    // not thread safe
      uint32_t                                                   billed_cpu_time_us = 0; // not thread safe

   private:
      struct private_type{};

      static void check_variable_sig_size(const packed_transaction_ptr& trx, uint32_t max) {
         for(const signature_type& sig : trx->get_signed_transaction().signatures)
            EOS_ASSERT(sig.variable_size() <= max, sig_variable_size_limit_exception,
                  "signature variable length component size (${s}) greater than subjective maximum (${m})", ("s", sig.variable_size())("m", max));
      }

   public:
      // creation of tranaction_metadata restricted to start_recover_keys and create_no_recover_keys below, public for make_shared
      explicit transaction_metadata( const private_type& pt, packed_transaction_ptr ptrx,
                                     fc::microseconds sig_cpu_usage, flat_set<public_key_type> recovered_pub_keys,
                                     trx_type t = trx_type::input )
         : _packed_trx( std::move( ptrx ) )
         , _sig_cpu_usage( sig_cpu_usage )
         , _recovered_pub_keys( std::move( recovered_pub_keys ) )
         , _trx_type( t ) {
      }

      transaction_metadata() = delete;
      transaction_metadata(const transaction_metadata&) = delete;
      transaction_metadata(transaction_metadata&&) = delete;
      transaction_metadata operator=(transaction_metadata&) = delete;
      transaction_metadata operator=(transaction_metadata&&) = delete;


      const packed_transaction_ptr& packed_trx()const { return _packed_trx; }
      const transaction_id_type& id()const { return _packed_trx->id(); }
      fc::microseconds signature_cpu_usage()const { return _sig_cpu_usage; }
      const flat_set<public_key_type>& recovered_keys()const { return _recovered_pub_keys; }
      size_t get_estimated_size() const;
      trx_type get_trx_type() const { return _trx_type; };
      bool implicit() const { return _trx_type == trx_type::implicit; };
      bool scheduled() const { return _trx_type == trx_type::scheduled; };
      bool is_dry_run() const { return _trx_type == trx_type::dry_run; };
      bool is_read_only() const { return _trx_type == trx_type::read_only; };
      bool is_transient() const { return _trx_type == trx_type::read_only || _trx_type == trx_type::dry_run; };

      /// Thread safe.
      /// @returns transaction_metadata_ptr or exception via future
      static recover_keys_future
      start_recover_keys( packed_transaction_ptr trx, boost::asio::io_context& thread_pool,
                          const chain_id_type& chain_id, fc::microseconds time_limit,
                          trx_type t, uint32_t max_variable_sig_size = UINT32_MAX );
      /// Thread safe.
      /// @returns transaction_metadata_ptr or throws
      static transaction_metadata_ptr
      recover_keys( packed_transaction_ptr trx,
                    const chain_id_type& chain_id, fc::microseconds time_limit,
                    trx_type t, uint32_t max_variable_sig_size = UINT32_MAX );

      /// @returns constructed transaction_metadata with no key recovery (sig_cpu_usage=0, recovered_pub_keys=empty)
      static transaction_metadata_ptr
      create_no_recover_keys( packed_transaction_ptr trx, trx_type t ) {
         return std::make_shared<transaction_metadata>( private_type(), std::move(trx),
               fc::microseconds(), flat_set<public_key_type>(), t );
      }

};

} } // eosio::chain
