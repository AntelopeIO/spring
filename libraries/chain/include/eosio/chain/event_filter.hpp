#pragma once

#include <compare>
#include <vector>
#include <optional>
#include <cstdint>
#include <iostream>
#include <fc/variant_object.hpp>
#include <fc/io/json.hpp>
#include <fc/reflect/reflect.hpp>

namespace eosio::chain {

// ---------------------------------------------------------------------------
struct pos_tag_t {
   std::optional<uint8_t> ord;
   uint64_t tag;

   friend bool operator==(const pos_tag_t&, const pos_tag_t&) = default;
};

// ---------------------------------------------------------------------------
struct offset_tag_t {
   uint8_t  offset;
   uint64_t tag;

   friend bool operator==(const offset_tag_t&, const offset_tag_t&) = default;
};

// ---------------------------------------------------------------------------
struct tag_spec {
   pos_tag_t pos_tag;
   std::optional<std::vector<offset_tag_t>> offset_tags;

   friend bool operator==(const tag_spec&, const tag_spec&) = default;
};

// ---------------------------------------------------------------------------
struct tag_filter_t {
   struct and_f {
      std::vector<tag_filter_t> filter_list;

      friend bool operator==(const and_f&, const and_f&) = default;
   };

   struct or_f  {
      std::vector<tag_filter_t> filter_list;

      friend bool operator==(const or_f&, const or_f&) = default;
   };

   std::variant<and_f, or_f, tag_spec> and_or_tags;

   friend bool operator==(const tag_filter_t&, const tag_filter_t&) = default;
};

} // namespace eosio::chain

// ---------------------------------------------------------------------------
FC_REFLECT(eosio::chain::pos_tag_t, (ord)(tag))
FC_REFLECT(eosio::chain::offset_tag_t, (offset)(tag))
FC_REFLECT(eosio::chain::tag_spec, (pos_tag)(offset_tags))
FC_REFLECT(eosio::chain::tag_filter_t::and_f, (filter_list))
FC_REFLECT(eosio::chain::tag_filter_t::or_f, (filter_list))
FC_REFLECT(eosio::chain::tag_filter_t, (and_or_tags))

// ---------------------------------------------------------------------------
namespace std {
   inline std::ostream& operator<<(std::ostream& os, const eosio::chain::tag_filter_t& tf) {
      os << fc::json::to_string(tf, fc::time_point::maximum());
      return os;
   }
}