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
struct tag_list_t {
   using list_t = std::vector<offset_tag_t>;

   pos_tag_t             pos_tag;
   std::optional<list_t> offset_tags;

   friend bool operator==(const tag_list_t&, const tag_list_t&) = default;
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

   std::variant<and_f, or_f, tag_list_t> and_or_tags;

   friend bool operator==(const tag_filter_t&, const tag_filter_t&) = default;
};

} // namespace eosio::chain

// ---------------------------------------------------------------------------
FC_REFLECT(eosio::chain::pos_tag_t, (ord)(tag))
FC_REFLECT(eosio::chain::offset_tag_t, (offset)(tag))
FC_REFLECT(eosio::chain::tag_list_t, (pos_tag)(offset_tags))
FC_REFLECT(eosio::chain::tag_filter_t::and_f, (filter_list))
FC_REFLECT(eosio::chain::tag_filter_t::or_f, (filter_list))
FC_REFLECT(eosio::chain::tag_filter_t, (and_or_tags))

template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
template<class... Ts> overload(Ts...) -> overload<Ts...>;

// ---------------------------------------------------------------------------
namespace std {

inline std::ostream& operator<<(std::ostream& os, const eosio::chain::pos_tag_t& ot) {
   if (ot.ord)
      os << "pos_tag(" << (int)*ot.ord << ", " << ot.tag << ')';
   else
      os << "pos_tag({}, " << ot.tag << ')';
   return os;
}

inline std::ostream& operator<<(std::ostream& os, const eosio::chain::offset_tag_t& ot) {
   os << "offset_tag(" << (int)ot.offset << ", " << ot.tag << ')';
   return os;
}

inline std::ostream& operator<<(std::ostream& os, const eosio::chain::tag_list_t& ts) {
   os << "tag_list(" << ts.pos_tag;
   if (ts.offset_tags && !ts.offset_tags->empty()) {
      for (const auto& ot : *ts.offset_tags)
         os << ", " << ot;
   }
   os << ')';
   return os;
}

inline std::ostream& operator<<(std::ostream& os, const eosio::chain::tag_filter_t& tf) {
   using namespace eosio::chain;

   auto print_list = [&](const std::vector<tag_filter_t>& l, std::string_view oper) {
      os << oper << '(';
      switch (l.size()) {
      case 0:
         os << "[]";
         break;
      default:
         os << "[";
         for (size_t i=0; i<l.size(); ++i) {
            os << l[i];
            if (i+i < l.size())
               os << ", ";
         }
         os << ']';
         break;
      };
      os <<  ')';
   };

   std::visit(overload{[&](const tag_list_t& s) { os << s; },
            [&](const tag_filter_t::and_f& l) { print_list(l.filter_list, "and"); },
                         [&](const tag_filter_t::or_f& l) { print_list(l.filter_list, "or"); }},
              tf.and_or_tags);
   return os;
}
} // namespace std