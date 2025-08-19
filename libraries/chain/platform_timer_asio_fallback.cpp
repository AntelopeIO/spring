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
   ++generation;
   timer_running_forever = tp == fc::time_point::maximum();
   if(timer_running_forever) {
      _state.store(timer_state_t{.state = state_t::running, .callback_in_flight = false, .generation_running = generation});
      return;
   }
   fc::microseconds x = tp.time_since_epoch() - fc::time_point::now().time_since_epoch();
   timer_running_forever = false;
   if(x.count() <= 0) {
      _state.store(timer_state_t{.state = state_t::timed_out, .callback_in_flight = false, .generation_running = generation});
   } else {
      _state.store(timer_state_t{.state = state_t::running, .callback_in_flight = false, .generation_running = generation});
      my->timer->expires_after(std::chrono::microseconds(x.count()));
      my->timer->async_wait([this, generation=generation](const boost::system::error_code& ec) {
         if(ec)
            return;
         expire_now(generation);
      });
   }
}

void platform_timer::expire_now(generation_t expired_generation) {
   timer_state_t expected{.state = state_t::running, .callback_in_flight = false, .generation_running = expired_generation};
   if (_state.compare_exchange_strong(expected, timer_state_t{state_t::timed_out, true, expired_generation})) {
      call_expiration_callback();
      _state.store(timer_state_t{state_t::timed_out, false, expired_generation});
   }
}

void platform_timer::interrupt_timer() {
   const generation_t generation_running = _state.load().generation_running;
   timer_state_t expected{.state = state_t::running, .callback_in_flight = false, .generation_running = generation_running};
   if (_state.compare_exchange_strong(expected, timer_state_t{state_t::interrupted, true, generation_running})) {
      call_expiration_callback();
      _state.store(timer_state_t{state_t::interrupted, false, generation_running});
   }
}

void platform_timer::stop() {
   // if still running, then interrupt so expire_now() and interrupt_timer() can't start a callback call
   timer_state_t prior_state{.state = state_t::running, .callback_in_flight = false, .generation_running = generation};
   if (_state.compare_exchange_strong(prior_state, timer_state_t{state_t::interrupted, false, generation})) {
      prior_state = timer_state_t{state_t::interrupted, false, generation};
   }

   for (; prior_state.callback_in_flight; prior_state = _state.load())
      boost::core::sp_thread_pause();

   if(prior_state.state == state_t::stopped)
      return;
   _state.store(timer_state_t{.state = state_t::stopped, .callback_in_flight = false, .generation_running = generation});
   if(prior_state.state == state_t::timed_out || timer_running_forever)
      return;

   my->timer->cancel();
}

}}
