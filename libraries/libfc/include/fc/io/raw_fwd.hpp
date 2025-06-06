#pragma once
#include <fc/container/flat_fwd.hpp>
#include <fc/container/deque_fwd.hpp>
#include <fc/io/varint.hpp>
#include <fc/array.hpp>
#include <fc/safe.hpp>
#include <deque>
#include <vector>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <set>
#include <variant>
#include <filesystem>

namespace eosio::chain {
   struct signed_block;
}

namespace fc {
   class time_point;
   class time_point_sec;
   class variant;
   class variant_object;

   template<typename IntType, typename EnumType> class enum_type;
   namespace ip { class endpoint; }

   namespace ecc { class public_key; class private_key; }
   template<typename Storage> class fixed_string;

   namespace raw {

    template<class T>
    concept TrivialScalar = std::is_scalar_v<T> && !std::is_pointer_v<T>;

    template<class T>
    concept NotTrivialScalar = !TrivialScalar<T>;
   
    template<typename T>
    inline size_t pack_size(  const T& v );

    template <typename Stream> void unpack(Stream& s, eosio::chain::signed_block& v);

    template<typename Stream, typename Storage> inline void pack( Stream& s, const fc::fixed_string<Storage>& u );
    template<typename Stream, typename Storage> inline void unpack( Stream& s, fc::fixed_string<Storage>& u );

    template<typename Stream, typename IntType, typename EnumType>
    inline void pack( Stream& s, const fc::enum_type<IntType,EnumType>& tp );
    template<typename Stream, typename IntType, typename EnumType>
    inline void unpack( Stream& s, fc::enum_type<IntType,EnumType>& tp );



    template<typename Stream, typename T> inline void pack( Stream& s, const std::set<T>& value );
    template<typename Stream, typename T> inline void unpack( Stream& s, std::set<T>& value );
    template<typename Stream, typename T> inline void pack( Stream& s, const std::unordered_set<T>& value );
    template<typename Stream, typename T> inline void unpack( Stream& s, std::unordered_set<T>& value );

    template<typename Stream, typename... T> void pack( Stream& s, const std::variant<T...>& sv );
    template<typename Stream, typename... T> void unpack( Stream& s, std::variant<T...>& sv );

    template<typename Stream, typename T> inline void pack( Stream& s, const std::deque<T>& value );
    template<typename Stream, typename T> inline void unpack( Stream& s, std::deque<T>& value );

    template<typename Stream, typename T, typename... U>
    inline void pack( Stream& s, const boost::container::deque<T, U...>& value );
    template<typename Stream, typename T, typename... U>
    inline void unpack( Stream& s, boost::container::deque<T, U...>& value );

    template<typename Stream, typename K, typename V> inline void pack( Stream& s, const std::unordered_map<K,V>& value );
    template<typename Stream, typename K, typename V> inline void unpack( Stream& s, std::unordered_map<K,V>& value );

    template<typename Stream, typename K, typename V> inline void pack( Stream& s, const std::map<K,V>& value );
    template<typename Stream, typename K, typename V> inline void unpack( Stream& s, std::map<K,V>& value );

    template<typename Stream, typename K, typename V> inline void pack( Stream& s, const std::pair<K,V>& value );
    template<typename Stream, typename K, typename V> inline void unpack( Stream& s, std::pair<K,V>& value );

   template<typename Stream, TrivialScalar T, std::size_t S> inline void pack( Stream& s, const std::array<T,S>& value );
   template<typename Stream, NotTrivialScalar T, std::size_t S> inline void pack( Stream& s, const std::array<T,S>& value );
   template<typename Stream, TrivialScalar T, std::size_t S> inline void unpack( Stream& s, std::array<T,S>& value );
   template<typename Stream, NotTrivialScalar T, std::size_t S> inline void unpack( Stream& s, std::array<T,S>& value );

    template<typename Stream> inline void pack( Stream& s, const variant_object& v );
    template<typename Stream> inline void unpack( Stream& s, variant_object& v );
    template<typename Stream> inline void pack( Stream& s, const variant& v );
    template<typename Stream> inline void unpack( Stream& s, variant& v );

    template<typename Stream> inline void pack( Stream& s, const std::filesystem::path& v );
    template<typename Stream> inline void unpack( Stream& s, std::filesystem::path& v );
    template<typename Stream> inline void pack( Stream& s, const ip::endpoint& v );
    template<typename Stream> inline void unpack( Stream& s, ip::endpoint& v );


    template<typename Stream, typename T> void unpack( Stream& s, std::optional<T>& v );
    template<typename Stream, typename T> void pack( Stream& s, const std::optional<T>& v );
    template<typename Stream, typename T> void pack( Stream& s, const safe<T>& v );
    template<typename Stream, typename T> void unpack( Stream& s, fc::safe<T>& v );

    template<typename Stream> void unpack( Stream& s, time_point& );
    template<typename Stream> void pack( Stream& s, const time_point& );
    template<typename Stream> void unpack( Stream& s, time_point_sec& );
    template<typename Stream> void pack( Stream& s, const time_point_sec& );
    template<typename Stream> void unpack( Stream& s, std::string& );
    template<typename Stream> void pack( Stream& s, const std::string& );
    template<typename Stream> void unpack( Stream& s, fc::ecc::public_key& );
    template<typename Stream> void pack( Stream& s, const fc::ecc::public_key& );
    template<typename Stream> void unpack( Stream& s, fc::ecc::private_key& );
    template<typename Stream> void pack( Stream& s, const fc::ecc::private_key& );

    template<typename Stream, typename T> inline void pack( Stream& s, const T& v );
    template<typename Stream, typename T> inline void unpack( Stream& s, T& v );

    template<typename Stream, typename T> inline void pack( Stream& s, const std::vector<T>& v );
    template<typename Stream, typename T> inline void unpack( Stream& s, std::vector<T>& v );

    template<typename Stream> inline void pack( Stream& s, const signed_int& v );
    template<typename Stream> inline void unpack( Stream& s, signed_int& vi );

    template<typename Stream> inline void pack( Stream& s, const unsigned_int& v );
    template<typename Stream> inline void unpack( Stream& s, unsigned_int& vi );

    template<typename Stream> inline void pack( Stream& s, const char* v );
    template<typename Stream> inline void pack( Stream& s, const std::vector<char>& value );
    template<typename Stream> inline void unpack( Stream& s, std::vector<char>& value );

   template<typename Stream, TrivialScalar T, std::size_t N> inline void pack( Stream& s, const fc::array<T,N>& v);
   template<typename Stream, NotTrivialScalar T, std::size_t N> inline void pack( Stream& s, const fc::array<T,N>& v);
   template<typename Stream, TrivialScalar T, std::size_t N> inline void unpack( Stream& s, fc::array<T,N>& v);
   template<typename Stream, NotTrivialScalar T, std::size_t N> inline void unpack( Stream& s, fc::array<T,N>& v);

    template<typename Stream> inline void pack( Stream& s, const bool& v );
    template<typename Stream> inline void unpack( Stream& s, bool& v );

    template<typename T> inline std::vector<char> pack( const T& v );
    template<typename T> inline T unpack( const std::vector<char>& s );
    template<typename T> inline T unpack( const char* d, uint32_t s );
    template<typename T> inline void unpack( const char* d, uint32_t s, T& v );
} }
