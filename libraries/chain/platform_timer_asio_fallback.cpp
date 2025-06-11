#include <eosio/chain/platform_timer.hpp>
#include <eosio/chain/platform_timer_accuracy.hpp>

#include <fc/fwd_impl.hpp>
#include <fc/log/logger_config.hpp> //set_os_thread_name()

#include <boost/core/yield_primitives.hpp>
#include <boost/asio.hpp>

#include <mutex>
#include <thread>

namespace eosio { namespace chain {

//a thread is shared for all instances
static std::mutex timer_ref_mutex;
static unsigned refcount;
static std::thread checktime_thread;
static std::unique_ptr<boost::asio::io_context> checktime_ios;

struct platform_timer::impl {
   std::unique_ptr<boost::asio::high_resolution_timer> timer;
};

platform_timer::platform_timer() {
   static_assert(std::atomic<timer_state_t>::is_always_lock_free, "Only lock-free atomics AS-safe.");
   static_assert(sizeof(impl) <= fwd_size);

   std::lock_guard guard(timer_ref_mutex);

   if(refcount++ == 0) {
      std::promise<void> p;
      auto f = p.get_future();
      checktime_thread = std::thread([&p]() {
         fc::set_thread_name("checktime");
         checktime_ios = std::make_unique<boost::asio::io_context>();
         boost::asio::io_context::work work(*checktime_ios);
         p.set_value();

         checktime_ios->run();
      });
      f.get();
   }

   my->timer = std::make_unique<boost::asio::high_resolution_timer>(*checktime_ios);

   //compute_and_print_timer_accuracy(*this);
}

platform_timer::~platform_timer() {
   stop();
   my->timer.reset();
   if(std::lock_guard guard(timer_ref_mutex); --refcount == 0) {
      checktime_ios->stop();
      checktime_thread.join();
      checktime_ios.reset();
   }
}

void platform_timer::start(fc::time_point tp) {
   assert(timer_state() == state_t::stopped);
   timer_running_forever = tp == fc::time_point::maximum();
   if(timer_running_forever) {
      _state.store(timer_state_t{.state = state_t::running, .callback_in_flight = false});
      return;
   }
   fc::microseconds x = tp.time_since_epoch() - fc::time_point::now().time_since_epoch();
   timer_running_forever = false;
   if(x.count() <= 0) {
      _state.store(timer_state_t{.state = state_t::timed_out, .callback_in_flight = false});
   } else {
      _state.store(timer_state_t{.state = state_t::running, .callback_in_flight = false});
      my->timer->expires_after(std::chrono::microseconds(x.count()));
      my->timer->async_wait([this](const boost::system::error_code& ec) {
         if(ec)
            return;
         expire_now();
      });
   }
}

void platform_timer::expire_now() {
   timer_state_t expected{.state = state_t::running, .callback_in_flight = false};
   if (_state.compare_exchange_strong(expected, timer_state_t{state_t::timed_out, true})) {
      call_expiration_callback();
      _state.store(timer_state_t{state_t::timed_out, false});
   }
}

void platform_timer::interrupt_timer() {
   timer_state_t expected{.state = state_t::running, .callback_in_flight = false};
   if (_state.compare_exchange_strong(expected, timer_state_t{state_t::interrupted, true})) {
      call_expiration_callback();
      _state.store(timer_state_t{state_t::interrupted, false});
   }
}

void platform_timer::stop() {
   while (_state.load().callback_in_flight)
      boost::core::sp_thread_pause();

   const timer_state_t prior_state = _state.load();
   if(prior_state.state == state_t::stopped)
      return;
   _state.store(timer_state_t{.state = state_t::stopped, .callback_in_flight = false});
   if(prior_state.state == state_t::timed_out || timer_running_forever)
      return;

   my->timer->cancel();
}

}}
