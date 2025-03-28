#pragma once

#include <eosio/chain/database_utils.hpp>
#include <eosio/chain/exceptions.hpp>
#include <fc/variant_object.hpp>
#include <fc/io/random_access_file.hpp>
#include <boost/core/demangle.hpp>
#include <ostream>
#include <memory>

namespace eosio { namespace chain {
   /**
    * History:
    * Version 1: initial version with string identified sections and rows
    */
   static const uint32_t current_snapshot_version = 1;

   namespace detail {
      template<typename T>
      struct snapshot_section_traits {
         static std::string section_name() {
            return boost::core::demangle(typeid(T).name());
         }
      };

      template<typename T>
      struct snapshot_row_traits {
         using value_type = std::decay_t<T>;
         using snapshot_type = value_type;

         static const snapshot_type& to_snapshot_row( const value_type& value, const chainbase::database& ) {
            return value;
         };
      };

      /**
       * Due to a pattern in our code of overloading `operator << ( std::ostream&, ... )` to provide
       * human-readable string forms of data, we cannot directly use ostream as those operators will
       * be used instead of the expected operators.  In otherwords:
       * fc::raw::pack(fc::datastream...)
       * will end up calling _very_ different operators than
       * fc::raw::pack(std::ostream...)
       */
      struct ostream_wrapper {
         explicit ostream_wrapper(std::ostream& s)
         :inner(s) {

         }

         ostream_wrapper(ostream_wrapper &&) = default;
         ostream_wrapper(const ostream_wrapper& ) = default;

         auto& write( const char* d, size_t s ) {
            return inner.write(d, s);
         }

         auto& put(char c) {
           return inner.put(c);
         }

         auto tellp() const {
            return inner.tellp();
         }

         auto& seekp(std::ostream::pos_type p) {
            return inner.seekp(p);
         }

         std::ostream& inner;
      };


      struct abstract_snapshot_row_writer {
         virtual void write(ostream_wrapper& out) const = 0;
         virtual void write(fc::sha256::encoder& out) const = 0;
         virtual fc::variant to_variant() const = 0;
         virtual std::string row_type_name() const = 0;
      };

      template<typename T>
      struct snapshot_row_writer : abstract_snapshot_row_writer {
         explicit snapshot_row_writer( const T& data )
         :data(data) {}

         template<typename DataStream>
         void write_stream(DataStream& out) const {
            fc::raw::pack(out, data);
         }

         void write(ostream_wrapper& out) const override {
            write_stream(out);
         }

         void write(fc::sha256::encoder& out) const override {
            write_stream(out);
         }

         fc::variant to_variant() const override {
            fc::variant var;
            fc::to_variant(data, var);
            return var;
         }

         std::string row_type_name() const override {
            return boost::core::demangle( typeid( T ).name() );
         }

         const T& data;
      };

      template<typename T>
      snapshot_row_writer<T> make_row_writer( const T& data) {
         return snapshot_row_writer<T>(data);
      }
   }

   class snapshot_writer {
      public:
         class section_writer {
            public:
               template<typename T>
               void add_row( const T& row, const chainbase::database& db ) {
                  _writer.write_row(detail::make_row_writer(detail::snapshot_row_traits<T>::to_snapshot_row(row, db)));
               }

            private:
               friend class snapshot_writer;
               section_writer(snapshot_writer& writer)
               :_writer(writer)
               {

               }
               snapshot_writer& _writer;
         };

         template<typename F>
         void write_section(const std::string section_name, F f) {
            write_start_section(section_name);
            auto section = section_writer(*this);
            f(section);
            write_end_section();
         }

         template<typename T, typename F>
         void write_section(F f) {
            write_section(detail::snapshot_section_traits<T>::section_name(), f);
         }

         virtual ~snapshot_writer(){};

         virtual const char* name() const = 0;

      protected:
         virtual void write_start_section( const std::string& section_name ) = 0;
         virtual void write_row( const detail::abstract_snapshot_row_writer& row_writer ) = 0;
         virtual void write_end_section() = 0;
   };

   using snapshot_writer_ptr = std::shared_ptr<snapshot_writer>;

   namespace detail {
      struct abstract_snapshot_row_reader {
         virtual void provide(std::istream& in) const = 0;
         virtual void provide(const fc::variant&) const = 0;
         virtual void provide(fc::datastream<const char*>&) const = 0;
         virtual std::string row_type_name() const = 0;
      };

      template<typename T>
      struct is_chainbase_object {
         static constexpr bool value = false;
      };

      template<uint16_t TypeNumber, typename Derived>
      struct is_chainbase_object<chainbase::object<TypeNumber, Derived>> {
         static constexpr bool value = true;
      };

      template<typename T>
      constexpr bool is_chainbase_object_v = is_chainbase_object<T>::value;

      struct row_validation_helper {
         template<typename T, typename F>
         static auto apply(const T& data, F f) -> std::enable_if_t<is_chainbase_object_v<T>> {
            auto orig = data.id;
            f();
            EOS_ASSERT(orig == data.id, snapshot_exception,
                       "Snapshot for ${type} mutates row member \"id\" which is illegal",
                       ("type",boost::core::demangle( typeid( T ).name() )));
         }

         template<typename T, typename F>
         static auto apply(const T&, F f) -> std::enable_if_t<!is_chainbase_object_v<T>> {
            f();
         }
      };

      template<typename T>
      struct snapshot_row_reader : abstract_snapshot_row_reader {
         explicit snapshot_row_reader( T& data )
         :data(data) {}


         void provide(std::istream& in) const override {
            row_validation_helper::apply(data, [&in,this](){
               fc::raw::unpack(in, data);
            });
         }

         void provide(const fc::variant& var) const override {
            row_validation_helper::apply(data, [&var,this]() {
               fc::from_variant(var, data);
            });
         }

         void provide(fc::datastream<const char*>& ds) const override{
            row_validation_helper::apply(data, [&ds,this]() {
               fc::raw::unpack(ds, data);
            });
         }

         std::string row_type_name() const override {
            return boost::core::demangle( typeid( T ).name() );
         }

         T& data;
      };

      template<typename T>
      snapshot_row_reader<T> make_row_reader( T& data ) {
         return snapshot_row_reader<T>(data);
      }
   }

   class snapshot_reader {
      public:
         class section_reader {
            public:
               template<typename T>
               auto read_row( T& out ) -> std::enable_if_t<std::is_same<std::decay_t<T>, typename detail::snapshot_row_traits<T>::snapshot_type>::value,bool> {
                  auto reader = detail::make_row_reader(out);
                  return _reader.read_row(reader);
               }

               template<typename T>
               auto read_row( T& out, chainbase::database& ) -> std::enable_if_t<std::is_same<std::decay_t<T>, typename detail::snapshot_row_traits<T>::snapshot_type>::value,bool> {
                  return read_row(out);
               }

               template<typename T>
               auto read_row( T& out, chainbase::database& db ) -> std::enable_if_t<!std::is_same<std::decay_t<T>, typename detail::snapshot_row_traits<T>::snapshot_type>::value,bool> {
                  auto temp = typename detail::snapshot_row_traits<T>::snapshot_type();
                  auto reader = detail::make_row_reader(temp);
                  bool result = _reader.read_row(reader);
                  detail::snapshot_row_traits<T>::from_snapshot_row(std::move(temp), out, db);
                  return result;
               }

               bool empty() {
                  return _reader.empty();
               }

            private:
               friend class snapshot_reader;
               section_reader(snapshot_reader& _reader)
               :_reader(_reader)
               {}

               snapshot_reader& _reader;

         };

      template<typename F>
      void read_section(const std::string& section_name, F f) {
         set_section(section_name);
         auto section = section_reader(*this);
         f(section);
         clear_section();
      }

      template<typename T, typename F>
      void read_section(F f) {
         read_section(detail::snapshot_section_traits<T>::section_name(), f);
      }

      virtual void validate() = 0;

      virtual void return_to_header() = 0;

      virtual size_t total_row_count() = 0;

      virtual bool supports_threading() const {return false;}

      virtual ~snapshot_reader(){};

      protected:
         virtual void set_section( const std::string& section_name ) = 0;
         virtual bool read_row( detail::abstract_snapshot_row_reader& row_reader ) = 0;
         virtual bool empty( ) = 0;
         virtual void clear_section() = 0;
   };

   using snapshot_reader_ptr = std::shared_ptr<snapshot_reader>;

   class variant_snapshot_writer : public snapshot_writer {
      public:
         variant_snapshot_writer(fc::mutable_variant_object& snapshot);

         const char* name() const override { return "variant snapshot"; }
         void write_start_section( const std::string& section_name ) override;
         void write_row( const detail::abstract_snapshot_row_writer& row_writer ) override;
         void write_end_section( ) override;
         void finalize();

      private:
         fc::mutable_variant_object& snapshot;
         std::string current_section_name;
         fc::variants current_rows;
   };

   class variant_snapshot_reader : public snapshot_reader {
      public:
         explicit variant_snapshot_reader(const fc::variant& snapshot);

         void validate() override;
         void set_section( const string& section_name ) override;
         bool read_row( detail::abstract_snapshot_row_reader& row_reader ) override;
         bool empty ( ) override;
         void clear_section() override;
         void return_to_header() override;
         size_t total_row_count() override;

      private:
         const fc::variant& snapshot;
         const fc::variant_object* cur_section;
         uint64_t cur_row;
   };

   class ostream_snapshot_writer : public snapshot_writer {
      public:
         explicit ostream_snapshot_writer(std::ostream& snapshot);

         const char* name() const override { return "snapshot"; }
         void write_start_section( const std::string& section_name ) override;
         void write_row( const detail::abstract_snapshot_row_writer& row_writer ) override;
         void write_end_section( ) override;
         void finalize();

         static const uint32_t magic_number = 0x30510550;

      private:
         detail::ostream_wrapper snapshot;
         std::streampos          header_pos;
         std::streampos          section_pos;
         uint64_t                row_count;
   };

   class ostream_json_snapshot_writer : public snapshot_writer {
      public:
         explicit ostream_json_snapshot_writer(std::ostream& snapshot);

         const char* name() const override { return "JSON snapshot"; }
         void write_start_section( const std::string& section_name ) override;
         void write_row( const detail::abstract_snapshot_row_writer& row_writer ) override;
         void write_end_section() override;
         void finalize();

         static const uint32_t magic_number = 0x30510550;

      private:
         detail::ostream_wrapper snapshot;
         uint64_t                row_count;
   };

   class istream_snapshot_reader : public snapshot_reader {
      public:
         explicit istream_snapshot_reader(std::istream& snapshot);

         void validate() override;
         void set_section( const string& section_name ) override;
         bool read_row( detail::abstract_snapshot_row_reader& row_reader ) override;
         bool empty ( ) override;
         void clear_section() override;
         void return_to_header() override;
         size_t total_row_count() override;

      private:
         bool validate_section() const;

         std::istream&  snapshot;
         std::streampos header_pos;
         uint64_t       num_rows;
         uint64_t       cur_row;
   };

   class istream_json_snapshot_reader : public snapshot_reader {
      public:
         explicit istream_json_snapshot_reader(const std::filesystem::path& p);
         ~istream_json_snapshot_reader();

         void validate() override;
         void set_section( const string& section_name ) override;
         bool read_row( detail::abstract_snapshot_row_reader& row_reader ) override;
         bool empty ( ) override;
         void clear_section() override;
         void return_to_header() override;
         size_t total_row_count() override;

      private:
         bool validate_section() const;

         std::unique_ptr<struct istream_json_snapshot_reader_impl> impl;
   };

   class threaded_snapshot_reader : public snapshot_reader {
      public:
         explicit threaded_snapshot_reader(const std::filesystem::path& snapshot_path);

         void validate() override;
         void set_section( const string& section_name ) override;
         bool read_row( detail::abstract_snapshot_row_reader& row_reader ) override;
         bool empty ( ) override;
         void clear_section() override;
         void return_to_header() override;
         size_t total_row_count() override;
         bool supports_threading() const override {return true;}

      private:
         fc::random_access_file                   snapshot_file;
         const boost::interprocess::mapped_region mapped_snap;
         const char* const                        mapped_snap_addr;

         thread_local inline static fc::datastream<const char*> ds = fc::datastream<const char*>(nullptr, 0);
         thread_local inline static uint64_t                    num_rows;
         thread_local inline static uint64_t                    cur_row;
   };

   class integrity_hash_snapshot_writer : public snapshot_writer {
      public:
         explicit integrity_hash_snapshot_writer(fc::sha256::encoder&  enc);

         const char* name() const override { return "integrity hash"; }
         void write_start_section( const std::string& section_name ) override;
         void write_row( const detail::abstract_snapshot_row_writer& row_writer ) override;
         void write_end_section( ) override;
         void finalize();

      private:
         fc::sha256::encoder&  enc;

   };
   
   struct snapshot_written_row_counter {
      snapshot_written_row_counter(const size_t total, const char* name) : total(total), name(name) {}
      void progress() {
         if(++count % 50000 == 0 && time(NULL) - last_print >= 5) {
            ilog("${n} creation ${pct}% complete", ("n", name)("pct",std::min((unsigned)(((double)count/total)*100),100u)));
            last_print = time(NULL);
         }
      }
      size_t count = 0;
      const size_t total = 0;
      const char* name = nullptr;
      time_t last_print = time(NULL);
   };

   fc::variant snapshot_info(snapshot_reader& snapshot);
}}
