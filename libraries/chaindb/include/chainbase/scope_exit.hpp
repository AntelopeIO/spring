#pragma once
#include <utility>
#include <exception>

namespace chainbase {
   template<typename F>
   struct scope_exit {
      [[nodiscard]] scope_exit(F&& f) : _f(std::move(f)) {}
      scope_exit(const scope_exit&) = delete;
      scope_exit& operator=(const scope_exit&) = delete;
      ~scope_exit() { if(!_canceled) _f(); }
      void cancel() { _canceled = true; }
      F _f;
      bool _canceled = false;
   };

   template<typename F>
   struct scope_fail {
      scope_fail(F&& f) : _f{static_cast<F&&>(f)}, _exception_count{std::uncaught_exceptions()} {}
      ~scope_fail() {
         if(_exception_count != std::uncaught_exceptions()) _f();
      }
      F _f;
      int _exception_count;
   };
}