#pragma once

#include <fc/fwd.hpp>
#include <fc/crypto/packhash.hpp>
#include <fc/io/raw_fwd.hpp>

namespace fc {

namespace detail {
struct alignas(64) alignmeto64 {};
}

struct xxh3 : public add_packhash_to_hash<xxh3> {
   xxh3() = default;
   explicit xxh3(const uint64_t h) : _hash(h) {}

   ///XXX: no data() since would that be confusing LE/BE? but that breaks common interface across hash types

   static xxh3 hash(const char* d, uint32_t dlen);
   static xxh3 hash(const std::string&);

   template<typename T>
   static xxh3 hash( const T& t ) {
      return packhash(t);
   }

   class encoder {
      public:
         encoder();

         void write( const char* d, uint32_t dlen );
         void put( char c ) { write( &c, 1 ); }
         void reset();
         xxh3 result();

      private:
         struct impl;
         //XXH3_state_t documents requirement of 64 byte alignment: oof tracking down that spookiness was fun...
         fc::fwd<impl,576,detail::alignmeto64> my;
   };

   friend auto operator<=>(const xxh3&, const xxh3&) = default;

   uint64_t _hash = 0;
};

}

FC_REFLECT(xxh3, (_hash))

