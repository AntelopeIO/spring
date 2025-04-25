#include <chainbase/undo_index.hpp>
#include <chainbase/chainbase.hpp>
#include <filesystem>

#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>

#include <boost/test/unit_test.hpp>
#include <boost/test/data/monomorphic.hpp>
#include <boost/test/data/test_case.hpp>

namespace {
int exception_counter = 0;
int throw_at = -1;
struct test_exception_base {};
template<typename E>
struct test_exception : E, test_exception_base {
  template<typename... A>
  test_exception(A&&... a) : E{static_cast<E&&>(a)...} {}
};
template<typename E, typename... A>
void throw_point(A&&... a) {
   if(throw_at != -1 && exception_counter++ >= throw_at) {
     throw test_exception<E>{static_cast<A&&>(a)...};
   }
}
template<typename F>
void test_exceptions(F&& f) {
   for(throw_at = 0; ; ++throw_at) {
      exception_counter = 0;
      try {
         f();
         break;
      } catch(test_exception_base&) {}
   }
   throw_at = -1;
   exception_counter = 0;
}

struct throwing_copy {
   throwing_copy() { throw_point<std::bad_alloc>(); }
   throwing_copy(const throwing_copy&) { throw_point<std::bad_alloc>(); }
   throwing_copy(throwing_copy&&) noexcept = default;
   throwing_copy& operator=(const throwing_copy&) { throw_point<std::bad_alloc>(); return *this; }
   throwing_copy& operator=(throwing_copy&&) noexcept = default;
};

namespace bip = boost::interprocess;
namespace fs  = std::filesystem;

template<typename T>
using test_allocator_base = chainbase::chainbase_node_allocator<T, chainbase::segment_manager>;

template<typename T>
class test_allocator : public test_allocator_base<T> {
public:
   using base = test_allocator_base<T>;
   test_allocator(chainbase::segment_manager *mgr) : base(mgr) {}
   template<typename U>
   test_allocator(const test_allocator<U>& o) : base(o.get_segment_manager()) {}
   template<typename U>
   struct rebind { using other = test_allocator<U>; };
   typename base::pointer allocate(std::size_t count) {
      throw_point<std::bad_alloc>();
      return base::allocate(count);
   }
};


using chainbase::scope_fail;

struct basic_element_t {
   template<typename C>
   basic_element_t(C&& c, chainbase::constructor_tag) { c(*this); }
   
   uint64_t id;
   throwing_copy dummy;
};

// TODO: Replace with boost::multi_index::key once we bump our minimum Boost version to at least 1.69
template<typename T>
struct key_impl;
template<typename C, typename T>
struct key_impl<T C::*> { template<auto F> using fn = boost::multi_index::member<C, T, F>; };

template<auto Fn>
using key = typename key_impl<decltype(Fn)>::template fn<Fn>;

}

template <typename... T>
struct undo_index_in_segment {
   using undo_index_t = chainbase::undo_index<T...>;

   template <typename A>
   undo_index_in_segment(A& alloc) : segment_manager(*alloc.get_segment_manager()) {
      p = segment_manager.construct<undo_index_t>("")(alloc);
   }

   ~undo_index_in_segment() {
      if(p)
         segment_manager.destroy_ptr(p);
   }

   undo_index_t* const operator->() {
      return p;
   }

   undo_index_t& operator*() {
      return *p;
   }

   chainbase::segment_manager& segment_manager;
   undo_index_t* p = nullptr;

   undo_index_in_segment(const undo_index_in_segment&) = delete;
   undo_index_in_segment(undo_index_in_segment&&) = delete;
   undo_index_in_segment& operator=(const undo_index_in_segment&) = delete;
   undo_index_in_segment& operator=(undo_index_in_segment&&) = delete;
};

BOOST_AUTO_TEST_SUITE(undo_index_tests)

#define EXCEPTION_TEST_CASE(name)                               \
   void name##impl();                                           \
   BOOST_AUTO_TEST_CASE(name) { test_exceptions(&name##impl); } \
   void name##impl ()

EXCEPTION_TEST_CASE(test_simple) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<basic_element_t, test_allocator<basic_element_t>,
                            boost::multi_index::ordered_unique<key<&basic_element_t::id>>> i0(alloc);
      i0->emplace([](basic_element_t& elem) {});
      const basic_element_t* element = i0->find(0);
      BOOST_TEST((element != nullptr && element->id == 0));
      const basic_element_t* e1 = i0->find(1);
      BOOST_TEST(e1 == nullptr);
      i0->emplace([](basic_element_t& elem) {});
      const basic_element_t* e2 = i0->find(1);
      BOOST_TEST((e2 != nullptr && e2->id == 1));

      i0->modify(*element, [](basic_element_t& elem) {});
      i0->remove(*element);
      element = i0->find(0);
      BOOST_TEST(element == nullptr);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

struct test_element_t {
   template<typename C>
   test_element_t(C&& c, chainbase::constructor_tag) { c(*this);  }
   
   uint64_t id;
   int secondary;
   throwing_copy dummy;
};

// If an exception is thrown while an undo session is active, undo will restore the state.
template<typename C>
auto capture_state(const C& index) {
   std::vector<std::pair<test_element_t, const test_element_t*>> vec;
   for(const auto& elem : index) {
     vec.emplace_back(elem, &elem);
   }
   return scope_fail{[vec = std::move(vec), &index]{
      BOOST_TEST(index.size() == vec.size());
      for(const auto& [elem, ptr] : vec) {
         auto * actual0 = index.find(elem.id);
         BOOST_TEST(actual0 == ptr); // reference stability is guaranteed
         if (actual0 != nullptr) {
            BOOST_TEST(actual0->id == elem.id);
            BOOST_TEST(actual0->secondary == elem.secondary);
         }
         auto actual1iter = index.template get<1>().find(elem.secondary);
         BOOST_TEST((actual1iter != index.template get<1>().end() && &*actual1iter == actual0));
      }
   }};
}

EXCEPTION_TEST_CASE(test_insert_undo) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<test_element_t, test_allocator<test_element_t>,
                            boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0(alloc);
      i0->emplace([](test_element_t& elem) { elem.secondary = 42; });
      BOOST_TEST(i0->find(0)->secondary == 42);
      {
         auto undo_checker = capture_state(*i0);
         auto session = i0->start_undo_session(true);
         i0->emplace([](test_element_t& elem) { elem.secondary = 12; });
         BOOST_TEST(i0->find(1)->secondary == 12);
      }
      BOOST_TEST(i0->find(0)->secondary == 42);
      BOOST_TEST(i0->find(1) == nullptr);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

EXCEPTION_TEST_CASE(test_insert_squash) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<test_element_t, test_allocator<test_element_t>,
                            boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0(alloc);
      i0->emplace([](test_element_t& elem) { elem.secondary = 42; });
      BOOST_TEST(i0->find(0)->secondary == 42);
      {
         auto undo_checker = capture_state(*i0);
         auto session0 = i0->start_undo_session(true);
         auto session1 = i0->start_undo_session(true);
         i0->emplace([](test_element_t& elem) { elem.secondary = 12; });
         BOOST_TEST(i0->find(1)->secondary == 12);
         session1.squash();
         BOOST_TEST(i0->find(1)->secondary == 12);
      }
      BOOST_TEST(i0->find(0)->secondary == 42);
      BOOST_TEST(i0->find(1) == nullptr);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );}

EXCEPTION_TEST_CASE(test_insert_push) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<test_element_t, test_allocator<test_element_t>,
                            boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&test_element_t::secondary> > > i0(alloc);
      i0->emplace([](test_element_t& elem) { elem.secondary = 42; });
      BOOST_TEST(i0->find(0)->secondary == 42);
      {
         auto undo_checker = capture_state(*i0);
         auto session = i0->start_undo_session(true);
         i0->emplace([](test_element_t& elem) { elem.secondary = 12; });
         BOOST_TEST(i0->find(1)->secondary == 12);
         session.push();
         i0->commit(i0->revision());
      }
      BOOST_TEST(!i0->has_undo_session());
      BOOST_TEST(i0->find(0)->secondary == 42);
      BOOST_TEST(i0->find(1)->secondary == 12);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

EXCEPTION_TEST_CASE(test_modify_undo) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<test_element_t, test_allocator<test_element_t>,
                            boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&test_element_t::secondary>>> i0(alloc);
      i0->emplace([](test_element_t& elem) { elem.secondary = 42; });
      BOOST_TEST(i0->find(0)->secondary == 42);
      {
         auto undo_checker = capture_state(*i0);
         auto session = i0->start_undo_session(true);
         i0->modify(*i0->find(0), [](test_element_t& elem) { elem.secondary = 18; });
         BOOST_TEST(i0->find(0)->secondary == 18);
      }
      BOOST_TEST(i0->find(0)->secondary == 42);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

EXCEPTION_TEST_CASE(test_modify_squash) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<test_element_t, test_allocator<test_element_t>,
                            boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&test_element_t::secondary>>> i0(alloc);
      i0->emplace([](test_element_t& elem) { elem.secondary = 42; });
      BOOST_TEST(i0->find(0)->secondary == 42);
      {
         auto undo_checker = capture_state(*i0);
         auto session0 = i0->start_undo_session(true);
         auto session1 = i0->start_undo_session(true);
         i0->modify(*i0->find(0), [](test_element_t& elem) { elem.secondary = 18; });
         BOOST_TEST(i0->find(0)->secondary == 18);
         session1.squash();
         BOOST_TEST(i0->find(0)->secondary == 18);
      }
      BOOST_TEST(i0->find(0)->secondary == 42);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

EXCEPTION_TEST_CASE(test_modify_push) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<test_element_t, test_allocator<test_element_t>,
                            boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&test_element_t::secondary>>> i0(alloc);
      i0->emplace([](test_element_t& elem) { elem.secondary = 42; });
      BOOST_TEST(i0->find(0)->secondary == 42);
      {
         auto undo_checker = capture_state(*i0);
         auto session = i0->start_undo_session(true);
         i0->modify(*i0->find(0), [](test_element_t& elem) { elem.secondary = 18; });
         BOOST_TEST(i0->find(0)->secondary == 18);
         session.push();
         i0->commit(i0->revision());
      }
      BOOST_TEST(!i0->has_undo_session());
      BOOST_TEST(i0->find(0)->secondary == 18);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

EXCEPTION_TEST_CASE(test_remove_undo) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<test_element_t, test_allocator<test_element_t>,
                            boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&test_element_t::secondary>>> i0(alloc);
      i0->emplace([](test_element_t& elem) { elem.secondary = 42; });
      BOOST_TEST(i0->find(0)->secondary == 42);
      {
         auto undo_checker = capture_state(*i0);
         auto session = i0->start_undo_session(true);
         i0->remove(*i0->find(0));
         BOOST_TEST(i0->find(0) == nullptr);
      }
      BOOST_TEST(i0->find(0)->secondary == 42);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

EXCEPTION_TEST_CASE(test_remove_squash) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<test_element_t, test_allocator<test_element_t>,
                            boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&test_element_t::secondary>>> i0(alloc);
      i0->emplace([](test_element_t& elem) { elem.secondary = 42; });
      BOOST_TEST(i0->find(0)->secondary == 42);
      {
         auto undo_checker = capture_state(*i0);
         auto session0 = i0->start_undo_session(true);
         auto session1 = i0->start_undo_session(true);
         i0->remove(*i0->find(0));
         BOOST_TEST(i0->find(0) == nullptr);
         session1.squash();
         BOOST_TEST(i0->find(0) == nullptr);
      }
      BOOST_TEST(i0->find(0)->secondary == 42);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

EXCEPTION_TEST_CASE(test_remove_push) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<test_element_t, test_allocator<test_element_t>,
                            boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&test_element_t::secondary>>> i0(alloc);
      i0->emplace([](test_element_t& elem) { elem.secondary = 42; });
      BOOST_TEST(i0->find(0)->secondary == 42);
      {
         auto undo_checker = capture_state(*i0);
         auto session = i0->start_undo_session(true);
         i0->remove(*i0->find(0));
         BOOST_TEST(i0->find(0) == nullptr);
         session.push();
         i0->commit(i0->revision());
      }
      BOOST_TEST(!i0->has_undo_session());
      BOOST_TEST(i0->find(0) == nullptr);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

EXCEPTION_TEST_CASE(test_insert_modify) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<test_element_t, test_allocator<test_element_t>,
                            boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&test_element_t::secondary>>> i0(alloc);
      i0->emplace([](test_element_t& elem) { elem.secondary = 42; });
      BOOST_TEST(i0->find(0)->secondary == 42);
      i0->emplace([](test_element_t& elem) { elem.secondary = 12; });
      BOOST_TEST(i0->find(1)->secondary == 12);
      i0->modify(*i0->find(1), [](test_element_t& elem) { elem.secondary = 24; });
      BOOST_TEST(i0->find(1)->secondary == 24);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

EXCEPTION_TEST_CASE(test_insert_modify_undo) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<test_element_t, test_allocator<test_element_t>,
                            boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&test_element_t::secondary>>> i0(alloc);
      i0->emplace([](test_element_t& elem) { elem.secondary = 42; });
      BOOST_TEST(i0->find(0)->secondary == 42);
      {
         auto undo_checker = capture_state(*i0);
         auto session = i0->start_undo_session(true);
         i0->emplace([](test_element_t& elem) { elem.secondary = 12; });
         BOOST_TEST(i0->find(1)->secondary == 12);
         i0->modify(*i0->find(1), [](test_element_t& elem) { elem.secondary = 24; });
         BOOST_TEST(i0->find(1)->secondary == 24);
      }
      BOOST_TEST(i0->find(0)->secondary == 42);
      BOOST_TEST(i0->find(1) == nullptr);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}


EXCEPTION_TEST_CASE(test_insert_modify_squash) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<test_element_t, test_allocator<test_element_t>,
                            boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&test_element_t::secondary>>> i0(alloc);
      i0->emplace([](test_element_t& elem) { elem.secondary = 42; });
      BOOST_TEST(i0->find(0)->secondary == 42);
      {
         auto undo_checker = capture_state(*i0);
         auto session1 = i0->start_undo_session(true);
         i0->emplace([](test_element_t& elem) { elem.secondary = 12; });
         BOOST_TEST(i0->find(1)->secondary == 12);
         auto session2 = i0->start_undo_session(true);
         i0->modify(*i0->find(1), [](test_element_t& elem) { elem.secondary = 24; });
         BOOST_TEST(i0->find(1)->secondary == 24);
         session2.squash();
      }
      BOOST_TEST(i0->find(0)->secondary == 42);
      BOOST_TEST(i0->find(1) == nullptr);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

EXCEPTION_TEST_CASE(test_insert_remove_undo) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<test_element_t, test_allocator<test_element_t>,
                            boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&test_element_t::secondary>>> i0(alloc);
      i0->emplace([](test_element_t& elem) { elem.secondary = 42; });
      BOOST_TEST(i0->find(0)->secondary == 42);
      {
         auto undo_checker = capture_state(*i0);
         auto session = i0->start_undo_session(true);
         i0->emplace([](test_element_t& elem) { elem.secondary = 12; });
         BOOST_TEST(i0->find(1)->secondary == 12);
         i0->remove(*i0->find(1));
         BOOST_TEST(i0->find(1) == nullptr);
      }
      BOOST_TEST(i0->find(0)->secondary == 42);
      BOOST_TEST(i0->find(1) == nullptr);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

EXCEPTION_TEST_CASE(test_insert_remove_squash) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<test_element_t, test_allocator<test_element_t>,
                            boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&test_element_t::secondary>>> i0(alloc);
      i0->emplace([](test_element_t& elem) { elem.secondary = 42; });
      BOOST_TEST(i0->find(0)->secondary == 42);
      {
         auto undo_checker = capture_state(*i0);
         auto session1 = i0->start_undo_session(true);
         i0->emplace([](test_element_t& elem) { elem.secondary = 12; });
         BOOST_TEST(i0->find(1)->secondary == 12);
         auto session2 = i0->start_undo_session(true);
         i0->remove(*i0->find(1));
         BOOST_TEST(i0->find(1) == nullptr);
         session2.squash();
      }
      BOOST_TEST(i0->find(0)->secondary == 42);
      BOOST_TEST(i0->find(1) == nullptr);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

EXCEPTION_TEST_CASE(test_modify_modify_undo) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<test_element_t, test_allocator<test_element_t>,
                            boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&test_element_t::secondary>>> i0(alloc);
      i0->emplace([](test_element_t& elem) { elem.secondary = 42; });
      BOOST_TEST(i0->find(0)->secondary == 42);
      {
         auto undo_checker = capture_state(*i0);
         auto session = i0->start_undo_session(true);
         i0->modify(*i0->find(0), [](test_element_t& elem) { elem.secondary = 18; });
         BOOST_TEST(i0->find(0)->secondary == 18);
         i0->modify(*i0->find(0), [](test_element_t& elem) { elem.secondary = 24; });
         BOOST_TEST(i0->find(0)->secondary == 24);
      }
      BOOST_TEST(i0->find(0)->secondary == 42);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

EXCEPTION_TEST_CASE(test_modify_modify_squash) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<test_element_t, test_allocator<test_element_t>,
                            boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&test_element_t::secondary>>> i0(alloc);
      i0->emplace([](test_element_t& elem) { elem.secondary = 42; });
      BOOST_TEST(i0->find(0)->secondary == 42);
      {
         auto undo_checker = capture_state(*i0);
         auto session1 = i0->start_undo_session(true);
         i0->modify(*i0->find(0), [](test_element_t& elem) { elem.secondary = 18; });
         BOOST_TEST(i0->find(0)->secondary == 18);
         auto session2 = i0->start_undo_session(true);
         i0->modify(*i0->find(0), [](test_element_t& elem) { elem.secondary = 24; });
         BOOST_TEST(i0->find(0)->secondary == 24);
         session2.squash();
      }
      BOOST_TEST(i0->find(0)->secondary == 42);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

EXCEPTION_TEST_CASE(test_modify_remove_undo) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<test_element_t, test_allocator<test_element_t>,
                            boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&test_element_t::secondary>>> i0(alloc);
      i0->emplace([](test_element_t& elem) { elem.secondary = 42; });
      BOOST_TEST(i0->find(0)->secondary == 42);
      {
         auto undo_checker = capture_state(*i0);
         auto session = i0->start_undo_session(true);
         i0->modify(*i0->find(0), [](test_element_t& elem) { elem.secondary = 18; });
         BOOST_TEST(i0->find(0)->secondary == 18);
         i0->remove(*i0->find(0));
         BOOST_TEST(i0->find(0) == nullptr);
      }
      BOOST_TEST(i0->find(0)->secondary == 42);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

EXCEPTION_TEST_CASE(test_modify_remove_squash) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<test_element_t, test_allocator<test_element_t>,
                            boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&test_element_t::secondary>>> i0(alloc);
      i0->emplace([](test_element_t& elem) { elem.secondary = 42; });
      BOOST_TEST(i0->find(0)->secondary == 42);
      {
         auto undo_checker = capture_state(*i0);
         auto session1 = i0->start_undo_session(true);
         i0->modify(*i0->find(0), [](test_element_t& elem) { elem.secondary = 18; });
         BOOST_TEST(i0->find(0)->secondary == 18);
         auto session2 = i0->start_undo_session(true);
         i0->remove(*i0->find(0));
         BOOST_TEST(i0->find(0) == nullptr);
         session2.squash();
      }
      BOOST_TEST(i0->find(0)->secondary == 42);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

EXCEPTION_TEST_CASE(test_squash_one) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<test_element_t, test_allocator<test_element_t>,
                            boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&test_element_t::secondary>>> i0(alloc);
      i0->emplace([](test_element_t& elem) { elem.secondary = 42; });
      BOOST_TEST(i0->find(0)->secondary == 42);
      {
         i0->modify(*i0->find(0), [](test_element_t& elem) { elem.secondary = 18; });
         BOOST_TEST(i0->find(0)->secondary == 18);
         auto session2 = i0->start_undo_session(true);
         i0->remove(*i0->find(0));
         BOOST_TEST(i0->find(0) == nullptr);
         session2.squash();
      }
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

EXCEPTION_TEST_CASE(test_insert_non_unique) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<test_element_t, test_allocator<test_element_t>,
                            boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&test_element_t::secondary>>> i0(alloc);
      i0->emplace([](test_element_t& elem) { elem.secondary = 42; });
      BOOST_TEST(i0->find(0)->secondary == 42);
      BOOST_CHECK_THROW(i0->emplace([](test_element_t& elem) { elem.secondary = 42; }),  std::exception);
      BOOST_TEST(i0->find(0)->secondary == 42);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

struct conflict_element_t {
   template<typename C>
   conflict_element_t(C&& c, chainbase::constructor_tag) { c(*this); }
   uint64_t id;
   int x0;
   int x1;
   int x2;
   throwing_copy dummy;
};

EXCEPTION_TEST_CASE(test_modify_conflict) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<conflict_element_t, test_allocator<conflict_element_t>,
                            boost::multi_index::ordered_unique<key<&conflict_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&conflict_element_t::x0>>,
                            boost::multi_index::ordered_unique<key<&conflict_element_t::x1>>,
                            boost::multi_index::ordered_unique<key<&conflict_element_t::x2>>> i0(alloc);
      // insert 3 elements
      i0->emplace([](conflict_element_t& elem) { elem.x0 = 0; elem.x1 = 10; elem.x2 = 10; });
      i0->emplace([](conflict_element_t& elem) { elem.x0 = 11; elem.x1 = 1; elem.x2 = 11; });
      i0->emplace([](conflict_element_t& elem) { elem.x0 = 12; elem.x1 = 12; elem.x2 = 2; });
      {
         auto session = i0->start_undo_session(true);
         // set them to a different value
         i0->modify(*i0->find(0), [](conflict_element_t& elem) { elem.x0 = 10; elem.x1 = 10; elem.x2 = 10; });
         i0->modify(*i0->find(1), [](conflict_element_t& elem) { elem.x0 = 11; elem.x1 = 11; elem.x2 = 11; });
         i0->modify(*i0->find(2), [](conflict_element_t& elem) { elem.x0 = 12; elem.x1 = 12; elem.x2 = 12; });
         // create a circular conflict with the original values
         i0->modify(*i0->find(0), [](conflict_element_t& elem) { elem.x0 = 10; elem.x1 = 1; elem.x2 = 10; });
         i0->modify(*i0->find(1), [](conflict_element_t& elem) { elem.x0 = 11; elem.x1 = 11; elem.x2 = 2; });
         i0->modify(*i0->find(2), [](conflict_element_t& elem) { elem.x0 = 0; elem.x1 = 12; elem.x2 = 12; });
      }
      BOOST_TEST(i0->find(0)->x0 == 0);
      BOOST_TEST(i0->find(1)->x1 == 1);
      BOOST_TEST(i0->find(2)->x2 == 2);
      // Check lookup in the other indices
      BOOST_TEST(i0->get<1>().find(0)->x0 == 0);
      BOOST_TEST(i0->get<1>().find(11)->x0 == 11);
      BOOST_TEST(i0->get<1>().find(12)->x0 == 12);
      BOOST_TEST(i0->get<2>().find(10)->x1 == 10);
      BOOST_TEST(i0->get<2>().find(1)->x1 == 1);
      BOOST_TEST(i0->get<2>().find(12)->x1 == 12);
      BOOST_TEST(i0->get<3>().find(10)->x2 == 10);
      BOOST_TEST(i0->get<3>().find(11)->x2 == 11);
      BOOST_TEST(i0->get<3>().find(2)->x2 == 2);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

BOOST_DATA_TEST_CASE(test_insert_fail, boost::unit_test::data::make({true, false}), use_undo) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());

      undo_index_in_segment<conflict_element_t, test_allocator<conflict_element_t>,
                                                boost::multi_index::ordered_unique<key<&conflict_element_t::id>>,
                                                boost::multi_index::ordered_unique<key<&conflict_element_t::x0>>,
                                                boost::multi_index::ordered_unique<key<&conflict_element_t::x1>>,
                                                boost::multi_index::ordered_unique<key<&conflict_element_t::x2>>> i0(alloc);

      // insert 3 elements
      i0->emplace([](conflict_element_t& elem) { elem.x0 = 10; elem.x1 = 10; elem.x2 = 10; });
      i0->emplace([](conflict_element_t& elem) { elem.x0 = 11; elem.x1 = 11; elem.x2 = 11; });
      i0->emplace([](conflict_element_t& elem) { elem.x0 = 12; elem.x1 = 12; elem.x2 = 12; });
      {
         auto session = i0->start_undo_session(true);
         // Insert a value with a duplicate
         BOOST_CHECK_THROW(i0->emplace([](conflict_element_t& elem) { elem.x0 = 81; elem.x1 = 11; elem.x2 = 91; }), std::logic_error);
      }
      BOOST_TEST(i0->find(0)->x0 == 10);
      BOOST_TEST(i0->find(1)->x1 == 11);
      BOOST_TEST(i0->find(2)->x2 == 12);
      // Check lookup in the other indices
      BOOST_TEST(i0->get<1>().find(10)->x0 == 10);
      BOOST_TEST(i0->get<1>().find(11)->x0 == 11);
      BOOST_TEST(i0->get<1>().find(12)->x0 == 12);
      BOOST_TEST(i0->get<2>().find(10)->x1 == 10);
      BOOST_TEST(i0->get<2>().find(11)->x1 == 11);
      BOOST_TEST(i0->get<2>().find(12)->x1 == 12);
      BOOST_TEST(i0->get<3>().find(10)->x2 == 10);
      BOOST_TEST(i0->get<3>().find(11)->x2 == 11);
      BOOST_TEST(i0->get<3>().find(12)->x2 == 12);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

EXCEPTION_TEST_CASE(test_modify_fail) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<conflict_element_t, test_allocator<conflict_element_t>,
                            boost::multi_index::ordered_unique<key<&conflict_element_t::id>>,
                            boost::multi_index::ordered_unique<key<&conflict_element_t::x0>>,
                            boost::multi_index::ordered_unique<key<&conflict_element_t::x1>>,
                            boost::multi_index::ordered_unique<key<&conflict_element_t::x2>>> i0(alloc);
      // insert 3 elements
      i0->emplace([](conflict_element_t& elem) { elem.x0 = 10; elem.x1 = 10; elem.x2 = 10; });
      i0->emplace([](conflict_element_t& elem) { elem.x0 = 11; elem.x1 = 11; elem.x2 = 11; });
      i0->emplace([](conflict_element_t& elem) { elem.x0 = 12; elem.x1 = 12; elem.x2 = 12; });
      {
         auto session = i0->start_undo_session(true);
         // Insert a value with a duplicate
         i0->emplace([](conflict_element_t& elem) { elem.x0 = 71; elem.x1 = 81; elem.x2 = 91; });
         BOOST_CHECK_THROW(i0->modify(i0->get(3), [](conflict_element_t& elem) { elem.x0 = 71; elem.x1 = 10; elem.x2 = 91; }), std::logic_error);
      }
      BOOST_TEST(i0->get<0>().size() == 3u);
      BOOST_TEST(i0->get<1>().size() == 3u);
      BOOST_TEST(i0->get<2>().size() == 3u);
      BOOST_TEST(i0->get<3>().size() == 3u);
      BOOST_TEST(i0->find(0)->x0 == 10);
      BOOST_TEST(i0->find(1)->x1 == 11);
      BOOST_TEST(i0->find(2)->x2 == 12);
      // Check lookup in the other indices
      BOOST_TEST(i0->get<1>().find(10)->x0 == 10);
      BOOST_TEST(i0->get<1>().find(11)->x0 == 11);
      BOOST_TEST(i0->get<1>().find(12)->x0 == 12);
      BOOST_TEST(i0->get<2>().find(10)->x1 == 10);
      BOOST_TEST(i0->get<2>().find(11)->x1 == 11);
      BOOST_TEST(i0->get<2>().find(12)->x1 == 12);
      BOOST_TEST(i0->get<3>().find(10)->x2 == 10);
      BOOST_TEST(i0->get<3>().find(11)->x2 == 11);
      BOOST_TEST(i0->get<3>().find(12)->x2 == 12);
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}

struct by_secondary {};

BOOST_AUTO_TEST_CASE(test_project) {
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      chainbase::pinnable_mapped_file db(temp, true, 1024 * 1024, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<basic_element_t> alloc(db.get_segment_manager());
      undo_index_in_segment<test_element_t, test_allocator<test_element_t>,
                            boost::multi_index::ordered_unique<key<&test_element_t::id>>,
                            boost::multi_index::ordered_unique<boost::multi_index::tag<by_secondary>, key<&test_element_t::secondary>>> i0(alloc);
      i0->emplace([](test_element_t& elem) { elem.secondary = 42; });
      BOOST_TEST(i0->project<by_secondary>(i0->begin()) == i0->get<by_secondary>().begin());
      BOOST_TEST(i0->project<by_secondary>(i0->end()) == i0->get<by_secondary>().end());
      BOOST_TEST(i0->project<1>(i0->begin()) == i0->get<by_secondary>().begin());
      BOOST_TEST(i0->project<1>(i0->end()) == i0->get<by_secondary>().end());
   } catch ( ... ) {
      fs::remove_all( temp );
      throw;
   }
   fs::remove_all( temp );
}


BOOST_AUTO_TEST_SUITE_END()
