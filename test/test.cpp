#define BOOST_TEST_MODULE chainbase test
#include <boost/test/unit_test.hpp>
#include <chainbase/chainbase.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>

#include <iostream>
#include "temp_directory.hpp"

using namespace chainbase;
using namespace boost::multi_index;

namespace bip = boost::interprocess;
//BOOST_TEST_SUITE( serialization_tests, clean_database_fixture )

struct book : public chainbase::object<0, book> {

   template<typename Constructor>
   book(  Constructor&& c, chainbase::constructor_tag ) {
       c(*this);
    }

    id_type id;
    int a = 0;
    int b = 1;
};

typedef multi_index_container<
  book,
  indexed_by<
     ordered_unique< member<book,book::id_type,&book::id> >,
     ordered_unique< BOOST_MULTI_INDEX_MEMBER(book,int,a) >,
     ordered_unique< BOOST_MULTI_INDEX_MEMBER(book,int,b) >
  >,
  chainbase::node_allocator<book>
> book_index;

CHAINBASE_SET_INDEX_TYPE( book, book_index )


BOOST_AUTO_TEST_CASE( open_and_create ) {
   temp_directory temp_dir;
   const auto& temp = temp_dir.path();
   std::cerr << temp << " \n";

   chainbase::database db(temp, database::read_write, 1024*1024*8, false, pinnable_mapped_file::map_mode::mapped);
   chainbase::database db2(temp, database::read_only, 0, true); /// open an already created db
   BOOST_CHECK_THROW( db2.add_index< book_index >(), std::runtime_error ); /// index does not exist in read only database

   db.add_index< book_index >();
   BOOST_CHECK_THROW( db.add_index<book_index>(), std::logic_error ); /// cannot add same index twice


   db2.add_index< book_index >(); /// index should exist now


   BOOST_TEST_MESSAGE( "Creating book" );
   const auto& new_book = db.create<book>( []( book& b ) {
      b.a = 3;
      b.b = 4;
   } );
   const auto& copy_new_book = db2.get( book::id_type(0) );
   BOOST_REQUIRE( &new_book != &copy_new_book ); ///< these are mapped to different address ranges

   BOOST_REQUIRE_EQUAL( new_book.a, copy_new_book.a );
   BOOST_REQUIRE_EQUAL( new_book.b, copy_new_book.b );

   db.modify( new_book, [&]( book& b ) {
      b.a = 5;
      b.b = 6;
   });
   BOOST_REQUIRE_EQUAL( new_book.a, 5 );
   BOOST_REQUIRE_EQUAL( new_book.b, 6 );

   BOOST_REQUIRE_EQUAL( new_book.a, copy_new_book.a );
   BOOST_REQUIRE_EQUAL( new_book.b, copy_new_book.b );

   {
      auto session = db.start_undo_session(true);
      db.modify( new_book, [&]( book& b ) {
         b.a = 7;
         b.b = 8;
      });

      BOOST_REQUIRE_EQUAL( new_book.a, 7 );
      BOOST_REQUIRE_EQUAL( new_book.b, 8 );
   }
   BOOST_REQUIRE_EQUAL( new_book.a, 5 );
   BOOST_REQUIRE_EQUAL( new_book.b, 6 );

   {
      auto session = db.start_undo_session(true);
      const auto& book2 = db.create<book>( [&]( book& b ) {
         b.a = 9;
         b.b = 10;
      });

      BOOST_REQUIRE_EQUAL( new_book.a, 5 );
      BOOST_REQUIRE_EQUAL( new_book.b, 6 );
      BOOST_REQUIRE_EQUAL( book2.a, 9 );
      BOOST_REQUIRE_EQUAL( book2.b, 10 );
   }
   BOOST_CHECK_THROW( db2.get( book::id_type(1) ), std::out_of_range );
   BOOST_REQUIRE_EQUAL( new_book.a, 5 );
   BOOST_REQUIRE_EQUAL( new_book.b, 6 );


   {
      auto session = db.start_undo_session(true);
      db.modify( new_book, [&]( book& b ) {
         b.a = 7;
         b.b = 8;
      });

      BOOST_REQUIRE_EQUAL( new_book.a, 7 );
      BOOST_REQUIRE_EQUAL( new_book.b, 8 );
      session.push();
   }
   BOOST_REQUIRE_EQUAL( new_book.a, 7 );
   BOOST_REQUIRE_EQUAL( new_book.b, 8 );
   db.undo();
   BOOST_REQUIRE_EQUAL( new_book.a, 5 );
   BOOST_REQUIRE_EQUAL( new_book.b, 6 );

   BOOST_REQUIRE_EQUAL( new_book.a, copy_new_book.a );
   BOOST_REQUIRE_EQUAL( new_book.b, copy_new_book.b );
}


// -----------------------------------------------------------------------------
//            Allow boost to print `shared_vector`
// -----------------------------------------------------------------------------
namespace std {
   template <typename T>
   std::ostream& operator<<(std::ostream& os, const shared_vector<T>& v)
   {
      os << "[";
      for (size_t i = 0; i < v.size(); ++i) {
         os << v[i];
         if (i != v.size() - 1)
            os << ", ";
      }
      os << "]\n";
      return os;
   }

   std::ostream& operator<<(std::ostream& os, const shared_string& ms) {
      os << std::string_view(ms.data(), ms.size());
      return os;
   }
}

// -----------------------------------------------------------------------------
//            Check `shared_vector` APIs
// -----------------------------------------------------------------------------
template<class SV, class VecOfVec, class Alloc, std::enable_if_t<std::is_constructible_v<typename SV::value_type, int>, int> = 0 >
void check_shared_vector_apis(VecOfVec& vec_of_vec, const Alloc& expected_alloc)
{
   // check constructors
   // ------------------
   const std::array int_array  { 0, 1, 2, 3, 4, 5 };
   const std::array int_array2 { 6, 7 };
      
   {
      // check constructor `shared_cow_vector(Iter begin, Iter end)`
      // -----------------------------------------------------------
      vec_of_vec.clear();
      vec_of_vec.emplace_back(int_array.cbegin(), int_array.cend());
      const auto& v = vec_of_vec.back();
      BOOST_REQUIRE_EQUAL(v.size(), int_array.size());
      for (size_t i=0; i<int_array.size(); ++i)
         BOOST_REQUIRE_EQUAL(v[i], int_array[i]);

      // check that objects are allocated where we expect (i.e. using the same allocator as `vec_of_vec`)
      // ------------------------------------------------------------------------------------------------
      BOOST_REQUIRE(v.get_allocator() == expected_alloc);
      if constexpr(!std::is_same_v<typename SV::value_type, int>)
         BOOST_REQUIRE(v[0].get_allocator() == expected_alloc);
   }
      
   {
      // check constructor `shared_cow_vector(const T* ptr, std::size_t size)` 
      // ---------------------------------------------------------------------
      vec_of_vec.clear();
      vec_of_vec.emplace_back(&int_array[0], int_array.size());
      const auto& v = vec_of_vec.back();
      BOOST_REQUIRE_EQUAL(v.size(), int_array.size());
      for (size_t i=0; i<int_array.size(); ++i)
         BOOST_REQUIRE_EQUAL(v[i], int_array[i]);
   }

   {
      // check copy constructor. Verify copy-on-write after assign
      // ---------------------------------------------------------
      vec_of_vec.clear();
      vec_of_vec.emplace_back(int_array.cbegin(), int_array.cend());
      vec_of_vec.emplace_back(vec_of_vec[0]);
      auto& v0 = vec_of_vec[0];
      auto& v1 = vec_of_vec[1];
      BOOST_REQUIRE_EQUAL(v0, v1);               // check that copy construction worked
      BOOST_REQUIRE_EQUAL(v0.data(), v1.data()); // check copy_on_write (cow)

      // now change vector and verify copy happened
      v0 = SV(int_array.cbegin(), int_array.cend());
      BOOST_REQUIRE_EQUAL(v0, v1);               // still holding same values
      BOOST_REQUIRE_NE(v0.data(), v1.data());    // but copy happened after v0 modified with `assign`
   }

   {
      // check move constructor.
      // -----------------------
      vec_of_vec.clear();
      vec_of_vec.emplace_back(int_array.cbegin(), int_array.cend());
      auto& v0 = vec_of_vec[0];
      vec_of_vec.emplace_back(std::move(v0));
      auto& v1 = vec_of_vec[1];
      BOOST_REQUIRE_EQUAL(v0.size(), 0u);
      BOOST_REQUIRE_EQUAL(v0.data(), nullptr); 
      for (size_t i=0; i<int_array.size(); ++i)
         BOOST_REQUIRE_EQUAL(v1[i], int_array[i]);
   }

   {
      // check `initializer_list` and `std::vector` constructors.
      // ---------------------------------------------------------
      vec_of_vec.clear();
      vec_of_vec.emplace_back(std::initializer_list<int> { 0, 1, 2, 3, 4, 5 });
      vec_of_vec.emplace_back(std::vector<int>{int_array.cbegin(), int_array.cend()});
      
      auto& v0 = vec_of_vec[0];
      auto& v1 = vec_of_vec[1];
      BOOST_REQUIRE_EQUAL(v0, v1);               // same values
      BOOST_REQUIRE_NE(v0.data(), v1.data());    // different pointers
   }

   {
      // check assignment operator. Verify copy-on-write after assign
      // ------------------------------------------------------------
      vec_of_vec.clear();
      vec_of_vec.emplace_back(int_array.cbegin(), int_array.cend());
      vec_of_vec.emplace_back(vec_of_vec[0]);
      {
         auto& v0 = vec_of_vec[0];
         auto& v1 = vec_of_vec[1];
         BOOST_REQUIRE_EQUAL(v0, v1);               // check that copy construction worked
         BOOST_REQUIRE_EQUAL(v0.data(), v1.data()); // check copy_on_write (cow)

         // assignment should not break cow if they are already the same
         v0 = v1;
         BOOST_REQUIRE_EQUAL(v0.data(), v1.data()); 
      }

      // add a third one and assign it to v0
      vec_of_vec.emplace_back(int_array.cbegin(), int_array.cend());
      {
         auto& v0 = vec_of_vec[0];
         auto& v1 = vec_of_vec[1];
         auto& v2 = vec_of_vec[2];

         v0 = v2;
         BOOST_REQUIRE_NE(v0.data(), v1.data()); 
         BOOST_REQUIRE_EQUAL(v0.data(), v2.data()); // check copy_on_write (cow)
         BOOST_REQUIRE_EQUAL(v0, v1); 
         BOOST_REQUIRE_EQUAL(v0, v2); 
         BOOST_REQUIRE_EQUAL(v1, v2); 
      }
      
   }

   {
      // check move assignment operator.
      // -------------------------------
      vec_of_vec.clear();
      vec_of_vec.emplace_back(int_array.cbegin(), int_array.cend());
      vec_of_vec.emplace_back(int_array2.cbegin(), int_array2.cend());
      {
         auto& v0 = vec_of_vec[0];
         auto& v1 = vec_of_vec[1];
         
         BOOST_REQUIRE_NE(v0.data(), v1.data()); 
         BOOST_REQUIRE_NE(v0, v1);
         BOOST_REQUIRE_EQUAL(v0.size(), int_array.size());
         BOOST_REQUIRE_EQUAL(v1.size(), int_array2.size());

         v0 = std::move(v1);
         BOOST_REQUIRE_EQUAL(v0.size(), int_array2.size());
         BOOST_REQUIRE_EQUAL(v1.size(), 0u);
         BOOST_REQUIRE(v1.empty());

         v1 = std::move(v0);
         BOOST_REQUIRE_EQUAL(v1.size(), int_array2.size());
         BOOST_REQUIRE_EQUAL(v0.size(), 0u);
      }
   }

   { 
      // check assignment from std::vector.
      // ----------------------------------
      vec_of_vec.clear();
      vec_of_vec.emplace_back(int_array.cbegin(), int_array.cend());
      vec_of_vec.emplace_back();
      
      auto& v0 = vec_of_vec[0];
      auto& v1 = vec_of_vec[1];
      auto v = SV(int_array.cbegin(), int_array.cend());
      v1 = v;
      BOOST_REQUIRE_EQUAL(v0, v1);
      BOOST_REQUIRE_NE(v0.data(), v1.data()); 
   }

   { 
      // check move assignment from std::vector.
      // ---------------------------------------
      vec_of_vec.clear();
      vec_of_vec.emplace_back(int_array.cbegin(), int_array.cend());
      vec_of_vec.emplace_back();
      
      auto& v0 = vec_of_vec[0];
      auto& v1 = vec_of_vec[1];

      v1 = SV(int_array.cbegin(), int_array.cend());
      BOOST_REQUIRE_EQUAL(v0, v1);
      BOOST_REQUIRE_NE(v0.data(), v1.data()); 
   }

   {
      // check emplace_back(), clear(), size()
      // -------------------------------------
      vec_of_vec.clear();
      vec_of_vec.emplace_back(int_array.cbegin(), int_array.cend());
      auto& v = vec_of_vec.back();
      auto sz = v.size();
      v.emplace_back(1);
      v.emplace_back(2);
      BOOST_REQUIRE_EQUAL(v.size(), sz + 2);

      v.clear();
      BOOST_REQUIRE_EQUAL(v.size(), 0u);
   }

   { 
      // check clear_and_construct()
      // ---------------------------
      vec_of_vec.clear();
      vec_of_vec.emplace_back(int_array.cbegin(), int_array.cend());
      vec_of_vec.emplace_back(int_array2.cbegin(), int_array2.cend());
      
      auto& v0 = vec_of_vec[0];
      auto& v1 = vec_of_vec[1];
      BOOST_REQUIRE_NE(v0, v1);
      BOOST_REQUIRE_NE(v0.data(), v1.data());

      v1.clear_and_construct(v0.size(), 0, [&](auto* dest, std::size_t idx) {
         std::construct_at(dest, v0[idx]);
      });
      
      BOOST_REQUIRE_EQUAL(v0, v1);
      BOOST_REQUIRE_NE(v0.data(), v1.data());
   }
}

// -------------------------------------------------------------------------------
// crtp counter to check thee `shared_cow_vector` constructs and destroys correctly
// -------------------------------------------------------------------------------
template <class T>
class instance_counter {
public:
   instance_counter() { ++_count; }
   instance_counter(const instance_counter&) { ++_count; }
   instance_counter(instance_counter&&) = default;

   bool operator==(const instance_counter&) const { return true; }
   
   instance_counter& operator=(const instance_counter&) = default;
   instance_counter& operator=(instance_counter&&) = default;
   
  ~instance_counter() { --_count; }

  static int num_instances() { return _count; }

private:
  static inline uint32_t _count = 0;
};

// ----------------------------------------------------------------------------------------
// `check_shared_vector_apis` requires a type that can be constructed from integers, and
// has an operator=(int). Let's create a type with a non-trivial constructor which fulfills
// that contract.
// Also let's count the instances of my_string so we can make sure they are all destroyed
// correctly
// ----------------------------------------------------------------------------------------
struct my_string : public instance_counter<my_string> {
   static inline constexpr const char* trailer = "_00000000000000000000000000000000000"; // bypasss short string optimization
   my_string() = default;
   my_string(int i) : _s(std::to_string(i) + trailer) {}

   bool operator==(const my_string& o) const = default;
   bool operator==(int i) const { return _s == std::to_string(i) + trailer; }

   friend std::ostream& operator<<(std::ostream& os, const my_string& ms) {
      os << std::string_view(ms._s.data(), ms._s.size());
      return os;
   }

   auto get_allocator() const { return _s.get_allocator(); }

   shared_string _s;
};

// -----------------------------------------------------------------------------
//   Test `shared_cow_vector` APIs when using std::allocator.
// -----------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(shared_vector_apis_stdalloc) {
   std::optional<chainbase::allocator<char>> expected_alloc;
   
   {
      // do the test with `shared_vector<int>` (trivial destructor)
      // ----------------------------------------------------------
      using sv = shared_vector<int>;
      sv v;
      std::vector<sv, std::allocator<sv>> vec_of_vec;
      
      check_shared_vector_apis<sv, decltype(vec_of_vec)>(vec_of_vec, expected_alloc);
   }

   {
      // do the test with `shared_vector<my_string>` (non-trivial destructor)
      // --------------------------------------------------------------------
      using sv = shared_vector<my_string>;
      sv v;
      std::vector<sv, std::allocator<sv>> vec_of_vec;
      
      check_shared_vector_apis<sv, decltype(vec_of_vec)>(vec_of_vec, expected_alloc);

      // clear both vectors. If our implementation of `shared_cow_vector` is correct, we should have an exact
      // match of the number of constructed and destroyed `my_string` objects, and therefore after clearing the vectors
      // the count should be zero.
      // -------------------------------------------------------------------------------------------------------------
      v.clear();
      vec_of_vec.clear();
      BOOST_REQUIRE_EQUAL(my_string::num_instances(), 0);
   }
}

// -------------------------------------------------------------------------------------------
//   Test `shared_cow_vector` APIs when the vectors are allocated in the shared memory segment
// -------------------------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(shared_vector_apis_segment_alloc) {
   
   temp_directory temp_dir;
   const auto& temp = temp_dir.path();

   pinnable_mapped_file pmf(temp, true, 1024 * 1024, false, pinnable_mapped_file::map_mode::mapped);
   std::optional<chainbase::allocator<char>> expected_alloc = chainbase::allocator<char>(pmf.get_segment_manager());

   size_t free_memory = pmf.get_segment_manager()->get_free_memory();
   
   {
      // do the test with `shared_vector<int>` (trivial destructor)
      // ----------------------------------------------------------
      using sv = shared_vector<int>;
      chainbase::allocator<sv> sv_alloc(pmf.get_segment_manager());
      sv v;

      bip::vector<sv, chainbase::allocator<sv>> vec_of_vec(sv_alloc);
      
      check_shared_vector_apis<sv, decltype(vec_of_vec)>(vec_of_vec, expected_alloc);
   }

   {
      // do the test with `shared_vector<my_string>` (non-trivial destructor)
      // --------------------------------------------------------------------
      using sv = shared_vector<my_string>;
      chainbase::allocator<sv> sv_alloc(pmf.get_segment_manager());
      sv v;
      
      bip::vector<sv, chainbase::allocator<sv>> vec_of_vec(sv_alloc);
      
      check_shared_vector_apis<sv, decltype(vec_of_vec)>(vec_of_vec, expected_alloc);

      // clear both vectors. If our implementation of `shared_cow_vector` is correct, we should have an exact
      // match of the number of constructed and destroyed `my_string` objects, and therefore after clearing the vectors
      // the count should be zero.
      // -------------------------------------------------------------------------------------------------------------
      v.clear();
      vec_of_vec.clear();
      BOOST_REQUIRE_EQUAL(my_string::num_instances(), 0);
   }

   // make sure we didn't leak memory
   // -------------------------------
   BOOST_REQUIRE_EQUAL(free_memory, pmf.get_segment_manager()->get_free_memory());
}

// -----------------------------------------------------------------------------
//   Test `shared_cow_string` APIs - in addition to what's already tested above
// -----------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(shared_cow_string_apis) {
   const std::string test_string { "this is just a random text string" };

   {
      // test constructors
      // -----------------
      shared_cow_string s0 { test_string.cbegin(), test_string.cend() };
      BOOST_REQUIRE_EQUAL(s0, test_string);
      BOOST_REQUIRE_EQUAL(s0.size(), test_string.size());

      shared_cow_string s1 { test_string.c_str(), test_string.size() };
      BOOST_REQUIRE_EQUAL(s1, test_string);

      shared_cow_string s2 { test_string.c_str() };
      BOOST_REQUIRE_EQUAL(s2, test_string);

      shared_cow_string s3 { std::string_view(test_string) };
      BOOST_REQUIRE_EQUAL(s3, test_string);

      shared_cow_string s4 { test_string.size(), boost::container::default_init_t() };
      BOOST_REQUIRE_EQUAL(s4.data()[test_string.size()], 0); // null terminator should be added by constructor
      std::memcpy(s4.mutable_data(), test_string.c_str(), test_string.size());
      BOOST_REQUIRE_EQUAL(s4, test_string);

      shared_cow_string s5(s4);
      BOOST_REQUIRE_EQUAL(s5, test_string);
      BOOST_REQUIRE_EQUAL((void *)s5.data(), (void *)s4.data()); // check copy-on-write

      shared_cow_string s6(std::move(s4));
      BOOST_REQUIRE_EQUAL(s6, test_string);
      BOOST_REQUIRE_EQUAL((void *)s6.data(), (void *)s5.data()); // copy-on-write should remain between s6 and s5
      
      BOOST_REQUIRE_EQUAL(s4.size(), 0u);         // s4 moved from... should now be empty
      BOOST_REQUIRE_EQUAL(s4.data(), nullptr);
      BOOST_REQUIRE_EQUAL(s4.mutable_data(), nullptr);
   }

   {
      // test operator=()
      // ----------------
      shared_cow_string s0 { test_string };
      BOOST_REQUIRE_EQUAL(s0, test_string);

      shared_cow_string s1;
      BOOST_REQUIRE_EQUAL(s1.size(), 0u);
      BOOST_REQUIRE_EQUAL(s1.data(), nullptr);
      
      s1 = s0;                                   // copy assignment
      BOOST_REQUIRE_EQUAL(s1, test_string);
      BOOST_REQUIRE_EQUAL((void *)s1.data(), (void *)s0.data()); // check copy-on-write

      s1 = std::move(s0);                        // move assignment
      BOOST_REQUIRE_EQUAL(s1, test_string);
      BOOST_REQUIRE_NE(s1.data(), s0.data());    // check copy-on-write broken
      BOOST_REQUIRE_EQUAL(s0.size(), 0u);        // s0 moved from... should now be empty
      BOOST_REQUIRE_EQUAL(s0.data(), nullptr);
   }

   {
      // test begin()/end()
      // ------------------
      shared_cow_string s0 { test_string.cbegin(), test_string.cend() };
      shared_cow_string s1 { s0.begin(), s0.end() };
      BOOST_REQUIRE_EQUAL(s0, s1);
      BOOST_REQUIRE_NE((void *)s0.data(), (void *)s1.data());  
   }

   {
      // test assign
      // -----------
      shared_cow_string s0;
      s0.assign((const char *)test_string.c_str(), test_string.size());
      BOOST_REQUIRE_EQUAL(s0, test_string);
      BOOST_REQUIRE_EQUAL(s0.size(), test_string.size());
      
      shared_cow_string s1;
      s1.assign((const unsigned char *)test_string.c_str(), test_string.size());
      BOOST_REQUIRE_EQUAL(s1, test_string);
      BOOST_REQUIRE_EQUAL(s1.size(), test_string.size());
   }

   {
      // test comparison operator
      // ------------------------
      shared_cow_string s0("abc");
      shared_cow_string s1("");
      shared_cow_string s2;
      shared_cow_string s3("xaaa");

      BOOST_REQUIRE_LT(s0, s3);
      BOOST_REQUIRE_LT(s1, s3);
      BOOST_REQUIRE_LT(s2, s3);
   }
}


// -----------------------------------------------------------------------------
//      Check chainbase operations on items containing `shared` types
// -----------------------------------------------------------------------------
struct titled_book : public chainbase::object<0, titled_book> {

   template<typename Constructor>
   titled_book( Constructor&& c, chainbase::constructor_tag ) : title(), authors() {
      c(*this);
   }

   id_type id;
   shared_string title;
   shared_vector<shared_string> authors;
};

typedef multi_index_container<
  titled_book,
  indexed_by<
     ordered_unique< member<titled_book,titled_book::id_type,&titled_book::id> >,
     ordered_unique< BOOST_MULTI_INDEX_MEMBER(titled_book,shared_string,title) >
  >,
  chainbase::node_allocator<titled_book>
> titled_book_index;

CHAINBASE_SET_INDEX_TYPE( titled_book, titled_book_index )


BOOST_AUTO_TEST_CASE( shared_string_object ) {
   temp_directory temp_dir;
   const auto& temp = temp_dir.path();

   chainbase::database db(temp, database::read_write, 1024*1024*8);
   chainbase::database db2(temp, database::read_only, 0, true); /// open an already created db
   BOOST_CHECK_THROW( db2.add_index< titled_book_index >(), std::runtime_error ); /// index does not exist in read only database

   db.add_index< titled_book_index >();
   BOOST_CHECK_THROW( db.add_index<titled_book_index>(), std::logic_error ); /// cannot add same index twice

   db2.add_index< titled_book_index >(); /// index should exist now

   BOOST_TEST_MESSAGE( "Creating titled_book" );
   const auto& new_titled_book = db.create<titled_book>( []( titled_book& b) {
      b.title = "Moby Dick";
      b.authors = { "Herman Melville" };
   } );
   const auto& copy_new_titled_book = db2.get( titled_book::id_type(0) );
   BOOST_REQUIRE( &new_titled_book != &copy_new_titled_book ); ///< these are mapped to different address ranges

   BOOST_REQUIRE( new_titled_book.title == copy_new_titled_book.title );
   BOOST_REQUIRE( new_titled_book.authors == copy_new_titled_book.authors );

   
   std::vector apm_authors { "Carl Bernstein", "Bob Woodward" };
   db.modify( new_titled_book, [&]( titled_book& b ) {
      b.title = "All the President's Men";
      b.authors = apm_authors;
   });
   BOOST_REQUIRE( new_titled_book.title == "All the President's Men" );
   BOOST_REQUIRE( new_titled_book.authors[0] == apm_authors[0] );
   BOOST_REQUIRE( new_titled_book.authors[1] == apm_authors[1] );

   BOOST_REQUIRE( new_titled_book.title == copy_new_titled_book.title );
   BOOST_REQUIRE( new_titled_book.authors == copy_new_titled_book.authors );
}


// behavior of these tests are dependent on linux's overcommit behavior, they are also dependent on the system not having
// enough memory+swap to balk at 6TB request
#if defined(__linux__)

static bool overcommit_protection_enabled() {
   std::ifstream ocmem_file("/proc/sys/vm/overcommit_memory");
   BOOST_REQUIRE(ocmem_file.good());
   std::string ocmem_contents((std::istreambuf_iterator<char>(ocmem_file)), std::istreambuf_iterator<char>());

   return ocmem_contents.at(0) == '0' || ocmem_contents.at(0) == '2';
}

BOOST_AUTO_TEST_CASE( mapped_big_boy ) {
   temp_directory temp_dir;
   const auto& temp = temp_dir.path();

   //silently pass test if system not configured for overcommit protection
   if(!overcommit_protection_enabled())
      return;

   BOOST_REQUIRE_THROW(chainbase::database(temp, database::read_write, 1024ull*1024*1024*1024*6, false, pinnable_mapped_file::map_mode::mapped_private), boost::interprocess::interprocess_exception);
   chainbase::database(temp, database::read_write, 0, false);
}

BOOST_AUTO_TEST_CASE( mapped_big_boy_extra ) {
   temp_directory temp_dir;
   const auto& temp = temp_dir.path();

   //silently pass test if system not configured for overcommit protection
   if(!overcommit_protection_enabled())
      return;

   chainbase::database(temp, database::read_write, 1024ull*1024*1024*1024*6, false);
   BOOST_REQUIRE_THROW(chainbase::database(temp, database::read_write, 0, false, pinnable_mapped_file::map_mode::mapped_private), boost::interprocess::interprocess_exception);
   chainbase::database(temp, database::read_write, 0, false);
}

#endif

// BOOST_AUTO_TEST_SUITE_END()
