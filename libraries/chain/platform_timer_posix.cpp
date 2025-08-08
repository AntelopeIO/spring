#include <eosio/chain/platform_timer.hpp>
#include <eosio/chain/platform_timer_accuracy.hpp>

#include <fc/time.hpp>
#include <fc/fwd_impl.hpp>
#include <fc/exception/exception.hpp>

#include <boost/core/yield_primitives.hpp>

#include <atomic>
#include <mutex>

#include <signal.h>
#include <time.h>
#include <sys/types.h>

namespace eosio::chain {

static_assert(std::atomic_bool::is_always_lock_free, "Only lock-free atomics AS-safe.");

struct platform_timer::impl {
   constexpr static unsigned num_timers = 8;
   static_assert(std::has_single_bit(num_timers), "num_timers must be a power of two");

   std::array<timer_t, num_timers> timerid;

   struct signal_initer {
      signal_initer() {
         FC_ASSERT(SIGRTMIN + num_timers <= SIGRTMAX, "too many timers");
         for(unsigned i = 0; i < num_timers; ++i) {
            struct sigaction act;
            sigemptyset(&act.sa_mask);
            act.sa_sigaction = impl::sig_handler;
            act.sa_flags = SA_SIGINFO | SA_RESTART;
            FC_ASSERT(sigaction(SIGRTMIN+i, &act, NULL) == 0, "failed to aquire SIGRTMIN+${i} signal", ("i", i));
         }
      }
   };

   static void sig_handler(int, siginfo_t* si, void*) {
      platform_timer* self = (platform_timer*)si->si_value.sival_ptr;
      self->expire_now((si->si_signo - SIGRTMIN) % num_timers);
   }
};

platform_timer::platform_timer() {
   static_assert(std::atomic<timer_state_t>::is_always_lock_free, "Only lock-free atomics AS-safe.");
   static_assert(sizeof(impl) <= fwd_size);
   static_assert(std::numeric_limits<generation_t>::max() > impl::num_timers-1, "generation_t rolls over before timer count does");

   static impl::signal_initer the_signal_initer;

   for(unsigned i = 0; i < impl::num_timers; ++i) {
      struct sigevent se;
      se.sigev_notify = SIGEV_SIGNAL;
      se.sigev_signo = SIGRTMIN+i;
      se.sigev_value.sival_ptr = (void*)this;

      FC_ASSERT(timer_create(CLOCK_REALTIME, &se, &my->timerid[i]) == 0, "failed to create timer");
   }

   compute_and_print_timer_accuracy(*this);
}

platform_timer::~platform_timer() {
   for(unsigned i = 0; i < impl::num_timers; ++i)
      timer_delete(my->timerid[i]);
}

void platform_timer::start(fc::time_point tp) {
   assert(timer_state() == state_t::stopped);
   generation = (generation + 1) % impl::num_timers;
   timer_running_forever = tp == fc::time_point::maximum();
   if(timer_running_forever) {
      _state.store(timer_state_t{.state = state_t::running, .callback_in_flight = false, .generation_running = generation});
      return;
   }
   fc::microseconds x = tp.time_since_epoch() - fc::time_point::now().time_since_epoch();
   if(x.count() <= 0) {
      _state.store(timer_state_t{.state = state_t::timed_out, .callback_in_flight = false, .generation_running = generation});
   } else {
      time_t secs = x.count() / 1000000;
      long nsec = (x.count() - (secs*1000000)) * 1000;
      struct itimerspec enable = {{0, 0}, {secs, nsec}};
      _state.store(timer_state_t{.state = state_t::running, .callback_in_flight = false, .generation_running = generation});
      if(timer_settime(my->timerid[generation], 0, &enable, NULL) != 0) {
         _state.store(timer_state_t{.state = state_t::timed_out, .callback_in_flight = false, .generation_running = generation});
      }
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
   timer_state_t expected{.state = state_t::running, .callback_in_flight = false, .generation_running = generation};
   if (_state.compare_exchange_strong(expected, timer_state_t{state_t::interrupted, true, generation})) {
      call_expiration_callback();
      _state.store(timer_state_t{state_t::interrupted, false, generation});
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
   struct itimerspec disable = {{0, 0}, {0, 0}};
   timer_settime(my->timerid[generation], 0, &disable, NULL);
}

}
