#include <eosio/chain/block_handle.hpp>
#include <fc/io/cfile.hpp>
#include <filesystem>

namespace eosio::chain {

// -------------------------------------------------------------------------------------------
// prior to writing magic and version numbers, we simply serialized the class (*this) to
// file. Let's call this the implicit version 0, which is not supported anymore in
// Spring 1.0.1 and above.
// However, we need to make sure that `chain_head_magic` can't match the tag of a std::variant
// -------------------------------------------------------------------------------------------
constexpr uint64_t chain_head_magic   = 0xf1f2f3f4f4f3f2f1;
constexpr uint64_t chain_head_version = 1;


void block_handle::write(const std::filesystem::path& state_file) {
   if (!is_valid())
      return;

   ilog("Writing chain_head block ${bn} ${id}", ("bn", block_num())("id", id()));

   fc::datastream<fc::cfile> f;
   f.set_file_path(state_file);
   f.open("wb");
   fc::raw::pack(f, chain_head_magic);
   fc::raw::pack(f, chain_head_version);
   fc::raw::pack(f, *this);
}

bool block_handle::read(const std::filesystem::path& state_file) {
   if (!std::filesystem::exists(state_file))
      return false;

   EOS_ASSERT(std::filesystem::file_size(state_file) >= 2 * sizeof(chain_head_magic), chain_exception,
              "File `chain_head.dat` seems to be corrupted. The best course of action might be to restart from a snapshot" );

   try {
      fc::datastream<fc::cfile> f;
      f.set_file_path(state_file);
      f.open("rb");

      uint64_t magic, version;
      fc::raw::unpack(f, magic);
      fc::raw::unpack(f, version);

      EOS_ASSERT(magic == chain_head_magic && version == chain_head_version, chain_exception,
                 "Error reading `chain_head.dat` file. It is likely a Spring 1.0.0 version which is not supported by Spring 1.0.1 and above. "
                 "The best course of action might be to restart from a snapshot" );

      fc::raw::unpack(f, *this);
      ilog("Loading chain_head block ${bn} ${id}", ("bn", block_num())("id", id()));
   } FC_CAPTURE_AND_RETHROW( (state_file) );

   // remove the `chain_head.dat` file only if we were able to successfully load it.
   std::filesystem::remove(state_file);
   return true;
}

} /// namespace eosio::chain
