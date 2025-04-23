#pragma once

#include <eosio/chain/controller.hpp>
#include <eosio/chain/types.hpp>

#include <string>

namespace eosio::chain {

namespace fs = std::filesystem;

template<typename T>
class pending_snapshot {
public:
   using next_t = eosio::chain::next_function<T>;

   pending_snapshot(const chain::block_id_type& block_id, block_timestamp_type timestamp, const next_t& next, std::string pending_path, std::string final_path)
       : block_id(block_id), timestamp(timestamp), next(next), pending_path(std::move(pending_path)), final_path(std::move(final_path)) {}

   uint32_t get_height() const {
      return chain::block_header::num_from_id(block_id);
   }

   static fs::path get_final_path(const chain::block_id_type& block_id, const fs::path& snapshots_dir) {
      return snapshots_dir / fc::format_string("snapshot-${id}.bin", fc::mutable_variant_object()("id", block_id));
   }

   static fs::path get_pending_path(const chain::block_id_type& block_id, const fs::path& snapshots_dir) {
      return snapshots_dir / fc::format_string(".pending-snapshot-${id}.bin", fc::mutable_variant_object()("id", block_id));
   }

   static fs::path get_temp_path(const chain::block_id_type& block_id, const fs::path& snapshots_dir) {
      return snapshots_dir / fc::format_string(".incomplete-snapshot-${id}.bin", fc::mutable_variant_object()("id", block_id));
   }

   // call only with lib_id that is irreversible
   T finalize(const block_id_type& lib_id, const chain::controller& chain) const {
      auto lib_num = chain::block_header::num_from_id(lib_id);
      auto block_num = get_height();

      assert(lib_num >= block_num);

      bool valid = lib_id == block_id;
      if (!valid) {
         // Could attempt to look up the block_id, but not finding it doesn't necessarily mean it is not
         // irreversible. Might be running without a block log or might have been loaded via a snapshot.
         // Also, finalize called before forkdb is pruned of non-irreversible blocks, finding it doesn't necessarily
         // mean it is irreversible.
         if (lib_num > block_num) { // assume it is irreversible since unable to 100% determine
            valid = true;
         }
      }

      std::error_code ec;
      if(!valid) {
         fs::remove(fs::path(pending_path), ec);
         ilog("Snapshot created at block id ${id} invalidated because block was forked out", ("id", block_id));
         EOS_THROW(chain::snapshot_finalization_exception,
                   "Snapshotted block was forked out of the chain.  ID: ${id}", ("id", block_id));
      }

      fs::rename(fs::path(pending_path), fs::path(final_path), ec);
      EOS_ASSERT(!ec, chain::snapshot_finalization_exception,
                 "Unable to finalize valid snapshot of block number ${bn}: [code: ${ec}] ${message}",
                 ("bn", block_num)("ec", ec.value())("message", ec.message()));

      ilog("Snapshot created at block ${bn} available at ${fn}", ("bn", block_num)("fn", final_path));

      return {block_id, block_num, timestamp, chain::chain_snapshot_header::current_version, final_path};
   }

   chain::block_id_type block_id;
   block_timestamp_type timestamp;
   next_t next;
   std::string pending_path;
   std::string final_path;
};
}// namespace eosio::chain
