#include <barrier>
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
   std::barrier b(num_threads+1);
   for (size_t i = 0; i < num_threads; ++i) {
      boost::asio::post(pool.get_executor(), [&]() {
         lock_guard lock(m);
         t.start(fc::time_point::now() + fc::milliseconds(15));
         std::this_thread::sleep_for(std::chrono::milliseconds(50));
         t.stop();
         b.arrive_and_drop();
      });
      if (i % 2 == 0) {
         boost::asio::post(pool.get_executor(), [&]() {
            t.interrupt_timer();
         });
      }
   }
   b.arrive_and_wait();
   pool.stop();

   BOOST_TEST(calls == num_threads);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace eosio
