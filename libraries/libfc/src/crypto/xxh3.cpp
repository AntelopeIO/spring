#include <fc/crypto/xxh3.hpp>
#include <fc/fwd_impl.hpp>

#ifdef __x86_64__
#include <xxHash/xxh_x86dispatch.h>
#endif
#include <xxHash/xxhash.h>

namespace fc {

struct xxh3::encoder::impl {
   XXH3_state_t ctx;
};

xxh3::encoder::encoder() {
   reset();
}
void xxh3::encoder::reset() {
   XXH3_64bits_reset(&my->ctx);
}
void xxh3::encoder::write(const char* d, uint32_t dlen) {
   XXH3_64bits_update(&my->ctx, d, dlen);
}
xxh3 xxh3::encoder::result() {
   xxh3 h;
   h._hash = XXH3_64bits_digest(&my->ctx);
   return h;
}

xxh3 xxh3::hash(const char* d, uint32_t dlen) {
  encoder e;
  e.write(d,dlen);
  return e.result();
}
xxh3 xxh3::hash(const std::string& s) {
  return hash(s.data(), s.size());
}

}
