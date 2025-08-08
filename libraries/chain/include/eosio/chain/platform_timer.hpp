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
   void set_expiration_callback(void(*func)(void*), void* user) {
      bool expect_false = false;
      while(!atomic_compare_exchange_strong(&_callback_variables_busy, &expect_false, true))
         expect_false = false;
      auto reset_busy = fc::make_scoped_exit([this]() {
         _callback_variables_busy.store(false, std::memory_order_release);
      });
      EOS_ASSERT(!(func && _expiration_callback), misc_exception, "Setting a platform_timer callback when one already exists");

      _expiration_callback = func;
      _expiration_callback_data = user;
   }

   enum class state_t : uint8_t {
      running = 0,
      timed_out,
      interrupted,
      stopped
   };
   state_t timer_state() const { return _state.load().state; }

private:
   using generation_t = uint16_t;

   void expire_now(generation_t expired_generation);

   struct timer_state_t {
      state_t state = state_t::stopped;
      bool callback_in_flight = false;
      generation_t generation_running = 0;
   };
   std::atomic<timer_state_t> _state;
   bool timer_running_forever = false;
   generation_t generation = 0;

   struct impl;
   constexpr static size_t fwd_size = 64;
   fc::fwd<impl,fwd_size> my;

   void call_expiration_callback() {
      bool expect_false = false;
      if(atomic_compare_exchange_strong(&_callback_variables_busy, &expect_false, true)) {
         void(*cb)(void*) = _expiration_callback;
         void* cb_data = _expiration_callback_data;
         if(cb) {
            cb(cb_data);
         }
         _callback_variables_busy.store(false, std::memory_order_release);
      }
   }

   std::atomic_bool _callback_variables_busy = false;
   void(*_expiration_callback)(void*) = nullptr;
   void* _expiration_callback_data = nullptr;
};

}}
