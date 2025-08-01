#pragma once
#include <eosio/chain/abi_def.hpp>
#include <eosio/chain/trace.hpp>
#include <eosio/chain/contract_types.hpp>
#include <eosio/chain/exceptions.hpp>
#include <utility>
#include <fc/variant_object.hpp>
#include <fc/variant_dynamic_bitset.hpp>
#include <fc/scoped_exit.hpp>
#include <fc/time.hpp>

namespace eosio::chain {

using std::map;
using std::string;
using std::function;
using std::pair;
using namespace fc;

namespace impl {
   struct abi_from_variant;
   struct abi_to_variant;

   struct abi_traverse_context;
   struct abi_traverse_context_with_path;
   struct binary_to_variant_context;
   struct variant_to_binary_context;
   struct action_data_to_variant_context;
}

/**
 *  Describes the binary representation message and table contents so that it can
 *  be converted to and from JSON.
 */
struct abi_serializer {

   /// passed recursion_depth on each invocation
   using yield_function_t = fc::optional_delegate<void(size_t)>;

   abi_serializer(){ configure_built_in_types(); }
   abi_serializer( abi_def abi, const yield_function_t& yield );
   [[deprecated("use the overload with yield_function_t[=create_yield_function(max_serialization_time)]")]]
   abi_serializer( const abi_def& abi, const fc::microseconds& max_serialization_time );
   void set_abi( abi_def abi, const yield_function_t& yield );
   [[deprecated("use the overload with yield_function_t[=create_yield_function(max_serialization_time)]")]]
   void set_abi(const abi_def& abi, const fc::microseconds& max_serialization_time);

   /// @return string_view of `t` or internal string type
   std::string_view resolve_type(const std::string_view& t)const;
   bool      is_array(const std::string_view& type)const;
   std::optional<fc::unsigned_int> is_szarray(const std::string_view& type)const;
   bool      is_optional(const std::string_view& type)const;
   bool      is_type( const std::string_view& type, const yield_function_t& yield )const;
   bool      is_type(const std::string_view& type, const fc::microseconds& max_serialization_time)const;
   bool      is_builtin_type(const std::string_view& type)const;
   bool      is_integer(const std::string_view& type) const;
   int       get_integer_size(const std::string_view& type) const;
   bool      is_struct(const std::string_view& type)const;

   /// @return string_view of `type`
   std::string_view fundamental_type(const std::string_view& type)const;

   const struct_def& get_struct(const std::string_view& type)const;

   type_name get_action_type(name action)const;
   type_name get_table_type(name action)const;
   type_name get_action_result_type(name action_result)const;

   std::optional<string>  get_error_message( uint64_t error_code )const;

   fc::variant binary_to_variant( const std::string_view& type, const bytes& binary, const yield_function_t& yield, bool short_path = false )const;
   fc::variant binary_to_variant( const std::string_view& type, const bytes& binary, const fc::microseconds& max_action_data_serialization_time, bool short_path = false )const;
   fc::variant binary_to_variant( const std::string_view& type, fc::datastream<const char*>& binary, const yield_function_t& yield, bool short_path = false )const;
   fc::variant binary_to_variant( const std::string_view& type, fc::datastream<const char*>& binary, const fc::microseconds& max_action_data_serialization_time, bool short_path = false )const;

   bytes       variant_to_binary( const std::string_view& type, const fc::variant& var, const fc::microseconds& max_action_data_serialization_time, bool short_path = false )const;
   bytes       variant_to_binary( const std::string_view& type, const fc::variant& var, const yield_function_t& yield, bool short_path = false )const;
   void        variant_to_binary( const std::string_view& type, const fc::variant& var, fc::datastream<char*>& ds, const fc::microseconds& max_action_data_serialization_time, bool short_path = false )const;
   void        variant_to_binary( const std::string_view& type, const fc::variant& var, fc::datastream<char*>& ds, const yield_function_t& yield, bool short_path = false )const;

   template<typename T, typename Resolver>
   static void to_variant( const T& o, fc::variant& vo, const Resolver& resolver, const yield_function_t& yield );
   template<typename T, typename Resolver>
   static void to_variant( const T& o, fc::variant& vo, const Resolver& resolver, const fc::microseconds& max_action_data_serialization_time );

   template<typename T, typename Resolver>
   static void to_log_variant( const T& o, fc::variant& vo, const Resolver& resolver, const yield_function_t& yield );
   template<typename T, typename Resolver>
   static void to_log_variant( const T& o, fc::variant& vo, const Resolver& resolver, const fc::microseconds& max_action_data_serialization_time );

   template<typename T, typename Resolver>
   static void from_variant( const fc::variant& v, T& o, const Resolver& resolver, const yield_function_t& yield );
   template<typename T, typename Resolver>
   static void from_variant( const fc::variant& v, T& o, const Resolver& resolver, const fc::microseconds& max_action_data_serialization_time );

   template<typename Vec>
   static bool is_empty_abi(const Vec& abi_vec)
   {
      return abi_vec.size() <= 4;
   }

   template<typename Vec>
   static bool to_abi(const Vec& abi_vec, abi_def& abi)
   {
      if( !is_empty_abi(abi_vec) ) { /// 4 == packsize of empty Abi
         fc::datastream<const char*> ds( abi_vec.data(), abi_vec.size() );
         fc::raw::unpack( ds, abi );
         return true;
      }
      return false;
   }

   typedef std::function<fc::variant(fc::datastream<const char*>&, bool, bool, const abi_serializer::yield_function_t&)>  unpack_function;
   typedef std::function<void(const fc::variant&, fc::datastream<char*>&, bool, bool, const abi_serializer::yield_function_t&)>  pack_function;

   void add_specialized_unpack_pack( const string& name, std::pair<abi_serializer::unpack_function, abi_serializer::pack_function> unpack_pack );

   static constexpr size_t max_recursion_depth = 32; // arbitrary depth to prevent infinite recursion

   // create standard yield function that checks for max_serialization_time and max_recursion_depth.
   // restricts serialization time from creation of yield function until serialization is complete.
   // now() deadline captured at time of this call
   static yield_function_t create_yield_function(const fc::microseconds& max_serialization_time) {
      fc::time_point deadline = fc::time_point::now().safe_add(max_serialization_time);
      return [max_serialization_time, deadline](size_t recursion_depth) {
         EOS_ASSERT( recursion_depth < max_recursion_depth, abi_recursion_depth_exception,
                     "recursive definition, max_recursion_depth ${r} ", ("r", max_recursion_depth) );

         EOS_ASSERT( fc::time_point::now() < deadline, abi_serialization_deadline_exception,
                     "serialization time limit ${t}us exceeded", ("t", max_serialization_time) );
      };
   }

   static yield_function_t create_depth_yield_function() {
      return [](size_t recursion_depth) {
         EOS_ASSERT( recursion_depth < max_recursion_depth, abi_recursion_depth_exception,
                     "recursive definition, max_recursion_depth ${r} ", ("r", max_recursion_depth) );
      };
   }

private:

   map<type_name, type_name, std::less<>>     typedefs;
   map<type_name, struct_def, std::less<>>    structs;
   map<name,type_name>                        actions;
   map<name,type_name>                        tables;
   map<uint64_t, string>                      error_messages;
   map<type_name, variant_def, std::less<>>   variants;
   map<name,type_name>                        action_results;

   map<type_name, pair<unpack_function, pack_function>, std::less<>> built_in_types;
   void configure_built_in_types();

   fc::variant _binary_to_variant( const std::string_view& type, const bytes& binary, impl::binary_to_variant_context& ctx )const;
   fc::variant _binary_to_variant( const std::string_view& type, fc::datastream<const char*>& binary, impl::binary_to_variant_context& ctx )const;
   void        _binary_to_variant( const std::string_view& type, fc::datastream<const char*>& stream,
                                   fc::mutable_variant_object& obj, impl::binary_to_variant_context& ctx )const;

   bytes       _variant_to_binary( const std::string_view& type, const fc::variant& var, impl::variant_to_binary_context& ctx )const;
   void        _variant_to_binary( const std::string_view& type, const fc::variant& var,
                                   fc::datastream<char*>& ds, impl::variant_to_binary_context& ctx )const;

   static std::string_view _remove_bin_extension(const std::string_view& type);
   bool _is_type( const std::string_view& type, impl::abi_traverse_context& ctx )const;

   void validate( impl::abi_traverse_context& ctx )const;

   friend struct impl::abi_from_variant;
   friend struct impl::abi_to_variant;
   friend struct impl::abi_traverse_context_with_path;
};

namespace impl {
   const static size_t hex_log_max_size = 64;
   struct abi_traverse_context {
      abi_traverse_context( abi_serializer::yield_function_t yield, fc::microseconds max_action_data_serialization )
      : yield(std::move( yield ))
      , max_action_serialization_time(max_action_data_serialization)
      {
      }

      void logging() { log = true; } // generate variant for logging
      bool is_logging() const { return log; }

      void check_deadline()const { yield( recursion_depth ); }
      abi_serializer::yield_function_t get_yield_function() { return yield; }

      fc::scoped_exit<std::function<void()>> enter_scope();

   protected:
      abi_serializer::yield_function_t  yield;
      // if set then restricts each individual action data serialization
      fc::microseconds                  max_action_serialization_time;
      size_t                            recursion_depth = 1;
      bool                              log = false;
   };

   struct empty_path_root {};

   struct array_type_path_root {
   };

   struct struct_type_path_root {
      map<type_name, struct_def>::const_iterator  struct_itr;
   };

   struct variant_type_path_root {
      map<type_name, variant_def>::const_iterator variant_itr;
   };

   using path_root = std::variant<empty_path_root, array_type_path_root, struct_type_path_root, variant_type_path_root>;

   struct empty_path_item {};

   struct array_index_path_item {
      path_root                                   type_hint;
      uint32_t                                    array_index = 0;
   };

   struct field_path_item {
      map<type_name, struct_def>::const_iterator  parent_struct_itr;
      uint32_t                                    field_ordinal = 0;
   };

   struct variant_path_item {
      map<type_name, variant_def>::const_iterator variant_itr;
      uint32_t                                    variant_ordinal = 0;
   };

   using path_item = std::variant<empty_path_item, array_index_path_item, field_path_item, variant_path_item>;

   struct abi_traverse_context_with_path : public abi_traverse_context {
      abi_traverse_context_with_path( const abi_serializer& abis, abi_serializer::yield_function_t yield, fc::microseconds max_action_data_serialization_time, const std::string_view& type )
      : abi_traverse_context( std::move( yield ), max_action_data_serialization_time ), abis(abis)
      {
         set_path_root(type);
      }

      abi_traverse_context_with_path( const abi_serializer& abis, const abi_traverse_context& ctx, const std::string_view& type )
      : abi_traverse_context(ctx), abis(abis)
      {
         set_path_root(type);
      }

      void set_path_root( const std::string_view& type );

      fc::scoped_exit<std::function<void()>> push_to_path( const path_item& item );

      void set_array_index_of_path_back( uint32_t i );
      void hint_array_type_if_in_array();
      void hint_struct_type_if_in_array( const map<type_name, struct_def>::const_iterator& itr );
      void hint_variant_type_if_in_array( const map<type_name, variant_def>::const_iterator& itr );

      string get_path_string()const;

      string maybe_shorten( const std::string_view& str );

   protected:
      const abi_serializer&  abis;
      path_root              root_of_path;
      vector<path_item>      path;
   public:
      bool                   short_path = false;
   };

   struct binary_to_variant_context : public abi_traverse_context_with_path {
      using abi_traverse_context_with_path::abi_traverse_context_with_path;
   };

   struct action_data_to_variant_context : public binary_to_variant_context {
      action_data_to_variant_context( const abi_serializer& abis, const abi_traverse_context& ctx, const std::string_view& type )
            : binary_to_variant_context(abis, ctx, type)
      {
         short_path = true; // Just to be safe while avoiding the complexity of threading an override boolean all over the place
         if (max_action_serialization_time.count() > 0) {
            fc::time_point deadline = fc::time_point::now().safe_add(max_action_serialization_time);
            yield = [deadline, y=yield, max=max_action_serialization_time](size_t depth) {
               y(depth); // call provided yield that might include an overall time limit or not
               EOS_ASSERT( fc::time_point::now() < deadline, abi_serialization_deadline_exception,
                           "serialization action data time limit ${t}us exceeded", ("t", max) );
            };
         }
      }
   };

   struct variant_to_binary_context : public abi_traverse_context_with_path {
      using abi_traverse_context_with_path::abi_traverse_context_with_path;

      fc::scoped_exit<std::function<void()>> disallow_extensions_unless( bool condition );

      bool extensions_allowed()const { return allow_extensions; }

   protected:
      bool                   allow_extensions = true;
   };

   /// limits the string size to default max_length of output_name
   string limit_size( const std::string_view& str );

   /**
    * Determine if a type contains ABI related info, perhaps deeply nested
    * @tparam T - the type to check
    */
   template<typename T>
   constexpr bool single_type_requires_abi_v() {
      return std::is_base_of<transaction, T>::value ||
             std::is_same<T, packed_transaction>::value ||
             std::is_same<T, transaction_trace>::value ||
             std::is_same<T, transaction_receipt>::value ||
             std::is_same<T, action_trace>::value ||
             std::is_same<T, signed_transaction>::value ||
             std::is_same<T, signed_block>::value ||
             std::is_same<T, action>::value;
   }

   /**
    * Basic constexpr for a type, aliases the basic check directly
    * @tparam T - the type to check
    */
   template<typename T>
   struct type_requires_abi {
      static constexpr bool value() {
         return single_type_requires_abi_v<T>();
      }
   };

   /**
    * specialization that catches common container patterns and checks their contained-type
    * @tparam Container - a templated container type whose first argument is the contained type
    */
   template<template<typename ...> class Container, typename T, typename ...Args >
   struct type_requires_abi<Container<T, Args...>> {
      static constexpr bool value() {
         return single_type_requires_abi_v<T>();
      }
   };

   template<typename T>
   constexpr bool type_requires_abi_v() {
      return type_requires_abi<T>::value();
   }

   /**
    * convenience aliases for creating overload-guards based on whether the type contains ABI related info
    */
   template<typename T>
   using not_require_abi_t = std::enable_if_t<!type_requires_abi_v<T>(), int>;

   template<typename T>
   using require_abi_t = std::enable_if_t<type_requires_abi_v<T>(), int>;

   struct abi_to_variant {
      /**
       * template which overloads add for types which are not relvant to ABI information
       * and can be degraded to the normal ::to_variant(...) processing
       */
      template<typename M, typename Resolver, not_require_abi_t<M> = 1>
      static void add( mutable_variant_object &mvo, const char* name, const M& v, const Resolver&, abi_traverse_context& ctx )
      {
         auto h = ctx.enter_scope();
         mvo(name,v);
      }

      /**
       * template which overloads add for types which contain ABI information in their trees
       * for these types we create new ABI aware visitors
       */
      template<typename M, typename Resolver, require_abi_t<M> = 1>
      static void add( mutable_variant_object &mvo, const char* name, const M& v, const Resolver& resolver, abi_traverse_context& ctx );

      /**
       * template which overloads add for vectors of types which contain ABI information in their trees
       * for these members we call ::add in order to trigger further processing
       */
      template<typename M, typename Resolver, require_abi_t<M> = 1>
      static void add( mutable_variant_object &mvo, const char* name, const vector<M>& v, const Resolver& resolver, abi_traverse_context& ctx )
      {
         auto h = ctx.enter_scope();
         fc::variants array;
         array.reserve(v.size());

         for (const auto& iter: v) {
            mutable_variant_object elem_mvo;
            add(elem_mvo, "_", iter, resolver, ctx);
            array.emplace_back(std::move(elem_mvo["_"]));
         }
         mvo(name, std::move(array));
      }

      /**
      * template which overloads add for deques of types which contain ABI information in their trees
      * for these members we call ::add in order to trigger further processing
      */
      template<typename M, typename Resolver, require_abi_t<M> = 1>
      static void add( mutable_variant_object &mvo, const char* name, const deque<M>& v, const Resolver& resolver, abi_traverse_context& ctx )
      {
         auto h = ctx.enter_scope();
         deque<fc::variant> array;

         for (const auto& iter: v) {
            mutable_variant_object elem_mvo;
            add(elem_mvo, "_", iter, resolver, ctx);
            array.emplace_back(std::move(elem_mvo["_"]));
         }
         mvo(name, std::move(array));
      }
      /**
       * template which overloads add for shared_ptr of types which contain ABI information in their trees
       * for these members we call ::add in order to trigger further processing
       */
      template<typename M, typename Resolver, require_abi_t<M> = 1>
      static void add( mutable_variant_object &mvo, const char* name, const std::shared_ptr<M>& v, const Resolver& resolver, abi_traverse_context& ctx )
      {
         auto h = ctx.enter_scope();
         if( !v ) return;
         mutable_variant_object obj_mvo;
         add(obj_mvo, "_", *v, resolver, ctx);
         mvo(name, std::move(obj_mvo["_"]));
      }

      template<typename Resolver>
      struct add_static_variant
      {
         mutable_variant_object& obj_mvo;
         const Resolver& resolver;
         abi_traverse_context& ctx;

         add_static_variant( mutable_variant_object& o, const Resolver& r, abi_traverse_context& ctx )
               :obj_mvo(o), resolver(r), ctx(ctx) {}

         typedef void result_type;
         template<typename T> void operator()( T& v )const
         {
            add(obj_mvo, "_", v, resolver, ctx);
         }
      };

      template<typename Resolver, typename... Args>
      static void add( mutable_variant_object &mvo, const char* name, const std::variant<Args...>& v, const Resolver& resolver, abi_traverse_context& ctx )
      {
         auto h = ctx.enter_scope();
         mutable_variant_object obj_mvo;
         add_static_variant<Resolver> adder(obj_mvo, resolver, ctx);
         std::visit(adder, v);
         mvo(name, std::move(obj_mvo["_"]));
      }

      template<typename Resolver>
      static bool add_special_logging( mutable_variant_object& mvo, const char* name, const action& act, const Resolver& resolver, abi_traverse_context& ctx ) {
         if( !ctx.is_logging() ) return false;

         try {

            if( act.account == config::system_account_name && act.name == "setcode"_n ) {
               auto setcode_act = act.data_as<setcode>();
               if( setcode_act.code.size() > 0 ) {
                  fc::sha256 code_hash = fc::sha256::hash(setcode_act.code.data(), (uint32_t) setcode_act.code.size());
                  mvo("code_hash", code_hash);
               }
               return false; // still want the hex data included
            }

         } catch(...) {} // return false

         return false;
      }

      /**
       * overload of to_variant_object for actions
       *
       * This matches the FC_REFLECT for this type, but this is provided to extract the contents of act.data
       * @tparam Resolver
       * @param act
       * @param resolver
       * @return
       */
      template<typename Resolver>
      static void add( mutable_variant_object &out, const char* name, const action& act, const Resolver& resolver, abi_traverse_context& ctx )
      {
         static_assert(fc::reflector<action>::total_member_count == 4);
         auto h = ctx.enter_scope();
         mutable_variant_object mvo;
         mvo("account", act.account);
         mvo("name", act.name);
         mvo("authorization", act.authorization);

         if( add_special_logging(mvo, name, act, resolver, ctx) ) {
            out(name, std::move(mvo));
            return;
         }

         auto set_hex_data = [&](mutable_variant_object& mvo, const char* name, const bytes& data) {
            if( !ctx.is_logging() ) {
               mvo(name, data);
            } else {
               fc::mutable_variant_object sub_obj;
               sub_obj( "size", data.size() );
               if( data.size() > impl::hex_log_max_size ) {
                  sub_obj( "trimmed_hex", std::vector<char>(&data[0], &data[0] + impl::hex_log_max_size) );
               } else {
                  sub_obj( "hex", data );
               }
               mvo(name, std::move(sub_obj));
            }
         };

         try {
            auto abi_optional = resolver(act.account);
            if (abi_optional) {
               const abi_serializer& abi = *abi_optional;
               auto type = abi.get_action_type(act.name);
               if (!type.empty()) {
                  try {
                     action_data_to_variant_context _ctx(abi, ctx, type);
                     mvo( "data", abi._binary_to_variant( type, act.data, _ctx ));
                  } catch(...) {
                     // any failure to serialize data, then leave as not serialized
                     set_hex_data(mvo, "data", act.data);
                  }
               } else {
                  set_hex_data(mvo, "data", act.data);
               }
            } else {
               set_hex_data(mvo, "data", act.data);
            }
         } catch(...) {
            set_hex_data(mvo, "data", act.data);
         }
         set_hex_data(mvo, "hex_data", act.data);
         out(name, std::move(mvo));
      }

      /**
       * overload of to_variant_object for action_trace
       *
       * This matches the FC_REFLECT for this type, but this is provided to extract the contents of action_trace.return_value
       * @tparam Resolver
       * @param action_trace
       * @param resolver
       * @return
       */
      template<typename Resolver>
      static void add( mutable_variant_object& out, const char* name, const action_trace& act_trace, const Resolver& resolver, abi_traverse_context& ctx )
      {
         static_assert(fc::reflector<action_trace>::total_member_count == 17);
         auto h = ctx.enter_scope();
         mutable_variant_object mvo;

         mvo("action_ordinal", act_trace.action_ordinal);
         mvo("creator_action_ordinal", act_trace.creator_action_ordinal);
         mvo("closest_unnotified_ancestor_action_ordinal", act_trace.closest_unnotified_ancestor_action_ordinal);
         mvo("receipt", act_trace.receipt);
         mvo("receiver", act_trace.receiver);
         add(mvo, "act", act_trace.act, resolver, ctx);
         mvo("context_free", act_trace.context_free);
         mvo("elapsed", act_trace.elapsed);
         mvo("console", act_trace.console);
         mvo("trx_id", act_trace.trx_id);
         mvo("block_num", act_trace.block_num);
         mvo("block_time", act_trace.block_time);
         mvo("producer_block_id", act_trace.producer_block_id);
         mvo("account_ram_deltas", act_trace.account_ram_deltas);
         mvo("except", act_trace.except);
         mvo("error_code", act_trace.error_code);

         mvo("return_value_hex_data", act_trace.return_value);
         auto act = act_trace.act;
         try {
            auto abi_optional = resolver(act.account);
            if (abi_optional) {
               const abi_serializer& abi = *abi_optional;
               auto type = abi.get_action_result_type(act.name);
               if (!type.empty()) {
                  action_data_to_variant_context _ctx(abi, ctx, type);
                  mvo( "return_value_data", abi._binary_to_variant( type, act_trace.return_value, _ctx ));
               }
            }
         } catch(...) {}
         out(name, std::move(mvo));
      }

      /**
       * overload of to_variant_object for packed_transaction
       *
       * This matches the FC_REFLECT for this type, but this is provided to allow extracting the contents of ptrx.transaction
       * @tparam Resolver
       * @param act
       * @param resolver
       * @return
       */
      template<typename Resolver>
      static void add( mutable_variant_object &out, const char* name, const packed_transaction& ptrx, const Resolver& resolver, abi_traverse_context& ctx )
      {
         static_assert(fc::reflector<packed_transaction>::total_member_count == 4);
         auto h = ctx.enter_scope();
         mutable_variant_object mvo;
         auto trx = ptrx.get_transaction();
         mvo("id", trx.id());
         mvo("signatures", ptrx.get_signatures());
         mvo("compression", ptrx.get_compression());
         mvo("packed_context_free_data", ptrx.get_packed_context_free_data());
         mvo("context_free_data", ptrx.get_context_free_data());
         if( !ctx.is_logging() )
            mvo("packed_trx", ptrx.get_packed_transaction());
         add(mvo, "transaction", trx, resolver, ctx);

         out(name, std::move(mvo));
      }

      /**
       * overload of to_variant_object for transaction
       *
       * This matches the FC_REFLECT for this type, but this is provided to allow extracting the contents of trx.transaction_extensions
       */
      template<typename Resolver>
      static void add( mutable_variant_object &out, const char* name, const transaction& trx, const Resolver& resolver, abi_traverse_context& ctx )
      {
         static_assert(fc::reflector<transaction>::total_member_count == 9);
         auto h = ctx.enter_scope();
         mutable_variant_object mvo;
         mvo("expiration", trx.expiration);
         mvo("ref_block_num", trx.ref_block_num);
         mvo("ref_block_prefix", trx.ref_block_prefix);
         mvo("max_net_usage_words", trx.max_net_usage_words);
         mvo("max_cpu_usage_ms", trx.max_cpu_usage_ms);
         mvo("delay_sec", trx.delay_sec);
         add(mvo, "context_free_actions", trx.context_free_actions, resolver, ctx);
         add(mvo, "actions", trx.actions, resolver, ctx);

         // process contents of block.transaction_extensions
         auto exts = trx.validate_and_extract_extensions();
         if (auto it = exts.find(deferred_transaction_generation_context::extension_id()); it != exts.end()) {
            const auto& deferred_transaction_generation = std::get<deferred_transaction_generation_context>(it->second);
            mvo("deferred_transaction_generation", deferred_transaction_generation);
         }

         out(name, std::move(mvo));
      }

      static void add_block_header_finality_extension( mutable_variant_object& mvo, const header_extension_multimap& header_exts );

      /**
       * overload of to_variant_object for signed_block
       *
       * This matches the FC_REFLECT for this type, but this is provided to allow extracting the contents of
       * block.header_extensions and block.block_extensions
       */
      template<typename Resolver>
      static void add( mutable_variant_object &out, const char* name, const signed_block& block, const Resolver& resolver, abi_traverse_context& ctx )
      {
         static_assert(fc::reflector<signed_block>::total_member_count == 12);
         auto h = ctx.enter_scope();
         mutable_variant_object mvo;
         mvo("timestamp", block.timestamp);
         mvo("producer", block.producer);
         mvo("confirmed", block.confirmed);
         mvo("previous", block.previous);
         mvo("transaction_mroot", block.transaction_mroot);
         mvo("action_mroot", block.action_mroot);
         mvo("schedule_version", block.schedule_version);
         mvo("new_producers", block.new_producers);

         // process contents of block.header_extensions
         flat_multimap<uint16_t, block_header_extension> header_exts = block.validate_and_extract_header_extensions();
         if (auto it = header_exts.find(protocol_feature_activation::extension_id()); it != header_exts.end()) {
            const auto& new_protocol_features = std::get<protocol_feature_activation>(it->second).protocol_features;
            fc::variants pf_array;
            pf_array.reserve(new_protocol_features.size());
            for (auto feature : new_protocol_features) {
               mutable_variant_object feature_mvo;
               add(feature_mvo, "feature_digest", feature, resolver, ctx);
               pf_array.push_back(std::move(feature_mvo));
            }
            mvo("new_protocol_features", pf_array);
         }
         if (auto it = header_exts.find(producer_schedule_change_extension::extension_id()); it != header_exts.end()) {
            const auto& new_producer_schedule = std::get<producer_schedule_change_extension>(it->second);
            mvo("new_producer_schedule", new_producer_schedule);
         }
         add_block_header_finality_extension(mvo, header_exts);

         mvo("producer_signature", block.producer_signature);
         add(mvo, "transactions", block.transactions, resolver, ctx);

         // process contents of block.block_extensions
         auto block_exts = block.validate_and_extract_extensions();
         if (auto it = block_exts.find(additional_block_signatures_extension::extension_id()); it != block_exts.end()) {
            const auto& additional_signatures = std::get<additional_block_signatures_extension>(it->second);
            mvo("additional_signatures", additional_signatures);
         }
         if (auto it = block_exts.find(quorum_certificate_extension::extension_id()); it != block_exts.end()) {
            const auto& qc_extension = std::get<quorum_certificate_extension>(it->second);
            mvo("qc_extension", qc_extension);
         }

         out(name, std::move(mvo));
      }
   };

   /**
    * Reflection visitor that uses a resolver to resolve ABIs for nested types
    * this will degrade to the common fc::to_variant as soon as the type no longer contains
    * ABI related info
    *
    * @tparam Resolver - callable with the signature (const name& code_account) -> std::optional<abi_def>
    */
   template<typename T, typename Resolver>
   class abi_to_variant_visitor
   {
      public:
         abi_to_variant_visitor( mutable_variant_object& _mvo, const T& _val, const Resolver& _resolver, abi_traverse_context& _ctx )
         :_vo(_mvo)
         ,_val(_val)
         ,_resolver(_resolver)
         ,_ctx(_ctx)
         {}

         /**
          * Visit a single member and add it to the variant object
          * @tparam Member - the member to visit
          * @tparam Class - the class we are traversing
          * @tparam member - pointer to the member
          * @param name - the name of the member
          */
         template<typename Member, class Class, Member (Class::*member) >
         void operator()( const char* name )const
         {
            abi_to_variant::add( _vo, name, (_val.*member), _resolver, _ctx );
         }

      private:
         mutable_variant_object& _vo;
         const T& _val;
         const Resolver& _resolver;
         abi_traverse_context& _ctx;
   };

   struct abi_from_variant {
      /**
       * template which overloads extract for types which are not relvant to ABI information
       * and can be degraded to the normal ::from_variant(...) processing
       */
      template<typename M, typename Resolver, not_require_abi_t<M> = 1>
      static void extract( const fc::variant& v, M& o, const Resolver&, abi_traverse_context& ctx )
      {
         auto h = ctx.enter_scope();
         from_variant(v, o);
      }

      /**
       * template which overloads extract for types which contain ABI information in their trees
       * for these types we create new ABI aware visitors
       */
      template<typename M, typename Resolver, require_abi_t<M> = 1>
      static void extract( const fc::variant& v, M& o, const Resolver& resolver, abi_traverse_context& ctx );

      /**
       * template which overloads extract for vectors of types which contain ABI information in their trees
       * for these members we call ::extract in order to trigger further processing
       */
      template<typename M, typename Resolver, require_abi_t<M> = 1>
      static void extract( const fc::variant& v, vector<M>& o, const Resolver& resolver, abi_traverse_context& ctx )
      {
         auto h = ctx.enter_scope();
         const variants& array = v.get_array();
         o.clear();
         o.reserve( array.size() );
         for( auto itr = array.begin(); itr != array.end(); ++itr ) {
            M o_iter;
            extract(*itr, o_iter, resolver, ctx);
            o.emplace_back(std::move(o_iter));
         }
      }

 /**
  * template which overloads extract for deque of types which contain ABI information in their trees
  * for these members we call ::extract in order to trigger further processing
  */
      template<typename M, typename Resolver, require_abi_t<M> = 1>
      static void extract( const fc::variant& v, deque<M>& o, const Resolver& resolver, abi_traverse_context& ctx )
      {
         auto h = ctx.enter_scope();
         const variants& array = v.get_array();
         o.clear();
         for( auto itr = array.begin(); itr != array.end(); ++itr ) {
            M o_iter;
            extract(*itr, o_iter, resolver, ctx);
            o.emplace_back(std::move(o_iter));
         }
      }


      /**
       * template which overloads extract for shared_ptr of types which contain ABI information in their trees
       * for these members we call ::extract in order to trigger further processing
       */
      template<typename M, typename Resolver, require_abi_t<M> = 1>
      static void extract( const fc::variant& v, std::shared_ptr<M>& o, const Resolver& resolver, abi_traverse_context& ctx )
      {
         auto h = ctx.enter_scope();
         const variant_object& vo = v.get_object();
         M obj;
         extract(vo, obj, resolver, ctx);
         o = std::make_shared<M>(obj);
      }

      /**
       * Non templated overload that has priority for the action structure
       * this type has members which must be directly translated by the ABI so it is
       * exploded and processed explicitly
       */
      template<typename Resolver>
      static void extract( const fc::variant& v, action& act, const Resolver& resolver, abi_traverse_context& ctx )
      {
         auto h = ctx.enter_scope();
         const variant_object& vo = v.get_object();
         EOS_ASSERT(vo.contains("account"), packed_transaction_type_exception, "Missing account");
         EOS_ASSERT(vo.contains("name"), packed_transaction_type_exception, "Missing name");
         from_variant(vo["account"], act.account);
         from_variant(vo["name"], act.name);

         if (vo.contains("authorization")) {
            from_variant(vo["authorization"], act.authorization);
         }

         bool valid_empty_data = false;
         if( vo.contains( "data" ) ) {
            const auto& data = vo["data"];
            if( data.is_string() ) {
               from_variant(data, act.data);
               valid_empty_data = act.data.empty();
            } else if ( data.is_object() ) {
               auto abi_optional = resolver(act.account);
               if (abi_optional) {
                  const abi_serializer& abi = *abi_optional;
                  auto type = abi.get_action_type(act.name);
                  if (!type.empty()) {
                     variant_to_binary_context _ctx(abi, ctx, type);
                     _ctx.short_path = true; // Just to be safe while avoiding the complexity of threading an override boolean all over the place
                     act.data = abi._variant_to_binary( type, data, _ctx );
                     valid_empty_data = act.data.empty();
                  }
               }
            }
         }

         if( !valid_empty_data && act.data.empty() ) {
            if( vo.contains( "hex_data" ) ) {
               const auto& data = vo["hex_data"];
               if( data.is_string() ) {
                  from_variant(data, act.data);
               }
            }
         }

         EOS_ASSERT(valid_empty_data || !act.data.empty(), packed_transaction_type_exception,
                    "Failed to deserialize data for ${account}:${name}", ("account", act.account)("name", act.name));
      }

      template<typename Resolver>
      static void extract( const fc::variant& v, packed_transaction& ptrx, const Resolver& resolver, abi_traverse_context& ctx )
      {
         auto h = ctx.enter_scope();
         const variant_object& vo = v.get_object();
         EOS_ASSERT(vo.contains("signatures"), packed_transaction_type_exception, "Missing signatures");
         EOS_ASSERT(vo.contains("compression"), packed_transaction_type_exception, "Missing compression");
         std::vector<signature_type> signatures;
         packed_transaction::compression_type compression;
         from_variant(vo["signatures"], signatures);
         from_variant(vo["compression"], compression);

         bytes packed_cfd;
         std::vector<bytes> cfd;
         bool use_packed_cfd = false;
         if( vo.contains("packed_context_free_data") && vo["packed_context_free_data"].is_string() && !vo["packed_context_free_data"].as_string().empty() ) {
            from_variant(vo["packed_context_free_data"], packed_cfd );
            use_packed_cfd = true;
         } else if( vo.contains("context_free_data") ) {
            from_variant(vo["context_free_data"], cfd);
         }

         if( vo.contains("packed_trx") && vo["packed_trx"].is_string() && !vo["packed_trx"].as_string().empty() ) {
            bytes packed_trx;
            from_variant(vo["packed_trx"], packed_trx);
            if( use_packed_cfd ) {
               ptrx = packed_transaction( std::move( packed_trx ), std::move( signatures ), std::move( packed_cfd ), compression );
            } else {
               ptrx = packed_transaction( std::move( packed_trx ), std::move( signatures ), std::move( cfd ), compression );
            }
         } else {
            EOS_ASSERT(vo.contains("transaction"), packed_transaction_type_exception, "Missing transaction");
            if( use_packed_cfd ) {
               transaction trx;
               extract( vo["transaction"], trx, resolver, ctx );
               ptrx = packed_transaction( std::move(trx), std::move(signatures), std::move(packed_cfd), compression );
            } else {
               signed_transaction trx;
               extract( vo["transaction"], trx, resolver, ctx );
               trx.signatures = std::move( signatures );
               trx.context_free_data = std::move(cfd);
               ptrx = packed_transaction( std::move( trx ), compression );
            }
         }
      }
   };

   /**
    * Reflection visitor that uses a resolver to resolve ABIs for nested types
    * this will degrade to the common fc::from_variant as soon as the type no longer contains
    * ABI related info
    *
    * @tparam Reslover - callable with the signature (const name& code_account) -> std::optional<abi_def>
    */
   template<typename T, typename Resolver>
   class abi_from_variant_visitor : public reflector_init_visitor<T>
   {
      public:
         abi_from_variant_visitor( const variant_object& _vo, T& v, const Resolver& _resolver, abi_traverse_context& _ctx )
         : reflector_init_visitor<T>(v)
         ,_vo(_vo)
         ,_resolver(_resolver)
         ,_ctx(_ctx)
         {}

         /**
          * Visit a single member and extract it from the variant object
          * @tparam Member - the member to visit
          * @tparam Class - the class we are traversing
          * @tparam member - pointer to the member
          * @param name - the name of the member
          */
         template<typename Member, class Class, Member (Class::*member)>
         void operator()( const char* name )const
         {
            auto itr = _vo.find(name);
            if( itr != _vo.end() )
               abi_from_variant::extract( itr->value(), const_cast<std::remove_const_t<Member>&>(this->obj.*member), _resolver, _ctx );
         }

      private:
         const variant_object& _vo;
         const Resolver& _resolver;
         abi_traverse_context& _ctx;
   };

   template<typename M, typename Resolver, require_abi_t<M>>
   void abi_to_variant::add( mutable_variant_object &mvo, const char* name, const M& v, const Resolver& resolver, abi_traverse_context& ctx )
   {
      auto h = ctx.enter_scope();
      mutable_variant_object member_mvo;
      fc::reflector<M>::visit( impl::abi_to_variant_visitor<M, Resolver>( member_mvo, v, resolver, ctx) );
      mvo(name, std::move(member_mvo));
   }

   template<typename M, typename Resolver, require_abi_t<M>>
   void abi_from_variant::extract( const fc::variant& v, M& o, const Resolver& resolver, abi_traverse_context& ctx )
   {
      auto h = ctx.enter_scope();
      const variant_object& vo = v.get_object();
      fc::reflector<M>::visit( abi_from_variant_visitor<M, decltype(resolver)>( vo, o, resolver, ctx ) );
   }
} /// namespace eosio::chain::impl

template<typename T, typename Resolver>
void abi_serializer::to_variant( const T& o, fc::variant& vo, const Resolver& resolver, const yield_function_t& yield ) try {
   mutable_variant_object mvo;
   impl::abi_traverse_context ctx( yield, fc::microseconds{} );
   impl::abi_to_variant::add(mvo, "_", o, resolver, ctx);
   vo = std::move(mvo["_"]);
} FC_RETHROW_EXCEPTIONS(error, "Failed to serialize: ${type}", ("type", boost::core::demangle( typeid(o).name() ) ))

template<typename T, typename Resolver>
void abi_serializer::to_variant( const T& o, fc::variant& vo, const Resolver& resolver, const fc::microseconds& max_action_data_serialization_time ) try {
   mutable_variant_object mvo;
   impl::abi_traverse_context ctx( create_depth_yield_function(), max_action_data_serialization_time );
   impl::abi_to_variant::add(mvo, "_", o, resolver, ctx);
   vo = std::move(mvo["_"]);
} FC_RETHROW_EXCEPTIONS(error, "Failed to serialize: ${type}", ("type", boost::core::demangle( typeid(o).name() ) ))

template<typename T, typename Resolver>
void abi_serializer::to_log_variant( const T& o, fc::variant& vo, const Resolver& resolver, const yield_function_t& yield ) try {
    mutable_variant_object mvo;
    impl::abi_traverse_context ctx( yield, fc::microseconds{} );
    ctx.logging();
    impl::abi_to_variant::add(mvo, "_", o, resolver, ctx);
    vo = std::move(mvo["_"]);
} FC_RETHROW_EXCEPTIONS(error, "Failed to serialize: ${type}", ("type", boost::core::demangle( typeid(o).name() ) ))

template<typename T, typename Resolver>
void abi_serializer::to_log_variant( const T& o, fc::variant& vo, const Resolver& resolver, const fc::microseconds& max_action_data_serialization_time ) try {
   mutable_variant_object mvo;
   impl::abi_traverse_context ctx( create_depth_yield_function(), max_action_data_serialization_time );
   ctx.logging();
   impl::abi_to_variant::add(mvo, "_", o, resolver, ctx);
   vo = std::move(mvo["_"]);
} FC_RETHROW_EXCEPTIONS(error, "Failed to serialize: ${type}", ("type", boost::core::demangle( typeid(o).name() ) ))

template<typename T, typename Resolver>
void abi_serializer::from_variant( const fc::variant& v, T& o, const Resolver& resolver, const yield_function_t& yield ) try {
   impl::abi_traverse_context ctx( yield, fc::microseconds{} );
   impl::abi_from_variant::extract(v, o, resolver, ctx);
} FC_RETHROW_EXCEPTIONS(error, "Failed to deserialize variant", ("variant",v))

template<typename T, typename Resolver>
void abi_serializer::from_variant( const fc::variant& v, T& o, const Resolver& resolver, const fc::microseconds& max_action_data_serialization_time ) try {
   impl::abi_traverse_context ctx( create_depth_yield_function(), max_action_data_serialization_time );
   impl::abi_from_variant::extract(v, o, resolver, ctx);
} FC_RETHROW_EXCEPTIONS(error, "Failed to deserialize variant", ("variant",v))

using abi_serializer_cache_t = std::unordered_map<account_name, std::optional<abi_serializer>>;
using resolver_fn_t = std::function<std::optional<abi_serializer>(const account_name& name)>;
   
class abi_resolver {
public:
   explicit abi_resolver(abi_serializer_cache_t&& abi_serializers) :
      abi_serializers(std::move(abi_serializers))
   {}

   std::optional<std::reference_wrapper<const abi_serializer>> operator()(const account_name& account) const {
      auto it = abi_serializers.find(account);
      if (it != abi_serializers.end() && it->second)
         return std::reference_wrapper<const abi_serializer>(*it->second);
      return {};
   };

private:
   abi_serializer_cache_t abi_serializers;
};

class abi_serializer_cache_builder {
public:
   explicit abi_serializer_cache_builder(resolver_fn_t resolver) :
      resolver_(std::move(resolver))
   {
   }

   abi_serializer_cache_builder(const abi_serializer_cache_builder&) = delete;

   abi_serializer_cache_builder&& add_serializers(const chain::signed_block_ptr& block) && {
      for( const auto& receipt: block->transactions ) {
         if( std::holds_alternative<chain::packed_transaction>( receipt.trx ) ) {
            const auto& pt = std::get<chain::packed_transaction>( receipt.trx );
            const auto& t = pt.get_transaction();
            for( const auto& a: t.actions )
               add_to_cache( a );
            for( const auto& a: t.context_free_actions )
               add_to_cache( a );
         }
      }
      return std::move(*this);
   }

   abi_serializer_cache_builder&& add_serializers(const transaction_trace_ptr& trace_ptr) && {
      for( const auto& trace: trace_ptr->action_traces ) {
         add_to_cache(trace.act);
      }
      return std::move(*this);
   }

   abi_serializer_cache_t&& get() && {
      return std::move(abi_serializers);
   }

private:
   void add_to_cache(const chain::action& a) {
      auto it = abi_serializers.find( a.account );
      if( it == abi_serializers.end() ) {
         try {
            abi_serializers.emplace_hint( it, a.account, resolver_( a.account ) );
         } catch( ... ) {
            // keep behavior of not throwing on invalid abi, will result in hex data
         }
      }
   }

   resolver_fn_t resolver_;
   abi_serializer_cache_t abi_serializers;
};

/*
 * This is equivalent to a resolver, except that everytime the abi_serializer for an account 
 * is retrieved, it is stored in an unordered_map, so we won't waste time retrieving it again.
 * This is handy when parsing packed_transactions received in a fc::variant.
 */
class caching_resolver {
public:
   explicit caching_resolver(resolver_fn_t resolver) :
      resolver_(std::move(resolver))
   {
   }

   // make it non-copiable (we should only move it for performance reasons)
   caching_resolver(const caching_resolver&) = delete;
   caching_resolver& operator=(const caching_resolver&) = delete;

   std::optional<std::reference_wrapper<const abi_serializer>> operator()(const account_name& account) const {
      auto it = abi_serializers.find(account);
      if (it != abi_serializers.end()) {
         if (it->second)
            return *it->second;
         return {};
      }
      auto serializer = resolver_(account);
      auto& dest = abi_serializers[account]; // add entry regardless
      if (serializer) {
         // we got a serializer, so move it into the cache
         dest = abi_serializer_cache_t::mapped_type{std::move(*serializer)};
         return *dest; // and return a reference to it
      }
      return {}; 
   };

private:
   const resolver_fn_t resolver_;
   mutable abi_serializer_cache_t abi_serializers;
};
      

} // eosio::chain
