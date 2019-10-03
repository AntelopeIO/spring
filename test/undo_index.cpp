#include <chainbase/undo_index.hpp>

#include <boost/multi_index/key.hpp>

#define BOOST_TEST_MAIN
#include <boost/test/included/unit_test.hpp>

struct basic_element_t {
   uint64_t id;
};

BOOST_AUTO_TEST_CASE(test_simple) {
   eosio::undo_index<basic_element_t, std::allocator<void>, boost::multi_index::key<&basic_element_t::id>> i0;
   i0.emplace([](basic_element_t& elem) {});
   const basic_element_t* element = i0.find(0);
   BOOST_TEST((element != nullptr && element->id == 0));
   const basic_element_t* e1 = i0.find(1);
   BOOST_TEST(e1 == nullptr);
   i0.emplace([](basic_element_t& elem) {});
   const basic_element_t* e2 = i0.find(1);
   BOOST_TEST((e2 != nullptr && e2->id == 1));

   i0.remove(*element);
   element = i0.find(0);
   BOOST_TEST(element == nullptr);
}

struct test_element_t {
   uint64_t id;
   int secondary;
};

BOOST_AUTO_TEST_CASE(test_insert_undo) {
   eosio::undo_index<test_element_t, std::allocator<void>,
                     boost::multi_index::key<&test_element_t::id>,
                     boost::multi_index::key<&test_element_t::secondary> > i0;
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


BOOST_AUTO_TEST_CASE(test_modify_undo) {
   eosio::undo_index<test_element_t, std::allocator<void>,
                     boost::multi_index::key<&test_element_t::id>,
                     boost::multi_index::key<&test_element_t::secondary> > i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   {
   auto session = i0.start_undo_session(true);
   i0.modify(*i0.find(0), [](test_element_t& elem) { elem.secondary = 18; });
   BOOST_TEST(i0.find(0)->secondary == 18);
   }
   BOOST_TEST(i0.find(0)->secondary == 42);
}


BOOST_AUTO_TEST_CASE(test_remove_undo) {
   eosio::undo_index<test_element_t, std::allocator<void>,
                     boost::multi_index::key<&test_element_t::id>,
                     boost::multi_index::key<&test_element_t::secondary> > i0;
   i0.emplace([](test_element_t& elem) { elem.secondary = 42; });
   BOOST_TEST(i0.find(0)->secondary == 42);
   {
   auto session = i0.start_undo_session(true);
   i0.remove(*i0.find(0));
   BOOST_TEST(i0.find(0) == nullptr);
   }
   BOOST_TEST(i0.find(0)->secondary == 42);
}

BOOST_AUTO_TEST_CASE(test_insert_modify_undo) {
   eosio::undo_index<test_element_t, std::allocator<void>,
                     boost::multi_index::key<&test_element_t::id>,
                     boost::multi_index::key<&test_element_t::secondary> > i0;
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
   eosio::undo_index<test_element_t, std::allocator<void>,
                     boost::multi_index::key<&test_element_t::id>,
                     boost::multi_index::key<&test_element_t::secondary> > i0;
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
   eosio::undo_index<test_element_t, std::allocator<void>,
                     boost::multi_index::key<&test_element_t::id>,
                     boost::multi_index::key<&test_element_t::secondary> > i0;
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
   eosio::undo_index<test_element_t, std::allocator<void>,
                     boost::multi_index::key<&test_element_t::id>,
                     boost::multi_index::key<&test_element_t::secondary> > i0;
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
   eosio::undo_index<test_element_t, std::allocator<void>,
                     boost::multi_index::key<&test_element_t::id>,
                     boost::multi_index::key<&test_element_t::secondary> > i0;
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
   eosio::undo_index<test_element_t, std::allocator<void>,
                     boost::multi_index::key<&test_element_t::id>,
                     boost::multi_index::key<&test_element_t::secondary> > i0;
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
   eosio::undo_index<test_element_t, std::allocator<void>,
                     boost::multi_index::key<&test_element_t::id>,
                     boost::multi_index::key<&test_element_t::secondary> > i0;
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
   eosio::undo_index<test_element_t, std::allocator<void>,
                     boost::multi_index::key<&test_element_t::id>,
                     boost::multi_index::key<&test_element_t::secondary> > i0;
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

struct conflict_element_t {
   uint64_t id;
   int x0;
   int x1;
   int x2;
};

BOOST_AUTO_TEST_CASE(test_modify_conflict) {
   eosio::undo_index<conflict_element_t, std::allocator<void>,
                     boost::multi_index::key<&conflict_element_t::id>,
                     boost::multi_index::key<&conflict_element_t::x0>,
                     boost::multi_index::key<&conflict_element_t::x1>,
                     boost::multi_index::key<&conflict_element_t::x2> > i0;
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
}
