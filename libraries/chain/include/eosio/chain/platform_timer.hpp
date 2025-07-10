#pragma once
#include <fc/time.hpp>
#include <fc/fwd.hpp>
#include <fc/scoped_exit.hpp>

#include <eosio/chain/exceptions.hpp>

#include <atomic>

namespace eosio { namespace chain {

struct platform_timer {
   platform_timer();
   ~platform_timer();

   //start() & stop() are not thread safe to each other; i.e. do not overlap calls to start() and stop()
   void start(fc::time_point tp);
   void stop();
   //interrupt_timer() can be called from any thread
   void interrupt_timer();

   /* Sets a callback for when timer expires. Be aware this could fire from a signal handling context and/or
      on any particular thread. Only a single callback can be registered at once; trying to register more will
      result in an exception. Setting to nullptr disables any current set callback.
      Also, stop() is not perfectly synchronized with the callback. It is possible for stop() to return and the
      callback still execute if the timer expires and stop() is called nearly simultaneously.
      However, set_expiration_callback() is synchronized with the callback.
   */
   void set_expiration_callback(void(*func)(void*), void* user, bool appending = false) {
      bool expect_false = false;
      while(!atomic_compare_exchange_strong(&_callback_variables_busy, &expect_false, true))
         expect_false = false;
      auto reset_busy = fc::make_scoped_exit([this]() {
         _callback_variables_busy.store(false, std::memory_order_release);
      });
      EOS_ASSERT(!(!appending && func && !_expiration_callbacks.empty()), misc_exception, "Setting a platform_timer callback when one already exists");

      if (!func && !_expiration_callbacks.empty()) {
         _expiration_callbacks.pop_back();
         return;
      }

      _expiration_callbacks.push_back({func, user});
   }

   enum class state_t : uint8_t {
      running = 0,
      timed_out,
      interrupted,
      stopped
   };
   state_t timer_state() const { return _state.load().state; }

private:
   void expire_now();

   struct timer_state_t {
      state_t state = state_t::stopped;
      bool callback_in_flight = false;
   };
   std::atomic<timer_state_t> _state;
   bool timer_running_forever = false;

   struct impl;
   constexpr static size_t fwd_size = 8;
   fc::fwd<impl,fwd_size> my;

   void call_expiration_callback() {
      bool expect_false = false;
      if(atomic_compare_exchange_strong(&_callback_variables_busy, &expect_false, true)) {
         for (auto it = _expiration_callbacks.rbegin(); it != _expiration_callbacks.rend(); ++it) {
            if (it->first) {
               it->first(it->second);
            }
         }

         _callback_variables_busy.store(false, std::memory_order_release);
      }
   }

   std::atomic_bool _callback_variables_busy = false;
   std::vector<std::pair<void(*)(void*), void*>> _expiration_callbacks;
};

}}
