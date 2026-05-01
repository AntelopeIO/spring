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

   // Returns true if this block carries a strong QC for a block that is not
   // in `head_handle`'s ancestry.
   //
   // Under Savanna a strong QC for some block B implies that at least 2/3 of
   // finalizer weight voted strong on B. The safety rule for strong votes
   // locks those finalizers on B, so they cannot subsequently vote in a way
   // that would let any branch not extending B form its own QC. Therefore if
   // `head_handle` is on a branch that does not include the QC target, no
   // block built on `head_handle` can ever be covered by a future QC, and
   // `head_handle`'s branch cannot win fork-choice -- it is permanently
   // locked out.
   //
   // Returns false in legacy (non-Savanna) mode and when no strong QC is
   // present.
   //
   // Thread-safety: safe to call concurrently with block production / apply.
   // `block_state_ptr` is a shared_ptr (its copy is atomic) and the
   // `finality_core` it references is immutable after construction. The
   // accessors used (`latest_qc_claim`, `get_block_reference`, `extends`)
   // are const reads against that immutable state.
   bool locks_out_branch_of(const block_handle& head_handle) const {
      if (!std::holds_alternative<block_state_ptr>(_bsp) ||
          !std::holds_alternative<block_state_ptr>(head_handle._bsp))
         return false;

      const auto& bsp      = std::get<block_state_ptr>(_bsp);
      const auto& head_bsp = std::get<block_state_ptr>(head_handle._bsp);

      const auto qc = bsp->core.latest_qc_claim();
      if (!qc.is_strong_qc)
         return false;

      const auto& this_id = bsp->id();
      const auto& head_id = head_bsp->id();

      // If head is on this block's branch (head is this block, or this block extends head),
      // they share the QC's chain -- head's branch can produce blocks that include the QC
      // target as an ancestor. Not locked out.
      if (head_id == this_id || bsp->core.extends(head_id))
         return false;

      // If the QC target is in head's ancestry (or is head itself), head's branch already
      // includes the block the QC was formed for. Not locked out.
      const auto& qc_target_id = bsp->core.get_block_reference(qc.block_num).block_id;
      if (head_id == qc_target_id || head_bsp->core.extends(qc_target_id))
         return false;

      // Otherwise head's branch and the QC target are on incompatible branches: any block
      // built on head conflicts with the QC target, and no QC can ever be formed on head's
      // branch. Locked out.
      return true;
   }
};

} // namespace eosio::chain

FC_REFLECT(eosio::chain::block_handle, (_bsp))
