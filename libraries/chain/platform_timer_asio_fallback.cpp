#include <eosio/chain/platform_timer.hpp>
#include <eosio/chain/platform_timer_accuracy.hpp>

#include <fc/fwd_impl.hpp>
#include <fc/log/logger_config.hpp> //set_os_thread_name()

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
   assert(_state == state_t::stopped);
   timer_running_forever = tp == fc::time_point::maximum();
   if(timer_running_forever) {
      _state = state_t::running;
      return;
   }
   fc::microseconds x = tp.time_since_epoch() - fc::time_point::now().time_since_epoch();
   timer_running_forever = false;
   if(x.count() <= 0)
      _state = state_t::timed_out;
   else {
      _state = state_t::running;
      my->timer->expires_after(std::chrono::microseconds(x.count()));
      my->timer->async_wait([this](const boost::system::error_code& ec) {
         if(ec)
            return;
         expire_now();
      });
   }
}

void platform_timer::expire_now() {
   state_t expected = state_t::running;
   if (_state.compare_exchange_strong(expected, state_t::timed_out)) {
      call_expiration_callback();
   }
}

void platform_timer::interrupt_timer() {
   state_t expected = state_t::running;
   if (_state.compare_exchange_strong(expected, state_t::interrupted)) {
      call_expiration_callback();
   }
}

void platform_timer::stop() {
   const state_t prior_state = _state;
   if(prior_state == state_t::stopped)
      return;
   _state = state_t::stopped;
   if(prior_state == state_t::timed_out || timer_running_forever)
      return;

   my->timer->cancel();
}

}}
