#pragma once
#include <fc/io/datastream.hpp>
#include <boost/crc.hpp>

namespace fc {

/**
* Provides a datasteam wrapper around another datastream for calculation of checksum.
* Example use:
*      fc::datastream<fc::cfile>                      persist_cfile;
*      fc::datastream_crc<fc::datastream<fc::cfile>>  persist_stream{persist_cfile};
*
*      persist_stream.seekp(0);
*      fc::raw::pack(persist_stream, 'a');
*      uint32_t cs = persist_stream.check_sum();
*      fc::raw::pack(persist_stream, cs); // write checksum to file
*      // ...
*      persist_stream.seekp(0);
*      char c;
*      fc::raw::unpack(persist_stream, c);
*      uint32_t calc_cs = persist_stream.check_sum();
*      uint32_t cs;
*      fc::raw::unpack(persist_stream, cs);
*      FC_ASSERT(calc_cs == cs, "checksum not equal");
*/
template <typename DS, typename CRC = boost::crc_32_type>
class datastream_crc {
public:
   // ds must outlive datasteam_crc
   explicit datastream_crc( DS& ds ) : ds_(ds) {}

   void skip(size_t s) {
      ds_.skip(s);
   }

   bool read(char* d, size_t s) {
      bool r = ds_.read(d, s);
      crc_.process_bytes(d, s);
      return r;
   }

   bool write(const char* d, size_t s) {
      crc_.process_bytes(d, s);
      return ds_.write(d, s);
   }

   bool put(char c) {
      crc_.process_byte(c);
      return ds_.put(c);
   }

   bool get(unsigned char& c) {
      bool r = ds_.get(c);
      crc_.process_byte(c);
      return r;
   }

   bool get(char& c) {
      bool r = ds_.get(c);
      crc_.process_byte(c);
      return r;
   }

   auto pos() const {
      return ds_.pos();
   }

   bool valid() const {
      return ds_.valid();
   }

   // only use with p==0, otherwise use seekp() below
   bool seekp(size_t p) {
      assert(p == 0);
      if (p == 0) {
         crc_.reset();
         return ds_.seekp(0);
      }
      return false;
   }

   size_t tellp() const {
      return ds_.tellp();
   }
   size_t remaining() const {
      return ds_.remaining();
   }

   // extension to datastream

   bool seekp(size_t p, const CRC& crc) {
      crc_ = crc;
      return ds_.seekp(p);
   }

   uint32_t checksum() const {
      return crc_.checksum();
   }

   CRC crc() const {
      return crc_;
   }

private:
   DS& ds_;
   CRC crc_;
};


} // namespace fc
