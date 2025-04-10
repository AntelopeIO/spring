#include <eosio/chain/block.hpp>
#include <eosio/chain/finalizer_authority.hpp>
#include <eosio/chain/merkle.hpp>
#include <fc/io/raw.hpp>
#include <fc/bitutil.hpp>
#include <algorithm>

namespace eosio { namespace chain {
   digest_type block_header::digest()const
   {
      return digest_type::hash(*this);
   }

   uint32_t block_header::num_from_id(const block_id_type& id)
   {
      return fc::endian_reverse_u32(id._hash[0]);
   }

   block_id_type block_header::calculate_id()const
   {
      // Do not include signed_block_header attributes in id, specifically exclude producer_signature.
      block_id_type result = digest(); //fc::sha256::hash(*static_cast<const block_header*>(this));
      result._hash[0] &= 0xffffffff00000000;
      result._hash[0] += fc::endian_reverse_u32(block_num()); // store the block num in the ID, 160 bits is plenty for the hash
      return result;
   }

   header_extension_multimap block_header::validate_and_extract_header_extensions()const {
      using decompose_t = block_header_extension_types::decompose_t;

      header_extension_multimap results;

      uint16_t id_type_lower_bound = 0;

      for( size_t i = 0; i < header_extensions.size(); ++i ) {
         const auto& e = header_extensions[i];
         auto id = e.first;

         EOS_ASSERT( id >= id_type_lower_bound, invalid_block_header_extension,
                     "Block header extensions are not in the correct order (ascending id types required)"
         );

         auto iter = results.emplace(std::piecewise_construct,
            std::forward_as_tuple(id),
            std::forward_as_tuple()
         );

         auto match = decompose_t::extract<block_header_extension>( id, e.second, iter->second );
         EOS_ASSERT( match, invalid_block_header_extension,
                     "Block header extension with id type ${id} is not supported",
                     ("id", id)
         );

         if( match->enforce_unique ) {
            EOS_ASSERT( i == 0 || id > id_type_lower_bound, invalid_block_header_extension,
                        "Block header extension with id type ${id} is not allowed to repeat",
                        ("id", id)
            );
         }


         id_type_lower_bound = id;
      }

      return results;
   }

   // Does not validate ordering, assumes validate_and_extract_header_extensions() has been called in block_state creation
   std::optional<block_header_extension> block_header::extract_header_extension(uint16_t extension_id)const {
      using decompose_t = block_header_extension_types::decompose_t;

      assert(std::ranges::is_sorted(header_extensions)); // currently all extensions are unique so default compare works

      for( size_t i = 0; i < header_extensions.size(); ++i ) {
         const auto& e = header_extensions[i];
         auto id = e.first;

         if (id > extension_id)
            break;
         if (id != extension_id)
            continue;

         block_header_extension ext;

         auto match = decompose_t::extract<block_header_extension>( id, e.second, ext );
         EOS_ASSERT( match, invalid_block_header_extension,
                     "Block header extension with id type ${id} is not supported",
                     ("id", id)
         );

         return ext;
      }

      return {};
   }

   bool block_header::contains_header_extension(uint16_t extension_id)const {
      return std::any_of(header_extensions.cbegin(), header_extensions.cend(), [&](const auto& p) {
         return p.first == extension_id;
      });
   }

} }
