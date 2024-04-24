#include <eosio/chain/block.hpp>

namespace eosio::chain {
   void additional_block_signatures_extension::reflector_init() {
      static_assert( fc::raw::has_feature_reflector_init_on_unpacked_reflected_types,
                     "additional_block_signatures_extension expects FC to support reflector_init" );

      EOS_ASSERT( signatures.size() > 0, ill_formed_additional_block_signatures_extension,
                  "Additional block signatures extension must contain at least one signature",
      );

      set<signature_type> unique_sigs;

      for( const auto& s : signatures ) {
         auto res = unique_sigs.insert( s );
         EOS_ASSERT( res.second, ill_formed_additional_block_signatures_extension,
                     "Signature ${s} was repeated in the additional block signatures extension",
                     ("s", s)
         );
      }
   }

   void quorum_certificate_extension::reflector_init() {
      static_assert( fc::raw::has_feature_reflector_init_on_unpacked_reflected_types, "quorum_certificate_extension expects FC to support reflector_init" );
      static_assert( extension_id() == 3, "extension id for quorum_certificate_extension must be 3" );
   }

   flat_multimap<uint16_t, block_extension> signed_block::validate_and_extract_extensions()const {
      using decompose_t = block_extension_types::decompose_t;

      flat_multimap<uint16_t, block_extension> results;

      uint16_t id_type_lower_bound = 0;

      for( size_t i = 0; i < block_extensions.size(); ++i ) {
         const auto& e = block_extensions[i];
         auto id = e.first;

         EOS_ASSERT( id >= id_type_lower_bound, invalid_block_extension,
                     "Block extensions are not in the correct order (ascending id types required)"
         );

         auto iter = results.emplace(std::piecewise_construct,
            std::forward_as_tuple(id),
            std::forward_as_tuple()
         );

         auto match = decompose_t::extract<block_extension>( id, e.second, iter->second );
         EOS_ASSERT( match, invalid_block_extension,
                     "Block extension with id type ${id} is not supported",
                     ("id", id)
         );

         if( match->enforce_unique ) {
            EOS_ASSERT( i == 0 || id > id_type_lower_bound, invalid_block_header_extension,
                        "Block extension with id type ${id} is not allowed to repeat",
                        ("id", id)
            );
         }


         id_type_lower_bound = id;
      }

      return results;
   }

   // Does not validate ordering, assumes validate_and_extract_extensions() has been called in verify_qc_claim()
   std::optional<block_extension> signed_block::extract_extension(uint16_t extension_id)const {
      using decompose_t = block_extension_types::decompose_t;

      assert(std::ranges::is_sorted(block_extensions)); // currently all extensions are unique so default compare works

      for( size_t i = 0; i < block_extensions.size(); ++i ) {
         const auto& e = block_extensions[i];
         auto id = e.first;

         if (id > extension_id)
            break;
         if (id != extension_id)
            continue;

         std::optional<block_extension> ext;
         ext.emplace();

         auto match = decompose_t::extract<block_extension>( id, e.second, *ext );
         EOS_ASSERT( match, invalid_block_extension,
                     "Block extension with id type ${id} is not supported",
                     ("id", id)
         );

         return ext;
      }

      return {};
   }

   bool signed_block::contains_extension(uint16_t extension_id)const {
      return std::any_of(block_extensions.cbegin(), block_extensions.cend(), [&](const auto& p) {
         return p.first == extension_id;
      });
   }

} /// namespace eosio::chain
