#pragma once
#include <eosio/chain/host_context.hpp>

namespace chainbase { class database; }

namespace eosio { namespace chain {

class controller;
class account_metadata_object;

class apply_context : public host_context {
   /// Constructor
   public:
      apply_context(controller& con, transaction_context& trx_ctx, uint32_t action_ordinal, uint32_t depth=0);

   /// Execution methods:
   public:

      void exec_one();
      void exec();
      void execute_inline( action&& a ) override;
      void execute_context_free_inline( action&& a ) override;
      void schedule_deferred_transaction( const uint128_t& sender_id, account_name payer, transaction&& trx, bool replace_existing ) override;
      bool cancel_deferred_transaction( const uint128_t& sender_id, account_name sender );
      bool cancel_deferred_transaction( const uint128_t& sender_id ) override { return cancel_deferred_transaction(sender_id, receiver); }

      // Not callable from apply_context (actions)
      uint32_t get_call_data(std::span<char> memory) const override {
         EOS_ASSERT(false, sync_call_validate_exception, "get_call_data can be only used in sync call");
      };
      void set_call_return_value(std::span<const char> return_value) override {
         EOS_ASSERT(false, sync_call_validate_exception, "set_call_return_value can be only used in sync call");
      };

   protected:
      uint32_t schedule_action( uint32_t ordinal_of_action_to_schedule, account_name receiver, bool context_free );
      uint32_t schedule_action( action&& act_to_schedule, account_name receiver, bool context_free );


   /// Authorization methods:
   public:

      /**
       * @brief Require @ref account to have approved of this message
       * @param account The account whose approval is required
       *
       * This method will check that @ref account is listed in the message's declared authorizations, and marks the
       * authorization as used. Note that all authorizations on a message must be used, or the message is invalid.
       *
       * @throws missing_auth_exception If no sufficient permission was found
       */
      void require_authorization(const account_name& account) override;
      bool has_authorization(const account_name& account) const override;
      void require_authorization(const account_name& account, const permission_name& permission) override;

      /**
       * Requires that the current action be delivered to account
       */
      void require_recipient(account_name account) override;

      /**
       * Return true if the current action has already been scheduled to be
       * delivered to the specified account.
       */
      bool has_recipient(account_name account)const override;

   /// Console methods:
   public:

      void console_append( std::string_view val ) override {
         _pending_console_output += val;
      }

      void update_db_usage( const account_name& payer, int64_t delta ) override;

   /// Misc methods:
   public:
      int get_action( uint32_t type, uint32_t index, char* buffer, size_t buffer_size )const override;
      int get_context_free_data( uint32_t index, char* buffer, size_t buffer_size )const override;

      uint64_t next_global_sequence();
      uint64_t next_recv_sequence( const account_metadata_object& receiver_account );
      uint64_t next_auth_sequence( account_name actor );

      void add_ram_usage( account_name account, int64_t ram_delta );
      void finalize_trace( action_trace& trace, const fc::time_point& start );

      bool is_context_free()const override { return context_free; }
      bool is_privileged()const override { return privileged; }
      const action& get_action()const override { return *act; }
      const action* get_action_ptr()const { return act; }

      action_name get_sender() const override;

      bool is_applying_block() const { return trx_context.explicit_billed_cpu_time; }
      bool is_eos_vm_oc_whitelisted() const;
      bool should_use_eos_vm_oc()const;

   private:
      const action*                 act = nullptr; ///< action being applied
      // act pointer may be invalidated on call to trx_context.schedule_action
      uint32_t                      first_receiver_action_ordinal = 0;
      uint32_t                      action_ordinal = 0;
      bool                          privileged   = false;
      bool                          context_free = false;

   private:
      vector< std::pair<account_name, uint32_t> > _notified; ///< keeps track of new accounts to be notifed of current message
      vector<uint32_t>                    _inline_actions; ///< action_ordinals of queued inline actions
      vector<uint32_t>                    _cfa_inline_actions; ///< action_ordinals of queued inline context-free actions
      std::string                         _pending_console_output;
      flat_set<account_delta>             _account_ram_deltas; ///< flat_set of account_delta so json is an array of objects

      //bytes                               _cached_trx;
};

using apply_handler = std::function<void(apply_context&)>;

} } // namespace eosio::chain

//FC_REFLECT(eosio::chain::apply_context::apply_results, (applied_actions)(deferred_transaction_requests)(deferred_transactions_count))
