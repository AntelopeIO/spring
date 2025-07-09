#include <eosio/chain/platform_timer.hpp>
#include <eosio/chain/platform_timer_accuracy.hpp>

#include <fc/time.hpp>
#include <fc/fwd_impl.hpp>
#include <fc/exception/exception.hpp>
#include <fc/log/logger_config.hpp> //set_os_thread_name()

#include <boost/core/yield_primitives.hpp>

#include <mutex>
#include <thread>

#include <sys/event.h>

// this makes kqueue calls compatible with FreeBSD and this timer can be safely used
#if defined(__FreeBSD__)
#define EV_SET64(kev, ident, filter, flags, fflags, data, udata, ext0, ext1) EV_SET(kev, ident, filter, flags, fflags, data, reinterpret_cast<void*>(udata))
#define kevent64(kq, changelist, nchanges, eventlist, nevents, flags, timeout) kevent(kq, changelist, nchanges, eventlist, nevents, timeout)
#define kevent64_s kevent
#define KEVENT_FLAG_IMMEDIATE 0
#define NOTE_CRITICAL 0
#endif

namespace eosio { namespace chain {

// a kqueue & thread is shared for all platform_timer_macos instances
static std::mutex timer_ref_mutex;
static unsigned next_timerid;
static unsigned refcount;
static int kqueue_fd;
static std::thread kevent_thread;

struct platform_timer::impl {
   uint64_t timerid;

   constexpr static uint64_t quit_event_id = 1;
};

platform_timer::platform_timer() {
   static_assert(sizeof(impl) <= fwd_size);

   std::lock_guard guard(timer_ref_mutex);

   if(refcount++ == 0) {
      kqueue_fd = kqueue();

      FC_ASSERT(kqueue_fd != -1, "failed to create kqueue");

      //set up a EVFILT_USER which will be signaled to shut down the thread
      struct kevent64_s quit_event;
      EV_SET64(&quit_event, impl::quit_event_id, EVFILT_USER, EV_ADD|EV_ENABLE, NOTE_FFNOP, 0, 0, 0, 0);
      FC_ASSERT(kevent64(kqueue_fd, &quit_event, 1, NULL, 0, KEVENT_FLAG_IMMEDIATE, NULL) == 0, "failed to create quit event");

      kevent_thread = std::thread([]() {
         fc::set_thread_name("checktime");
         while(true) {
            struct kevent64_s anEvent;
            int c = kevent64(kqueue_fd, NULL, 0, &anEvent, 1, 0, NULL);

            if(c == 1 && anEvent.filter == EVFILT_TIMER) {
               platform_timer* self = (platform_timer*)anEvent.udata;
               self->expire_now();
            }
            else if(c == 1 && anEvent.filter == EVFILT_USER)
               return;
            else if(c == -1 && errno == EINTR)
               continue;
            else if(c == -1)
               return; //?? not much we can do now
         }
      });
   }

   my->timerid = next_timerid++;

   compute_and_print_timer_accuracy(*this);
}

platform_timer::~platform_timer() {
   stop();
   if(std::lock_guard guard(timer_ref_mutex); --refcount == 0) {
      struct kevent64_s signal_quit_event;
      EV_SET64(&signal_quit_event, impl::quit_event_id, EVFILT_USER, 0, NOTE_TRIGGER, 0, 0, 0, 0);

      if(kevent64(kqueue_fd, &signal_quit_event, 1, NULL, 0, KEVENT_FLAG_IMMEDIATE, NULL) != -1)
         kevent_thread.join();
      close(kqueue_fd);
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
      struct kevent64_s aTimerEvent;
      EV_SET64(&aTimerEvent, my->timerid, EVFILT_TIMER, EV_ADD|EV_ENABLE|EV_ONESHOT, NOTE_USECONDS|NOTE_CRITICAL, x.count(), (uint64_t)this, 0, 0);

      _state.store(timer_state_t{.state = state_t::running, .callback_in_flight = false});
      if(kevent64(kqueue_fd, &aTimerEvent, 1, NULL, 0, KEVENT_FLAG_IMMEDIATE, NULL) != 0)
         _state.store(timer_state_t{.state = state_t::timed_out, .callback_in_flight = false});
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
   // if still running, then interrupt so expire_now() and interrupt_timer() can't start a callback call
   timer_state_t prior_state{.state = state_t::running, .callback_in_flight = false};
   if (_state.compare_exchange_strong(prior_state, timer_state_t{state_t::interrupted, false})) {
      prior_state = timer_state_t{state_t::interrupted, false};
   }

   for (; prior_state.callback_in_flight; prior_state = _state.load())
      boost::core::sp_thread_pause();

   if(prior_state.state == state_t::stopped)
      return;
   _state.store(timer_state_t{.state = state_t::stopped, .callback_in_flight = false});
   if(prior_state.state == state_t::timed_out || timer_running_forever)
      return;

   struct kevent64_s stop_timer_event;
   EV_SET64(&stop_timer_event, my->timerid, EVFILT_TIMER, EV_DELETE, 0, 0, 0, 0, 0);
   kevent64(kqueue_fd, &stop_timer_event, 1, NULL, 0, KEVENT_FLAG_IMMEDIATE, NULL);
}

}}
