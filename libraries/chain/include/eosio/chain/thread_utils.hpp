#pragma once

#include <eosio/chain/name.hpp>
#include <fc/exception/exception.hpp>
#include <fc/log/logger_config.hpp>
#include <fc/scoped_exit.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/use_future.hpp>
#include <future>
#include <list>
#include <memory>
#include <optional>
#include <thread>

namespace eosio { namespace chain {

   // Avoid GCC warning:
   // libraries/chain/include/eosio/chain/thread_utils.hpp:28:15: warning: use of ‘std::hardware_destructive_interference_size’ [-Winterference-size]
   //   28 |       alignas(hardware_destructive_interference_size)
   //      |               ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
   // thread_utils.hpp:28:15: note: its value can vary between compiler versions or with different ‘-mtune’ or ‘-mcpu’ flags
   // thread_utils.hpp:28:15: note: if this use is part of a public ABI, change it to instead use a constant variable you define
   // thread_utils.hpp:28:15: note: the default value for the current CPU tuning is 64 bytes
   // thread_utils.hpp:28:15: note: you can stabilize this value with ‘--param hardware_destructive_interference_size=64’, or disable this warning with ‘-Wno-interference-size’
   //
   // 64 bytes on x86-64 │ L1_CACHE_BYTES │ L1_CACHE_SHIFT │ __cacheline_aligned │ ...
   constexpr std::size_t hardware_destructive_interference_sz = 64;

   // Use instead of std::atomic when std::atomic does not support type
   template <typename T>
   class large_atomic {
      mutable std::mutex mtx;
      T value{};
   public:
      T load() const {
         std::lock_guard g(mtx);
         return value;
      }
      void store(const T& v) {
         std::lock_guard g(mtx);
         value = v;
      }

      class accessor {
         std::lock_guard<std::mutex> g;
         T& v;
      public:
         accessor(std::mutex& m, T& v)
            : g(m), v(v) {}
         T& value() { return v; }
      };

      auto make_accessor() { return accessor{mtx, value}; }
   };

   template <typename T>
   class copyable_atomic {
      std::atomic<T> value;
   public:
      copyable_atomic() = default;
      copyable_atomic(T v) noexcept
         : value(v) {}
      copyable_atomic(const copyable_atomic& rhs)
         : value(rhs.value.load(std::memory_order_relaxed)) {}
      copyable_atomic(copyable_atomic&& rhs) noexcept
         : value(rhs.value.load(std::memory_order_relaxed)) {}

      T load(std::memory_order mo = std::memory_order_seq_cst) const noexcept { return value.load(mo); }
      void store(T v, std::memory_order mo = std::memory_order_seq_cst) noexcept { value.store(v, mo); }

      template<typename DS>
      friend DS& operator<<(DS& ds, const copyable_atomic& ca) {
         fc::raw::pack(ds, ca.load(std::memory_order_relaxed));
         return ds;
      }

      template<typename DS>
      friend DS& operator>>(DS& ds, copyable_atomic& ca) {
         T v;
         fc::raw::unpack(ds, v);
         ca.store(v, std::memory_order_relaxed);
         return ds;
      }
   };

   /**
    * Wrap a std::future so that any exception on destruction is caught, logged, and dropped.
    * @tparam Response future<Response> type
    */
   template<typename Response>
   struct log_and_drop_future {
      log_and_drop_future() = default;
      explicit log_and_drop_future(std::future<Response>&& f) : fut(f) {};

      // Only call on default constructed log_and_drop_future.
      log_and_drop_future& operator=(std::future<Response>&& f) {
         assert(!fut.valid());
         fut = std::move(f);
         return *this;
      }

      Response get() { cancel(); return fut.get(); }
      bool valid() const { return fut.valid(); }
      void cancel() { fut_exit_scope_handler.cancel(); }
   private:
      std::future<Response> fut;
      fc::scoped_exit<std::function<void()>> fut_exit_scope_handler = [this] {
         if (fut.valid()) {
            try {
               fut.get();
            } FC_LOG_AND_DROP()
         }
      };
   };

   inline std::string thread_name_base_from_typeinfo(const std::type_info& tinfo) {
      std::string tn = boost::core::demangle(tinfo.name());
      const size_t offset = tn.rfind("::");
      if(offset != std::string::npos)
         tn.erase(0, offset+2);
      tn = tn.substr(0, tn.find('>'));
      return tn;
   }

   inline std::string set_current_thread_name_to_typename(const std::type_info& tinfo, const unsigned i) {
      const std::string tn = thread_name_base_from_typeinfo(tinfo) + "-" + std::to_string(i);
      fc::set_thread_name(tn);
      return tn;
   }

   /**
    * Wrapper class for thread pool of boost asio io_context run.
    * Also names threads so that tools like htop can see thread name.
    * Example: named_thread_pool<struct net> thread_pool;
    *      or: struct net{}; named_thread_pool<net> thread_pool;
    * @param NamePrefixTag is a type name appended with -## of thread.
    *                    A short NamePrefixTag type name (6 chars or under) is recommended as console_appender uses
    *                    9 chars for the thread name.
    */
   template<typename NamePrefixTag>
   class named_thread_pool {
   public:
      using on_except_t = std::function<void(const fc::exception& e)>;
      using init_t = std::function<void(size_t)>; // init is passed the thread pool index

      named_thread_pool() = default;

      ~named_thread_pool(){
         stop();
      }

      boost::asio::io_context& get_executor() { return _ioc; }

      /// Spawn threads, can be re-started after stop().
      /// Assumes start()/stop() called from the same thread or externally protected.
      /// Blocks until all threads are created and completed their init function, or an exception is thrown
      ///  during thread startup or an init function. Exceptions thrown during these stages are rethrown from start()
      ///  but some threads might still have been started. Calling stop() after such a failure is safe.
      /// @param num_threads is number of threads spawned, if 0 then no threads are spawned and stop() is a no-op.
      /// @param on_except is the function to call if io_context throws an exception, is called from thread pool thread.
      ///                  if an empty function then logs and rethrows exception on thread which will terminate. Not called
      ///                  for exceptions during the init function (such exceptions are rethrown from start())
      /// @param init is an optional function to call at startup to initialize any data.
      /// @throw assert_exception if already started and not stopped.
      void start( size_t num_threads, on_except_t on_except, init_t init = {} ) {
         FC_ASSERT( !_ioc_work, "Thread pool already started" );
         if (num_threads == 0)
            return;
         _ioc_work.emplace( boost::asio::make_work_guard( _ioc ) );
         _ioc.restart();
         _thread_pool.reserve( num_threads );

         std::promise<void> start_complete;
         std::atomic<uint32_t> threads_remaining = num_threads;
         std::exception_ptr pending_exception;
         std::mutex pending_exception_mutex;

         try {
            for( size_t i = 0; i < num_threads; ++i ) {
               _thread_pool.emplace_back( std::thread( &named_thread_pool::run_thread, this, i, on_except, init, std::ref(start_complete),
                                                       std::ref(threads_remaining), std::ref(pending_exception), std::ref(pending_exception_mutex) ) );
            }
         }
         catch( ... ) {
            /// only an exception from std::thread's ctor should end up here. shut down all threads to ensure no
            ///  potential access to the promise, atomic, etc above performed after throwing out of start
            stop();
            throw;
         }
         start_complete.get_future().get();
      }

      /// destroy work guard, stop io_context, join thread_pool
      /// not thread safe, expected to only be called from thread that called start()
      void stop() {
         if (_thread_pool.size() > 0) {
            tlog("stoping ${i}", ("i", boost::core::demangle(typeid(this).name())));
            _ioc_work.reset();
            _ioc.stop();
            for( auto& t : _thread_pool ) {
               t.join();
            }
            _thread_pool.clear();
            tlog("stopped ${i}", ("i", boost::core::demangle(typeid(this).name())));
         }
      }

      on_except_t make_on_except_abort() {
         return [tn=thread_name_base_from_typeinfo(typeid(this))](const fc::exception& e) {
            elog("Unexpected exception in a ${n} thread, aborting: ${e}", ("n", tn)("e", e.to_detail_string()));
            abort();
         };
      }

   private:
      void run_thread( size_t i, const on_except_t& on_except, const init_t& init, std::promise<void>& start_complete,
                       std::atomic<uint32_t>& threads_remaining, std::exception_ptr& pending_exception, std::mutex& pending_exception_mutex ) {

         std::string tn;

         auto decrement_remaining = [&]() {
            if( !--threads_remaining ) {
               if( pending_exception )
                  start_complete.set_exception( pending_exception );
               else
                  start_complete.set_value();
            }
         };

         try {
            try {
               tn = set_current_thread_name_to_typename( typeid(this), i );
               if ( init )
                  init(i);
            } FC_LOG_AND_RETHROW()
         }
         catch( ... ) {
            std::lock_guard<std::mutex> l( pending_exception_mutex );
            pending_exception = std::current_exception();
            decrement_remaining();
            return;
         }

         decrement_remaining();

         try {
            _ioc.run();
         } catch( const fc::exception& e ) {
            if( on_except ) {
               on_except( e );
            } else {
               elog( "Exiting thread ${t} on exception: ${e}", ("t", tn)("e", e.to_detail_string()) );
               throw;
            }
         } catch( const std::exception& e ) {
            fc::std_exception_wrapper se( FC_LOG_MESSAGE( warn, "${what}: ", ("what", e.what()) ),
                                          std::current_exception(), BOOST_CORE_TYPEID( e ).name(), e.what() );
            if( on_except ) {
               on_except( se );
            } else {
               elog( "Exiting thread ${t} on exception: ${e}", ("t", tn)("e", se.to_detail_string()) );
               throw;
            }
         } catch( ... ) {
            if( on_except ) {
               fc::unhandled_exception ue( FC_LOG_MESSAGE( warn, "unknown exception" ), std::current_exception() );
               on_except( ue );
            } else {
               elog( "Exiting thread ${t} on unknown exception", ("t", tn) );
               throw;
            }
         }
      }

   private:
      using ioc_work_t = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

      boost::asio::io_context        _ioc;
      std::vector<std::thread>       _thread_pool;
      std::optional<ioc_work_t>      _ioc_work;
   };

   /// Submit work to be done in a thread pool, and then wait for that work to complete (or until a thread throws an exception
   /// which will be rethrown on the thread waiting for completion)
   template<typename NamePrefixTag>
   struct sync_threaded_work {
      boost::asio::io_context& io_context() {
         return ctx;
      }

      void run(const unsigned num_threads) {
         run(num_threads, std::chrono::years::max(), [](){});
      }

      /// ping will be called every ping_interval while waiting for all work to complete. This time isn't precise, but
      /// good enough for a log or similar.
      template<typename Rep, typename Period, typename F>
      void run(const unsigned num_threads, const std::chrono::duration<Rep, Period>& ping_interval, F&& ping) {
         std::vector<std::promise<void>> thread_promises(num_threads);
         std::list<std::thread> threads;

         //this scoped_exit can go away with jthread; but still marked experimental in libc++18
         auto join = fc::make_scoped_exit([&threads]() {
            for(std::thread& t : threads)
               t.join();
         });

         for(unsigned i = 0; i < num_threads; ++i)
            threads.emplace_back([this, i, &prom = thread_promises[i]] {
               try {
                  set_current_thread_name_to_typename(typeid(this), i);
                  ctx.run();
                  prom.set_value();
               }
               catch(...) {
                  ctx.stop();
                  prom.set_exception(std::current_exception());
               }
            });

         for(std::promise<void>& p : thread_promises) {
            std::future<void> f = p.get_future();
            while(f.wait_for(ping_interval) != std::future_status::ready)
               ping();
            f.get();
         }
      }

   private:
      boost::asio::io_context ctx;
   };

   template<typename T>
   concept SupportsASIOPost = boost::asio::execution::is_executor<std::decay_t<T>>::value ||
                              std::is_same_v<std::decay_t<T>, boost::asio::io_context>;

   // async on executor and return future
   template<SupportsASIOPost E, typename F>
   auto post_async_task( E&& ioc, F&& f ) {
      return boost::asio::post( std::forward<E>(ioc), boost::asio::use_future(std::forward<F>(f)) );
   }

} } // eosio::chain

namespace fc {

template<typename T> struct get_typename<eosio::chain::copyable_atomic<T>> {
   static const char* name()  {
      static std::string n = std::string("copyable_atomic<") + get_typename<T>::name() + ">";
      return n.c_str();
   }
};

template<typename T>
void to_variant(const eosio::chain::copyable_atomic<T>& e, fc::variant& v) {
   T t = e.load();
   if constexpr (std::is_same_v<T, bool>) {
      v = t;
   } else {
      to_variant( t, v );
   }
}

template<typename T>
void from_variant(const fc::variant& v, eosio::chain::copyable_atomic<T>& e) {
   T t;
   from_variant( v, t );
   e.store(t);
}

} // namespace fc
