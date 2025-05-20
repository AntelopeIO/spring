#pragma once

#include <cstdint>
#include <cassert>
#include <vector>
#include <string>
#include <string_view>
#include <stdexcept>

namespace fc {

// -------------------------------------------------------------------------------
//      see https://github.com/AntelopeIO/spring/wiki/ABI-1.3:-bitset-type
// -------------------------------------------------------------------------------


// stores a bitset in a std::vector<uint8_t>
//
// - bits 0-7 in first byte, 8-15 in second, ...
// - least significant bit of byte 0 is bit 0 of bitset.
// - unused bits must be zero.
// ---------------------------------------------------------------------------------
struct bitset {
   using buffer_type                         = std::vector<uint8_t>;
   using size_type                           = size_t;
   static constexpr size_type bits_per_block = 8;
   static constexpr size_type npos           = static_cast<size_type>(-1);

   static constexpr size_type calc_num_blocks(size_type num_bits) {
      return (num_bits + bits_per_block - 1) / bits_per_block;
   }

   static size_type block_index(size_type pos) noexcept { return pos / bits_per_block; }
   static uint8_t   bit_index(size_type pos)   noexcept { return static_cast<uint8_t>(pos % bits_per_block); }
   static uint8_t   bit_mask(size_type pos)    noexcept { return uint8_t(1) << bit_index(pos); }

   size_type size() const { return m_num_bits; }

   size_type num_blocks() const {
      assert(m_bits.size() == calc_num_blocks(m_num_bits));
      return m_bits.size();
   }

   bitset() = default;

   bitset(size_type num_bits) {
      resize(num_bits);
   }

   bitset(std::string_view s) {
      *this = bitset::from_string(s);
   }

   bitset(const bitset&)            = default;
   bitset(bitset&&)                 = default;
   bitset& operator=(const bitset&) = default;
   bitset& operator=(bitset&&)      = default;

   void resize(size_type num_bits) {
      m_bits.resize(calc_num_blocks(num_bits), 0);
      m_num_bits = num_bits;
      zero_unused_bits();
   }

   void set(size_type pos) {
      assert(pos < m_num_bits);
      m_bits[block_index(pos)] |= bit_mask(pos);
   }

   void clear(size_type pos) {
      assert(pos < m_num_bits);
      m_bits[block_index(pos)] &= ~bit_mask(pos);
   }

   bool test(size_type pos) const {
      return (*this)[pos];
   }

   bool operator[](size_type pos) const {
      assert(pos < m_num_bits);
      return !!(m_bits[block_index(pos)] & bit_mask(pos));
   }

   void flip(size_type pos) {
      assert(pos < m_num_bits);
      if (test(pos))
         clear(pos);
      else
         set(pos);
   }

   void flip() {
      for (auto& byte : m_bits)
         byte = ~byte;
      zero_unused_bits();
   }

   bool none() const {
      for (auto& byte : m_bits)
         if (byte)
            return false;
      return true;
   }

   void zero_all_bits() {
      for (auto& byte : m_bits)
         byte = 0;
   }

   bitset operator|=(const bitset& o) {
      assert(size() == o.size());
      for (size_t i=0; i<m_bits.size(); ++i)
         m_bits[i] |= o.m_bits[i];
      return *this;
   }

   void zero_unused_bits() {
      assert (m_bits.size() == calc_num_blocks(m_num_bits));

      // if != 0 this is the number of bits used in the last block
      const size_type extra_bits = bit_index(size());

      if (extra_bits != 0)
         m_bits.back() &= (uint8_t(1) << extra_bits) - 1;
   }

   bool unused_bits_zeroed() const {
      // if != 0 this is the number of bits used in the last block
      const size_type extra_bits = bit_index(size());
      return extra_bits == 0 || (m_bits.back() & ~((uint8_t(1) << extra_bits) - 1)) == 0;
   }

   friend auto operator<(const bitset& a, const bitset& b) {
      return std::tuple(a.m_num_bits, a.m_bits) < std::tuple(b.m_num_bits, b.m_bits);
   }

   friend bool operator==(const bitset& a, const bitset& b) {
      return std::tuple(a.m_num_bits, a.m_bits) == std::tuple(b.m_num_bits, b.m_bits);
   }

   uint8_t& byte(size_t i) {
      assert(i < m_bits.size());
      return m_bits[i];
   }

   const uint8_t& byte(size_t i) const {
      assert(i < m_bits.size());
      return m_bits[i];
   }

   std::string to_string() const {
      std::string res;
      res.resize(size());
      size_t idx = 0;
      for (auto i = size(); i-- > 0;)
         res[idx++] = (*this)[i] ? '1' : '0';
      return res;
   }

   static bitset from_string(std::string_view s) {
      bitset bs;
      auto   num_bits = s.size();
      bs.resize(num_bits);

      for (size_t i = 0; i < num_bits; ++i) {
         switch (s[i]) {
         case '0':
            break; // nothing to do, all bits initially 0
         case '1':
            bs.set(num_bits - i - 1); // high bitset indexes come first in the JSON representation
            break;
         default:
            throw std::invalid_argument( "unexpected character in bitset string representation" );
            break;
         }
      }
      assert(bs.unused_bits_zeroed());
      return bs;
   }

   template <typename Stream>
   friend Stream& operator<<(Stream& ds, const bitset& bs) {
      ds << bs.to_string();
      return ds;
   }

   template <typename Stream>
   friend Stream& operator>>(Stream& ds, bitset& bs) {
      std::string s;
      ds >> s;
      bs = bitset::from_string(s);
      return ds;
   }

private:
   size_type   m_num_bits{0}; // members order matters for comparison operators
   buffer_type m_bits;        // must be after `m_num_bits`
};

} // namespace fc
