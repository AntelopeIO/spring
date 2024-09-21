#pragma once
#include <boost/asio.hpp>
#include <boost/heap/binomial_heap.hpp>

#include <condition_variable>
#include <mutex>
#include <queue>

namespace appbase {
// adapted from: https://www.boost.org/doc/libs/1_69_0/doc/html/boost_asio/example/cpp11/invocation/prioritised_handlers.cpp

// Indicate non-unique handlers. If an existing handler at the specified priority already exists then there is
// no reason to insert a new handler to be processed.
//
// Add entries for each new non-unique handler type.
enum class handler_id {
   unique,                                // identifies handler is unique, will not de-dup
   process_incoming_block                 // process blocks already added to forkdb
};

enum class exec_queue {
   read_only,          // the queue storing tasks which are safe to execute
                       // in parallel with other read-only & read_exclusive tasks in the read-only
                       // thread pool as well as on the main app thread.
                       // Multi-thread safe as long as nothing is executed from the read_write queue.
   read_write,         // the queue storing tasks which can be only executed
                       // on the app thread while read-only tasks are
                       // not being executed in read-only threads. Single threaded.
   read_exclusive      // the queue storing tasks which should only be executed
                       // in parallel with other read_exclusive or read_only tasks in the
                       // read-only thread pool. Will never be executed on the main thread.
                       // If no read-only thread pool is available that calls one of the execute_* with
                       // read_exclusive then this queue grows unbounded. exec_pri_queue asserts
                       // if asked to queue a read_exclusive task when init'ed with 0 read-only threads.
};

// Locking has to be coordinated by caller, use with care.
class exec_pri_queue : public boost::asio::execution_context
{
public:

   ~exec_pri_queue() {
      clear(read_only_handlers_);
      clear(read_write_handlers_);
      clear(read_exclusive_handlers_);
   }

   // inform how many read_threads will be calling read_only/read_exclusive queues
   // expected to only be called at program startup, not thread safe, not safe to call when lock_enabled_
   void init_read_threads(size_t num_read_threads) {
      assert(!lock_enabled_);
      num_read_threads_ = num_read_threads;
   }

   // not strictly thread safe, see init_read_threads comment
   size_t get_read_threads() const {
      return num_read_threads_;
   }

   void stop() {
      std::lock_guard g( mtx_ );
      exiting_blocking_ = true;
      cond_.notify_all();
   }

   void enable_locking(std::function<bool()> should_exit) {
      assert(num_read_threads_ > 0 && num_waiting_ == 0);
      lock_enabled_ = true;
      max_waiting_ = num_read_threads_;
      should_exit_ = std::move(should_exit);
      exiting_blocking_ = false;
   }

   void disable_locking() {
      lock_enabled_ = false;
      should_exit_ = [](){ assert(false); return true; }; // should not be called when locking is disabled
   }

   template <typename Function>
   void add(int priority, exec_queue q, size_t order, Function&& function) {
      assert( num_read_threads_ > 0 || q != exec_queue::read_exclusive);
      prio_queue& que = priority_que(q);
      std::unique_ptr<queued_handler_base> handler(new queued_handler<Function>(handler_id::unique, priority, order, std::forward<Function>(function)));
      if (lock_enabled_ || q == exec_queue::read_exclusive) { // called directly from any thread for read_exclusive
         std::lock_guard g( mtx_ );
         que.push( handler.release() );
         if (num_waiting_)
            cond_.notify_one();
      } else {
         que.push( handler.release() );
      }
   }

   // called from appbase::application_base::exec poll_one() or run_one()
   template <typename Function>
   void add(handler_id id, int priority, exec_queue q, size_t order, Function&& function) {
      assert( num_read_threads_ > 0 || q != exec_queue::read_exclusive);
      if (id == handler_id::unique) {
         return add(priority, q, order, std::forward<Function>(function));
      }
      prio_queue& que = priority_que(q);
      std::unique_lock g( mtx_, std::defer_lock );
      if (lock_enabled_ || q == exec_queue::read_exclusive) {
         // called directly from any thread for read_exclusive
         g.lock();
      }
      if (!que.empty()) {
         // find the associated priority
         auto end = que.ordered_end();
         auto i = std::lower_bound(que.ordered_begin(), end, priority, [](const auto& h, int priority) {
            return h->priority() > priority;
         });
         if (i != end) {
            // ordered iterator appears to only be a forward iterator
            auto p = i;
            for (; i != end; p = i, ++i) {
               if ((*i)->priority() != priority)
                  break;
            }
            if ((*p)->priority() == priority && (*p)->id() == id)
               return;
         }
      }
      que.push( new queued_handler<Function>(id, priority, order, std::forward<Function>(function)) );
      if (g.owns_lock() && num_waiting_)
         cond_.notify_one();
   }

   // only call when no lock required
   void clear() {
      read_only_handlers_ = prio_queue();
      read_write_handlers_ = prio_queue();
      read_exclusive_handlers_ = prio_queue();
   }

   bool execute_highest_locked(exec_queue q) {
      prio_queue& que = priority_que(q);
      std::unique_lock g(mtx_);
      if (que.empty())
         return false;
      auto t = pop(que);
      g.unlock();
      t->execute();
      return true;
   }

   // only call when no lock required
   bool execute_highest(exec_queue lhs, exec_queue rhs) {
      prio_queue& lhs_que = priority_que(lhs);
      prio_queue& rhs_que = priority_que(rhs);
      size_t size = lhs_que.size() + rhs_que.size();
      if (size == 0)
         return false;
      exec_queue q = rhs;
      if (!lhs_que.empty() && (rhs_que.empty() || *rhs_que.top() < *lhs_que.top()))
         q = lhs;
      prio_queue& que = priority_que(q);
      assert(que.top());
      // pop, then execute since read_write queue is used to switch to read window and the pop needs to happen before that lambda starts
      auto t = pop(que);
      t->execute();
      --size;
      return size > 0;
   }

   bool execute_highest_blocking_locked(exec_queue lhs, exec_queue rhs) {
      prio_queue& lhs_que = priority_que(lhs);
      prio_queue& rhs_que = priority_que(rhs);
      std::unique_lock g(mtx_);
      ++num_waiting_;
      cond_.wait(g, [&](){
         bool exit = exiting_blocking_ || should_exit_();
         bool empty = lhs_que.empty() && rhs_que.empty();
         if (empty || exit) {
            if (((empty && num_waiting_ == max_waiting_) || exit) && !exiting_blocking_) {
               exiting_blocking_ = true;
               cond_.notify_all();
            }
            return exit || exiting_blocking_; // same as calling should_exit(), but faster
         }
         return true;
      });
      --num_waiting_;
      if (exiting_blocking_ || should_exit_())
         return false;
      if (lhs_que.empty() && rhs_que.empty())
         return false;
      exec_queue q = rhs;
      if (!lhs_que.empty() && (rhs_que.empty() || *rhs_que.top() < *lhs_que.top()))
         q = lhs;
      auto t = pop(priority_que(q));
      g.unlock();
      t->execute();
      return true; // this should never return false unless all read threads should exit
   }

   // Only call when locking disabled
   size_t size(exec_queue q) const { return priority_que(q).size(); }
   size_t size() const { return read_only_handlers_.size() + read_write_handlers_.size() + read_exclusive_handlers_.size(); }

   // Only call when locking disabled
   bool empty(exec_queue q) const { return priority_que(q).empty(); }

   // Only call when locking disabled
   const auto& top(exec_queue q) const { return priority_que(q).top(); }

   class executor
   {
   public:
      executor(exec_pri_queue& q, handler_id id, int p, size_t o, exec_queue que)
            : context_(q), que_(que), id_(id), priority_(p), order_(o)
      {
      }

      exec_pri_queue& context() const noexcept
      {
         return context_;
      }

      template <typename Function, typename Allocator>
      void dispatch(Function f, const Allocator&) const
      {
         context_.add(id_, priority_, que_, order_, std::move(f));
      }

      template <typename Function, typename Allocator>
      void post(Function f, const Allocator&) const
      {
         context_.add(id_, priority_, que_, order_, std::move(f));
      }

      template <typename Function, typename Allocator>
      void defer(Function f, const Allocator&) const
      {
         context_.add(id_, priority_, que_, order_, std::move(f));
      }

      void on_work_started() const noexcept {}
      void on_work_finished() const noexcept {}

      bool operator==(const executor& other) const noexcept
      {
         return order_ == other.order_ && priority_ == other.priority_ && que_ == other.que_ && &context_ == &other.context_;
      }

      bool operator!=(const executor& other) const noexcept
      {
         return !operator==(other);
      }

   private:
      exec_pri_queue& context_;
      exec_queue que_;
      handler_id id_;
      int priority_;
      size_t order_;
   };

   template <typename Function>
   boost::asio::executor_binder<Function, executor>
   wrap(handler_id id, int priority, exec_queue q, size_t order, Function&& func)
   {
      return boost::asio::bind_executor( executor(*this, id, priority, order, q), std::forward<Function>(func) );
   }

   template <typename Function>
   boost::asio::executor_binder<Function, executor>
   wrap(int priority, exec_queue q, size_t order, Function&& func)
   {
      return boost::asio::bind_executor( executor(*this, handler_id::unique, priority, order, q), std::forward<Function>(func) );
   }

private:
   class queued_handler_base
   {
   public:
      queued_handler_base( handler_id id, int p, size_t order )
            : id_( id )
            , priority_( p )
            , order_( order )
      {
      }

      virtual ~queued_handler_base() = default;

      virtual void execute() = 0;

      handler_id id() const { return id_; }
      int priority() const { return priority_; }

      friend bool operator<(const queued_handler_base& a, const queued_handler_base& b) noexcept {
         // exclude id_
         return std::tie( a.priority_, a.order_ ) < std::tie( b.priority_, b.order_ );
      }

   private:
      handler_id id_; // unique identifier of handler
      int priority_;  // priority of handler, see application_base priority
      size_t order_;  // maintain order within priority grouping
   };

   template <typename Function>
   class queued_handler : public queued_handler_base
   {
   public:
      queued_handler(handler_id id, int p, size_t order, Function f)
            : queued_handler_base( id, p, order )
            , function_( std::move(f) )
      {
      }

      void execute() final
      {
         function_();
      }

   private:
      Function function_;
   };

   struct deref_less
   {
      template<typename Pointer>
      bool operator()(const Pointer& a, const Pointer& b) const noexcept(noexcept(*a < *b))
      {
         return *a < *b;
      }
   };

   using prio_queue = boost::heap::binomial_heap<queued_handler_base*, boost::heap::compare<deref_less>>;

   prio_queue& priority_que(exec_queue q) {
      switch (q) {
         case exec_queue::read_only:
            return read_only_handlers_;
         case exec_queue::read_write:
            return read_write_handlers_;
         case exec_queue::read_exclusive:
            return read_exclusive_handlers_;
      }
      assert(false);
      return read_only_handlers_;
   }

   const prio_queue& priority_que(exec_queue q) const {
      switch (q) {
         case exec_queue::read_only:
            return read_only_handlers_;
         case exec_queue::read_write:
            return read_write_handlers_;
         case exec_queue::read_exclusive:
            return read_exclusive_handlers_;
      }
      assert(false);
      return read_only_handlers_;
   }

   static std::unique_ptr<exec_pri_queue::queued_handler_base> pop(prio_queue& que) {
      // work around priority_queue not having a pop() that returns value
      // take back ownership of pointer
      auto t = std::unique_ptr<queued_handler_base>(que.top());
      que.pop();
      return t;
   }

   void clear(prio_queue& que) {
      while (!que.empty())
         pop(que);
   }

   size_t num_read_threads_ = 0;
   bool lock_enabled_ = false;
   mutable std::mutex mtx_;
   std::condition_variable cond_;
   uint32_t num_waiting_{0};
   uint32_t max_waiting_{0};
   bool exiting_blocking_{false};
   std::function<bool()> should_exit_; // called holding mtx_
   prio_queue read_only_handlers_;
   prio_queue read_write_handlers_;
   prio_queue read_exclusive_handlers_;
};

} // appbase
