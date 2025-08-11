#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

static_assert(std::endian::native == std::endian::little);

namespace fc::detail {
   constexpr std::array<std::uint32_t, 64> K = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
   };

   constexpr std::array<std::uint32_t, 8> H_INIT = {
      0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
   };

   //c++23 byteswap() me
   constexpr std::array<uint8_t, 4> to_bytes_big_endian(std::uint32_t val) {
      return std::bit_cast<std::array<uint8_t, 4>>(__builtin_bswap32(val));
   }

   constexpr std::uint32_t from_bytes_big_endian(const uint8_t* bytes) {
      std::array<uint8_t, 4> arr;
      std::copy_n(bytes, 4, arr.begin());

      return __builtin_bswap32(std::bit_cast<std::uint32_t>(arr));
   }

   constexpr std::uint32_t ch(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
      return (x & y) ^ (~x & z);
   }

   constexpr std::uint32_t maj(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
      return (x & y) ^ (x & z) ^ (y & z);
   }

   constexpr std::uint32_t sigma0(std::uint32_t x) {
      return std::rotr(x, 2) ^ std::rotr(x, 13) ^ std::rotr(x, 22);
   }

   constexpr std::uint32_t sigma1(std::uint32_t x) {
      return std::rotr(x, 6) ^ std::rotr(x, 11) ^ std::rotr(x, 25);
   }

   constexpr std::uint32_t gamma0(std::uint32_t x) {
      return std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3);
   }

   constexpr std::uint32_t gamma1(std::uint32_t x) {
      return std::rotr(x, 17) ^ std::rotr(x, 19) ^ (x >> 10);
   }

   constexpr std::array<char, 32> constsha256(std::string_view message) {
      std::vector<uint8_t> padded_message;
      padded_message.reserve(message.length() + 72);
      for(char c : message)
         padded_message.push_back(c);

      //append padding
      const size_t original_len_bits = message.length() * 8;
      padded_message.push_back(0x80);

      const size_t required_padding = (padded_message.size() % 64 > 56) ? (64 - (padded_message.size() % 64)) + 56
                                                                        : (56 - (padded_message.size() % 64));

      for(size_t i = 0; i < required_padding; ++i)
         padded_message.push_back(0x00);

      std::array<uint8_t, 8> len_be_bytes = std::bit_cast<std::array<uint8_t, 8>>(__builtin_bswap64(original_len_bits));
      padded_message.insert(padded_message.end(), len_be_bytes.begin(), len_be_bytes.end());

      //start hashing loop
      std::array<std::uint32_t, 8> h = H_INIT;

      for (size_t chunk_start = 0; chunk_start < padded_message.size(); chunk_start += 64) {
         const uint8_t* chunk = &padded_message[chunk_start];
         std::array<std::uint32_t, 64> w;

         for (int i = 0; i < 16; ++i)
            w[i] = from_bytes_big_endian(chunk + (i * 4));

         for (int i = 16; i < 64; ++i)
            w[i] = gamma1(w[i - 2]) + w[i - 7] + gamma0(w[i - 15]) + w[i - 16];

         std::uint32_t a = h[0];
         std::uint32_t b = h[1];
         std::uint32_t c = h[2];
         std::uint32_t d = h[3];
         std::uint32_t e = h[4];
         std::uint32_t f = h[5];
         std::uint32_t g = h[6];
         std::uint32_t h_var = h[7];

         for (int i = 0; i < 64; ++i) {
            std::uint32_t temp1 = h_var + sigma1(e) + ch(e, f, g) + K[i] + w[i];
            std::uint32_t temp2 = sigma0(a) + maj(a, b, c);
            h_var = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
         }

         h[0] += a;
         h[1] += b;
         h[2] += c;
         h[3] += d;
         h[4] += e;
         h[5] += f;
         h[6] += g;
         h[7] += h_var;
      }

      std::array<char, 32> final_hash{};
      for (int i = 0; i < 8; ++i) {
         std::array<uint8_t, 4> h_bytes = to_bytes_big_endian(h[i]);
         std::copy(h_bytes.begin(), h_bytes.end(), final_hash.begin() + (i * 4));
      }

      return final_hash;
   }
}