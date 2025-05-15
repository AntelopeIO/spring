#pragma once
#include <fc/io/raw.hpp>

namespace fc {
   template<typename Hash, typename... T>
   Hash digest(const T&... t )
   {
      typename Hash::encoder e;
      raw::pack(e,t...);
      return e.result();
   }

   //for sha3 only -- a way to get keccak via passing 'false' to result()
   struct keccak_digest {};
   template<typename Hash, typename... T>
   Hash digest(keccak_digest, const T&... t )
   {
      typename Hash::encoder e;
      raw::pack(e,t...);
      return e.result(false);
   }
}
