#pragma once

#include <eosio/chain/host_context.hpp>

namespace eosio { namespace chain {

// A bitmap. Only least significant bits can be set. Other bits must be 0.
// When a new flag is added, its enum value must be 1 bit left shift from the last flag.
// Update all_allowed_bits to include the newly added enum value.
enum class sync_call_flags : uint64_t {
   // `force_read_only` is a user's directive to the system, telling the system whether
   // the new call context created must operate in read-only mode or if it is
   // free to operate under its most permissible mode.
   // When the flag is not set, the new call context inherits the
   // `readonlyness` from the calling context; that is, if the calling context
   // is read only, the system will enforce read only in the new call context.
   force_read_only  = 1ull<<0,

   all_allowed_bits = force_read_only
};

class sync_call_context : public host_context {
public:

   sync_call_context(controller& con, transaction_context& trx_ctx, uint32_t ordinal, action_trace& current_action_trace, account_name sender, account_name receiver, bool privileged, uint32_t sync_call_depth, uint64_t flags, std::span<const char> data);

   uint32_t get_call_data(std::span<char> memory) const override;
   void set_call_return_value(std::span<const char> return_value) override;
   action_trace& get_current_action_trace() const override { return current_action_trace; }
   uint32_t get_sync_call_ordinal() override { return ordinal; }

   bool is_sync_call() const override { return true; }

   bool is_read_only() const override { return read_only; };
   action_name get_sender() const override;
   void console_append(std::string_view val) override;
   void store_console_marker() override;

   const uint32_t               ordinal = 1;
   action_trace&                current_action_trace;
   const account_name           sender{};
   // `read_only` represents what the read only status of the call context.
   // It tells the executing smart contract code whether or not it is in
   // read-only mode and therefore whether or not the system will enforce that it
   // is only allowed to do read-only activities
   const bool                   read_only = false;
   const std::span<const char>  data{}; // includes function name, arguments, and other information
   std::vector<char>            return_value{};

   bool has_recipient(account_name account) const override;
   void update_db_usage(const account_name& payer, int64_t delta) override;
   bool is_context_free()const override;
};

} } /// namespace eosio::chain
