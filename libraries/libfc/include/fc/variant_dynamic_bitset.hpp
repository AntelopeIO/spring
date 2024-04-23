#pragma once

#include <fc/variant.hpp>
#include <fc/bitutil.hpp>

#include "variant_object.hpp"

namespace fc
{
   inline void to_variant( const fc::dynamic_bitset& bs, fc::variant& v ) {
      auto num_blocks = bs.num_blocks();
      if ( num_blocks > MAX_NUM_ARRAY_ELEMENTS )
         throw std::range_error( "number of blocks of dynamic_bitset cannot be greather than MAX_NUM_ARRAY_ELEMENTS" );

      std::vector<fc::dynamic_bitset::block_type> blocks(num_blocks);
      boost::to_block_range(bs, blocks.begin());
      v = fc::mutable_variant_object("size", bs.size())
                                    ("bits", blocks);
   }

   inline void from_variant( const fc::variant& v, fc::dynamic_bitset& bs ) {
      fc::dynamic_bitset::size_type size;
      std::vector<fc::dynamic_bitset::block_type> blocks;

      from_variant(v["size"], size);
      from_variant(v["bits"], blocks);
      bs = { blocks.cbegin(), blocks.cend() };
      bs.resize(size);
   }
} // namespace fc
