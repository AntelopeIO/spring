#pragma once

#include <eosio/chain/block_state_legacy.hpp>
#include <eosio/chain/block_state.hpp>
#include <fc/reflect/reflect.hpp>

namespace eosio::chain {

// Created via controller::accept_block(const block_id_type& id, const signed_block_ptr& b)
// Valid to request id and signed_block_ptr it was created from.
struct block_handle {
private:
   std::variant<block_state_legacy_ptr, block_state_ptr> _bsp;

   friend struct fc::reflector<block_handle>;
   friend struct controller_impl;       // for `internal()` access below from controller
   friend struct block_handle_accessor; // for `internal()` access below from controller or tests

   // Avoid using internal block_state/block_state_legacy as those types are internal to controller.
   const auto& internal() const { return _bsp; }
   
public:
   block_handle() = default;
   explicit block_handle(block_state_legacy_ptr bsp) : _bsp(std::move(bsp)) {}
   explicit block_handle(block_state_ptr bsp) : _bsp(std::move(bsp)) {}

   bool is_valid() const { return !_bsp.valueless_by_exception() && std::visit([](const auto& bsp) { return !!bsp; }, _bsp); }

   block_num_type          block_num() const { return std::visit([](const auto& bsp) { return bsp->block_num(); }, _bsp); }
   block_num_type          irreversible_blocknum() const { return std::visit([](const auto& bsp) { return bsp->irreversible_blocknum(); }, _bsp); }
   block_timestamp_type    timestamp() const { return std::visit([](const auto& bsp) { return bsp->timestamp(); }, _bsp); };
   time_point              block_time() const { return std::visit([](const auto& bsp) { return time_point{bsp->timestamp()}; }, _bsp); };
   const block_id_type&    id() const { return std::visit<const block_id_type&>([](const auto& bsp) -> const block_id_type& { return bsp->id(); }, _bsp); }
   const block_id_type&    previous() const { return std::visit<const block_id_type&>([](const auto& bsp) -> const block_id_type& { return bsp->previous(); }, _bsp); }
   const signed_block_ptr& block() const { return std::visit<const signed_block_ptr&>([](const auto& bsp) -> const signed_block_ptr& { return bsp->block; }, _bsp); }
   const block_header&     header() const { return std::visit<const block_header&>([](const auto& bsp) -> const block_header& { return bsp->header; }, _bsp); };
   account_name            producer() const { return std::visit([](const auto& bsp) { return bsp->producer(); }, _bsp); }

   void write(const std::filesystem::path& state_file);
   bool read(const std::filesystem::path& state_file);
};

} // namespace eosio::chain

FC_REFLECT(eosio::chain::block_handle, (_bsp))
