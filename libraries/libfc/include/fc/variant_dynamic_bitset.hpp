#pragma once

#include <fc/variant.hpp>
#include <fc/bitutil.hpp>

namespace fc
{
   inline void to_variant( const fc::dynamic_bitset& bs, fc::variant& v ) {
      auto num_blocks = bs.num_blocks();
      if ( num_blocks > MAX_NUM_ARRAY_ELEMENTS )
         throw std::range_error( "number of blocks of dynamic_bitset cannot be greather than MAX_NUM_ARRAY_ELEMENTS" );

      std::string s;
      to_string(bs, s);
      // From boost::dynamic_bitset docs:
      //   A character in the string is '1' if the corresponding bit is set, and '0' if it is not. Character
      //   position i in the string corresponds to bit position b.size() - 1 - i.
      v = std::move(s);
   }

   inline void from_variant( const fc::variant& v, fc::dynamic_bitset& bs ) {
      std::string s = v.get_string();
      bs = fc::dynamic_bitset(s);
   }
} // namespace fc
