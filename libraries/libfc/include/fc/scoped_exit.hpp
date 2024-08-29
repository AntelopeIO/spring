#pragma once

#include <utility>

namespace fc {

   template<typename Callback>
   class scoped_exit {
      public:
         template<typename C>
         [[nodiscard]] scoped_exit( C&& c ):callback( std::forward<C>(c) ){}

         [[nodiscard]] scoped_exit( scoped_exit&& mv )
         :callback( std::move( mv.callback ) ),canceled(mv.canceled)
         {
            mv.canceled = true;
         }

         scoped_exit( const scoped_exit& ) = delete;
         scoped_exit& operator=( const scoped_exit& ) = delete;

         ~scoped_exit() {
            if (!canceled)
               try { callback(); } catch( ... ) {}
         }

         void cancel() { canceled = true; }

      private:
         Callback callback;
         bool canceled = false;
   };

   template<typename Callback>
   scoped_exit<Callback> make_scoped_exit( Callback&& c ) {
      return scoped_exit<Callback>( std::forward<Callback>(c) );
   }

   // ---------------------------------------------------------------------------
   // An object which assigns a value to a variable in its constructor, and resets
   // to its previous value in its destructor
   // ---------------------------------------------------------------------------
   template <class T>
   class scoped_set_value {
   public:
      template <class V>
      [[nodiscard]] scoped_set_value(T& var, V&& val,
                                     bool do_it = true) noexcept(std::is_nothrow_copy_constructible_v<T> &&
                                                                 std::is_nothrow_move_assignable_v<T>)
         : _v(var)
         , _do_it(do_it) {
         if (_do_it) {
            _old_value = std::move(_v);
            _v         = std::forward<V>(val);
         }
      }

      ~scoped_set_value() {
         if (_do_it)
            _v = std::move(_old_value);
      }

      void dismiss() noexcept { _do_it = false; }

      scoped_set_value(const scoped_set_value&)            = delete;
      scoped_set_value& operator=(const scoped_set_value&) = delete;
      scoped_set_value(scoped_set_value&&)                 = delete;
      scoped_set_value& operator=(scoped_set_value&&)      = delete;
      void*             operator new(std::size_t)          = delete;

   private:
      T&   _v;
      T    _old_value;
      bool _do_it;
   };
}
