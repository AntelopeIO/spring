#pragma once

#include <fc/variant.hpp>
#include <fc/bitset.hpp>
#include <fc/bitutil.hpp>

namespace fc
{
   inline void to_variant( const fc::bitset& bs, fc::variant& v ) {
      auto num_blocks = bs.num_blocks();
      if ( num_blocks > MAX_NUM_ARRAY_ELEMENTS )
         throw std::range_error( "number of blocks of bitset cannot be greather than MAX_NUM_ARRAY_ELEMENTS" );

      v = bs.to_string();
   }

   inline void from_variant( const fc::variant& v, fc::bitset& bs ) {
      std::string s = v.get_string();
      bs = fc::bitset(s);
   }
} // namespace fc
