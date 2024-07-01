#include <eosio/chain/block_handle.hpp>
#include <fc/io/cfile.hpp>
#include <filesystem>

namespace eosio::chain {

void block_handle::write(const std::filesystem::path& state_file) {
   if (!is_valid())
      return;

   ilog("Writing chain_head block ${bn} ${id}", ("bn", block_num())("id", id()));

   fc::datastream<fc::cfile> f;
   f.set_file_path(state_file);
   f.open("wb");

   fc::raw::pack(f, *this);
}

bool block_handle::read(const std::filesystem::path& state_file) {
   if (!std::filesystem::exists(state_file))
      return false;

   fc::datastream<fc::cfile> f;
   f.set_file_path(state_file);
   f.open("rb");

   fc::raw::unpack(f, *this);

   ilog("Loading chain_head block ${bn} ${id}", ("bn", block_num())("id", id()));

   std::filesystem::remove(state_file);

   return true;
}

} /// namespace eosio::chain
