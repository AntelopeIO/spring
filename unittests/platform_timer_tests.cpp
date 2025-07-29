#include <eosio/chain/thread_utils.hpp>
#include <eosio/chain/platform_timer.hpp>

#include <boost/test/unit_test.hpp>

namespace eosio {
using namespace std;
using namespace chain;

BOOST_AUTO_TEST_SUITE(platform_timer_tests)

BOOST_AUTO_TEST_CASE(correct_num_callbacks_test)
{
   named_thread_pool<struct test> pool;
   const size_t num_threads = 16;
   pool.start(num_threads, [](const fc::exception& e) {
      BOOST_ERROR("exception: " + e.to_detail_string());
   });

   std::atomic<size_t> calls{0};
   platform_timer t;
   t.set_expiration_callback([](void* a) {
      auto atom = (std::atomic<size_t>*)a;
      ++(*atom);
   }, &calls);
   std::mutex m;
   std::atomic<size_t> barrier(num_threads);
   for (size_t i = 0; i < num_threads; ++i) {
      boost::asio::post(pool.get_executor(), [&]() {
         lock_guard lock(m);
         t.start(fc::time_point::now() + fc::milliseconds(15));
         std::this_thread::sleep_for(std::chrono::milliseconds(50));
         t.stop();
         --barrier;
      });
      if (i % 2 == 0) {
         boost::asio::post(pool.get_executor(), [&, i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds((i+1)*10));
            t.interrupt_timer();
         });
      }
   }
   for (size_t i = 0; i < 5000; ++i) {
      if (barrier == 0)
         break;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }
   BOOST_TEST_REQUIRE(barrier == 0u);
   pool.stop();

   BOOST_TEST(calls == num_threads);
}

std::map<size_t, bool> callback_called;
std::mutex cc_mtx;
void called(size_t i) {
   std::lock_guard lock(cc_mtx);
   callback_called[i] = true;
}

/// Test would fail with a sleep in interrupt_timer() before fix
BOOST_AUTO_TEST_CASE(correct_callback_test)
{
   named_thread_pool<struct test> pool;
   named_thread_pool<struct interrupt> interrupt_pool;
   const size_t num_threads = 16;
   pool.start(num_threads, [](const fc::exception& e) {
      BOOST_ERROR("exception: " + e.to_detail_string());
   });
   interrupt_pool.start(num_threads, [](const fc::exception& e) {
      BOOST_ERROR("exception: " + e.to_detail_string());
   });

   platform_timer t;
   std::mutex m;
   std::atomic<size_t> barrier(num_threads*2);
   for (size_t i = 0; i < num_threads; ++i) {
      boost::asio::post(pool.get_executor(), [i, &t, &m, &barrier]() {
         lock_guard lock(m);
         t.set_expiration_callback(nullptr, nullptr);
         t.set_expiration_callback([](void* a) {
            called((size_t)a);
         }, (void*)i);
         t.start(fc::time_point::now() + fc::milliseconds(15));
         std::this_thread::sleep_for(std::chrono::milliseconds(50));
         t.stop();
         --barrier;
      });
      boost::asio::post(interrupt_pool.get_executor(), [&, i]() {
         std::this_thread::sleep_for(std::chrono::milliseconds((i+1)*20));
         t.interrupt_timer();
         --barrier;
      });
   }
   for (size_t i = 0; i < 5000; ++i) {
      if (barrier == 0)
         break;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }
   BOOST_TEST_REQUIRE(barrier == 0u);
   pool.stop();

   std::lock_guard lock(cc_mtx);
   for (size_t i = 0; i < num_threads; ++i) {
      BOOST_TEST(callback_called[i]);
   }
}


BOOST_AUTO_TEST_SUITE_END()

} // namespace eosio
