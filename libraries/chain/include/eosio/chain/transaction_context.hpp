#pragma once
#include <eosio/chain/controller.hpp>
#include <eosio/chain/trace.hpp>
#include <eosio/chain/platform_timer.hpp>

namespace eosio::benchmark {
   struct interface_in_benchmark; // for benchmark testing
}

namespace eosio::chain {

   struct transaction_checktime_timer {
      public:
         transaction_checktime_timer() = delete;
         transaction_checktime_timer(const transaction_checktime_timer&) = delete;
         transaction_checktime_timer(transaction_checktime_timer&&) = default;
         transaction_checktime_timer(platform_timer& timer);
         ~transaction_checktime_timer();

         void start(fc::time_point tp);
         void stop();

         platform_timer::state_t timer_state() const { return _timer.timer_state(); }

         /* Sets a callback for when timer expires. Be aware this could might fire from a signal handling context and/or
            on any particular thread. Only a single callback can be registered at once; trying to register more will
            result in an exception. Use nullptr to disable a previously set callback. */
         void set_expiration_callback(void(*func)(void*), void* user);

      private:
         platform_timer& _timer;

         friend controller_impl;
   };

   struct action_digests_t {
      enum class store_which_t { legacy, savanna, both };

      std::optional<digests_t> digests_l; // legacy
      std::optional<digests_t> digests_s; // savanna

      explicit action_digests_t(store_which_t sw) {
         if (sw == store_which_t::legacy || sw == store_which_t::both)
            digests_l = digests_t{};
         if (sw == store_which_t::savanna || sw == store_which_t::both)
            digests_s = digests_t{};
      }

      void append(action_digests_t&& o) {
         if (digests_l)
            fc::move_append(*digests_l, std::move(*o.digests_l));
         if (digests_s)
            fc::move_append(*digests_s, std::move(*o.digests_s));
      }

      void compute_and_append_digests_from(action_trace& trace) {
         if (digests_l)
            digests_l->emplace_back(trace.digest_legacy());
         if (digests_s)
            digests_s->emplace_back(trace.digest_savanna());
      }

      store_which_t store_which() const {
         if (digests_l && digests_s)
            return store_which_t::both;
         if (digests_l)
            return store_which_t::legacy;
         assert(digests_s);
         return store_which_t::savanna;
      }

      std::pair<size_t, size_t> size() const {
         return { digests_l ? digests_l->size() : 0, digests_s ? digests_s->size() : 0 };
      }

      void resize(std::pair<size_t, size_t> sz) {
         if (digests_l) digests_l->resize(sz.first);
         if (digests_s) digests_s->resize(sz.second);
      }
   };

   // transaction side affects to apply to block when block is assembled
   struct trx_block_context {
      std::optional<block_num_type>       proposed_schedule_block_num;
      producer_authority_schedule         proposed_schedule;

      std::optional<block_num_type>       proposed_fin_pol_block_num;
      finalizer_policy                    proposed_fin_pol;

      void apply(trx_block_context&& rhs) {
         if (rhs.proposed_schedule_block_num) {
            proposed_schedule_block_num = rhs.proposed_schedule_block_num;
            proposed_schedule = std::move(rhs.proposed_schedule);
         }
         if (rhs.proposed_fin_pol_block_num) {
            proposed_fin_pol_block_num = rhs.proposed_fin_pol_block_num;
            proposed_fin_pol = std::move(rhs.proposed_fin_pol);
         }
      }
   };

   class transaction_context {
      private:
         // construction/reset initialization
         void initialize();
         void reset();
         // common init called by init_for_* methods below
         void init( uint64_t initial_net_usage);

      public:

         transaction_context( controller& c,
                              const packed_transaction& t,
                              const transaction_id_type& trx_id, // trx_id diff than t.id() before replace_deferred
                              transaction_checktime_timer&& timer,
                              action_digests_t::store_which_t sad,
                              fc::time_point start,
                              transaction_metadata::trx_type type);
         ~transaction_context();

         void init_for_implicit_trx();

         void init_for_input_trx( uint64_t packed_trx_unprunable_size,
                                  uint64_t packed_trx_prunable_size );

         void init_for_deferred_trx( fc::time_point published );

         void exec();
         void finalize();
         void squash();
         void undo();

         inline void add_net_usage( uint64_t u ) { trace->net_usage += u; check_net_usage(); }

         void check_net_usage()const;

         void checktime()const;

         template <typename DigestType>
         inline DigestType hash_with_checktime( const char* data, uint32_t datalen )const {
            const size_t bs = eosio::chain::config::hashing_checktime_block_size;
            typename DigestType::encoder enc;
            while ( datalen > bs ) {
               enc.write( data, bs );
               data    += bs;
               datalen -= bs;
               checktime();
            }
            enc.write( data, datalen );
            return enc.result();
         }

         void pause_billing_timer();
         void resume_billing_timer(fc::time_point resume_from = fc::time_point{});

         uint32_t update_billed_cpu_time( fc::time_point now );

         std::tuple<int64_t, int64_t, bool, bool> max_bandwidth_billed_accounts_can_pay( bool force_elastic_limits = false )const;

         void validate_referenced_accounts( const transaction& trx, bool enforce_actor_whitelist_blacklist )const;

         bool is_dry_run()const { return trx_type == transaction_metadata::trx_type::dry_run; };
         bool is_read_only()const { return trx_type == transaction_metadata::trx_type::read_only; };
         bool is_transient()const { return trx_type == transaction_metadata::trx_type::read_only || trx_type == transaction_metadata::trx_type::dry_run; };
         bool is_implicit()const { return trx_type == transaction_metadata::trx_type::implicit; };
         bool is_scheduled()const { return trx_type == transaction_metadata::trx_type::scheduled; };
         bool has_undo()const;

         int64_t set_proposed_producers(vector<producer_authority> producers);
         void    set_proposed_finalizers(finalizer_policy&& fin_pol);

      private:

         friend struct controller_impl;
         friend class apply_context;
         friend struct benchmark::interface_in_benchmark; // defined in benchmark/bls.cpp

         void add_ram_usage( account_name account, int64_t ram_delta );

         action_trace& get_action_trace( uint32_t action_ordinal );
         const action_trace& get_action_trace( uint32_t action_ordinal )const;

         /** invalidates any action_trace references returned by get_action_trace */
         uint32_t schedule_action( const action& act, account_name receiver, bool context_free,
                                   uint32_t creator_action_ordinal, uint32_t closest_unnotified_ancestor_action_ordinal );

         /** invalidates any action_trace references returned by get_action_trace */
         uint32_t schedule_action( action&& act, account_name receiver, bool context_free,
                                   uint32_t creator_action_ordinal, uint32_t closest_unnotified_ancestor_action_ordinal );

         /** invalidates any action_trace references returned by get_action_trace */
         uint32_t schedule_action( uint32_t action_ordinal, account_name receiver, bool context_free,
                                   uint32_t creator_action_ordinal, uint32_t closest_unnotified_ancestor_action_ordinal );

         void execute_action( uint32_t action_ordinal, uint32_t recurse_depth );

         void schedule_transaction();
         void record_transaction( const transaction_id_type& id, fc::time_point_sec expire );

         void validate_cpu_usage_to_bill( int64_t billed_us, int64_t account_cpu_limit, bool check_minimum, int64_t subjective_billed_us )const;
         void validate_account_cpu_usage( int64_t billed_us, int64_t account_cpu_limit,  int64_t subjective_billed_us )const;
         void validate_account_cpu_usage_estimate( int64_t billed_us, int64_t account_cpu_limit, int64_t subjective_billed_us )const;

         void disallow_transaction_extensions( const char* error_msg )const;

         std::string get_tx_cpu_usage_exceeded_reason_msg(fc::microseconds& limit) const;

      /// Fields:
      public:

         controller&                                 control;
         const packed_transaction&                   packed_trx;
         const transaction_id_type&                  id;
         std::optional<chainbase::database::session> undo_session;
         transaction_trace_ptr                       trace;
         fc::time_point                              start;

         fc::time_point                published;

         action_digests_t              executed_action_receipts;
         flat_set<account_name>        bill_to_accounts;
         flat_set<account_name>        validate_ram_usage;

         /// the maximum number of virtual CPU instructions of the transaction that can be safely billed to the billable accounts
         uint64_t                      initial_max_billable_cpu = 0;

         fc::microseconds              delay;
         bool                          is_input           = false;
         bool                          apply_context_free = true;
         bool                          enforce_whiteblacklist = true;

         fc::time_point                block_deadline = fc::time_point::maximum();
         fc::microseconds              leeway = fc::microseconds( config::default_subjective_cpu_leeway_us );
         int64_t                       billed_cpu_time_us = 0;
         int64_t                       subjective_cpu_bill_us = 0;
         bool                          explicit_billed_cpu_time = false;

         transaction_checktime_timer   transaction_timer;

   private:
         bool                          is_initialized = false;
         transaction_metadata::trx_type trx_type;

         uint64_t                      net_limit = 0;
         bool                          net_limit_due_to_block = true;
         bool                          net_limit_due_to_greylist = false;
         uint64_t                      eager_net_limit = 0;
         uint64_t                      init_net_usage = 0;

         bool                          cpu_limit_due_to_greylist = false;

         fc::microseconds              max_transaction_time_subjective;
         fc::time_point                paused_time;
         fc::microseconds              initial_objective_duration_limit;
         fc::microseconds              objective_duration_limit;
         fc::time_point                _deadline = fc::time_point::maximum(); // calculated deadline
         int64_t                       deadline_exception_code = block_cpu_usage_exceeded::code_value;
         int64_t                       billing_timer_exception_code = block_cpu_usage_exceeded::code_value;
         fc::time_point                pseudo_start;
         fc::microseconds              billed_time;
         trx_block_context             trx_blk_context;

         enum class tx_cpu_usage_exceeded_reason {
            account_cpu_limit, // includes subjective billing
            on_chain_consensus_max_transaction_cpu_usage,
            user_specified_trx_max_cpu_usage_ms,
            node_configured_max_transaction_time,
            speculative_executed_adjusted_max_transaction_time // prev_billed_cpu_time_us > 0
         };
         tx_cpu_usage_exceeded_reason  tx_cpu_usage_reason = tx_cpu_usage_exceeded_reason::account_cpu_limit;
   };

} // namespace eosio::chain
