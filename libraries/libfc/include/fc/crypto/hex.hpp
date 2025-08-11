#pragma once
#include <fc/utility.hpp>
#include <fc/exception/exception.hpp>
#include <string>
#include <vector>

namespace fc {
    constexpr uint8_t from_hex( char c ) {
        if( c >= '0' && c <= '9' )
            return c - '0';
        if( c >= 'a' && c <= 'f' )
            return c - 'a' + 10;
        if( c >= 'A' && c <= 'F' )
            return c - 'A' + 10;
        FC_THROW_EXCEPTION( exception, "Invalid hex character '${c}'", ("c", std::string(&c,1) ) );
        return 0;
    }

    std::string to_hex( const char* d, uint32_t s );
    std::string to_hex( const std::vector<char>& data );

    /**
     *  @return the number of bytes decoded
     */
    size_t from_hex( const std::string& hex_str, char* out_data, size_t out_data_len );

    constexpr std::vector<char> from_hex(std::string_view hex_str) {
        if (hex_str.length() % 2 != 0)
            FC_THROW_EXCEPTION( exception, "Hex string must have an even number of characters" );

        std::vector<char> result;
        if (!hex_str.empty())
            result.reserve(hex_str.length() / 2);

        for (size_t i = 0; i < hex_str.length(); i += 2) {
            uint8_t high_nibble = from_hex(hex_str[i]);
            uint8_t low_nibble = from_hex(hex_str[i + 1]);

            result.push_back((high_nibble << 4) | low_nibble);
        }

        return result;
    }

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
