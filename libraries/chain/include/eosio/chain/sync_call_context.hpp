#pragma once

#include <eosio/chain/host_context.hpp>

namespace eosio { namespace chain {

enum class sync_call_flags {
   read_only                               = 1ull<<0,
   no_op_if_receiver_not_support_sync_call = 1ull<<1,
   last                                    = no_op_if_receiver_not_support_sync_call
};

class sync_call_context : public host_context {
public:
   sync_call_context(controller& con, transaction_context& trx_ctx, account_name sender, account_name receiver, uint32_t sync_call_depth, uint64_t flags, std::span<const char> data);

   uint32_t get_call_data(std::span<char> memory) const override;
   void set_call_return_value(std::span<const char> return_value) override;

   bool is_read_only()const;
   bool no_op_if_receiver_not_support_sync_call()const;
   action_name get_sender() const override;

   account_name           sender{};
   uint64_t               flags = 0;
   std::span<const char>  data{}; // includes function name, arguments, and other information
   std::vector<char>      return_value{};

   // Those cannot be called from sync_call_context. EOS_ASSERTs and tests will be added
   // in next PR.
   void require_authorization(const account_name& account) override;
   bool has_authorization(const account_name& account) const override;
   void require_authorization(const account_name& account, const permission_name& permission) override;
   void require_recipient(account_name account) override;
   bool has_recipient(account_name account)const override;
   void update_db_usage( const account_name& payer, int64_t delta ) override;
   int get_action( uint32_t type, uint32_t index, char* buffer, size_t buffer_size)const override;
   int get_context_free_data( uint32_t index, char* buffer, size_t buffer_size )const override;
   bool is_context_free()const override;
   bool is_privileged()const override;
   const action& get_action()const override;
   void execute_inline( action&& a ) override;
   void execute_context_free_inline( action&& a ) override;
   void schedule_deferred_transaction( const uint128_t& sender_id, account_name payer, transaction&& trx, bool replace_existing ) override;
   bool cancel_deferred_transaction( const uint128_t& sender_id ) override;
};

} } /// namespace eosio::chain
