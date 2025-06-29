#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/asset.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/finality_extension.hpp>
#include <fc/io/raw.hpp>
#include <fc/io/varint.hpp>
#include <fc/time.hpp>

namespace eosio::chain {

   const size_t abi_serializer::max_recursion_depth;

   using std::string;
   using std::string_view;

   template <typename T>
   inline fc::variant variant_from_stream(fc::datastream<const char*>& stream) {
      T temp;
      fc::raw::unpack( stream, temp );
      return fc::variant(temp);
   }

   template <typename T>
   inline fc::variant variant_from_stream(fc::datastream<const char*>& stream, const abi_serializer::yield_function_t& yield) {
      T temp;
      fc::raw::unpack( stream, temp );
      yield(0);
      // create yield function matching fc::variant requirements, 0 for recursive depth
      return fc::variant( temp, [yield](){ yield(0); } );
   }

   template <typename T>
   auto pack_function() {
      return []( const fc::variant& var, fc::datastream<char*>& ds, bool is_array, bool is_optional, const abi_serializer::yield_function_t& yield ){
         if( is_array )
            fc::raw::pack( ds, var.as<vector<T>>() );
         else if ( is_optional )
            fc::raw::pack( ds, var.as<std::optional<T>>() );
         else
            fc::raw::pack( ds,  var.as<T>());
      };
   }

   template <typename T>
   auto pack_unpack() {
      return std::make_pair<abi_serializer::unpack_function, abi_serializer::pack_function>(
         []( fc::datastream<const char*>& stream, bool is_array, bool is_optional, const abi_serializer::yield_function_t& yield) {
            if( is_array )
               return variant_from_stream<vector<T>>(stream);
            else if ( is_optional )
               return variant_from_stream<std::optional<T>>(stream);
            return variant_from_stream<T>(stream);
         },
         pack_function<T>()
      );
   }

   template <typename T>
   auto pack_unpack_deadline() {
      return std::make_pair<abi_serializer::unpack_function, abi_serializer::pack_function>(
         []( fc::datastream<const char*>& stream, bool is_array, bool is_optional, const abi_serializer::yield_function_t& yield) {
            if( is_array )
               return variant_from_stream<vector<T>>(stream);
            else if ( is_optional )
               return variant_from_stream<std::optional<T>>(stream);
            return variant_from_stream<T>(stream, yield);
         },
         pack_function<T>()
      );
   }

   abi_serializer::abi_serializer( abi_def abi, const yield_function_t& yield ) {
      configure_built_in_types();
      set_abi(std::move(abi), yield);
   }

   abi_serializer::abi_serializer( const abi_def& abi, const fc::microseconds& max_serialization_time) {
      configure_built_in_types();
      set_abi(abi, create_yield_function(max_serialization_time));
   }

   void abi_serializer::add_specialized_unpack_pack( const string& name,
                                                     std::pair<abi_serializer::unpack_function, abi_serializer::pack_function> unpack_pack ) {
      built_in_types[name] = std::move( unpack_pack );
   }

   void abi_serializer::configure_built_in_types() {

      built_in_types.emplace("bool",                      pack_unpack<uint8_t>());
      built_in_types.emplace("int8",                      pack_unpack<int8_t>());
      built_in_types.emplace("uint8",                     pack_unpack<uint8_t>());
      built_in_types.emplace("int16",                     pack_unpack<int16_t>());
      built_in_types.emplace("uint16",                    pack_unpack<uint16_t>());
      built_in_types.emplace("int32",                     pack_unpack<int32_t>());
      built_in_types.emplace("uint32",                    pack_unpack<uint32_t>());
      built_in_types.emplace("int64",                     pack_unpack<int64_t>());
      built_in_types.emplace("uint64",                    pack_unpack<uint64_t>());
      built_in_types.emplace("int128",                    pack_unpack<int128_t>());
      built_in_types.emplace("uint128",                   pack_unpack<uint128_t>());
      built_in_types.emplace("varint32",                  pack_unpack<fc::signed_int>());
      built_in_types.emplace("varuint32",                 pack_unpack<fc::unsigned_int>());

      // TODO: Add proper support for floating point types. For now this is good enough.
      built_in_types.emplace("float32",                   pack_unpack<float>());
      built_in_types.emplace("float64",                   pack_unpack<double>());
      built_in_types.emplace("float128",                  pack_unpack<float128_t>());

      built_in_types.emplace("time_point",                pack_unpack<fc::time_point>());
      built_in_types.emplace("time_point_sec",            pack_unpack<fc::time_point_sec>());
      built_in_types.emplace("block_timestamp_type",      pack_unpack<block_timestamp_type>());

      built_in_types.emplace("name",                      pack_unpack<name>());

      built_in_types.emplace("bytes",                     pack_unpack<bytes>());
      built_in_types.emplace("string",                    pack_unpack<string>());

      built_in_types.emplace("checksum160",               pack_unpack<checksum160_type>());
      built_in_types.emplace("checksum256",               pack_unpack<checksum256_type>());
      built_in_types.emplace("checksum512",               pack_unpack<checksum512_type>());

      built_in_types.emplace("public_key",                pack_unpack_deadline<public_key_type>());
      built_in_types.emplace("signature",                 pack_unpack_deadline<signature_type>());

      built_in_types.emplace("symbol",                    pack_unpack<symbol>());
      built_in_types.emplace("symbol_code",               pack_unpack<symbol_code>());
      built_in_types.emplace("asset",                     pack_unpack<asset>());
      built_in_types.emplace("extended_asset",            pack_unpack<extended_asset>());
   }

   void abi_serializer::set_abi(abi_def abi, const yield_function_t& yield) {
      impl::abi_traverse_context ctx(yield, fc::microseconds{});

      EOS_ASSERT(abi.version.starts_with("eosio::abi/1."), unsupported_abi_version_exception, "ABI has an unsupported version");

      size_t types_size = abi.types.size();
      size_t structs_size = abi.structs.size();
      size_t actions_size = abi.actions.size();
      size_t tables_size = abi.tables.size();
      size_t error_messages_size = abi.error_messages.size();
      size_t variants_size = abi.variants.value.size();
      size_t action_results_size = abi.action_results.value.size();

      typedefs.clear();
      structs.clear();
      actions.clear();
      tables.clear();
      error_messages.clear();
      variants.clear();
      action_results.clear();

      for( auto& st : abi.structs )
         structs[st.name] = std::move(st);

      for( auto& td : abi.types ) {
         EOS_ASSERT(!_is_type(td.new_type_name, ctx), duplicate_abi_type_def_exception,
                    "type already exists", ("new_type_name",impl::limit_size(td.new_type_name)));
         typedefs[std::move(td.new_type_name)] = std::move(td.type);
      }

      for( auto& a : abi.actions )
         actions[std::move(a.name)] = std::move(a.type);

      for( auto& t : abi.tables )
         tables[std::move(t.name)] = std::move(t.type);

      for( auto& e : abi.error_messages )
         error_messages[std::move(e.error_code)] = std::move(e.error_msg);

      for( auto& v : abi.variants.value )
         variants[v.name] = std::move(v);

      for( auto& r : abi.action_results.value )
         action_results[std::move(r.name)] = std::move(r.result_type);

      /**
       *  The ABI vector may contain duplicates which would make it
       *  an invalid ABI
       */
      EOS_ASSERT( typedefs.size() == types_size, duplicate_abi_type_def_exception, "duplicate type definition detected" );
      EOS_ASSERT( structs.size() == structs_size, duplicate_abi_struct_def_exception, "duplicate struct definition detected" );
      EOS_ASSERT( actions.size() == actions_size, duplicate_abi_action_def_exception, "duplicate action definition detected" );
      EOS_ASSERT( tables.size() == tables_size, duplicate_abi_table_def_exception, "duplicate table definition detected" );
      EOS_ASSERT( error_messages.size() == error_messages_size, duplicate_abi_err_msg_def_exception, "duplicate error message definition detected" );
      EOS_ASSERT( variants.size() == variants_size, duplicate_abi_variant_def_exception, "duplicate variant definition detected" );
      EOS_ASSERT( action_results.size() == action_results_size, duplicate_abi_action_results_def_exception, "duplicate action results definition detected" );

      validate(ctx);
   }

   void abi_serializer::set_abi(const abi_def& abi, const fc::microseconds& max_serialization_time) {
      return set_abi(abi, create_yield_function(max_serialization_time));
   }

   bool abi_serializer::is_builtin_type(const std::string_view& type)const {
      return built_in_types.find(type) != built_in_types.end();
   }

   bool abi_serializer::is_integer(const std::string_view& type) const {
      return type.starts_with("uint") || type.starts_with("int");
   }

   int abi_serializer::get_integer_size(const std::string_view& type) const {
      EOS_ASSERT( is_integer(type), invalid_type_inside_abi, "${type} is not an integer type",
                  ("type",impl::limit_size(type)));
      if( type.starts_with("uint") ) {
         return boost::lexical_cast<int>(type.substr(4));
      } else {
         return boost::lexical_cast<int>(type.substr(3));
      }
   }

   bool abi_serializer::is_struct(const std::string_view& type)const {
      return structs.find(resolve_type(type)) != structs.end();
   }

   bool abi_serializer::is_array(const string_view& type)const {
      return type.ends_with("[]");
   }

   std::optional<fc::unsigned_int> abi_serializer::is_szarray(const string_view& type) const {
      auto pos1 = type.find_last_of('[');
      auto pos2 = type.find_last_of(']');
      if(pos1 == string_view::npos || pos2 != type.size() - 1)
         return {};
      auto pos = pos1 + 1;
      if(pos == pos2)
         return {};

      fc::unsigned_int sz = 0;
      while(pos < pos2) {
         if( ! (type[pos] >= '0' && type[pos] <= '9') )
            return {};
         sz = 10 * sz +  (type[pos] - '0');
         ++pos;
      }
      return  std::optional<fc::unsigned_int>{sz};
   }

   bool abi_serializer::is_optional(const string_view& type)const {
      return type.ends_with("?");
   }

   bool abi_serializer::is_type(const std::string_view& type, const yield_function_t& yield)const {
      impl::abi_traverse_context ctx(yield, fc::microseconds{});
      return _is_type(type, ctx);
   }

   bool abi_serializer::is_type(const std::string_view& type, const fc::microseconds& max_serialization_time) const {
      return is_type(type, create_yield_function(max_serialization_time));
   }

   std::string_view abi_serializer::fundamental_type(const std::string_view& type)const {
      if( is_array(type) ) {
         return type.substr(0, type.size()-2);
      } else if (is_szarray (type) ){
         return type.substr(0, type.find_last_of('['));
      } else if ( is_optional(type) ) {
         return type.substr(0, type.size()-1);
      } else {
       return type;
      }
   }

   std::string_view abi_serializer::_remove_bin_extension(const std::string_view& type) {
      if( type.ends_with("$") )
         return type.substr(0, type.size()-1);
      else
         return type;
   }

   bool abi_serializer::_is_type(const std::string_view& rtype, impl::abi_traverse_context& ctx )const {
      auto h = ctx.enter_scope();
      auto type = fundamental_type(rtype);
      if( built_in_types.find(type) != built_in_types.end() ) return true;
      if( typedefs.find(type) != typedefs.end() ) return _is_type(typedefs.find(type)->second, ctx);
      if( structs.find(type) != structs.end() ) return true;
      if( variants.find(type) != variants.end() ) return true;
      return false;
   }

   const struct_def& abi_serializer::get_struct(const std::string_view& type)const {
      auto itr = structs.find(resolve_type(type) );
      EOS_ASSERT( itr != structs.end(), invalid_type_inside_abi, "Unknown struct ${type}", ("type",impl::limit_size(type)) );
      return itr->second;
   }

   void abi_serializer::validate( impl::abi_traverse_context& ctx )const {
      for( const auto& t : typedefs ) { try {
         vector<std::string_view> types_seen{t.first, t.second};
         auto itr = typedefs.find(t.second);
         while( itr != typedefs.end() ) {
            ctx.check_deadline();
            EOS_ASSERT( find(types_seen.begin(), types_seen.end(), itr->second) == types_seen.end(), abi_circular_def_exception,
                        "Circular reference in type ${type}", ("type", impl::limit_size(t.first)) );
            types_seen.emplace_back(itr->second);
            itr = typedefs.find(itr->second);
         }
      } FC_CAPTURE_AND_RETHROW( (t) ) }
      for( const auto& t : typedefs ) { try {
         EOS_ASSERT(_is_type(t.second, ctx), invalid_type_inside_abi, "${type}", ("type",impl::limit_size(t.second)) );
      } FC_CAPTURE_AND_RETHROW( (t) ) }
      for( const auto& s : structs ) { try {
         if( s.second.base != type_name() ) {
            const struct_def* current = &s.second;
            vector<std::string_view> types_seen{current->name};
            while( current->base != type_name() ) {
               ctx.check_deadline();
               const struct_def& base = get_struct(current->base); //<-- force struct to inherit from another struct
               EOS_ASSERT( find(types_seen.begin(), types_seen.end(), base.name) == types_seen.end(), abi_circular_def_exception,
                           "Circular reference in struct ${type}", ("type",impl::limit_size(s.second.name)) );
               types_seen.emplace_back(base.name);
               current = &base;
            }
         }
         for( const auto& field : s.second.fields ) { try {
            ctx.check_deadline();
            EOS_ASSERT(_is_type(_remove_bin_extension(field.type), ctx), invalid_type_inside_abi,
                       "${type}", ("type",impl::limit_size(field.type)) );
         } FC_CAPTURE_AND_RETHROW( (field) ) }
      } FC_CAPTURE_AND_RETHROW( (s) ) }
      for( const auto& s : variants ) { try {
         for( const auto& type : s.second.types ) { try {
            ctx.check_deadline();
            EOS_ASSERT(_is_type(type, ctx), invalid_type_inside_abi, "${type}", ("type",impl::limit_size(type)) );
         } FC_CAPTURE_AND_RETHROW( (type) ) }
      } FC_CAPTURE_AND_RETHROW( (s) ) }
      for( const auto& a : actions ) { try {
        ctx.check_deadline();
        EOS_ASSERT(_is_type(a.second, ctx), invalid_type_inside_abi, "${type}", ("type",impl::limit_size(a.second)) );
      } FC_CAPTURE_AND_RETHROW( (a)  ) }

      for( const auto& t : tables ) { try {
        ctx.check_deadline();
        EOS_ASSERT(_is_type(t.second, ctx), invalid_type_inside_abi, "${type}", ("type",impl::limit_size(t.second)) );
      } FC_CAPTURE_AND_RETHROW( (t)  ) }

      for( const auto& r : action_results ) { try {
        ctx.check_deadline();
        EOS_ASSERT(_is_type(r.second, ctx), invalid_type_inside_abi, "${type}", ("type",impl::limit_size(r.second)) );
      } FC_CAPTURE_AND_RETHROW( (r)  ) }
   }

   std::string_view abi_serializer::resolve_type(const std::string_view& type)const {
      auto itr = typedefs.find(type);
      if( itr != typedefs.end() ) {
         for( auto i = typedefs.size(); i > 0; --i ) { // avoid infinite recursion
            const std::string_view& t = itr->second;
            itr = typedefs.find( t );
            if( itr == typedefs.end() ) return t;
         }
      }
      return type;
   }

   void abi_serializer::_binary_to_variant( const std::string_view& type, fc::datastream<const char *>& stream,
                                            fc::mutable_variant_object& obj, impl::binary_to_variant_context& ctx )const
   {
      auto h = ctx.enter_scope();
      auto s_itr = structs.find(type);
      EOS_ASSERT( s_itr != structs.end(), invalid_type_inside_abi, "Unknown type ${type}", ("type",ctx.maybe_shorten(type)) );
      ctx.hint_struct_type_if_in_array( s_itr );
      const auto& st = s_itr->second;
      if( st.base != type_name() ) {
         _binary_to_variant(resolve_type(st.base), stream, obj, ctx);
      }
      bool encountered_extension = false;
      for( uint32_t i = 0; i < st.fields.size(); ++i ) {
         const auto& field = st.fields[i];
         bool extension = field.type.ends_with("$");
         encountered_extension |= extension;
         if( !stream.remaining() ) {
            if( extension ) {
               continue;
            }
            if( encountered_extension ) {
               EOS_THROW( abi_exception, "Encountered field '${f}' without binary extension designation while processing struct '${p}'",
                          ("f", ctx.maybe_shorten(field.name))("p", ctx.get_path_string()) );
            }
            EOS_THROW( unpack_exception, "Stream unexpectedly ended; unable to unpack field '${f}' of struct '${p}'",
                       ("f", ctx.maybe_shorten(field.name))("p", ctx.get_path_string()) );

         }
         auto h1 = ctx.push_to_path( impl::field_path_item{ .parent_struct_itr = s_itr, .field_ordinal = i } );
         auto field_type = resolve_type( extension ? _remove_bin_extension(field.type) : field.type );
         auto v = _binary_to_variant(field_type, stream, ctx);
         if( ctx.is_logging() && v.is_string() && field_type == "bytes" ) {
            fc::mutable_variant_object sub_obj;
            auto size = v.get_string().size() / 2; // half because it is in hex
            sub_obj( "size", size );
            if( size > impl::hex_log_max_size ) {
               sub_obj( "trimmed_hex", v.get_string().substr( 0, impl::hex_log_max_size*2 ) );
            } else {
               sub_obj( "hex", std::move( v ) );
            }
            obj( field.name, std::move(sub_obj) );
         } else {
            obj( field.name, std::move(v) );
         }
      }
   }

   fc::variant abi_serializer::_binary_to_variant( const std::string_view& type, fc::datastream<const char *>& stream,
                                                   impl::binary_to_variant_context& ctx )const
   {
      auto h = ctx.enter_scope();
      auto rtype = resolve_type(type);
      auto ftype = fundamental_type(rtype);
      auto fixed_array_sz = is_szarray(rtype);

      auto read_array = [&](fc::unsigned_int::base_uint sz) {
         ctx.hint_array_type_if_in_array();
         fc::variants vars;
         vars.reserve(std::min(sz, 1024u)); // limit the maximum size that can be reserved before data is read
         auto h1 = ctx.push_to_path( impl::array_index_path_item{} );
         for( fc::unsigned_int::base_uint i = 0; i < sz; ++i ) {
            ctx.set_array_index_of_path_back(i);
            auto v = _binary_to_variant(ftype, stream, ctx);
            // The exception below is commented out to allow array of optional as input data
            //EOS_ASSERT( !v.is_null(), unpack_exception, "Invalid packed array '${p}'", ("p", ctx.get_path_string()) );
            vars.emplace_back(std::move(v));
         }
         return fc::variant(std::move(vars));
      };

      if (fixed_array_sz) {
         return read_array(*fixed_array_sz);
      } else if( auto btype = built_in_types.find(ftype ); btype != built_in_types.end() ) {
         try {
            return btype->second.first(stream, is_array(rtype), is_optional(rtype), ctx.get_yield_function());
         } EOS_RETHROW_EXCEPTIONS( unpack_exception, "Unable to unpack ${class} type '${type}' while processing '${p}'",
                                   ("class", is_array(rtype) ? "array of built-in" : is_optional(rtype) ? "optional of built-in" : "built-in")
                                   ("type", impl::limit_size(ftype))("p", ctx.get_path_string()) )
      }
      if ( is_array(rtype) ) {
         fc::unsigned_int size;
         try {
            fc::raw::unpack(stream, size);
         } EOS_RETHROW_EXCEPTIONS( unpack_exception, "Unable to unpack size of array '${p}'", ("p", ctx.get_path_string()) )
         return read_array(size.value);
      } else if ( is_optional(rtype) ) {
         char flag;
         try {
            fc::raw::unpack(stream, flag);
         } EOS_RETHROW_EXCEPTIONS( unpack_exception, "Unable to unpack presence flag of optional '${p}'", ("p", ctx.get_path_string()) )
         return flag ? _binary_to_variant(ftype, stream, ctx) : fc::variant();
      } else {
         auto v_itr = variants.find(rtype);
         if( v_itr != variants.end() ) {
            ctx.hint_variant_type_if_in_array( v_itr );
            fc::unsigned_int select;
            try {
               fc::raw::unpack(stream, select);
            } EOS_RETHROW_EXCEPTIONS( unpack_exception, "Unable to unpack tag of variant '${p}'", ("p", ctx.get_path_string()) )
            EOS_ASSERT( (size_t)select < v_itr->second.types.size(), unpack_exception,
                        "Unpacked invalid tag (${select}) for variant '${p}'", ("select", select.value)("p",ctx.get_path_string()) );
            auto h1 = ctx.push_to_path( impl::variant_path_item{ .variant_itr = v_itr, .variant_ordinal = static_cast<uint32_t>(select) } );
            return fc::variants{v_itr->second.types[select], _binary_to_variant(v_itr->second.types[select], stream, ctx)};
         }
      }

      fc::mutable_variant_object mvo;
      _binary_to_variant(rtype, stream, mvo, ctx);
      // QUESTION: Is this assert actually desired? It disallows unpacking empty structs from datastream.
      EOS_ASSERT( mvo.size() > 0, unpack_exception, "Unable to unpack '${p}' from stream", ("p", ctx.get_path_string()) );
      return fc::variant( std::move(mvo) );
   }

   fc::variant abi_serializer::_binary_to_variant( const std::string_view& type, const bytes& binary, impl::binary_to_variant_context& ctx )const
   {
      auto h = ctx.enter_scope();
      fc::datastream<const char*> ds( binary.data(), binary.size() );
      return _binary_to_variant(type, ds, ctx);
   }

   fc::variant abi_serializer::binary_to_variant( const std::string_view& type, const bytes& binary, const yield_function_t& yield, bool short_path )const {
      impl::binary_to_variant_context ctx(*this, yield, fc::microseconds{}, type);
      ctx.short_path = short_path;
      return _binary_to_variant(type, binary, ctx);
   }

   fc::variant abi_serializer::binary_to_variant( const std::string_view& type, const bytes& binary, const fc::microseconds& max_action_data_serialization_time, bool short_path )const {
      impl::binary_to_variant_context ctx(*this, create_depth_yield_function(), max_action_data_serialization_time, type);
      ctx.short_path = short_path;
      return _binary_to_variant(type, binary, ctx);
   }

   fc::variant abi_serializer::binary_to_variant( const std::string_view& type, fc::datastream<const char*>& binary, const yield_function_t& yield, bool short_path )const {
      impl::binary_to_variant_context ctx(*this, yield, fc::microseconds{}, type);
      ctx.short_path = short_path;
      return _binary_to_variant(type, binary, ctx);
   }

   fc::variant abi_serializer::binary_to_variant( const std::string_view& type, fc::datastream<const char*>& binary, const fc::microseconds& max_action_data_serialization_time, bool short_path )const {
      impl::binary_to_variant_context ctx(*this, create_depth_yield_function(), max_action_data_serialization_time, type);
      ctx.short_path = short_path;
      return _binary_to_variant(type, binary, ctx);
   }

   void abi_serializer::_variant_to_binary( const std::string_view& type, const fc::variant& var, fc::datastream<char *>& ds, impl::variant_to_binary_context& ctx )const
   { try {
      auto h = ctx.enter_scope();
      auto rtype = resolve_type(type);

      auto v_itr = variants.end();
      auto s_itr = structs.end();
      auto fixed_array_sz = is_szarray(rtype);

      auto pack_array = [&](const vector<fc::variant>& vars) {
         auto h1 = ctx.push_to_path(impl::array_index_path_item{});
         auto h2 = ctx.disallow_extensions_unless(false);

         int64_t i = 0;
         for (const auto& var : vars) {
            ctx.set_array_index_of_path_back(i);
            _variant_to_binary(fundamental_type(rtype), var, ds, ctx);
            ++i;
         }
      };
      if (fixed_array_sz) {
         size_t sz = *fixed_array_sz;
         ctx.hint_array_type_if_in_array();
         const vector<fc::variant>& vars = var.get_array();
         EOS_ASSERT( vars.size() == sz, pack_exception,
                     "Incorrect number of values provided (${a}) for fixed-size (${b}) array type", ("a", sz)("b", vars.size()));
         pack_array(vars);
      } else if( auto btype = built_in_types.find(fundamental_type(rtype)); btype != built_in_types.end() ) {
         btype->second.second(var, ds, is_array(rtype), is_optional(rtype), ctx.get_yield_function());
      } else if ( is_array(rtype) ) {
         ctx.hint_array_type_if_in_array();
         const vector<fc::variant>& vars = var.get_array();
         fc::raw::pack(ds, (fc::unsigned_int)vars.size());
         pack_array(vars);
      } else if( is_optional(rtype) ) {
         char flag = !var.is_null();
         fc::raw::pack(ds, flag);
         if( flag ) {
            _variant_to_binary(fundamental_type(rtype), var, ds, ctx);
         }
      } else if( (v_itr = variants.find(rtype)) != variants.end() ) {
         ctx.hint_variant_type_if_in_array( v_itr );
         auto& v = v_itr->second;
         EOS_ASSERT( var.is_array() && var.size() == 2, pack_exception,
                    "Expected input to be an array of two items while processing variant '${p}'", ("p", ctx.get_path_string()) );
         EOS_ASSERT( var[size_t(0)].is_string(), pack_exception,
                    "Encountered non-string as first item of input array while processing variant '${p}'", ("p", ctx.get_path_string()) );
         auto variant_type_str = var[size_t(0)].get_string();
         auto it = find(v.types.begin(), v.types.end(), variant_type_str);
         EOS_ASSERT( it != v.types.end(), pack_exception,
                     "Specified type '${t}' in input array is not valid within the variant '${p}'",
                     ("t", ctx.maybe_shorten(variant_type_str))("p", ctx.get_path_string()) );
         fc::raw::pack(ds, fc::unsigned_int(it - v.types.begin()));
         auto h1 = ctx.push_to_path( impl::variant_path_item{ .variant_itr = v_itr, .variant_ordinal = static_cast<uint32_t>(it - v.types.begin()) } );
         _variant_to_binary( *it, var[size_t(1)], ds, ctx );
      } else if( (s_itr = structs.find(rtype)) != structs.end() ) {
         ctx.hint_struct_type_if_in_array( s_itr );
         const auto& st = s_itr->second;

         if( var.is_object() ) {
            const auto& vo = var.get_object();

            if( st.base != type_name() ) {
               auto h2 = ctx.disallow_extensions_unless(false);
               _variant_to_binary(resolve_type(st.base), var, ds, ctx);
            }
            bool disallow_additional_fields = false;
            for( uint32_t i = 0; i < st.fields.size(); ++i ) {
               const auto& field = st.fields[i];
               bool present = vo.contains(string(field.name).c_str());
               if( present || is_optional(field.type) ) {
                  if( disallow_additional_fields )
                     EOS_THROW( pack_exception, "Unexpected field '${f}' found in input object while processing struct '${p}'",
                                ("f", ctx.maybe_shorten(field.name))("p", ctx.get_path_string()) );
                  {
                     auto h1 = ctx.push_to_path( impl::field_path_item{ .parent_struct_itr = s_itr, .field_ordinal = i } );
                     auto h2 = ctx.disallow_extensions_unless( &field == &st.fields.back() );
                     _variant_to_binary(_remove_bin_extension(field.type), present ? vo[field.name] : fc::variant(nullptr), ds, ctx);
                  }
               } else if( field.type.ends_with("$") && ctx.extensions_allowed() ) {
                  disallow_additional_fields = true;
               } else if( disallow_additional_fields ) {
                  EOS_THROW( abi_exception, "Encountered field '${f}' without binary extension designation while processing struct '${p}'",
                             ("f", ctx.maybe_shorten(field.name))("p", ctx.get_path_string()) );
               } else {
                  EOS_THROW( pack_exception, "Missing field '${f}' in input object while processing struct '${p}'",
                             ("f", ctx.maybe_shorten(field.name))("p", ctx.get_path_string()) );
               }
            }
         } else if( var.is_array() ) {
            const auto& va = var.get_array();
            EOS_ASSERT( st.base == type_name(), invalid_type_inside_abi,
                        "Using input array to specify the fields of the derived struct '${p}'; input arrays are currently only allowed for structs without a base",
                        ("p",ctx.get_path_string()) );
            for( uint32_t i = 0; i < st.fields.size(); ++i ) {
               const auto& field = st.fields[i];
               if( va.size() > i ) {
                  auto h1 = ctx.push_to_path( impl::field_path_item{ .parent_struct_itr = s_itr, .field_ordinal = i } );
                  auto h2 = ctx.disallow_extensions_unless( &field == &st.fields.back() );
                  _variant_to_binary(_remove_bin_extension(field.type), va[i], ds, ctx);
               } else if( field.type.ends_with("$") && ctx.extensions_allowed() ) {
                  break;
               } else {
                  EOS_THROW( pack_exception, "Early end to input array specifying the fields of struct '${p}'; require input for field '${f}'",
                             ("p", ctx.get_path_string())("f", ctx.maybe_shorten(field.name)) );
               }
            }
         } else {
            EOS_THROW( pack_exception, "Unexpected input encountered while processing struct '${p}'", ("p",ctx.get_path_string()) );
         }
      } else {
         EOS_THROW( invalid_type_inside_abi, "Unknown type ${type}", ("type",ctx.maybe_shorten(type)) );
      }
   } FC_CAPTURE_AND_RETHROW() }

   bytes abi_serializer::_variant_to_binary( const std::string_view& type, const fc::variant& var, impl::variant_to_binary_context& ctx )const
   { try {
      auto h = ctx.enter_scope();
      if( !_is_type(type, ctx) ) {
         return var.as<bytes>();
      }

      bytes temp( 1024*1024 );
      fc::datastream<char*> ds(temp.data(), temp.size() );
      _variant_to_binary(type, var, ds, ctx);
      temp.resize(ds.tellp());
      return temp;
   } FC_CAPTURE_AND_RETHROW() }

   bytes abi_serializer::variant_to_binary( const std::string_view& type, const fc::variant& var, const yield_function_t& yield, bool short_path )const {
      impl::variant_to_binary_context ctx(*this, yield, fc::microseconds{}, type);
      ctx.short_path = short_path;
      return _variant_to_binary(type, var, ctx);
   }

   bytes abi_serializer::variant_to_binary( const std::string_view& type, const fc::variant& var, const fc::microseconds& max_action_data_serialization_time, bool short_path ) const {
      impl::variant_to_binary_context ctx(*this, create_depth_yield_function(), max_action_data_serialization_time, type);
      ctx.short_path = short_path;
      return _variant_to_binary(type, var, ctx);
   }

   void  abi_serializer::variant_to_binary( const std::string_view& type, const fc::variant& var, fc::datastream<char*>& ds, const yield_function_t& yield, bool short_path )const {
      impl::variant_to_binary_context ctx(*this, yield, fc::microseconds{}, type);
      ctx.short_path = short_path;
      _variant_to_binary(type, var, ds, ctx);
   }

   void  abi_serializer::variant_to_binary( const std::string_view& type, const fc::variant& var, fc::datastream<char*>& ds, const fc::microseconds& max_action_data_serialization_time, bool short_path ) const {
      impl::variant_to_binary_context ctx(*this, create_depth_yield_function(), max_action_data_serialization_time, type);
      ctx.short_path = short_path;
      _variant_to_binary(type, var, ds, ctx);
   }

   void impl::abi_to_variant::add_block_header_finality_extension( mutable_variant_object& mvo, const header_extension_multimap& header_exts ) {
      if (auto it = header_exts.find(finality_extension::extension_id()); it != header_exts.end()) {
         const auto& f_ext = std::get<finality_extension>(it->second);
         mvo("finality_extension", f_ext);
      }
   }

   type_name abi_serializer::get_action_type(name action)const {
      auto itr = actions.find(action);
      if( itr != actions.end() ) return itr->second;
      return type_name();
   }
   type_name abi_serializer::get_table_type(name action)const {
      auto itr = tables.find(action);
      if( itr != tables.end() ) return itr->second;
      return type_name();
   }

   type_name abi_serializer::get_action_result_type(name action_result)const {
      auto itr = action_results.find(action_result);
      if( itr != action_results.end() ) return itr->second;
      return type_name();
   }

   std::optional<string> abi_serializer::get_error_message( uint64_t error_code )const {
      auto itr = error_messages.find( error_code );
      if( itr == error_messages.end() )
         return std::optional<string>();

      return itr->second;
   }

   namespace impl {

      fc::scoped_exit<std::function<void()>> abi_traverse_context::enter_scope() {
         std::function<void()> callback = [this](){ --recursion_depth; };

         ++recursion_depth;
         yield( recursion_depth );

         return {std::move(callback)};
      }

      void abi_traverse_context_with_path::set_path_root( const std::string_view& type ) {
         auto rtype = abis.resolve_type(type);

         if( abis.is_array(rtype) ) {
            root_of_path =  array_type_path_root{};
         } else {
            auto itr1 = abis.structs.find(rtype);
            if( itr1 != abis.structs.end() ) {
               root_of_path = struct_type_path_root{ .struct_itr = itr1 };
            } else {
               auto itr2 = abis.variants.find(rtype);
               if( itr2 != abis.variants.end() ) {
                  root_of_path = variant_type_path_root{ .variant_itr = itr2 };
               }
            }
         }
      }

      fc::scoped_exit<std::function<void()>> abi_traverse_context_with_path::push_to_path( const path_item& item ) {
         std::function<void()> callback = [this](){
            EOS_ASSERT( path.size() > 0, abi_exception,
                        "invariant failure in variant_to_binary_context: path is empty on scope exit" );
            path.pop_back();
         };

         path.push_back( item );

         return {std::move(callback)};
      }

      void abi_traverse_context_with_path::set_array_index_of_path_back( uint32_t i ) {
         EOS_ASSERT( path.size() > 0, abi_exception, "path is empty" );

         auto& b = path.back();

         EOS_ASSERT( std::holds_alternative<array_index_path_item>(b), abi_exception, "trying to set array index without first pushing new array index item" );

         std::get<array_index_path_item>(b).array_index = i;
      }

      void abi_traverse_context_with_path::hint_array_type_if_in_array() {
         if( path.size() == 0 || !std::holds_alternative<array_index_path_item>(path.back()) )
            return;

         std::get<array_index_path_item>(path.back()).type_hint = array_type_path_root{};
      }

      void abi_traverse_context_with_path::hint_struct_type_if_in_array( const map<type_name, struct_def>::const_iterator& itr ) {
         if( path.size() == 0 || !std::holds_alternative<array_index_path_item>(path.back()) )
            return;

         std::get<array_index_path_item>(path.back()).type_hint = struct_type_path_root{ .struct_itr = itr };
      }

      void abi_traverse_context_with_path::hint_variant_type_if_in_array( const map<type_name, variant_def>::const_iterator& itr ) {
         if( path.size() == 0 || !std::holds_alternative<array_index_path_item>(path.back()) )
            return;

         std::get<array_index_path_item>(path.back()).type_hint = variant_type_path_root{ .variant_itr = itr };
      }

      constexpr size_t const_strlen( const char* str )
      {
          return (*str == 0) ? 0 : const_strlen(str + 1) + 1;
      }

      void output_name( std::ostream& s, const string_view& str, bool shorten, size_t max_length = 64 ) {
         constexpr size_t min_num_characters_at_ends = 4;
         constexpr size_t preferred_num_tail_end_characters = 6;
         constexpr const char* fill_in = "...";

         static_assert( min_num_characters_at_ends <= preferred_num_tail_end_characters,
                        "preferred number of tail end characters cannot be less than the imposed absolute minimum" );

         constexpr size_t fill_in_length = const_strlen( fill_in );
         constexpr size_t min_length = fill_in_length + 2*min_num_characters_at_ends;
         constexpr size_t preferred_min_length = fill_in_length + 2*preferred_num_tail_end_characters;

         max_length = std::max( max_length, min_length );

         if( !shorten || str.size() <= max_length ) {
            s << str;
            return;
         }

         size_t actual_num_tail_end_characters = preferred_num_tail_end_characters;
         if( max_length < preferred_min_length ) {
            actual_num_tail_end_characters = min_num_characters_at_ends + (max_length - min_length)/2;
         }

         s.write( str.data(), max_length - fill_in_length - actual_num_tail_end_characters );
         s.write( fill_in, fill_in_length );
         s.write( str.data() + (str.size() - actual_num_tail_end_characters), actual_num_tail_end_characters );
      }

      struct generate_path_string_visitor {
         using result_type = void;

         generate_path_string_visitor( bool shorten_names, bool track_only )
         : shorten_names(shorten_names), track_only( track_only )
         {}

         std::stringstream s;
         bool              shorten_names = false;
         bool              track_only     = false;
         path_item         last_path_item;

         void add_dot() {
            s << ".";
         }

         void operator()( const empty_path_item& item ) {
         }

         void operator()( const array_index_path_item& item ) {
            if( track_only ) {
               last_path_item = item;
               return;
            }

            s << "[" << item.array_index << "]";
         }

         void operator()( const field_path_item& item ) {
            if( track_only ) {
               last_path_item = item;
               return;
            }

            const auto& str = item.parent_struct_itr->second.fields.at(item.field_ordinal).name;
            output_name( s, str, shorten_names );
         }

         void operator()( const variant_path_item& item ) {
            if( track_only ) {
               last_path_item = item;
               return;
            }

            s << "<variant(" << item.variant_ordinal << ")=";
            const auto& str = item.variant_itr->second.types.at(item.variant_ordinal);
            output_name( s, str, shorten_names );
            s << ">";
         }

         void operator()( const empty_path_root& item ) {
         }

         void operator()( const array_type_path_root& item ) {
            s << "ARRAY";
         }

         void operator()( const struct_type_path_root& item ) {
            const auto& str = item.struct_itr->first;
            output_name( s, str, shorten_names );
         }

         void operator()( const variant_type_path_root& item ) {
            const auto& str = item.variant_itr->first;
            output_name( s, str, shorten_names );
         }
      };

      struct path_item_type_visitor {
         using result_type = void;

         path_item_type_visitor( std::stringstream& s, bool shorten_names )
         : s(s), shorten_names(shorten_names)
         {}

         std::stringstream& s;
         bool               shorten_names = false;

         void operator()( const empty_path_item& item ) {
         }

         void operator()( const array_index_path_item& item ) {
            const auto& th = item.type_hint;
            if( std::holds_alternative<struct_type_path_root>(th) ) {
               const auto& str = std::get<struct_type_path_root>(th).struct_itr->first;
               output_name( s, str, shorten_names );
            } else if( std::holds_alternative<variant_type_path_root>(th) ) {
               const auto& str = std::get<variant_type_path_root>(th).variant_itr->first;
               output_name( s, str, shorten_names );
            } else if( std::holds_alternative<array_type_path_root>(th) ) {
               s << "ARRAY";
            } else {
               s << "UNKNOWN";
            }
         }

         void operator()( const field_path_item& item ) {
            const auto& str = item.parent_struct_itr->second.fields.at(item.field_ordinal).type;
            output_name( s, str, shorten_names );
         }

         void operator()( const variant_path_item& item ) {
            const auto& str = item.variant_itr->second.types.at(item.variant_ordinal);
            output_name( s, str, shorten_names );
         }
      };

      string abi_traverse_context_with_path::get_path_string()const {
         bool full_path = !short_path;
         bool shorten_names = short_path;

         generate_path_string_visitor visitor(shorten_names, !full_path);
         if( full_path )
            std::visit( visitor, root_of_path );
         for( size_t i = 0, n = path.size(); i < n; ++i ) {
            if( full_path && !std::holds_alternative<array_index_path_item>(path[i]) )
               visitor.add_dot();

            std::visit( visitor, path[i] );

         }

         if( !full_path ) {
            if( std::holds_alternative<empty_path_item>(visitor.last_path_item) ) {
               std::visit( visitor, root_of_path );
            } else {
               path_item_type_visitor vis2(visitor.s, shorten_names);
               std::visit(vis2, visitor.last_path_item);
            }
         }

         return visitor.s.str();
      }

      string abi_traverse_context_with_path::maybe_shorten( const std::string_view& str ) {
         if( !short_path )
            return std::string(str);

         std::stringstream s;
         output_name( s, str, true );
         return s.str();
      }

      string limit_size( const std::string_view& str ) {
         std::stringstream s;
         output_name( s, str, false );
         return s.str();
      }

      fc::scoped_exit<std::function<void()>> variant_to_binary_context::disallow_extensions_unless( bool condition ) {
         std::function<void()> callback = [old_allow_extensions=allow_extensions, this](){
            allow_extensions = old_allow_extensions;
         };

         if( !condition ) {
            allow_extensions = false;
         }

         return {std::move(callback)};
      }
   }

}
