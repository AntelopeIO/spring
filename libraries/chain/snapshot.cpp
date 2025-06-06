#define RAPIDJSON_NAMESPACE eosio_rapidjson // This is ABSOLUTELY necessary anywhere that is using eosio_rapidjson

#include <eosio/chain/block_timestamp.hpp>
#include <eosio/chain/chain_snapshot.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/genesis_state.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/snapshot.hpp>
#include <eosio/chain/snapshot_detail.hpp>

#include <fc/scoped_exit.hpp>
#include <fc/variant_object.hpp>
#include <fc/io/json.hpp>

#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

using namespace eosio_rapidjson;

namespace eosio { namespace chain {

variant_snapshot_writer::variant_snapshot_writer(fc::mutable_variant_object& snapshot)
: snapshot(snapshot)
{
   snapshot.set("sections", fc::variants());
   snapshot.set("version", current_snapshot_version );
}

void variant_snapshot_writer::write_start_section( const std::string& section_name ) {
   current_rows.clear();
   current_section_name = section_name;
}

void variant_snapshot_writer::write_row( const detail::abstract_snapshot_row_writer& row_writer ) {
   current_rows.emplace_back(row_writer.to_variant());
}

void variant_snapshot_writer::write_end_section( ) {
   snapshot["sections"].get_array().emplace_back(fc::mutable_variant_object()("name", std::move(current_section_name))("rows", std::move(current_rows)));
}

void variant_snapshot_writer::finalize() {

}

variant_snapshot_reader::variant_snapshot_reader(const fc::variant& snapshot)
:snapshot(snapshot)
,cur_section(nullptr)
,cur_row(0)
{
}

void variant_snapshot_reader::validate() {
   EOS_ASSERT(snapshot.is_object(), snapshot_validation_exception,
         "Variant snapshot is not an object");
   const fc::variant_object& o = snapshot.get_object();

   EOS_ASSERT(o.contains("version"), snapshot_validation_exception,
         "Variant snapshot has no version");

   const auto& version = o["version"];
   EOS_ASSERT(version.is_integer(), snapshot_validation_exception,
         "Variant snapshot version is not an integer");

   EOS_ASSERT(version.as_uint64() == (uint64_t)current_snapshot_version, snapshot_validation_exception,
         "Variant snapshot is an unsupported version.  Expected : ${expected}, Got: ${actual}",
         ("expected", current_snapshot_version)("actual",o["version"].as_uint64()));

   EOS_ASSERT(o.contains("sections"), snapshot_validation_exception,
         "Variant snapshot has no sections");

   const auto& sections = o["sections"];
   EOS_ASSERT(sections.is_array(), snapshot_validation_exception, "Variant snapshot sections is not an array");

   const auto& section_array = sections.get_array();
   for( const auto& section: section_array ) {
      EOS_ASSERT(section.is_object(), snapshot_validation_exception, "Variant snapshot section is not an object");

      const auto& so = section.get_object();
      EOS_ASSERT(so.contains("name"), snapshot_validation_exception,
            "Variant snapshot section has no name");

      EOS_ASSERT(so["name"].is_string(), snapshot_validation_exception,
                 "Variant snapshot section name is not a string");

      EOS_ASSERT(so.contains("rows"), snapshot_validation_exception,
                 "Variant snapshot section has no rows");

      EOS_ASSERT(so["rows"].is_array(), snapshot_validation_exception,
                 "Variant snapshot section rows is not an array");
   }
}

void variant_snapshot_reader::set_section( const string& section_name ) {
   const auto& sections = snapshot["sections"].get_array();
   for( const auto& section: sections ) {
      if (section["name"].as_string() == section_name) {
         cur_section = &section.get_object();
         return;
      }
   }

   EOS_THROW(snapshot_exception, "Variant snapshot has no section named ${n}", ("n", section_name));
}

bool variant_snapshot_reader::read_row( detail::abstract_snapshot_row_reader& row_reader ) {
   const auto& rows = (*cur_section)["rows"].get_array();
   row_reader.provide(rows.at(cur_row++));
   return cur_row < rows.size();
}

bool variant_snapshot_reader::empty ( ) {
   const auto& rows = (*cur_section)["rows"].get_array();
   return rows.empty();
}

void variant_snapshot_reader::clear_section() {
   cur_section = nullptr;
   cur_row = 0;
}

void variant_snapshot_reader::return_to_header() {
   clear_section();
}

size_t variant_snapshot_reader::total_row_count() {
   size_t total = 0;

   const fc::variants& sections = snapshot["sections"].get_array();
   for(const fc::variant& section : sections)
      total += section["rows"].get_array().size();

   return total;
}

ostream_snapshot_writer::ostream_snapshot_writer(std::ostream& snapshot)
:snapshot(snapshot)
,header_pos(snapshot.tellp())
,section_pos(-1)
,row_count(0)
{
   // write magic number
   auto totem = magic_number;
   snapshot.write((char*)&totem, sizeof(totem));

   // write version
   auto version = current_snapshot_version;
   snapshot.write((char*)&version, sizeof(version));
}

void ostream_snapshot_writer::write_start_section( const std::string& section_name )
{
   EOS_ASSERT(section_pos == std::streampos(-1), snapshot_exception, "Attempting to write a new section without closing the previous section");
   section_pos = snapshot.tellp();
   row_count = 0;

   uint64_t placeholder = std::numeric_limits<uint64_t>::max();

   // write a placeholder for the section size
   snapshot.write((char*)&placeholder, sizeof(placeholder));

   // write placeholder for row count
   snapshot.write((char*)&placeholder, sizeof(placeholder));

   // write the section name (null terminated)
   snapshot.write(section_name.data(), section_name.size());
   snapshot.put(0);
}

void ostream_snapshot_writer::write_row( const detail::abstract_snapshot_row_writer& row_writer ) {
   row_writer.write(snapshot);
   row_count++;
}

void ostream_snapshot_writer::write_end_section( ) {
   auto restore = snapshot.tellp();

   uint64_t section_size = restore - section_pos - sizeof(uint64_t);

   snapshot.seekp(section_pos);

   // write a the section size
   snapshot.write((char*)&section_size, sizeof(section_size));

   // write the row count
   snapshot.write((char*)&row_count, sizeof(row_count));

   snapshot.seekp(restore);

   section_pos = std::streampos(-1);
   row_count = 0;
}

void ostream_snapshot_writer::finalize() {
   uint64_t end_marker = std::numeric_limits<uint64_t>::max();

   // write a placeholder for the section size
   snapshot.write((char*)&end_marker, sizeof(end_marker));
}

ostream_json_snapshot_writer::ostream_json_snapshot_writer(std::ostream& snapshot)
      :snapshot(snapshot)
      ,row_count(0)
{
   snapshot << "{\n";
   // write magic number
   auto totem = magic_number;
   snapshot << "\"magic_number\":" << fc::json::to_string(totem, fc::time_point::maximum()) << "\n";

   // write version
   auto version = current_snapshot_version;
   snapshot << ",\"version\":" << fc::json::to_string(version, fc::time_point::maximum()) << "\n";
}

void ostream_json_snapshot_writer::write_start_section( const std::string& section_name )
{
   row_count = 0;
   snapshot.inner << "," << fc::json::to_string(section_name, fc::time_point::maximum()) << ":{\n\"rows\":[\n";
}

void ostream_json_snapshot_writer::write_row( const detail::abstract_snapshot_row_writer& row_writer ) {
   const auto yield = [&](size_t s) {};

   if(row_count != 0) snapshot.inner << ",";
   snapshot.inner << fc::json::to_string(row_writer.to_variant(), yield) << "\n";
   ++row_count;
}

void ostream_json_snapshot_writer::write_end_section( ) {
   snapshot.inner << "],\n\"num_rows\":" << row_count << "\n}\n";
   row_count = 0;
}

void ostream_json_snapshot_writer::finalize() {
   snapshot.inner << "}\n";
   snapshot.inner.flush();
}


istream_snapshot_reader::istream_snapshot_reader(std::istream& snapshot)
:snapshot(snapshot)
,header_pos(snapshot.tellg())
,num_rows(0)
,cur_row(0)
{

}

void istream_snapshot_reader::validate() {
   // make sure to restore the read pos
   auto restore_pos = fc::make_scoped_exit([this,pos=snapshot.tellg(),ex=snapshot.exceptions()](){
      snapshot.seekg(pos);
      snapshot.exceptions(ex);
   });

   snapshot.exceptions(std::istream::failbit|std::istream::eofbit);

   try {
      // validate totem
      auto expected_totem = ostream_snapshot_writer::magic_number;
      decltype(expected_totem) actual_totem;
      snapshot.read((char*)&actual_totem, sizeof(actual_totem));
      EOS_ASSERT(actual_totem == expected_totem, snapshot_exception,
                 "Binary snapshot has unexpected magic number!");

      // validate version
      auto expected_version = current_snapshot_version;
      decltype(expected_version) actual_version;
      snapshot.read((char*)&actual_version, sizeof(actual_version));
      EOS_ASSERT(actual_version == expected_version, snapshot_exception,
                 "Binary snapshot is an unsupported version.  Expected : ${expected}, Got: ${actual}",
                 ("expected", expected_version)("actual", actual_version));

      while (validate_section()) {}
   } FC_LOG_AND_RETHROW()
}

bool istream_snapshot_reader::validate_section() const {
   uint64_t section_size = 0;
   snapshot.read((char*)&section_size,sizeof(section_size));

   // stop when we see the end marker
   if (section_size == std::numeric_limits<uint64_t>::max()) {
      return false;
   }

   // seek past the section
   snapshot.seekg(snapshot.tellg() + std::streamoff(section_size));

   return true;
}

void istream_snapshot_reader::set_section( const string& section_name ) {
   auto restore_pos = fc::make_scoped_exit([this,pos=snapshot.tellg()](){
      snapshot.seekg(pos);
   });

   const std::streamoff header_size = sizeof(ostream_snapshot_writer::magic_number) + sizeof(current_snapshot_version);

   auto next_section_pos = header_pos + header_size;

   while (true) {
      snapshot.seekg(next_section_pos);
      uint64_t section_size = 0;
      snapshot.read((char*)&section_size,sizeof(section_size));
      if (section_size == std::numeric_limits<uint64_t>::max()) {
         break;
      }

      next_section_pos = snapshot.tellg() + std::streamoff(section_size);

      uint64_t row_count = 0;
      snapshot.read((char*)&row_count,sizeof(row_count));

      bool match = true;
      for(auto c : section_name) {
         if(snapshot.get() != c) {
            match = false;
            break;
         }
      }

      if (match && snapshot.get() == 0) {
         cur_row = 0;
         num_rows = row_count;

         // leave the stream at the right point
         restore_pos.cancel();
         return;
      }
   }

   EOS_THROW(snapshot_exception, "Binary snapshot has no section named ${n}", ("n", section_name));
}

bool istream_snapshot_reader::read_row( detail::abstract_snapshot_row_reader& row_reader ) {
   row_reader.provide(snapshot);
   return ++cur_row < num_rows;
}

bool istream_snapshot_reader::empty ( ) {
   return num_rows == 0;
}

void istream_snapshot_reader::clear_section() {
   num_rows = 0;
   cur_row = 0;
}

void istream_snapshot_reader::return_to_header() {
   snapshot.seekg( header_pos );
   clear_section();
}

size_t istream_snapshot_reader::total_row_count() {
   size_t total = 0;

   auto restore_pos = fc::make_scoped_exit([this,pos=snapshot.tellg()](){
      snapshot.seekg(pos);
   });

   const std::streamoff header_size = sizeof(ostream_snapshot_writer::magic_number) + sizeof(current_snapshot_version);

   std::streamoff next_section_pos = header_pos + header_size;

   while(true) {
      snapshot.seekg(next_section_pos);
      uint64_t section_size = 0;
      snapshot.read((char*)&section_size, sizeof(section_size));
      if(section_size == std::numeric_limits<uint64_t>::max())
         break;
      next_section_pos = snapshot.tellg() + std::streamoff(section_size);

      uint64_t row_count = 0;
      snapshot.read((char*)&row_count, sizeof(row_count));

      total += row_count;
   }

   return total;
}

struct istream_json_snapshot_reader_impl {
   uint64_t num_rows;
   uint64_t cur_row;
   eosio_rapidjson::Document doc;
   std::string sec_name;
};

istream_json_snapshot_reader::~istream_json_snapshot_reader() = default;

istream_json_snapshot_reader::istream_json_snapshot_reader(const std::filesystem::path& p)
   : impl{new istream_json_snapshot_reader_impl{0, 0, {}, {}}}
{
   FILE* fp = fopen(p.string().c_str(), "rb");
   EOS_ASSERT(fp, snapshot_exception, "Failed to open JSON snapshot: ${file}", ("file", p));
   auto close = fc::make_scoped_exit( [&fp]() { fclose( fp ); } );
   char readBuffer[65536];
   eosio_rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));
   impl->doc.ParseStream(is);
}

void istream_json_snapshot_reader::validate() {
   try {
      // validate totem
      auto expected_totem = ostream_json_snapshot_writer::magic_number;
      EOS_ASSERT(impl->doc.HasMember("magic_number"), snapshot_exception, "magic_number section not found" );
      auto actual_totem = impl->doc["magic_number"].GetUint();
      EOS_ASSERT( actual_totem == expected_totem, snapshot_exception, "JSON snapshot has unexpected magic number" );

      // validate version
      auto expected_version = current_snapshot_version;
      EOS_ASSERT(impl->doc.HasMember("version"), snapshot_exception, "version section not found" );
      auto actual_version = impl->doc["version"].GetUint();
      EOS_ASSERT( actual_version == expected_version, snapshot_exception,
                  "JSON snapshot is an unsupported version.  Expected : ${expected}, Got: ${actual}",
                  ("expected", expected_version)( "actual", actual_version ) );

   } catch( const std::exception& e ) {  \
      snapshot_exception fce(FC_LOG_MESSAGE( warn, "JSON snapshot validation threw IO exception (${what})",("what",e.what())));
      throw fce;
   }
}

bool istream_json_snapshot_reader::validate_section() const {
   return true;
}

void istream_json_snapshot_reader::set_section( const string& section_name ) {
   EOS_ASSERT( impl->doc.HasMember( section_name.c_str() ), snapshot_exception, "JSON snapshot has no section ${sec}", ("sec", section_name) );
   EOS_ASSERT( impl->doc[section_name.c_str()].HasMember( "num_rows" ), snapshot_exception, "JSON snapshot ${sec} num_rows not found", ("sec", section_name) );
   EOS_ASSERT( impl->doc[section_name.c_str()].HasMember( "rows" ), snapshot_exception, "JSON snapshot ${sec} rows not found", ("sec", section_name) );
   EOS_ASSERT( impl->doc[section_name.c_str()]["rows"].IsArray(), snapshot_exception, "JSON snapshot ${sec} rows is not an array", ("sec_name", section_name) );

   impl->sec_name = section_name;
   impl->num_rows = impl->doc[section_name.c_str()]["num_rows"].GetInt();
   ilog( "reading ${section_name}, num_rows: ${num_rows}", ("section_name", section_name)( "num_rows", impl->num_rows ) );
}

bool istream_json_snapshot_reader::read_row( detail::abstract_snapshot_row_reader& row_reader ) {
   EOS_ASSERT( impl->cur_row < impl->num_rows, snapshot_exception, "JSON snapshot ${sect}'s cur_row ${cur_row} >= num_rows ${num_rows}",
               ("sect_name", impl->sec_name)( "cur_row", impl->cur_row )( "num_rows", impl->num_rows ) );

   const eosio_rapidjson::Value& rows = impl->doc[impl->sec_name.c_str()]["rows"];
   eosio_rapidjson::StringBuffer buffer;
   eosio_rapidjson::Writer<eosio_rapidjson::StringBuffer> writer( buffer );
   rows[impl->cur_row].Accept( writer );

   const auto& row = fc::json::from_string( buffer.GetString() );
   row_reader.provide( row );
   return ++impl->cur_row < impl->num_rows;
}

bool istream_json_snapshot_reader::empty ( ) {
   return impl->num_rows == 0;
}

void istream_json_snapshot_reader::clear_section() {
   impl->num_rows = 0;
   impl->cur_row = 0;
   impl->sec_name = "";
}

void istream_json_snapshot_reader::return_to_header() {
   clear_section();
}

size_t istream_json_snapshot_reader::total_row_count() {
   size_t total = 0;

   for(const auto& section : impl->doc.GetObject())
      if(section.value.IsObject() && section.value.HasMember("num_rows"))
        total += section.value["num_rows"].GetUint64();

   return total;
}

threaded_snapshot_reader::threaded_snapshot_reader(const std::filesystem::path& snapshot_path) :
  snapshot_file(snapshot_path, fc::random_access_file::read_only),
  mapped_snap(snapshot_file, boost::interprocess::read_only),
  mapped_snap_addr((char*)mapped_snap.get_address()) {}

void threaded_snapshot_reader::validate() {
   try {
      using magic_number_t = std::decay_t<decltype(ostream_snapshot_writer::magic_number)>;
      using version_t = std::decay_t<decltype(current_snapshot_version)>;

      EOS_ASSERT(snapshot_file.unpack_from<magic_number_t>(0) == ostream_snapshot_writer::magic_number, snapshot_exception, "Binary snapshot has unexpected magic number!");

      const version_t actual_version = snapshot_file.unpack_from<version_t>(sizeof(magic_number_t));
      EOS_ASSERT(actual_version == current_snapshot_version, snapshot_exception, "Binary snapshot is an unsuppored version.  Expected : ${expected}, Got: ${actual}",
                                                                                 ("expected", current_snapshot_version)("actual", actual_version));

      uint64_t next_section_offs = sizeof(magic_number_t) + sizeof(version_t);
      while(true) {
         const uint64_t this_section_size = snapshot_file.unpack_from<uint64_t>(next_section_offs);
         if(this_section_size == std::numeric_limits<uint64_t>::max())
            break;
         next_section_offs += sizeof(this_section_size) + this_section_size;
      }
   } FC_LOG_AND_RETHROW()
}

void threaded_snapshot_reader::set_section(const string& section_name) {
   using magic_number_t = std::decay_t<decltype(ostream_snapshot_writer::magic_number)>;
   using version_t = std::decay_t<decltype(current_snapshot_version)>;

   uint64_t next_section_offs = sizeof(magic_number_t) + sizeof(version_t);
   while(true) {
      const uint64_t this_section_size      = snapshot_file.unpack_from<uint64_t>(next_section_offs);
      const uint64_t this_section_row_count = snapshot_file.unpack_from<uint64_t>(next_section_offs + sizeof(uint64_t));
      const uint64_t section_name_offset    = next_section_offs + sizeof(uint64_t) + sizeof(uint64_t);
      const uint64_t section_data_offset    = section_name_offset + section_name.size() + 1;

      //section size does not include the section size record itself, so + sizeof(uint64_t)
      EOS_ASSERT(next_section_offs + this_section_size + sizeof(uint64_t) < mapped_snap.get_size(), snapshot_exception, "Binary snapshot section too short");

      if(strncmp(section_name.c_str(), mapped_snap_addr+section_name_offset, section_name.size() + 1) == 0) {
         cur_row = 0;
         num_rows = this_section_row_count;
         ds = fc::datastream<const char*>(mapped_snap_addr+section_data_offset, mapped_snap.get_size() - section_data_offset);
         return;
      }

      next_section_offs += sizeof(this_section_size) + this_section_size;
   }

   EOS_THROW(snapshot_exception, "Binary snapshot has no section named ${n}", ("n", section_name));
}

bool threaded_snapshot_reader::read_row(detail::abstract_snapshot_row_reader& row_reader) {
   row_reader.provide(ds);
   return ++cur_row < num_rows;
}

bool threaded_snapshot_reader::empty ( ) {
   return num_rows == 0;
}

void threaded_snapshot_reader::clear_section() {
#ifdef __linux__
   //this might work elsewhere, but unsure about alignment requirements on madvise() elsewhere
   if(num_rows) {
      uintptr_t endp = (uintptr_t)ds.pos();
      ds.seekp(0);
      uintptr_t p = (uintptr_t)ds.pos();
      madvise((char*)(p & ~(boost::interprocess::mapped_region::get_page_size()-1)), endp-p, MADV_DONTNEED);
   }
#endif
   num_rows = 0;
   cur_row = 0;
}

void threaded_snapshot_reader::return_to_header() {
   clear_section();
}

size_t threaded_snapshot_reader::total_row_count() {
   using magic_number_t = std::decay_t<decltype(ostream_snapshot_writer::magic_number)>;
   using version_t = std::decay_t<decltype(current_snapshot_version)>;

   size_t total = 0;
   uint64_t next_section_offs = sizeof(magic_number_t) + sizeof(version_t);
   while(true) {
      const uint64_t this_section_size = snapshot_file.unpack_from<uint64_t>(next_section_offs);
      if(this_section_size == std::numeric_limits<uint64_t>::max())
         break;

      total += snapshot_file.unpack_from<uint64_t>(next_section_offs + sizeof(uint64_t));
      next_section_offs = next_section_offs + this_section_size + sizeof(uint64_t);
   }

   return total;
}

integrity_hash_snapshot_writer::integrity_hash_snapshot_writer(fc::sha256::encoder& enc)
:enc(enc)
{
}

void integrity_hash_snapshot_writer::write_start_section( const std::string& )
{
   // no-op for structural details
}

void integrity_hash_snapshot_writer::write_row( const detail::abstract_snapshot_row_writer& row_writer ) {
   row_writer.write(enc);
}

void integrity_hash_snapshot_writer::write_end_section( ) {
   // no-op for structural details
}

void integrity_hash_snapshot_writer::finalize() {
   // no-op for structural details
}

fc::variant snapshot_info(snapshot_reader& snapshot) {
   chain_snapshot_header header;
   snapshot.read_section<chain_snapshot_header>([&](auto &section){
      section.read_row(header);
   });
   if(header.version < chain_snapshot_header::minimum_compatible_version || header.version > chain_snapshot_header::current_version)
      wlog("Snapshot version ${v} is not supported by this version of spring-util, trying to parse anyways...");

   chain_id_type chain_id = chain_id_type::empty_chain_id();
   if(header.version <= 2) {
      snapshot.read_section<genesis_state>([&]( auto &section ) {
         genesis_state genesis;
         section.read_row(genesis);
         chain_id = genesis.compute_chain_id();
      });
   }
   else if(header.version <= 4) {
      snapshot.read_section<global_property_object>([&]( auto &section ) {
         legacy::snapshot_global_property_object_v3 legacy_global_properties; //layout is same up to chain_id for v3 & v4
         section.read_row(legacy_global_properties);
         chain_id = legacy_global_properties.chain_id;
      });
   }
   else {
      snapshot.read_section<global_property_object>([&]( auto &section ) {
         legacy::snapshot_global_property_object_v5 legacy_global_properties; //layout is same up to chain_id for v5+
         section.read_row(legacy_global_properties);
         chain_id = legacy_global_properties.chain_id;
      });
   }

   block_id_type head_block;
   block_timestamp_type head_block_time;
   if(header.version <= snapshot_detail::snapshot_block_header_state_legacy_v2::maximum_version) {
      snapshot.read_section("eosio::chain::block_state", [&]( auto &section ) {
         snapshot_detail::snapshot_block_header_state_legacy_v2 header_state;
         section.read_row(header_state);
         head_block = header_state.id;
         head_block_time = header_state.header.timestamp;
      });
   }
   else if(header.version <= snapshot_detail::snapshot_block_header_state_legacy_v3::maximum_version) {
      snapshot.read_section("eosio::chain::block_state", [&]( auto &section ) {
         snapshot_detail::snapshot_block_header_state_legacy_v3 header_state;
         section.read_row(header_state);
         head_block = header_state.id;
         head_block_time = header_state.header.timestamp;
      });
   }
   else {
      snapshot.read_section("eosio::chain::block_state", [&]( auto &section ) {
         snapshot_detail::snapshot_block_state_data_v8 header_state;
         section.read_row(header_state);
         if(header_state.bs_l) {
            head_block = header_state.bs_l->id;
            head_block_time = header_state.bs_l->header.timestamp;
         }
         else if(header_state.bs) {
            head_block = header_state.bs->block_id;
            head_block_time = header_state.bs->header.timestamp;
         }
      });
   }

   return fc::mutable_variant_object()("version", header.version)
                                      ("chain_id", chain_id)
                                      ("head_block_id", head_block)
                                      ("head_block_num", block_header::num_from_id(head_block))
                                      ("head_block_time", head_block_time);
}

}}
