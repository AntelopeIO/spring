#pragma once

#include <eosio/chain/host_context.hpp>

namespace eosio { namespace chain {

// A bitmap. Only least significant bits can be set. Other bits must be 0.
// When a new flag is added, its enum value must be 1 bit left shift from the last flag.
// Update all_allowed_bits to include the newly added enum value.
enum class sync_call_flags {
   read_only        = 1ull<<0,
   all_allowed_bits = read_only
};

class sync_call_context : public host_context {
public:
   sync_call_context(controller& con, transaction_context& trx_ctx, account_name sender, account_name receiver, bool privileged, uint32_t sync_call_depth, uint64_t flags, std::span<const char> data);

   uint32_t get_call_data(std::span<char> memory) const override;
   void set_call_return_value(std::span<const char> return_value) override;

   bool is_sync_call() const override { return true; }

   bool is_read_only()const;
   action_name get_sender() const override;

   account_name           sender{};
   uint64_t               flags = 0;
   std::span<const char>  data{}; // includes function name, arguments, and other information
   std::vector<char>      return_value{};

   bool has_recipient(account_name account)const override;
   void update_db_usage( const account_name& payer, int64_t delta ) override;
   bool is_context_free()const override;
};

} } /// namespace eosio::chain
