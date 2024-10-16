#pragma once
#include <fc/utility.hpp>
#include <string>
#include <vector>

namespace fc {
    uint8_t from_hex( char c );
    std::string to_hex( const char* d, uint32_t s );
    std::string to_hex( const std::vector<char>& data );

    /**
     *  @return the number of bytes decoded
     */
    size_t from_hex( const std::string& hex_str, char* out_data, size_t out_data_len );

    /**
     *  @return the hex string of `n`
     */
   template<typename I>
   std::string itoh(I n, size_t hlen = sizeof(I)<<1) {
       static const char* digits = "0123456789abcdef";
       std::string r(hlen, '0');
       for(size_t i = 0, j = (hlen - 1) * 4 ; i < hlen; ++i, j -= 4)
           r[i] = digits[(n>>j) & 0x0f];
       return r;
   }
} 
