#include <boost/test/unit_test.hpp>
#include <boost/iostreams/read.hpp>
#include <boost/iostreams/write.hpp>

#include <fc/io/random_access_file.hpp>
#include <fc/io/fstream.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/crypto/rand.hpp>

#ifdef __linux__
#include <linux/fs.h>
#endif

BOOST_AUTO_TEST_SUITE(random_access_file)

BOOST_AUTO_TEST_CASE(basic) try {
   fc::temp_directory tmpdir;
   std::filesystem::path filepath = tmpdir.path() / "file";

   fc::random_access_file f(filepath);

   BOOST_REQUIRE_EQUAL(f.size(), 0u);
   f.pack_to((uint64_t)0x0, 0);
   BOOST_REQUIRE_EQUAL(f.size(), 8u);
   f.pack_to((uint64_t)0x11111111'11111111, 4);   //overwrites some data
   BOOST_REQUIRE_EQUAL(f.size(), 12u);
   f.pack_to((uint64_t)0x44444444'44444444, 20);  //makes a hole
   BOOST_REQUIRE_EQUAL(f.size(), 28u);
   f.pack_to_end((uint64_t)0xbbbbbbbb'bbbbbbbb);
   BOOST_REQUIRE_EQUAL(f.size(), 36u);

   BOOST_REQUIRE_EQUAL(UINT64_C(0x11111111'00000000), f.unpack_from<uint64_t>(0));
   BOOST_REQUIRE_EQUAL(UINT64_C(0x11111111'11111111), f.unpack_from<uint64_t>(4));
   BOOST_REQUIRE_EQUAL(UINT64_C(0x0), f.unpack_from<uint64_t>(12));  //holes are defined as being 0x00ed
   BOOST_REQUIRE_EQUAL(UINT64_C(0x44444444'44444444), f.unpack_from<uint64_t>(20));
   BOOST_REQUIRE_EQUAL(UINT64_C(0xbbbbbbbb'bbbbbbbb), f.unpack_from<uint64_t>(28));

   f.resize(32);
   BOOST_REQUIRE_EXCEPTION(f.unpack_from<uint64_t>(28), fc::out_of_range_exception, [](const fc::out_of_range_exception& e) {
      return e.top_message().find("unexpected end of file") !=  std::string::npos;
   });
   BOOST_REQUIRE_EQUAL(f.size(), 32u);
   f.resize(36);
   BOOST_REQUIRE_EQUAL(UINT64_C(0x00000000'bbbbbbbb), f.unpack_from<uint64_t>(28)); //filled in with 0x00

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(open_errors) try {
   fc::temp_directory tmpdir;
   std::filesystem::path dirfilepath = tmpdir.path() / "dirfile";

   //try to open a directory
   std::filesystem::create_directory(dirfilepath);
   BOOST_REQUIRE_EXCEPTION(fc::random_access_file f(dirfilepath), fc::assert_exception, [](const fc::assert_exception& e) {
      return e.top_message().find("Failed to open") != std::string::npos;
   });

   //TODO: previously there was a test here to ensure an error was thrown when opening a file that is not writable. Unfortunately
   // that turned out tricky to get running properly in CI where the test runs as root (bypassing permission checks) and seems to
   // run on a filesystem lacking support for immutable files.
} FC_LOG_AND_RETHROW();

//make sure writes that require flushing buffer periodically work correctly
BOOST_AUTO_TEST_CASE(long_datastream) try {
   fc::temp_directory tmpdir;
   std::filesystem::path filepath = tmpdir.path() / "file";

   fc::random_access_file f(filepath);

   const unsigned number_of_entries_to_do = 5000000; //each loop below handles 13 bytes; this will give 65MB file

   fc::sha256 hash_of_written_data;
   {
      fc::random_access_file::write_datastream ds = f.write_ds(0);
      fc::sha256::encoder enc;
      for(unsigned i = 0; i < number_of_entries_to_do; ++i) { //each loop here writes 8+5=13 bytes
         fc::raw::pack(ds, (uint64_t)i);
         fc::raw::pack(ds, std::string("sup?")); //encodes as 5 bytes; provides some "skew" to the data across buffer boundaries

         fc::raw::pack(enc, (uint64_t)i);
         fc::raw::pack(enc, std::string("sup?"));
      }
      hash_of_written_data = enc.result();
   }

   //try reading everything back via read_datastream
   {
      fc::random_access_file::read_datastream ds = f.read_ds(0);
      fc::sha256::encoder enc;

      for(unsigned i = 0; i < number_of_entries_to_do; ++i) { //each loop here writes 8+5=13 bytes
         uint64_t red_number;
         std::string red_string;
         fc::raw::unpack(ds, red_number);
         fc::raw::unpack(ds, red_string);

         fc::raw::pack(enc, red_number);
         fc::raw::pack(enc, red_string);
      }
      BOOST_REQUIRE_EQUAL(enc.result(), hash_of_written_data);
   }

   //try reading back via random access
   {
      for(uint64_t i = 0; i < number_of_entries_to_do; ++i)
         BOOST_REQUIRE_EQUAL(i, f.unpack_from<uint64_t>(i*13));
   }

   //load everything in to memory via a completely different impl
   {
      std::string s;
      fc::read_file_contents(filepath, s);
      BOOST_REQUIRE_EQUAL(fc::sha256::hash(s.data(), s.size()), hash_of_written_data);
   }

   //load everything in to memory via device
   {
      fc::random_access_file::device device = f.seekable_device();
      boost::beast::flat_buffer buff;
      const unsigned read_amount = 72*1024;
      std::streamsize red;
      while((red = boost::iostreams::read(device, (char*)buff.prepare(read_amount).data(), read_amount)) != -1) {
         buff.commit(red);
      }
      BOOST_REQUIRE_EQUAL(fc::sha256::hash((char*)buff.cdata().data(), buff.size()), hash_of_written_data);
   }
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(long_write_sink) try {
   fc::temp_directory tmpdir;
   std::filesystem::path filepath = tmpdir.path() / "file";

   fc::random_access_file f(filepath);

   const unsigned target_file_size = 100*1024*1024;

   const uint64_t first_extra_trailer = 0x11223344'bbccddee;
   fc::sha256 first_part_hash;
   {
      fc::random_access_file::device device = f.seekable_device();
      fc::sha256::encoder first_part_hash_enc;
      const unsigned write_stride = 6247;

      for(unsigned wrote = 0; wrote < target_file_size; ) {
         const unsigned to_write = std::min(target_file_size-wrote, write_stride);
         char b[write_stride];
         fc::rand_bytes(b, sizeof(b));

         boost::iostreams::write(device, b, to_write);
         first_part_hash_enc.write(b, to_write);
         wrote += to_write;
      }

      first_part_hash = first_part_hash_enc.result();
      //write_device is unbuffered, so even though device is still "live", we're allowed to do this without damaging data
      f.pack_to_end(first_extra_trailer);
   }

   BOOST_REQUIRE_EQUAL(f.unpack_from<uint64_t>(target_file_size), first_extra_trailer);

   {
      //re-read entire file via read_device
      fc::sha256::encoder first_part_hash_enc;
      fc::random_access_file::device device = f.seekable_device();

      const int read_stride = 128*1024;
      for(unsigned wrote = 0; wrote < target_file_size; wrote += read_stride) {
         char b[read_stride];
         BOOST_REQUIRE_EQUAL(boost::iostreams::read(device, b, read_stride), read_stride);
         first_part_hash_enc.write(b, read_stride);
      }

      BOOST_REQUIRE_EQUAL(first_part_hash, first_part_hash_enc.result());

      //read the trailer
      char b[16];
      BOOST_REQUIRE_EQUAL(boost::iostreams::read(device, b, sizeof(b)), 8);
      uint64_t foundtrailer;
      memcpy(&foundtrailer, b, sizeof(foundtrailer));
      BOOST_REQUIRE_EQUAL(foundtrailer, first_extra_trailer);

      //try reading more, but we're at EOF
      BOOST_REQUIRE_EQUAL(boost::iostreams::read(device, b, sizeof(b)), -1);
   }

   //overwrite the trailer
   const uint64_t second_extra_trailer = 0x00ffee11'22ddcc33;
   f.pack_to(second_extra_trailer, target_file_size);

   //re-read the entire file again just like above, just check that second_extra_trailer is there instead (and tweak read stride to something different)
   {
      fc::sha256::encoder first_part_hash_enc;
      fc::random_access_file::device device = f.seekable_device();

      const int read_stride = 256*1024;
      for(unsigned wrote = 0; wrote < target_file_size; wrote += read_stride) {
         char b[read_stride];
         BOOST_REQUIRE_EQUAL(boost::iostreams::read(device, b, read_stride), read_stride);
         first_part_hash_enc.write(b, read_stride);
      }

      BOOST_REQUIRE_EQUAL(first_part_hash, first_part_hash_enc.result());

      //read the trailer
      char b[16];
      BOOST_REQUIRE_EQUAL(boost::iostreams::read(device, b, sizeof(b)), 8);
      uint64_t foundtrailer;
      memcpy(&foundtrailer, b, sizeof(foundtrailer));
      BOOST_REQUIRE_EQUAL(foundtrailer, second_extra_trailer);

      //try reading more, but we're at EOF
      BOOST_REQUIRE_EQUAL(boost::iostreams::read(device, b, sizeof(b)), -1);
   }

} FC_LOG_AND_RETHROW();

//make sure can read/write file after destruction of file object
//this test also implicitly tests that reading isn't "prebuffered"; which may need to be revisited in the future
BOOST_AUTO_TEST_CASE(after_dtor_datastreams) try {
   fc::temp_directory tmpdir;
   std::filesystem::path filepath = tmpdir.path() / "file";

   std::optional<fc::random_access_file> f(filepath);
   fc::random_access_file::read_datastream rd0 = f->read_ds(0);
   fc::random_access_file::read_datastream rd8 = f->read_ds(8);
   fc::random_access_file::read_datastream rd16 = f->read_ds(16);
   fc::random_access_file::read_datastream rd24 = f->read_ds(24);
   std::optional<fc::random_access_file::write_datastream> wds0(std::in_place, f->write_ds(0));
   std::optional<fc::random_access_file::write_datastream> wds8(std::in_place, f->write_ds(8));

   f.reset();
   fc::raw::pack(*wds0, (uint64_t)0x11112222'33334444);
   fc::raw::pack(*wds0, (uint64_t)0x55667788'99aabbcc);
   wds0.reset(); //flushes the above writes
   fc::raw::pack(*wds8, (uint64_t)0xffeeffee'00880088); //overwrites bytes 8-15
   fc::raw::pack(*wds8, (uint64_t)0x77665544'33221100);
   fc::raw::pack(*wds8, (uint64_t)0xbeefbeef'beefbeef);
   wds8.reset(); //flushes above writes

   uint64_t got;
   fc::raw::unpack(rd24, got);
   BOOST_REQUIRE_EQUAL(got, UINT64_C(0xbeefbeef'beefbeef));

   fc::raw::unpack(rd16, got);
   BOOST_REQUIRE_EQUAL(got, UINT64_C(0x77665544'33221100));
   fc::raw::unpack(rd16, got);
   BOOST_REQUIRE_EQUAL(got, UINT64_C(0xbeefbeef'beefbeef));

   fc::raw::unpack(rd0, got);
   BOOST_REQUIRE_EQUAL(got, UINT64_C(0x11112222'33334444));
   fc::raw::unpack(rd0, got);
   BOOST_REQUIRE_EQUAL(got, UINT64_C(0xffeeffee'00880088));
   fc::raw::unpack(rd0, got);
   BOOST_REQUIRE_EQUAL(got, UINT64_C(0x77665544'33221100));
   fc::raw::unpack(rd0, got);
   BOOST_REQUIRE_EQUAL(got, UINT64_C(0xbeefbeef'beefbeef));

   fc::raw::unpack(rd8, got);
   BOOST_REQUIRE_EQUAL(got, UINT64_C(0xffeeffee'00880088));

} FC_LOG_AND_RETHROW();

//tests that after the file is removed access is still possible
BOOST_AUTO_TEST_CASE(after_removal_datastreams) try {
   fc::temp_directory tmpdir;
   std::filesystem::path filepath = tmpdir.path() / "file";

   std::optional<fc::random_access_file> f(filepath);
   std::optional<fc::random_access_file::write_datastream> wds0(std::in_place, f->write_ds(0));
   fc::random_access_file::read_datastream rd0 = f->read_ds(0);

   fc::raw::pack(*wds0, (uint64_t)0x11112222'33334444);
   fc::raw::pack(*wds0, (uint64_t)0x55667788'99aabbcc);
   //the above are still buffered when...
   f.reset();
   std::filesystem::remove(filepath);

   //then they are flushed to the file
   wds0.reset();

   //can still read
   std::pair<uint64_t, uint64_t> got;
   fc::raw::unpack(rd0, got);
   BOOST_REQUIRE_EQUAL(got.first, UINT64_C(0x11112222'33334444));
   BOOST_REQUIRE_EQUAL(got.second, UINT64_C(0x55667788'99aabbcc));

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(hole_punch) try {
   fc::temp_directory tmpdir;
   std::filesystem::path filepath = tmpdir.path() / "file";

   fc::random_access_file f(filepath);
   fc::random_access_file::device device = f.seekable_device();

   const unsigned first_part_size = 16*1024*1024+678;
   const unsigned second_part_size = 15*1024*1024+444;
   const unsigned last_part_size = 22*1024*1024+23;

   fc::sha256 first_part_hash;
   fc::sha256 last_part_hash;
   {
      std::vector<char> buff;
      buff.resize(first_part_size);
      fc::rand_bytes(buff.data(), buff.size());
      BOOST_REQUIRE_EQUAL(device.write(buff.data(), buff.size()), (std::streamsize)buff.size());
      first_part_hash = fc::sha256::hash(buff.data(), buff.size());
   }
   {
      std::vector<char> buff;
      buff.resize(second_part_size);
      fc::rand_bytes(buff.data(), buff.size());
      BOOST_REQUIRE_EQUAL(device.write(buff.data(), buff.size()), (std::streamsize)buff.size());
   }
   {
      std::vector<char> buff;
      buff.resize(last_part_size);
      fc::rand_bytes(buff.data(), buff.size());
      BOOST_REQUIRE_EQUAL(device.write(buff.data(), buff.size()), (std::streamsize)buff.size());
      last_part_hash = fc::sha256::hash(buff.data(), buff.size());
   }

   f.punch_hole(first_part_size, first_part_size+second_part_size);
   device.seek(0, std::ios_base::beg);
   {
      std::vector<char> buff;
      buff.resize(first_part_size);
      BOOST_REQUIRE_EQUAL(device.read(buff.data(), buff.size()), (std::streamsize)buff.size());
      BOOST_REQUIRE_EQUAL(first_part_hash, fc::sha256::hash(buff.data(), buff.size()));
   }
   {
      std::vector<char> buff;
      buff.resize(second_part_size);
      BOOST_REQUIRE_EQUAL(device.read(buff.data(), buff.size()), (std::streamsize)buff.size());
      //just look at a smallish middle span of the punched hole
      std::vector<char> buff_subsection(buff.data() + 1024*1024, buff.data() + 2*1024*1024);
      std::vector<char> zero_buff;
      zero_buff.resize(buff_subsection.size());
      BOOST_REQUIRE(buff_subsection == zero_buff);
   }
   {
      std::vector<char> buff;
      buff.resize(last_part_size);
      BOOST_REQUIRE_EQUAL(device.read(buff.data(), buff.size()), (std::streamsize)buff.size());
      BOOST_REQUIRE_EQUAL(last_part_hash, fc::sha256::hash(buff.data(), buff.size()));
   }
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(read_only) try {
   fc::temp_directory tmpdir;
   std::filesystem::path filepath = tmpdir.path() / "file";

   {
      fc::random_access_file f(filepath);
      f.pack_to_end(uint64_t(0xbeef));
   }
   {
      fc::random_access_file f(filepath, fc::random_access_file::read_only);
      BOOST_REQUIRE_EQUAL(f.unpack_from<uint64_t>(0), 0xbeefu);
      BOOST_REQUIRE_EXCEPTION(f.pack_to_end(uint64_t(0xbeef)), fc::assert_exception, [](const fc::assert_exception& e) {
         return e.top_message().find("write failure on file") != std::string::npos && e.top_message().find("Bad file descriptor") != std::string::npos;
      });
   }

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()