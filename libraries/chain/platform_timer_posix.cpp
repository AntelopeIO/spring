#include <eosio/chain/platform_timer.hpp>
#include <eosio/chain/platform_timer_accuracy.hpp>

#include <fc/time.hpp>
#include <fc/fwd_impl.hpp>
#include <fc/exception/exception.hpp>

#include <atomic>
#include <mutex>

#include <signal.h>
#include <time.h>
#include <sys/types.h>

namespace eosio::chain {

static_assert(std::atomic_bool::is_always_lock_free, "Only lock-free atomics AS-safe.");

struct platform_timer::impl {
   timer_t timerid;

   static void sig_handler(int, siginfo_t* si, void*) {
      platform_timer* self = (platform_timer*)si->si_value.sival_ptr;
      self->expire_now();
   }
};

platform_timer::platform_timer() {
   static_assert(sizeof(impl) <= fwd_size);

   static bool initialized;
   static std::mutex initialized_mutex;

   if(std::lock_guard guard(initialized_mutex); !initialized) {
      struct sigaction act;
      sigemptyset(&act.sa_mask);
      act.sa_sigaction = impl::sig_handler;
      act.sa_flags = SA_SIGINFO | SA_RESTART;
      FC_ASSERT(sigaction(SIGRTMIN, &act, NULL) == 0, "failed to aquire SIGRTMIN signal");
      initialized = true;
   }

   struct sigevent se;
   se.sigev_notify = SIGEV_SIGNAL;
   se.sigev_signo = SIGRTMIN;
   se.sigev_value.sival_ptr = (void*)this;

   FC_ASSERT(timer_create(CLOCK_REALTIME, &se, &my->timerid) == 0, "failed to create timer");

   compute_and_print_timer_accuracy(*this);
}

platform_timer::~platform_timer() {
   timer_delete(my->timerid);
}

void platform_timer::start(fc::time_point tp) {
   assert(_state == state_t::stopped);
   timer_running_forever = tp == fc::time_point::maximum();
   if(timer_running_forever) {
      _state = state_t::running;
      return;
   }
   fc::microseconds x = tp.time_since_epoch() - fc::time_point::now().time_since_epoch();
   if(x.count() <= 0)
      _state = state_t::timed_out;
   else {
      time_t secs = x.count() / 1000000;
      long nsec = (x.count() - (secs*1000000)) * 1000;
      struct itimerspec enable = {{0, 0}, {secs, nsec}};
      _state = state_t::running;
      if(timer_settime(my->timerid, 0, &enable, NULL) != 0)
         _state = state_t::timed_out;
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
   struct itimerspec disable = {{0, 0}, {0, 0}};
   timer_settime(my->timerid, 0, &disable, NULL);
}

}
