#pragma once

#include <eosio/chain/types.hpp>
#include <charconv>
#include <iostream>

namespace eosio::chain {

struct version_t {
   uint8_t major = 0;
   uint8_t minor = 0;
   bool    valid = false;

   version_t() = default;

   version_t(uint8_t major, uint8_t minor) : major(major), minor(minor), valid(true) {}

   version_t(std::string_view sv) {
      auto last = sv.data() + sv.size();
      auto [ptr, ec] = std::from_chars(sv.data(), last, major);
      if (ec == std::errc() && *ptr == '.')
         valid = (std::from_chars(ptr+1, last, minor).ec == std::errc());
   }

   friend auto operator<=>(const version_t&, const version_t&) = default;
   friend bool operator==(const version_t&, const version_t&) = default;

   std::string str() const {
      return std::to_string(major) + "." + std::to_string(minor);
   }

   friend std::ostream& operator<<(std::ostream& s, const version_t& v) {
      s << "version_t(" << v.str() << ")";
      return s;
   }

   bool is_valid() const {
      return valid;
   }
};

using type_name      = string;
using field_name     = string;
using call_name      = string;

struct type_def {
   type_def() = default;
   type_def(const type_name& new_type_name, const type_name& type)
   :new_type_name(new_type_name), type(type)
   {}

   type_name   new_type_name;
   type_name   type;
};

struct field_def {
   field_def() = default;
   field_def(const field_name& name, const type_name& type)
   :name(name), type(type)
   {}

   field_name name;
   type_name  type;

   bool operator==(const field_def& other) const {
      return std::tie(name, type) == std::tie(other.name, other.type);
   }
};

struct struct_def {
   struct_def() = default;
   struct_def(const type_name& name, const type_name& base, const vector<field_def>& fields)
   :name(name), base(base), fields(fields)
   {}

   type_name            name;
   type_name            base;
   vector<field_def>    fields;

   bool operator==(const struct_def& other) const {
      return std::tie(name, base, fields) == std::tie(other.name, other.base, other.fields);
   }
};

struct action_def {
   action_def() = default;
   action_def(const action_name& name, const type_name& type, const string& ricardian_contract)
   :name(name), type(type), ricardian_contract(ricardian_contract)
   {}

   action_name name;
   type_name   type;
   string      ricardian_contract;
};

struct table_def {
   table_def() = default;
   table_def(const table_name& name, const type_name& index_type, const vector<field_name>& key_names, const vector<type_name>& key_types, const type_name& type)
   :name(name), index_type(index_type), key_names(key_names), key_types(key_types), type(type)
   {}

   table_name         name;        // the name of the table
   type_name          index_type;  // the kind of index, i64, i128i128, etc
   vector<field_name> key_names;   // names for the keys defined by key_types
   vector<type_name>  key_types;   // the type of key parameters
   type_name          type;        // type of binary data stored in this table
};

struct clause_pair {
   clause_pair() = default;
   clause_pair( const string& id, const string& body )
   : id(id), body(body)
   {}

   string id;
   string body;
};

struct error_message {
   error_message() = default;
   error_message( uint64_t error_code, const string& error_msg )
   : error_code(error_code), error_msg(error_msg)
   {}

   uint64_t error_code;
   string   error_msg;
};

struct variant_def {
   type_name            name;
   vector<type_name>    types;
};

struct action_result_def {
   action_result_def() = default;
   action_result_def(const action_name& name, const type_name& result_type)
   :name(name), result_type(result_type)
   {}

   action_name name;
   type_name   result_type;
};

struct call_data_header {  // match with CDT definition
   static constexpr uint32_t current_version = 0;  // sync call

   uint32_t version   = 0;
   uint64_t func_name = 0;

   bool is_version_valid() { return version <= current_version; }
};

struct call_def {
   call_def() = default;
   call_def(const call_name& name, const type_name& type, uint64_t id)
   :name(name), type(type), id(id)
   {}

   call_name name;
   type_name type;
   uint64_t  id = 0;
};

struct call_result_def {
   call_result_def() = default;
   call_result_def(const call_name& name, const type_name& result_type)
   :name(name), result_type(result_type)
   {}

   call_name name;
   type_name result_type;
};

template<typename T>
struct may_not_exist {
   T value{};
};

struct abi_def {
   abi_def() = default;
   abi_def(const vector<type_def>& types, const vector<struct_def>& structs, const vector<action_def>& actions, const vector<table_def>& tables, const vector<clause_pair>& clauses, const vector<error_message>& error_msgs)
   :types(types)
   ,structs(structs)
   ,actions(actions)
   ,tables(tables)
   ,ricardian_clauses(clauses)
   ,error_messages(error_msgs)
   {}

   string                                    version = "";
   vector<type_def>                          types;
   vector<struct_def>                        structs;
   vector<action_def>                        actions;
   vector<table_def>                         tables;
   vector<clause_pair>                       ricardian_clauses;
   vector<error_message>                     error_messages;
   extensions_type                           abi_extensions;
   may_not_exist<vector<variant_def>>        variants;
   may_not_exist<vector<action_result_def>>  action_results;
   may_not_exist<vector<call_def>>           calls;
   may_not_exist<vector<call_result_def>>    call_results;

   version_t get_version() const {
      static const std::string version_header = "eosio::abi/";
      if (!version.starts_with(version_header))
         return {};
      std::string_view version_str(version.c_str() + version_header.size(), version.size() - version_header.size());
      return version_t(version_str);
   }
};

abi_def eosio_contract_abi(const abi_def& eosio_system_abi);
vector<type_def> common_type_defs();

extern unsigned char eosio_abi_bin[2132];

} /// namespace eosio::chain

namespace fc {

template<typename ST, typename T>
datastream<ST>& operator << (datastream<ST>& s, const eosio::chain::may_not_exist<T>& v) {
   raw::pack(s, v.value);
   return s;
}

template<typename ST, typename T>
datastream<ST>& operator >> (datastream<ST>& s, eosio::chain::may_not_exist<T>& v) {
   if (s.remaining())
      raw::unpack(s, v.value);
   return s;
}

template<typename T>
void to_variant(const eosio::chain::may_not_exist<T>& e, fc::variant& v) {
   to_variant( e.value, v);
}

template<typename T>
void from_variant(const fc::variant& v, eosio::chain::may_not_exist<T>& e) {
   from_variant( v, e.value );
}

} // namespace fc

FC_REFLECT( eosio::chain::type_def                         , (new_type_name)(type) )
FC_REFLECT( eosio::chain::field_def                        , (name)(type) )
FC_REFLECT( eosio::chain::struct_def                       , (name)(base)(fields) )
FC_REFLECT( eosio::chain::action_def                       , (name)(type)(ricardian_contract) )
FC_REFLECT( eosio::chain::table_def                        , (name)(index_type)(key_names)(key_types)(type) )
FC_REFLECT( eosio::chain::clause_pair                      , (id)(body) )
FC_REFLECT( eosio::chain::error_message                    , (error_code)(error_msg) )
FC_REFLECT( eosio::chain::variant_def                      , (name)(types) )
FC_REFLECT( eosio::chain::action_result_def                , (name)(result_type) )
FC_REFLECT( eosio::chain::call_data_header                 , (version)(func_name) )
FC_REFLECT( eosio::chain::call_def                         , (name)(type)(id) )
FC_REFLECT( eosio::chain::call_result_def                  , (name)(result_type) )
FC_REFLECT( eosio::chain::abi_def                          , (version)(types)(structs)(actions)(tables)
                                                             (ricardian_clauses)(error_messages)(abi_extensions)(variants)(action_results)
                                                             (calls)(call_results) )
