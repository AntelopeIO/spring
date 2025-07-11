#include <fc/variant.hpp>
#include <fc/variant_object.hpp>
#include <fc/exception/exception.hpp>
#include <string.h>
#include <fc/crypto/base64.hpp>
#include <fc/crypto/hex.hpp>
#include <boost/scoped_array.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/io/json.hpp>
#include <fc/utf8.hpp>
#include <algorithm>

namespace fc
{
/**
 *  The TypeID is stored in the 'last byte' of the variant.
 */
void set_variant_type( variant* v, variant::type_id t)
{
   char* data = reinterpret_cast<char*>(v);
   data[ sizeof(variant) -1 ] = t;
}

variant::variant()
{
   set_variant_type( this, null_type );
}

variant::variant( fc::nullptr_t )
{
   set_variant_type( this, null_type );
}

variant::variant( uint8_t val )
{
   *reinterpret_cast<uint64_t*>(this)  = val;
   set_variant_type( this, uint64_type );
}

variant::variant( int8_t val )
{
   *reinterpret_cast<int64_t*>(this)  = val;
   set_variant_type( this, int64_type );
}

variant::variant( uint16_t val )
{
   *reinterpret_cast<uint64_t*>(this)  = val;
   set_variant_type( this, uint64_type );
}

variant::variant( int16_t val )
{
   *reinterpret_cast<int64_t*>(this)  = val;
   set_variant_type( this, int64_type );
}

variant::variant( uint32_t val )
{
   *reinterpret_cast<uint64_t*>(this)  = val;
   set_variant_type( this, uint64_type );
}

variant::variant( int32_t val )
{
   *reinterpret_cast<int64_t*>(this)  = val;
   set_variant_type( this, int64_type );
}

variant::variant( uint64_t val )
{
   *reinterpret_cast<uint64_t*>(this)  = val;
   set_variant_type( this, uint64_type );
}

variant::variant( int64_t val )
{
   *reinterpret_cast<int64_t*>(this)  = val;
   set_variant_type( this, int64_type );
}

variant::variant( float val )
{
   *reinterpret_cast<double*>(this)  = val;
   set_variant_type( this, double_type );
}

variant::variant( double val )
{
   *reinterpret_cast<double*>(this)  = val;
   set_variant_type( this, double_type );
}

variant::variant( bool val )
{
   *reinterpret_cast<bool*>(this)  = val;
   set_variant_type( this, bool_type );
}

variant::variant( char* str )
{
   *reinterpret_cast<std::string**>(this)  = new std::string( str );
   set_variant_type( this, string_type );
}

variant::variant( const char* str )
{
   *reinterpret_cast<std::string**>(this)  = new std::string( str );
   set_variant_type( this, string_type );
}

// TODO: do a proper conversion to utf8
variant::variant( wchar_t* str )
{
   size_t len = wcslen(str);
   boost::scoped_array<char> buffer(new char[len]);
   for (unsigned i = 0; i < len; ++i)
     buffer[i] = (char)str[i];
   *reinterpret_cast<std::string**>(this)  = new std::string(buffer.get(), len);
   set_variant_type( this, string_type );
}

// TODO: do a proper conversion to utf8
variant::variant( const wchar_t* str )
{
   size_t len = wcslen(str);
   boost::scoped_array<char> buffer(new char[len]);
   for (unsigned i = 0; i < len; ++i)
     buffer[i] = (char)str[i];
   *reinterpret_cast<std::string**>(this)  = new std::string(buffer.get(), len);
   set_variant_type( this, string_type );
}

variant::variant( std::string val )
{
   *reinterpret_cast<std::string**>(this)  = new std::string( std::move(val) );
   set_variant_type( this, string_type );
}
variant::variant( blob val )
{
   *reinterpret_cast<blob**>(this)  = new blob( std::move(val) );
   set_variant_type( this, blob_type );
}

variant::variant( variant_object obj)
{
   *reinterpret_cast<variant_object**>(this)  = new variant_object(std::move(obj));
   set_variant_type(this,  object_type );
}
variant::variant( mutable_variant_object obj)
{
   *reinterpret_cast<variant_object**>(this)  = new variant_object(std::move(obj));
   set_variant_type(this,  object_type );
}

variant::variant( variants arr )
{
   *reinterpret_cast<variants**>(this)  = new variants(std::move(arr));
   set_variant_type(this,  array_type );
}


typedef const variant_object* const_variant_object_ptr;
typedef const variants* const_variants_ptr;
typedef const blob*   const_blob_ptr;
typedef const std::string* const_string_ptr;

void variant::clear()
{
   switch( get_type() )
   {
     case object_type:
        delete *reinterpret_cast<variant_object**>(this);
        break;
     case array_type:
        delete *reinterpret_cast<variants**>(this);
        break;
     case string_type:
        delete *reinterpret_cast<std::string**>(this);
        break;
     case blob_type:
        delete *reinterpret_cast<blob**>(this);
        break;
     default:
        break;
   }
   set_variant_type( this, null_type );
}

variant::variant( const variant& v )
{
   switch( v.get_type() )
   {
       case object_type:
          *reinterpret_cast<variant_object**>(this)  =
             new variant_object(**reinterpret_cast<const const_variant_object_ptr*>(&v));
          set_variant_type( this, object_type );
          return;
       case array_type:
          *reinterpret_cast<variants**>(this)  =
             new variants(**reinterpret_cast<const const_variants_ptr*>(&v));
          set_variant_type( this,  array_type );
          return;
       case string_type:
          *reinterpret_cast<std::string**>(this)  =
             new std::string(**reinterpret_cast<const const_string_ptr*>(&v) );
          set_variant_type( this, string_type );
          return;
       case blob_type:
          *reinterpret_cast<blob**>(this)  =
             new blob(**reinterpret_cast<const const_blob_ptr*>(&v) );
          set_variant_type( this, blob_type );
          return;
       default:
          _data = v._data;
   }
}

variant::variant( variant&& v )
{
   _data = v._data;
   set_variant_type( &v, null_type );
}

variant::~variant()
{
   clear();
}

variant& variant::operator=( variant&& v )
{
   if( this == &v ) return *this;
   clear();
   _data = v._data;
   set_variant_type( &v, null_type );
   return *this;
}

variant& variant::operator=( const variant& v )
{
   if( this == &v )
      return *this;

   clear();
   switch( v.get_type() )
   {
      case object_type:
         *reinterpret_cast<variant_object**>(this)  =
            new variant_object((**reinterpret_cast<const const_variant_object_ptr*>(&v)));
         break;
      case array_type:
         *reinterpret_cast<variants**>(this)  =
            new variants((**reinterpret_cast<const const_variants_ptr*>(&v)));
         break;
      case string_type:
         *reinterpret_cast<std::string**>(this)  = new std::string((**reinterpret_cast<const const_string_ptr*>(&v)) );
         break;
      case blob_type:
         *reinterpret_cast<blob**>(this)  = new blob((**reinterpret_cast<const const_blob_ptr*>(&v)) );
         break;
      default:
         _data = v._data;
   }
   set_variant_type( this, v.get_type() );
   return *this;
}

void  variant::visit( const visitor& v )const
{
   switch( get_type() )
   {
      case null_type:
         v.handle();
         return;
      case int64_type:
         v.handle( *reinterpret_cast<const int64_t*>(this) );
         return;
      case uint64_type:
         v.handle( *reinterpret_cast<const uint64_t*>(this) );
         return;
      case double_type:
         v.handle( *reinterpret_cast<const double*>(this) );
         return;
      case bool_type:
         v.handle( *reinterpret_cast<const bool*>(this) );
         return;
      case string_type:
         v.handle( **reinterpret_cast<const const_string_ptr*>(this) );
         return;
      case array_type:
         v.handle( **reinterpret_cast<const const_variants_ptr*>(this) );
         return;
      case object_type:
         v.handle( **reinterpret_cast<const const_variant_object_ptr*>(this) );
         return;
      case blob_type:
         v.handle( **reinterpret_cast<const const_blob_ptr*>(this) );
         return;
      default:
         FC_THROW_EXCEPTION( assert_exception, "Invalid Type / Corrupted Memory" );
   }
}

variant::type_id variant::get_type()const
{
   return (type_id)reinterpret_cast<const char*>(this)[sizeof(*this)-1];
}

bool variant::is_null()const
{
   return get_type() == null_type;
}

bool variant::is_string()const
{
   return get_type() == string_type;
}
bool variant::is_bool()const
{
   return get_type() == bool_type;
}
bool variant::is_double()const
{
   return get_type() == double_type;
}
bool variant::is_uint64()const
{
   return get_type() == uint64_type;
}
bool variant::is_int64()const
{
   return get_type() == int64_type;
}

bool variant::is_integer()const
{
   switch( get_type() )
   {
      case int64_type:
      case uint64_type:
      case bool_type:
         return true;
      default:
         return false;
   }
   return false;
}
bool variant::is_numeric()const
{
   switch( get_type() )
   {
      case int64_type:
      case uint64_type:
      case double_type:
      case bool_type:
         return true;
      default:
         return false;
   }
   return false;
}

bool variant::is_object()const
{
   return get_type() == object_type;
}

bool variant::is_array()const
{
   return get_type() == array_type;
}
bool variant::is_blob()const
{
   return get_type() == blob_type;
}

int64_t variant::as_int64()const
{
   switch( get_type() )
   {
      case string_type:
          return to_int64(**reinterpret_cast<const const_string_ptr*>(this));
      case double_type:
          return int64_t(*reinterpret_cast<const double*>(this));
      case int64_type:
          return *reinterpret_cast<const int64_t*>(this);
      case uint64_type:
          return int64_t(*reinterpret_cast<const uint64_t*>(this));
      case bool_type:
          return *reinterpret_cast<const bool*>(this);
      case null_type:
          return 0;
      default:
         FC_THROW_EXCEPTION( bad_cast_exception, "Invalid cast from ${type} to int64", ("type", get_type()) );
   }
}

uint64_t variant::as_uint64()const
{ try {
   switch( get_type() )
   {
      case string_type:
          return to_uint64(**reinterpret_cast<const const_string_ptr*>(this));
      case double_type:
          return static_cast<uint64_t>(*reinterpret_cast<const double*>(this));
      case int64_type:
          return static_cast<uint64_t>(*reinterpret_cast<const int64_t*>(this));
      case uint64_type:
          return *reinterpret_cast<const uint64_t*>(this);
      case bool_type:
          return static_cast<uint64_t>(*reinterpret_cast<const bool*>(this));
      case null_type:
          return 0;
      default:
         FC_THROW_EXCEPTION( bad_cast_exception,"Invalid cast from ${type} to uint64", ("type",get_type()));
   }
} FC_CAPTURE_AND_RETHROW( (*this) ) }


double  variant::as_double()const
{
   switch( get_type() )
   {
      case string_type:
          return to_double(**reinterpret_cast<const const_string_ptr*>(this));
      case double_type:
          return *reinterpret_cast<const double*>(this);
      case int64_type:
          return static_cast<double>(*reinterpret_cast<const int64_t*>(this));
      case uint64_type:
          return static_cast<double>(*reinterpret_cast<const uint64_t*>(this));
      case bool_type:
          return *reinterpret_cast<const bool*>(this);
      case null_type:
          return 0;
      default:
         FC_THROW_EXCEPTION( bad_cast_exception, "Invalid cast from ${type} to double", ("type",get_type()) );
   }
}

bool  variant::as_bool()const
{
   switch( get_type() )
   {
      case string_type:
      {
          const std::string& s = **reinterpret_cast<const const_string_ptr*>(this);
          if( s == "true" )
             return true;
          if( s == "false" )
             return false;
          FC_THROW_EXCEPTION( bad_cast_exception, "Cannot convert string to bool (only \"true\" or \"false\" can be converted)" );
      }
      case double_type:
          return *reinterpret_cast<const double*>(this) != 0.0;
      case int64_type:
          return *reinterpret_cast<const int64_t*>(this) != 0;
      case uint64_type:
          return *reinterpret_cast<const uint64_t*>(this) != 0;
      case bool_type:
          return *reinterpret_cast<const bool*>(this);
      case null_type:
          return false;
      default:
         FC_THROW_EXCEPTION( bad_cast_exception, "Invalid cast from ${type} to bool" , ("type",get_type()));
   }
}

static std::string s_fc_to_string(double d)
{
   // +2 is required to ensure that the double is rounded correctly when read back in.  http://docs.oracle.com/cd/E19957-01/806-3568/ncg_goldberg.html
   std::stringstream ss;
   ss << std::setprecision(std::numeric_limits<double>::digits10 + 2) << std::fixed << d;
   return ss.str();
}

std::string variant::as_string()const
{
   switch( get_type() )
   {
      case string_type:
          return **reinterpret_cast<const const_string_ptr*>(this);
      case double_type:
          return s_fc_to_string(*reinterpret_cast<const double*>(this));
      case int64_type:
          return std::to_string(*reinterpret_cast<const int64_t*>(this));
      case uint64_type:
          return std::to_string(*reinterpret_cast<const uint64_t*>(this));
      case bool_type:
          return *reinterpret_cast<const bool*>(this) ? "true" : "false";
      case blob_type:
          if( get_blob().data.size() )
             return base64_encode( get_blob().data.data(), get_blob().data.size() );
          return std::string();
      case null_type:
          return std::string();
      default:
      FC_THROW_EXCEPTION( bad_cast_exception, "Invalid cast from ${type} to string", ("type", get_type() ) );
   }
}


/// @throw if get_type() != array_type | null_type
variants&         variant::get_array()
{
  if( get_type() == array_type )
     return **reinterpret_cast<variants**>(this);

  FC_THROW_EXCEPTION( bad_cast_exception, "Invalid cast from ${type} to Array", ("type",get_type()) );
}
blob&         variant::get_blob()
{
  if( get_type() == blob_type )
     return **reinterpret_cast<blob**>(this);

  FC_THROW_EXCEPTION( bad_cast_exception, "Invalid cast from ${type} to Blob", ("type",get_type()) );
}
const blob&         variant::get_blob()const
{
  if( get_type() == blob_type )
     return **reinterpret_cast<const const_blob_ptr*>(this);

  FC_THROW_EXCEPTION( bad_cast_exception, "Invalid cast from ${type} to Blob", ("type",get_type()) );
}

blob variant::as_blob()const
{
   switch( get_type() )
   {
      case null_type: return blob();
      case blob_type: return get_blob();
      case string_type:
      {
         const std::string& str = get_string();
         if( str.size() == 0 ) return blob();
         try {
            // pre-5.0 versions of variant added `=` to end of base64 encoded string in as_string() above.
            // fc version of base64_decode allows for extra `=` at the end of the string.
            // Other base64 decoders will not accept the extra `=`.
            std::vector<char> b64 = base64_decode( str );
            return { std::move(b64) };
         } catch(const std::exception&) {
            // unable to decode, return the raw chars
         }
         return blob( { std::vector<char>( str.begin(), str.end() ) } );
      }
      case object_type:
      case array_type:
         FC_THROW_EXCEPTION( bad_cast_exception, "Invalid cast from ${type} to Blob", ("type",get_type()) );
      default:
         return blob( { std::vector<char>( (char*)&_data, (char*)&_data + sizeof(_data) ) } );
   }
}


/// @throw if get_type() != array_type
const variants&       variant::get_array()const
{
  if( get_type() == array_type )
     return **reinterpret_cast<const const_variants_ptr*>(this);
  FC_THROW_EXCEPTION( bad_cast_exception, "Invalid cast from ${type} to Array", ("type",get_type()) );
}


/// @throw if get_type() != object_type | null_type
variant_object&        variant::get_object()
{
  if( get_type() == object_type )
     return **reinterpret_cast<variant_object**>(this);
  FC_THROW_EXCEPTION( bad_cast_exception, "Invalid cast from ${type} to Object", ("type",get_type()) );
}

const variant& variant::operator[]( const char* key )const
{
    return get_object()[key];
}
const variant&    variant::operator[]( size_t pos )const
{
    return get_array()[pos];
}
        /// @pre is_array()
size_t            variant::size()const
{
    return get_array().size();
}

size_t variant::estimated_size()const
{
   switch( get_type() )
   {
   case null_type:
   case int64_type:
   case uint64_type:
   case double_type:
   case bool_type:
      return sizeof(*this);
   case string_type:
      return as_string().length() + sizeof(std::string) + sizeof(*this);
   case array_type:
   {
      const auto& arr = get_array();
      auto arr_size = arr.size();
      size_t sum = sizeof(*this) + sizeof(variants);
      for (size_t iter = 0; iter < arr_size; ++iter) {
         sum += arr[iter].estimated_size();
      }
      return sum;
   }
   case object_type:
      return get_object().estimated_size() + sizeof(*this);
   case blob_type:
      return sizeof(blob) + get_blob().data.size() + sizeof(*this);
   default:
      FC_THROW_EXCEPTION( assert_exception, "Invalid Type / Corrupted Memory" );
   }
}

const std::string&        variant::get_string()const
{
  if( get_type() == string_type )
     return **reinterpret_cast<const const_string_ptr*>(this);
  FC_THROW_EXCEPTION( bad_cast_exception, "Invalid cast from type '${type}' to string", ("type",get_type()) );
}

/// @throw if get_type() != object_type
const variant_object&  variant::get_object()const
{
  if( get_type() == object_type )
     return **reinterpret_cast<const const_variant_object_ptr*>(this);
  FC_THROW_EXCEPTION( bad_cast_exception, "Invalid cast from type '${type}' to Object", ("type",get_type()) );
}

void from_variant( const variant& var,  variants& vo )
{
   vo = var.get_array();
}

//void from_variant( const variant& var,  variant_object& vo )
//{
//   vo  = var.get_object();
//}

void from_variant( const variant& var,  variant& vo ) { vo = var; }

void to_variant( const uint8_t& var,  variant& vo )  { vo = uint64_t(var); }
// TODO: warn on overflow?
void from_variant( const variant& var,  uint8_t& vo ){ vo = static_cast<uint8_t>(var.as_uint64()); }

void to_variant( const int8_t& var,  variant& vo )  { vo = int64_t(var); }
// TODO: warn on overflow?
void from_variant( const variant& var,  int8_t& vo ){ vo = static_cast<int8_t>(var.as_int64()); }

void to_variant( const uint16_t& var,  variant& vo )  { vo = uint64_t(var); }
// TODO: warn on overflow?
void from_variant( const variant& var,  uint16_t& vo ){ vo = static_cast<uint16_t>(var.as_uint64()); }

void to_variant( const int16_t& var,  variant& vo )  { vo = int64_t(var); }
// TODO: warn on overflow?
void from_variant( const variant& var,  int16_t& vo ){ vo = static_cast<int16_t>(var.as_int64()); }

void to_variant( const uint32_t& var,  variant& vo )  { vo = uint64_t(var); }
void from_variant( const variant& var,  uint32_t& vo )
{
   vo = static_cast<uint32_t>(var.as_uint64());
}

void to_variant( const int32_t& var,  variant& vo )  {
   vo = int64_t(var);
}

void from_variant( const variant& var,  int32_t& vo )
{
   vo = static_cast<int32_t>(var.as_int64());
}

void to_variant( const unsigned __int128& var,  variant& vo )  {
   vo = boost::multiprecision::uint128_t( var ).str();
}

void from_variant( const variant& var,  unsigned __int128& vo )
{
   if( var.is_uint64() ) {
      vo = var.as_uint64();
   } else if( var.is_string() ) {
      vo = static_cast<unsigned __int128>( boost::multiprecision::uint128_t(var.as_string()) );
   } else {
      FC_THROW_EXCEPTION( bad_cast_exception, "Cannot convert variant of type '${type}' into a uint128_t", ("type", var.get_type()) );
   }
}

void to_variant( const __int128& var,  variant& vo )  {
   vo = boost::multiprecision::int128_t( var ).str();
}

void from_variant( const variant& var,  __int128& vo )
{
   if( var.is_int64() ) {
      vo = var.as_int64();
   } else if( var.is_string() ) {
      vo = static_cast<__int128>( boost::multiprecision::int128_t(var.as_string()) );
   } else {
      FC_THROW_EXCEPTION( bad_cast_exception, "Cannot convert variant of type '${type}' into a int128_t", ("type", var.get_type()) );
   }
}

void from_variant( const variant& var,  int64_t& vo )
{
   vo = var.as_int64();
}

void from_variant( const variant& var,  uint64_t& vo )
{
   vo = var.as_uint64();
}

void from_variant( const variant& var,  bool& vo )
{
   vo = var.as_bool();
}

void from_variant( const variant& var,  double& vo )
{
   vo = var.as_double();
}

void from_variant( const variant& var,  float& vo )
{
   vo = static_cast<float>(var.as_double());
}

void to_variant( const std::string& s, variant& v )
{
   v = variant( std::string(s) );
}

void from_variant( const variant& var,  std::string& vo )
{
   vo = var.as_string();
}

void to_variant( const std::vector<char>& var,  variant& vo )
{
   FC_ASSERT( var.size() <= MAX_SIZE_OF_BYTE_ARRAYS );
   if( var.size() )
      vo = variant(to_hex(var.data(),var.size()));
   else vo = "";
}
void from_variant( const variant& var,  std::vector<char>& vo )
{
   const auto& str = var.get_string();
   FC_ASSERT( str.size() <= 2*MAX_SIZE_OF_BYTE_ARRAYS ); // Doubled because hex strings needs two characters per byte
   FC_ASSERT( str.size() % 2 == 0, "the length of hex string should be even number" );
   vo.resize( str.size() / 2 );
   if( vo.size() ) {
      size_t r = from_hex( str, vo.data(), vo.size() );
      FC_ASSERT( r == vo.size() );
   }
}

void to_variant( const blob& b, variant& v ) {
   v = variant(base64_encode(b.data.data(), b.data.size()));
}

void from_variant( const variant& v, blob& b ) {
   b.data = base64_decode(v.as_string());
}

void to_variant( const UInt<8>& n, variant& v ) { v = uint64_t(n); }
// TODO: warn on overflow?
void from_variant( const variant& v, UInt<8>& n ) { n = static_cast<uint8_t>(v.as_uint64()); }

void to_variant( const UInt<16>& n, variant& v ) { v = uint64_t(n); }
// TODO: warn on overflow?
void from_variant( const variant& v, UInt<16>& n ) { n = static_cast<uint16_t>(v.as_uint64()); }

void to_variant( const UInt<32>& n, variant& v ) { v = uint64_t(n); }
// TODO: warn on overflow?
void from_variant( const variant& v, UInt<32>& n ) { n = static_cast<uint32_t>(v.as_uint64()); }

void to_variant( const UInt<64>& n, variant& v ) { v = uint64_t(n); }
void from_variant( const variant& v, UInt<64>& n ) { n = v.as_uint64(); }

constexpr size_t minimize_max_size = 1024;

// same behavior as std::string::substr only removes invalid utf8, and lower ascii
void clean_append( std::string& app, const std::string_view& s, size_t pos = 0, size_t len = std::string::npos ) {
   std::string_view sub = s.substr( pos, len );
   app.reserve( app.size() + sub.size() );
   const bool escape_control_chars = false;
   app += escape_string( sub, nullptr, escape_control_chars );
}

std::string format_string( const std::string& frmt, const variant_object& args, bool minimize )
{
   std::string result;
   const std::string& format = ( minimize && frmt.size() > minimize_max_size ) ?
         frmt.substr( 0, minimize_max_size ) + "..." : frmt;

   const auto arg_num = (args.size() == 0) ? 1 : args.size();
   const auto max_format_size = std::max(minimize_max_size, format.size());
   // limit each arg size when minimize is set
   const auto minimize_sub_max_size = minimize ? ( max_format_size - format.size() ) / arg_num :  minimize_max_size;
   // reserve space for each argument replaced by ...
   result.reserve( max_format_size + 3 * args.size());
   size_t prev = 0;
   size_t next = format.find( '$' );
   while( prev != std::string::npos && prev < format.size() ) {
      if( next != std::string::npos ) {
         clean_append( result, format, prev, next - prev );
      } else {
         clean_append( result, format, prev );
      }

      // if we got to the end, return it.
      if( next == std::string::npos ) {
         return result;
      } else if( minimize && result.size() > minimize_max_size ) {
         result += "...";
         return result;
      }

      // if we are not at the end, then update the start
      prev = next + 1;

      if( format[prev] == '{' ) {
         // if the next char is a open, then find close
         next = format.find( '}', prev );
         // if we found close...
         if( next != std::string::npos ) {
            // the key is between prev and next
            std::string key = format.substr( prev + 1, (next - prev - 1) );

            auto val = args.find( key );
            bool replaced = true;
            if( val != args.end() ) {
               if( val->value().is_object() || val->value().is_array() ) {
                  if( minimize && (result.size() >= minimize_max_size)) {
                     replaced = false;
                  } else {
                     const auto max_length = minimize ? minimize_sub_max_size : std::numeric_limits<uint64_t>::max();
                     try {
                        // clean_append not needed as to_string is valid utf8
                        result += json::to_string( val->value(), fc::time_point::maximum(),
                                                   json::output_formatting::stringify_large_ints_and_doubles, max_length );
                     } catch (...) {
                        replaced = false;
                     }
                  }
               } else if( val->value().is_blob() ) {
                  if( minimize && val->value().get_blob().data.size() > minimize_sub_max_size ) {
                     replaced = false;
                  } else {
                     clean_append( result, val->value().as_string() );
                  }
               } else if( val->value().is_string() ) {
                  if( minimize && val->value().get_string().size() > minimize_sub_max_size ) {
                     auto sz = std::min( minimize_sub_max_size, minimize_max_size - result.size() );
                     clean_append( result, val->value().get_string(), 0, sz );
                     result += "...";
                  } else {
                     clean_append( result, val->value().get_string() );
                  }
               } else {
                  clean_append( result, val->value().as_string() );
               }
            } else {
               replaced = false;
            }
            if( !replaced ) {
               result += "${";
               clean_append( result, key );
               result += "}";
            }
            prev = next + 1;
            // find the next $
            next = format.find( '$', prev );
         } else {
            // we didn't find it.. continue to while...
         }
      } else {
         clean_append( result, format, prev, 1 );
         ++prev;
         next = format.find( '$', prev );
      }
   }
   return result;
}

   #ifdef __APPLE__
   #elif !defined(_MSC_VER)
   void to_variant( long long int s, variant& v ) { v = variant( int64_t(s) ); }
   void to_variant( unsigned long long int s, variant& v ) { v = variant( uint64_t(s)); }
   #endif

   bool operator == ( const variant& a, const variant& b )
   {
      if( a.is_string()  || b.is_string() ) return a.as_string() == b.as_string();
      if( a.is_double()  || b.is_double() ) return a.as_double() == b.as_double();
      if( a.is_int64()   || b.is_int64() )  return a.as_int64() == b.as_int64();
      if( a.is_uint64()  || b.is_uint64() ) return a.as_uint64() == b.as_uint64();
      if( a.is_array()   || b.is_array() )  return a.get_array() == b.get_array();
      return false;
   }

   bool operator != ( const variant& a, const variant& b )
   {
      return !( a == b );
   }

   bool operator ! ( const variant& a )
   {
      return !a.as_bool();
   }

   bool operator < ( const variant& a, const variant& b )
   {
      if( a.is_string()  || b.is_string() ) return a.as_string() < b.as_string();
      if( a.is_double()  || b.is_double() ) return a.as_double() < b.as_double();
      if( a.is_int64()   || b.is_int64() )  return a.as_int64() < b.as_int64();
      if( a.is_uint64()  || b.is_uint64() ) return a.as_uint64() < b.as_uint64();
      FC_ASSERT( false, "Invalid operation" );
   }

   bool operator > ( const variant& a, const variant& b )
   {
      if( a.is_string()  || b.is_string() ) return a.as_string() > b.as_string();
      if( a.is_double()  || b.is_double() ) return a.as_double() > b.as_double();
      if( a.is_int64()   || b.is_int64() )  return a.as_int64() > b.as_int64();
      if( a.is_uint64()  || b.is_uint64() ) return a.as_uint64() > b.as_uint64();
      FC_ASSERT( false, "Invalid operation" );
   }

   bool operator <= ( const variant& a, const variant& b )
   {
      if( a.is_string()  || b.is_string() ) return a.as_string() <= b.as_string();
      if( a.is_double()  || b.is_double() ) return a.as_double() <= b.as_double();
      if( a.is_int64()   || b.is_int64() )  return a.as_int64() <= b.as_int64();
      if( a.is_uint64()  || b.is_uint64() ) return a.as_uint64() <= b.as_uint64();
      FC_ASSERT( false, "Invalid operation" );
   }


   variant operator + ( const variant& a, const variant& b )
   {
      if( a.is_array()  && b.is_array() )
      {
         const variants& aa = a.get_array();
         const variants& ba = b.get_array();
         variants result;
         result.reserve( std::max(aa.size(),ba.size()) );
         auto num = std::max(aa.size(),ba.size());
         for( unsigned i = 0; i < num; ++i )
         {
            if( aa.size() > i && ba.size() > i )
               result[i]  = aa[i] + ba[i];
            else if( aa.size() > i )
               result[i]  = aa[i];
            else
               result[i]  = ba[i];
         }
         return result;
      }
      if( a.is_string()  || b.is_string() ) return a.as_string() + b.as_string();
      if( a.is_double()  || b.is_double() ) return a.as_double() + b.as_double();
      if( a.is_int64()   || b.is_int64() )  return a.as_int64() + b.as_int64();
      if( a.is_uint64()  || b.is_uint64() ) return a.as_uint64() + b.as_uint64();
      FC_ASSERT( false, "invalid operation ${a} + ${b}", ("a",a)("b",b) );
   }

   variant operator - ( const variant& a, const variant& b )
   {
      if( a.is_array()  && b.is_array() )
      {
         const variants& aa = a.get_array();
         const variants& ba = b.get_array();
         variants result;
         result.reserve( std::max(aa.size(),ba.size()) );
         auto num = std::max(aa.size(),ba.size());
         for( unsigned i = 0; i < num; --i )
         {
            if( aa.size() > i && ba.size() > i )
               result[i]  = aa[i] - ba[i];
            else if( aa.size() > i )
               result[i]  = aa[i];
            else
               result[i]  = ba[i];
         }
         return result;
      }
      if( a.is_string()  || b.is_string() ) return a.as_string() - b.as_string();
      if( a.is_double()  || b.is_double() ) return a.as_double() - b.as_double();
      if( a.is_int64()   || b.is_int64() )  return a.as_int64() - b.as_int64();
      if( a.is_uint64()  || b.is_uint64() ) return a.as_uint64() - b.as_uint64();
      FC_ASSERT( false, "invalid operation ${a} + ${b}", ("a",a)("b",b) );
   }
   variant operator * ( const variant& a, const variant& b )
   {
      if( a.is_double()  || b.is_double() ) return a.as_double() * b.as_double();
      if( a.is_int64()   || b.is_int64() )  return a.as_int64() * b.as_int64();
      if( a.is_uint64()  || b.is_uint64() ) return a.as_uint64() * b.as_uint64();
      if( a.is_array()  && b.is_array() )
      {
         const variants& aa = a.get_array();
         const variants& ba = b.get_array();
         variants result;
         result.reserve( std::max(aa.size(),ba.size()) );
         auto num = std::max(aa.size(),ba.size());
         for( unsigned i = 0; i < num; ++i )
         {
            if( aa.size() > i && ba.size() > i )
               result[i]  = aa[i] * ba[i];
            else if( aa.size() > i )
               result[i]  = aa[i];
            else
               result[i]  = ba[i];
         }
         return result;
      }
      FC_ASSERT( false, "invalid operation ${a} * ${b}", ("a",a)("b",b) );
   }
   variant operator / ( const variant& a, const variant& b )
   {
      if( a.is_double()  || b.is_double() ) return a.as_double() / b.as_double();
      if( a.is_int64()   || b.is_int64() )  return a.as_int64() / b.as_int64();
      if( a.is_uint64()  || b.is_uint64() ) return a.as_uint64() / b.as_uint64();
      if( a.is_array()  && b.is_array() )
      {
         const variants& aa = a.get_array();
         const variants& ba = b.get_array();
         variants result;
         result.reserve( std::max(aa.size(),ba.size()) );
         auto num = std::max(aa.size(),ba.size());
         for( unsigned i = 0; i < num; ++i )
         {
            if( aa.size() > i && ba.size() > i )
               result[i]  = aa[i] / ba[i];
            else if( aa.size() > i )
               result[i]  = aa[i];
            else
               result[i]  = ba[i];
         }
         return result;
      }
      FC_ASSERT( false, "invalid operation ${a} / ${b}", ("a",a)("b",b) );
   }
} // namespace fc
