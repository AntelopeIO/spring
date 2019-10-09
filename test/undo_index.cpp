#include <chainbase/undo_index.hpp>

#include <boost/multi_index/key.hpp>
#include <boost/multi_index/ordered_index.hpp>

#include <boost/test/unit_test.hpp>
#include <boost/test/data/monomorphic.hpp>

struct basic_element_t {
   template<typename C, typename A>
   basic_element_t(C&& c, const std::allocator<A>&) { c(*this); }
   uint64_t id;
};

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
   if(exception_counter++ == throw_at) throw E{static_cast<A&&>(a)...};
}
template<typename F>
void test_exceptions(F&& f) {
   for(throw_at = 0; ; ++throw_at) {
      try {
         f();
      } catch(...) {
         continue;
      }
      break;
   }
   throw_at = -1;
}
}

BOOST_AUTO_TEST_SUITE(undo_index_tests)

BOOST_AUTO_TEST_CASE(test_simple) {
   chainbase::undo_index<basic_element_t, std::allocator<void>, boost::multi_index::ordered_unique<boost::multi_index::key<&basic_element_t::id>>> i0;
   i0.emplace([](basic_element_t& elem) {});
   const basic_element_t* element = i0.find(0);
   BOOST_TEST((element != nullptr && element->id == 0));
   const basic_element_t* e1 = i0.find(1);
   BOOST_TEST(e1 == nullptr);
   i0.emplace([](basic_element_t& elem) {});
   const basic_element_t* e2 = i0.find(1);
   BOOST_TEST((e2 != nullptr && e2->id == 1));

   i0.modify(*element, [](basic_element_t& elem) {});
   i0.remove(*element);
   element = i0.find(0);
   BOOST_TEST(element == nullptr);
}

struct test_element_t {
   template<typename C, typename A>
   test_element_t(C&& c, const std::allocator<A>&) { c(*this); }
   uint64_t id;
   int secondary;
};

BOOST_AUTO_TEST_CASE(test_insert_undo) {
   chainbase::undo_index<test_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::secondary> > > i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   {
   auto session = i0.start_undo_session(true);
   i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
   BOOST_TEST(i0.find(1)->secondary == 12);
   }
   BOOST_TEST(i0.find(0)->secondary == 42);
   BOOST_TEST(i0.find(1) == nullptr);
}

BOOST_AUTO_TEST_CASE(test_insert_squash) {
   chainbase::undo_index<test_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::secondary> > > i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   {
   auto session0 = i0.start_undo_session(true);
   auto session1 = i0.start_undo_session(true);
   i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
   BOOST_TEST(i0.find(1)->secondary == 12);
   session1.squash();
   BOOST_TEST(i0.find(1)->secondary == 12);
   }
   BOOST_TEST(i0.find(0)->secondary == 42);
   BOOST_TEST(i0.find(1) == nullptr);
}

BOOST_AUTO_TEST_CASE(test_insert_push) {
   chainbase::undo_index<test_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::secondary> > > i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   {
   auto session = i0.start_undo_session(true);
   i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
   BOOST_TEST(i0.find(1)->secondary == 12);
   session.push();
   i0.commit(i0.revision());
   }
   BOOST_TEST(i0.stack().size() == 0);
   BOOST_TEST(i0.find(0)->secondary == 42);
   BOOST_TEST(i0.find(1)->secondary == 12);
}

BOOST_AUTO_TEST_CASE(test_modify_undo) {
   chainbase::undo_index<test_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::secondary>>> i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   {
   auto session = i0.start_undo_session(true);
   i0.modify(*i0.find(0), [](test_element_t& elem) { elem.secondary = 18; });
   BOOST_TEST(i0.find(0)->secondary == 18);
   }
   BOOST_TEST(i0.find(0)->secondary == 42);
}

BOOST_AUTO_TEST_CASE(test_modify_squash) {
   chainbase::undo_index<test_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::secondary>>> i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   {
   auto session0 = i0.start_undo_session(true);
   auto session1 = i0.start_undo_session(true);
   i0.modify(*i0.find(0), [](test_element_t& elem) { elem.secondary = 18; });
   BOOST_TEST(i0.find(0)->secondary == 18);
   session1.squash();
   BOOST_TEST(i0.find(0)->secondary == 18);
   }
   BOOST_TEST(i0.find(0)->secondary == 42);
}

BOOST_AUTO_TEST_CASE(test_modify_push) {
   chainbase::undo_index<test_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::secondary>>> i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   {
   auto session = i0.start_undo_session(true);
   i0.modify(*i0.find(0), [](test_element_t& elem) { elem.secondary = 18; });
   BOOST_TEST(i0.find(0)->secondary == 18);
   session.push();
   i0.commit(i0.revision());
   }
   BOOST_TEST(i0.stack().size() == 0);
   BOOST_TEST(i0.find(0)->secondary == 18);
}

BOOST_AUTO_TEST_CASE(test_remove_undo) {
   chainbase::undo_index<test_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::secondary>>> i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   {
   auto session = i0.start_undo_session(true);
   i0.remove(*i0.find(0));
   BOOST_TEST(i0.find(0) == nullptr);
   }
   BOOST_TEST(i0.find(0)->secondary == 42);
}

BOOST_AUTO_TEST_CASE(test_remove_squash) {
   chainbase::undo_index<test_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::secondary>>> i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   {
   auto session0 = i0.start_undo_session(true);
   auto session1 = i0.start_undo_session(true);
   i0.remove(*i0.find(0));
   BOOST_TEST(i0.find(0) == nullptr);
   session1.squash();
   BOOST_TEST(i0.find(0) == nullptr);
   }
   BOOST_TEST(i0.find(0)->secondary == 42);
}

BOOST_AUTO_TEST_CASE(test_remove_push) {
   chainbase::undo_index<test_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::secondary>>> i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   {
   auto session = i0.start_undo_session(true);
   i0.remove(*i0.find(0));
   BOOST_TEST(i0.find(0) == nullptr);
   session.push();
   i0.commit(i0.revision());
   }
   BOOST_TEST(i0.stack().size() == 0);
   BOOST_TEST(i0.find(0) == nullptr);
}

BOOST_AUTO_TEST_CASE(test_insert_modify) {
   chainbase::undo_index<test_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::secondary>>> i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
   BOOST_TEST(i0.find(1)->secondary == 12);
   i0.modify(*i0.find(1), [](test_element_t& elem) { elem.secondary = 24; });
   BOOST_TEST(i0.find(1)->secondary == 24);
}

BOOST_AUTO_TEST_CASE(test_insert_modify_undo) {
   chainbase::undo_index<test_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::secondary>>> i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   {
   auto session = i0.start_undo_session(true);
   i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
   BOOST_TEST(i0.find(1)->secondary == 12);
   i0.modify(*i0.find(1), [](test_element_t& elem) { elem.secondary = 24; });
   BOOST_TEST(i0.find(1)->secondary == 24);
   }
   BOOST_TEST(i0.find(0)->secondary == 42);
   BOOST_TEST(i0.find(1) == nullptr);
}


BOOST_AUTO_TEST_CASE(test_insert_modify_squash) {
   chainbase::undo_index<test_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::secondary>>> i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   {
   auto session1 = i0.start_undo_session(true);
   i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
   BOOST_TEST(i0.find(1)->secondary == 12);
   auto session2 = i0.start_undo_session(true);
   i0.modify(*i0.find(1), [](test_element_t& elem) { elem.secondary = 24; });
   BOOST_TEST(i0.find(1)->secondary == 24);
   session2.squash();
   }
   BOOST_TEST(i0.find(0)->secondary == 42);
   BOOST_TEST(i0.find(1) == nullptr);
}

BOOST_AUTO_TEST_CASE(test_insert_remove_undo) {
   chainbase::undo_index<test_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::secondary>>> i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   {
   auto session = i0.start_undo_session(true);
   i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
   BOOST_TEST(i0.find(1)->secondary == 12);
   i0.remove(*i0.find(1));
   BOOST_TEST(i0.find(1) == nullptr);
   }
   BOOST_TEST(i0.find(0)->secondary == 42);
   BOOST_TEST(i0.find(1) == nullptr);
}

BOOST_AUTO_TEST_CASE(test_insert_remove_squash) {
   chainbase::undo_index<test_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::secondary>>> i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   {
   auto session1 = i0.start_undo_session(true);
   i0.emplace([](test_element_t& elem) { elem.secondary = 12; });
   BOOST_TEST(i0.find(1)->secondary == 12);
   auto session2 = i0.start_undo_session(true);
   i0.remove(*i0.find(1));
   BOOST_TEST(i0.find(1) == nullptr);
   session2.squash();
   }
   BOOST_TEST(i0.find(0)->secondary == 42);
   BOOST_TEST(i0.find(1) == nullptr);
}

BOOST_AUTO_TEST_CASE(test_modify_modify_undo) {
   chainbase::undo_index<test_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::secondary>>> i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   {
   auto session = i0.start_undo_session(true);
   i0.modify(*i0.find(0), [](test_element_t& elem) { elem.secondary = 18; });
   BOOST_TEST(i0.find(0)->secondary == 18);
   i0.modify(*i0.find(0), [](test_element_t& elem) { elem.secondary = 24; });
   BOOST_TEST(i0.find(0)->secondary == 24);
   }
   BOOST_TEST(i0.find(0)->secondary == 42);
}

BOOST_AUTO_TEST_CASE(test_modify_modify_squash) {
   chainbase::undo_index<test_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::secondary>>> i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   {
   auto session1 = i0.start_undo_session(true);
   i0.modify(*i0.find(0), [](test_element_t& elem) { elem.secondary = 18; });
   BOOST_TEST(i0.find(0)->secondary == 18);
   auto session2 = i0.start_undo_session(true);
   i0.modify(*i0.find(0), [](test_element_t& elem) { elem.secondary = 24; });
   BOOST_TEST(i0.find(0)->secondary == 24);
   session2.squash();
   }
   BOOST_TEST(i0.find(0)->secondary == 42);
}

BOOST_AUTO_TEST_CASE(test_modify_remove_undo) {
   chainbase::undo_index<test_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::secondary>>> i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   {
   auto session = i0.start_undo_session(true);
   i0.modify(*i0.find(0), [](test_element_t& elem) { elem.secondary = 18; });
   BOOST_TEST(i0.find(0)->secondary == 18);
   i0.remove(*i0.find(0));
   BOOST_TEST(i0.find(0) == nullptr);
   }
   BOOST_TEST(i0.find(0)->secondary == 42);
}

BOOST_AUTO_TEST_CASE(test_modify_remove_squash) {
   chainbase::undo_index<test_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::secondary>>> i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   {
   auto session1 = i0.start_undo_session(true);
   i0.modify(*i0.find(0), [](test_element_t& elem) { elem.secondary = 18; });
   BOOST_TEST(i0.find(0)->secondary == 18);
   auto session2 = i0.start_undo_session(true);
   i0.remove(*i0.find(0));
   BOOST_TEST(i0.find(0) == nullptr);
   session2.squash();
   }
   BOOST_TEST(i0.find(0)->secondary == 42);
}

BOOST_AUTO_TEST_CASE(test_squash_one) {
   chainbase::undo_index<test_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::secondary>>> i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   {
   i0.modify(*i0.find(0), [](test_element_t& elem) { elem.secondary = 18; });
   BOOST_TEST(i0.find(0)->secondary == 18);
   auto session2 = i0.start_undo_session(true);
   i0.remove(*i0.find(0));
   BOOST_TEST(i0.find(0) == nullptr);
   session2.squash();
   }
}

BOOST_AUTO_TEST_CASE(test_insert_non_unique) {
   chainbase::undo_index<test_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&test_element_t::secondary>>> i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   BOOST_CHECK_THROW(i0.emplace([](test_element_t& elem) { elem.secondary = 42; }),  std::exception);
   BOOST_TEST(i0.find(0)->secondary == 42);
}

struct conflict_element_t {
   template<typename C, typename A>
   conflict_element_t(C&& c, const std::allocator<A>&) { c(*this); }
   uint64_t id;
   int x0;
   int x1;
   int x2;
};

BOOST_AUTO_TEST_CASE(test_modify_conflict) {
   chainbase::undo_index<conflict_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&conflict_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&conflict_element_t::x0>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&conflict_element_t::x1>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&conflict_element_t::x2>>> i0;
   // insert 3 elements
   i0.emplace([](conflict_element_t& elem) { elem.x0 = 0; elem.x1 = 10; elem.x2 = 10; });
   i0.emplace([](conflict_element_t& elem) { elem.x0 = 11; elem.x1 = 1; elem.x2 = 11; });
   i0.emplace([](conflict_element_t& elem) { elem.x0 = 12; elem.x1 = 12; elem.x2 = 2; });
   {
   auto session = i0.start_undo_session(true);
   // set them to a different value
   i0.modify(*i0.find(0), [](conflict_element_t& elem) { elem.x0 = 10; elem.x1 = 10; elem.x2 = 10; });
   i0.modify(*i0.find(1), [](conflict_element_t& elem) { elem.x0 = 11; elem.x1 = 11; elem.x2 = 11; });
   i0.modify(*i0.find(2), [](conflict_element_t& elem) { elem.x0 = 12; elem.x1 = 12; elem.x2 = 12; });
   // create a circular conflict with the original values
   i0.modify(*i0.find(0), [](conflict_element_t& elem) { elem.x0 = 10; elem.x1 = 1; elem.x2 = 10; });
   i0.modify(*i0.find(1), [](conflict_element_t& elem) { elem.x0 = 11; elem.x1 = 11; elem.x2 = 2; });
   i0.modify(*i0.find(2), [](conflict_element_t& elem) { elem.x0 = 0; elem.x1 = 12; elem.x2 = 12; });
   }
   BOOST_TEST(i0.find(0)->x0 == 0);
   BOOST_TEST(i0.find(1)->x1 == 1);
   BOOST_TEST(i0.find(2)->x2 == 2);
   // Check lookup in the other indices
   BOOST_TEST(i0.get<1>().find(0)->x0 == 0);
   BOOST_TEST(i0.get<1>().find(11)->x0 == 11);
   BOOST_TEST(i0.get<1>().find(12)->x0 == 12);
   BOOST_TEST(i0.get<2>().find(10)->x1 == 10);
   BOOST_TEST(i0.get<2>().find(1)->x1 == 1);
   BOOST_TEST(i0.get<2>().find(12)->x1 == 12);
   BOOST_TEST(i0.get<3>().find(10)->x2 == 10);
   BOOST_TEST(i0.get<3>().find(11)->x2 == 11);
   BOOST_TEST(i0.get<3>().find(2)->x2 == 2);
}

#if 0
BOOST_DATA_TEST_CASE(test_insert_fail, boost::unit_test::data::make({true, false}), use_undo) {
   chainbase::undo_index<conflict_element_t, std::allocator<void>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&conflict_element_t::id>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&conflict_element_t::x0>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&conflict_element_t::x1>>,
                         boost::multi_index::ordered_unique<boost::multi_index::key<&conflict_element_t::x2>>> i0;
   // insert 3 elements
   i0.emplace([](conflict_element_t& elem) { elem.x0 = 10; elem.x1 = 10; elem.x2 = 10; });
   i0.emplace([](conflict_element_t& elem) { elem.x0 = 11; elem.x1 = 11; elem.x2 = 11; });
   i0.emplace([](conflict_element_t& elem) { elem.x0 = 12; elem.x1 = 12; elem.x2 = 12; });
   {
   auto session = i0.start_undo_session(use_undo);
   // Insert a value with a duplicate
   i0.emplace([](conflict_element_t& elem) { elem.x0 = 81; elem.x1 = 11; elem.x2 = 91; });
   }
   BOOST_TEST(i0.find(0)->x0 == 0);
   BOOST_TEST(i0.find(1)->x1 == 1);
   BOOST_TEST(i0.find(2)->x2 == 2);
   // Check lookup in the other indices
   BOOST_TEST(i0.get<1>().find(0)->x0 == 0);
   BOOST_TEST(i0.get<1>().find(11)->x0 == 11);
   BOOST_TEST(i0.get<1>().find(12)->x0 == 12);
   BOOST_TEST(i0.get<2>().find(10)->x1 == 10);
   BOOST_TEST(i0.get<2>().find(1)->x1 == 1);
   BOOST_TEST(i0.get<2>().find(12)->x1 == 12);
   BOOST_TEST(i0.get<3>().find(10)->x2 == 10);
   BOOST_TEST(i0.get<3>().find(11)->x2 == 11);
   BOOST_TEST(i0.get<3>().find(2)->x2 == 2);
}
#endif

BOOST_AUTO_TEST_SUITE_END()
