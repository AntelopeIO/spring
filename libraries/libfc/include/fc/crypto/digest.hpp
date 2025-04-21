#pragma once
#include <fc/io/raw.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/crypto/sha256.hpp>

namespace fc {

   template<typename T>
   fc::sha256 digest( const T& value )
   {
      fc::sha256::encoder enc;
      fc::raw::pack( enc, value );
      return enc.result();
   }

   template <typename DigestType, typename F>
   class hash_encoder_with_checktime : public DigestType::encoder {
      public:
         hash_encoder_with_checktime(size_t checktime_block_size, F&& checktime) : DigestType::encoder(),
                                                                                   checktime_block_size(checktime_block_size),
                                                                                   checktime(std::forward<F>(checktime)) {}

         void write(const char* d, uint32_t dlen) {
            while (dlen > checktime_block_size) {
               DigestType::encoder::write(d, checktime_block_size);
               d    += checktime_block_size;
               dlen -= checktime_block_size;
               checktime();
            }
            DigestType::encoder::write(d, dlen);
            encoded_since_checktime += dlen;
            if(encoded_since_checktime > checktime_block_size) {
               checktime();
               encoded_since_checktime = 0;
            }
         }

      private:
         size_t encoded_since_checktime = 0;
         const size_t checktime_block_size = 0;
         F checktime;
      };

   template<typename F>
   using sha256_encoder_with_checktime = hash_encoder_with_checktime<sha256, F>;
}
