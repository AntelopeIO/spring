#pragma once

#include <eosio/chain/vote_message.hpp>
#include <eosio/chain/block_state.hpp>
#include <eosio/chain/controller.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>

#include <unordered_map>

namespace eosio::chain {

/**
 * Process votes in a dedicated thread pool.
 */
class vote_processor_t {
   // Even 3000 vote structs are less than 1MB per connection.
   // 2500 should never be reached unless a specific connection is sending garbage.
   static constexpr size_t max_votes_per_connection = 2500;
   // If we have not processed a vote in this amount of time, give up on it.
   static constexpr fc::microseconds too_old = fc::seconds(5);

   struct by_block_num;
   struct by_connection;
   struct by_last_received;

   struct vote {
      uint32_t            connection_id;
      fc::time_point      received;
      vote_message_ptr    msg;

      const block_id_type& id() const { return msg->block_id; }
      block_num_type block_num() const { return block_header::num_from_id(msg->block_id); }
   };

   using vote_index_type = boost::multi_index_container< vote,
      indexed_by<
         ordered_non_unique< tag<by_block_num>, const_mem_fun<vote, block_num_type, &vote::block_num>, std::greater<> >, // descending
         ordered_non_unique< tag<by_connection>, member<vote, uint32_t, &vote::connection_id> >,
         ordered_non_unique< tag<by_last_received>, member<vote, fc::time_point, &vote::received> >
      >
   >;

   using emit_vote_signal_func_t = std::function<void(const vote_signal_params&)>;
   using fetch_block_func_t = std::function<block_state_ptr(const block_id_type&)>;

   emit_vote_signal_func_t      emit_vote_signal_func;
   fetch_block_func_t           fetch_block_func;

   std::mutex                   mtx;
   vote_index_type              index;
   block_state_ptr              last_bsp;
   //               connection, count of messages
   std::unordered_map<uint32_t, uint16_t> num_messages;

   std::atomic<block_num_type>  lib{0};
   std::atomic<block_num_type>  largest_known_block_num{0};
   std::atomic<uint32_t>        queued_votes{0};
   std::atomic<bool>            stopped{true};
   named_thread_pool<vote>      thread_pool;

private:
   // called with unlocked mtx
   void emit(uint32_t connection_id, vote_result_t status, const vote_message_ptr& msg,
             const finalizer_authority_ptr& active_auth, const finalizer_authority_ptr& pending_auth) {
      if (connection_id != 0) { // this nodes vote was already signaled
         if (status != vote_result_t::duplicate) { // don't bother emitting duplicates
            emit_vote_signal_func(vote_signal_params{connection_id, status,
                                                     std::cref(msg), std::cref(active_auth), std::cref(pending_auth)});
         }
      }
   }

   // called with locked mtx
   void remove_connection(uint32_t connection_id) {
      auto& idx = index.get<by_connection>();
      idx.erase(idx.lower_bound(connection_id), idx.upper_bound(connection_id));
   }

   // called with locked mtx
   void remove_before_lib() {
      auto& idx = index.get<by_block_num>();
      idx.erase(idx.lower_bound(lib.load()), idx.end()); // descending
      // don't decrement num_messages as too many before lib should be considered an error
   }

   // called with locked mtx
   void remove_too_old() {
      auto& idx = index.get<by_last_received>();
      fc::time_point vote_too_old = fc::time_point::now() - too_old;
      idx.erase(idx.lower_bound(fc::time_point::min()), idx.upper_bound(vote_too_old));
      // don't decrement num_messages as too many that are too old should be considered an error
   }

   // called with locked mtx
   void queue_for_later(uint32_t connection_id, const vote_message_ptr& msg) {
      fc::time_point now = fc::time_point::now();
      remove_before_lib();
      remove_too_old();
      index.insert(vote{.connection_id = connection_id, .received = now, .msg = msg});
   }

   // called with locked mtx, returns with a locked mutex
   void process_any_queued_for_later(std::unique_lock<std::mutex>& g) {
      if (index.empty())
         return;
      remove_too_old();
      remove_before_lib();
      auto& idx = index.get<by_last_received>();
      std::vector<vote> unprocessed;
      for (auto i = idx.begin(); i != idx.end();) {
         if (stopped)
            return;
         vote v = std::move(*i);
         idx.erase(i);
         auto bsp = get_block(v.msg->block_id, g);
         // g is unlocked
         if (bsp) {
            aggregate_vote_result_t r = bsp->aggregate_vote(v.connection_id, *v.msg);
            emit(v.connection_id, r.result, v.msg, r.active_authority, r.pending_authority);

            g.lock();
            if (auto& num = num_messages[v.connection_id]; num != 0)
               --num;
         } else {
            unprocessed.push_back(std::move(v));
            g.lock();
         }
         i = idx.begin(); // need to update since unlocked in loop
      }
      for (auto& v : unprocessed) {
         index.insert(std::move(v));
      }
   }

   // called with locked mtx, returns with unlocked mtx
   block_state_ptr get_block(const block_id_type& id, std::unique_lock<std::mutex>& g) {
      block_state_ptr bsp;
      if (last_bsp && last_bsp->id() == id) {
         bsp = last_bsp;
      }
      g.unlock();

      if (!bsp) {
         bsp = fetch_block_func(id);
         if (bsp) {
            g.lock();
            last_bsp = bsp;
            largest_known_block_num = std::max(bsp->block_num(), largest_known_block_num.load());
            g.unlock();
         }
      }
      return bsp;
   }

public:
   explicit vote_processor_t(emit_vote_signal_func_t&& emit_vote_signal, fetch_block_func_t&& get_block)
      : emit_vote_signal_func(std::move(emit_vote_signal))
      , fetch_block_func(std::move(get_block))
   {
      assert(emit_vote_signal_func);
      assert(fetch_block_func);
   }

   ~vote_processor_t() {
      stopped = true;
   }

   size_t index_size() {
      std::lock_guard g(mtx);
      return index.size();
   }

   void start(size_t num_threads, decltype(thread_pool)::on_except_t&& on_except) {
      if (num_threads == 0)
         return;

      stopped = false;
      thread_pool.start( num_threads, std::move(on_except));
   }

   // called from main thread
   void notify_lib(block_num_type block_num) {
      lib = block_num;
   }

   // called from net threads
   void notify_new_block(async_t async) {
      if (stopped)
         return;
      auto process_any_queued = [this] {
         std::unique_lock g(mtx);
         process_any_queued_for_later(g);
      };
      if (async == async_t::no)
         process_any_queued();
      else {
         // would require a mtx lock to check if index is empty, post check to thread_pool
         boost::asio::post(thread_pool.get_executor(), process_any_queued);
      }
   }

   /// called from net threads and controller's thread pool
   /// msg is ignored vote_processor not start()ed
   void process_vote_message(uint32_t connection_id, const vote_message_ptr& msg, async_t async) {
      if (stopped)
         return;
      assert(msg);
      block_num_type msg_block_num = block_header::num_from_id(msg->block_id);
      if (msg_block_num <= lib.load(std::memory_order_relaxed))
         return;
      ++queued_votes;

      auto process_vote =  [this, connection_id, msg] {
         if (stopped)
            return;
         auto num_queued_votes = --queued_votes;
         if (block_header::num_from_id(msg->block_id) <= lib.load(std::memory_order_relaxed))
            return; // ignore any votes lower than lib
         std::unique_lock g(mtx);
         if (num_queued_votes == 0 && index.empty()) // caught up, clear num_messages
            num_messages.clear();
         if (auto& num_msgs = ++num_messages[connection_id]; num_msgs > max_votes_per_connection) {
            remove_connection(connection_id);
            g.unlock();
            // drop, too many from this connection to process, consider connection invalid
            // don't clear num_messages[connection_id] so we keep reporting max_exceeded until index is drained

            ilog("Exceeded max votes per connection ${n} > ${max} for ${c}",
                 ("n", num_msgs)("max", max_votes_per_connection)("c", connection_id));
            emit(connection_id, vote_result_t::max_exceeded, msg, {}, {});
         } else {
            block_state_ptr bsp = get_block(msg->block_id, g);
            // g is unlocked

            if (!bsp) {
               // queue up for later processing
               g.lock();
               queue_for_later(connection_id, msg);
            } else {
               aggregate_vote_result_t r = bsp->aggregate_vote(connection_id, *msg);
               emit(connection_id, r.result, msg, r.active_authority, r.pending_authority);

               g.lock();
               if (auto& num = num_messages[connection_id]; num != 0)
                  --num;

               process_any_queued_for_later(g);
            }
         }

      };

      if (async == async_t::no)
         process_vote();
      else
         boost::asio::post(thread_pool.get_executor(), process_vote);
   }

};

} // namespace eosio::chain
