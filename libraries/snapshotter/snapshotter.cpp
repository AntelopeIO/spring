#include <eosio/chain/controller.hpp>
#include <eosio/chain/fork_database.hpp>
#include <eosio/chain/deep_mind.hpp>

using namespace eosio::chain;

//extern "C" void doit(int db_fd, int forkdb_fd) {
extern "C" int makesnap(char* db_path, char* forkdb_path, char* protocol_features_path, char* snapshot_output_path) {
   //std::string db_path = "/proc/self/fd/" + std::to_string(db_fd);
   //std::string forkdb_path = "/proc/self/fd/" + std::to_string(forkdb_fd);

   protocol_feature_set pfs = initialize_protocol_features(protocol_features_path, false);
   deep_mind_handler deep;
   protocol_feature_manager protocol_features(std::move(pfs), [&deep](bool is_trx_transient) { return &deep; });

   try {
      chainbase::database db(db_path, chainbase::database::read_write);
      controller::add_indices(db);

      ///XXX the case where root in forkdb doesn't exist?
      fork_database forkdb(forkdb_path);
      forkdb.open([](block_timestamp_type, const flat_set<digest_type>&, const vector<digest_type>&){});
      block_handle lib_block_handle = forkdb.apply<block_handle>([&](auto& forkdb) {
         return block_handle(forkdb.root());
      });

      db.undo_all();

      ///XXX check lib_block_handle block_num == db revision

      {
         std::ofstream ofs(snapshot_output_path, (std::ios::out | std::ios::binary));
         std::shared_ptr<ostream_snapshot_writer> ofs_snap_writter = std::make_shared<ostream_snapshot_writer>(ofs);
         controller::write_nonlive_snapshot(ofs_snap_writter, db, protocol_features, forkdb, lib_block_handle);
         ofs_snap_writter->finalize();
      }
      return 0;
   }
   catch(fc::assert_exception& e) {
      std::cout << e.what() << std::endl;
   }
   return 1;
}