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
      for (int i = 0; i < v.size(); ++i) {
         os << v[i];
         if (i != v.size() - 1)
            os << ", ";
      }
      os << "]\n";
      return os;
   }
}

// -----------------------------------------------------------------------------
//            Check `shared_vector` APIs
// -----------------------------------------------------------------------------
template<class SV, class VecOfVec, std::enable_if_t<std::is_constructible_v<typename SV::value_type, int>, int> = 0 >
void check_shared_vector_apis(SV &v, VecOfVec& vec_of_vec)
{
   // check constructors
   // ------------------
   const std::array int_array { 0, 1, 2, 3, 4, 5 };
      
   {
      // check constructor `shared_cow_vector(Iter begin, Iter end)`
      // -----------------------------------------------------------
      vec_of_vec.emplace_back(int_array.cbegin(), int_array.cend());
      const auto& v = vec_of_vec.back();
      BOOST_REQUIRE_EQUAL(v.size(), int_array.size());
      for (size_t i=0; i<int_array.size(); ++i)
         BOOST_REQUIRE_EQUAL(v[i], int_array[i]);
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
      // check copy constructor. Verify copy-on-write
      // --------------------------------------------
      vec_of_vec.clear();
      vec_of_vec.emplace_back(int_array.cbegin(), int_array.cend());
      vec_of_vec.emplace_back(vec_of_vec[0]);
      auto& v0 = vec_of_vec[0];
      auto& v1 = vec_of_vec[1];
      BOOST_REQUIRE_EQUAL(v0, v1);               // check that copy construction worked
      BOOST_REQUIRE_EQUAL(v0.data(), v1.data()); // check copy_on_write (cow)

      // now change vector and verify copy happened
      //v0.data()[0] = 1;
      //BOOST_REQUIRE_NE(v0.data(), v1.data()); // check copy_on_write (cow)
      
      
   }
   
   {
      // check emplace_back(), clear(), size()
      // -------------------------------------
      auto sz = v.size();
      v.emplace_back(1);
      v.emplace_back(2);
      BOOST_REQUIRE_EQUAL(v.size(), sz + 2);

      v.clear();
      BOOST_REQUIRE_EQUAL(v.size(), 0);
   }
}

BOOST_AUTO_TEST_CASE(shared_vector_apis) {
   using sv = shared_vector<int>;
   sv v;
   std::vector<sv, std::allocator<sv>> vec_of_vec;
   check_shared_vector_apis(v, vec_of_vec);
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
   std::cerr << temp << " \n";

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


// BOOST_AUTO_TEST_SUITE_END()
