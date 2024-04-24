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

      std::string s;
      to_string(bs, s);
      v = std::move(s);
   }

   inline void from_variant( const fc::variant& v, fc::dynamic_bitset& bs ) {
      std::string s = v.get_string();
      bs = fc::dynamic_bitset(s);
   }
} // namespace fc
