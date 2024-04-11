#pragma once

#include <eosio/chain/hotstuff/hotstuff.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>

namespace eosio { namespace chain {

/**
 * Process votes in a dedicated thread pool.
 */
class vote_processor_t {
   static constexpr size_t max_votes_per_connection = 2500; // 3000 is less than 1MB per connection
   static constexpr std::chrono::milliseconds block_wait_time{10};

   struct by_block_num;
   struct by_connection;
   struct by_vote;

   struct vote {
      uint32_t        connection_id;
      vote_message    msg;

      const block_id_type& id() const { return msg.block_id; }
      block_num_type block_num() const { return block_header::num_from_id(msg.block_id); }
   };

   using vote_ptr = std::shared_ptr<vote>;
   using vote_signal_type = decltype(controller({},chain_id_type::empty_chain_id()).voted_block());

   typedef multi_index_container< vote_ptr,
      indexed_by<
         ordered_non_unique<tag<by_block_num>,
            composite_key<vote,
               const_mem_fun<vote, block_num_type, &vote::block_num>,
               const_mem_fun<vote, const block_id_type&, &vote::id>
            >, composite_key_compare< std::greater<>, sha256_less > // greater for block_num
         >,
         ordered_non_unique< tag<by_connection>, member<vote, uint32_t, &vote::connection_id> >,
         ordered_unique< tag<by_vote>, member<vote, vote_message, &vote::msg> >
      >
   > vote_index_type;

   fork_database&               fork_db;
   std::mutex                   mtx;
   std::condition_variable      cv;
   vote_index_type              index;
   //     connection, count of messages
   std::map<uint32_t, uint16_t> num_messages;
   std::atomic<block_num_type>  lib{0};
   std::atomic<bool>            stopped{false};
   vote_signal_type&            vote_signal;
   named_thread_pool<vote>      thread_pool;

private:
   template<typename Signal, typename Arg>
   void emit( const Signal& s, Arg&& a ) {
      try {
         s(std::forward<Arg>(a));
      } catch (std::bad_alloc& e) {
         wlog( "std::bad_alloc: ${w}", ("w", e.what()) );
         throw e;
      } catch (boost::interprocess::bad_alloc& e) {
         wlog( "boost::interprocess::bad alloc: ${w}", ("w", e.what()) );
         throw e;
      } catch ( controller_emit_signal_exception& e ) {
         wlog( "controller_emit_signal_exception: ${details}", ("details", e.to_detail_string()) );
         throw e;
      } catch ( fc::exception& e ) {
         wlog( "fc::exception: ${details}", ("details", e.to_detail_string()) );
      } catch ( std::exception& e ) {
         wlog( "std::exception: ${details}", ("details", e.what()) );
      } catch ( ... ) {
         wlog( "signal handler threw exception" );
      }
   }

   void emit(uint32_t connection_id, vote_status status, const vote_message& msg) {
      if (connection_id != 0) { // this nodes vote was already signaled
         emit( vote_signal, std::tuple{connection_id, status, std::cref(msg)} );
      }
   }

   void remove_connection(uint32_t connection_id) {
      auto& idx = index.get<by_connection>();
      idx.erase(idx.lower_bound(connection_id), idx.upper_bound(connection_id));
   }

   void remove_before_lib() {
      auto& idx = index.get<by_block_num>();
      idx.erase(idx.lower_bound(lib.load()), idx.end()); // descending
      // don't decrement num_messages as too many before lib should be considered an error
   }

   bool remove_all_for_block(auto& idx, auto& it, const block_id_type& id) {
      while (it != idx.end() && (*it)->id() == id) {
         --num_messages[(*it)->connection_id];
         it = idx.erase(it);
      }
      return it == idx.end();
   }

   bool skip_all_for_block(auto& idx, auto& it, const block_id_type& id) {
      while (it != idx.end() && (*it)->id() == id) {
         ++it;
      }
      return it == idx.end();
   }

public:
   explicit vote_processor_t(fork_database& forkdb, vote_signal_type& vote_signal)
      : fork_db(forkdb)
      , vote_signal(vote_signal) {}

   ~vote_processor_t() {
      stopped = true;
      std::lock_guard g(mtx);
      cv.notify_one();
   }

   void start(size_t num_threads) {
      assert(num_threads > 1); // need at least two as one is used for coordinatation
      thread_pool.start( num_threads, []( const fc::exception& e ) {
         elog( "Exception in vote processor thread pool, exiting: ${e}", ("e", e.to_detail_string()) );
      } );

      // one coordinator thread
      boost::asio::post(thread_pool.get_executor(), [&]() {
         block_id_type not_in_forkdb_id{};
         while (!stopped) {
            std::unique_lock g(mtx);
            cv.wait(g, [&]() {
               if (!index.empty() || stopped)
                  return true;
               return false;
            });
            if (stopped)
               break;
            remove_before_lib();
            if (index.empty()) {
               num_messages.clear();
               continue;
            }
            auto& idx = index.get<by_block_num>();
            if (auto i = idx.begin(); i != idx.end() && not_in_forkdb_id == (*i)->id()) { // same block as last while loop
               g.unlock();
               std::this_thread::sleep_for(block_wait_time);
               g.lock();
            }
            for (auto i = idx.begin(); i != idx.end();) {
               auto& vt = *i;
               block_state_ptr bsp = fork_db.apply_s<block_state_ptr>([&](const auto& forkdb) {
                  return forkdb.get_block(vt->id());
               });
               if (bsp) {
                  if (!bsp->is_proper_svnn_block()) {
                     if (remove_all_for_block(idx, i, bsp->id()))
                        break;
                     continue;
                  }
                  auto iter_of_bsp = i;
                  std::vector<vote_ptr> to_process;
                  to_process.reserve(std::min<size_t>(21u, idx.size())); // increase if we increase # of finalizers from 21
                  for(; i != idx.end() && bsp->id() == (*i)->id(); ++i) {
                     // although it is the highest contention on block state pending mutex posting all of the same bsp,
                     // the highest priority is processing votes for this block state.
                     to_process.push_back(*i);
                  }
                  bool should_break = remove_all_for_block(idx, iter_of_bsp, bsp->id());
                  g.unlock(); // do not hold lock when posting
                  for (auto& vptr : to_process) {
                     boost::asio::post(thread_pool.get_executor(), [this, bsp, vptr=std::move(vptr)]() {
                        vote_status s = bsp->aggregate_vote(vptr->msg);
                        emit(vptr->connection_id, s, vptr->msg);
                     });
                  }
                  if (should_break)
                     break;
               } else {
                  not_in_forkdb_id = vt->id();
                  if (skip_all_for_block(idx, i, (*i)->id()))
                     break;
               }
            }
         }
         dlog("Exiting vote processor coordinator thread");
      });
   }

   void notify_lib(block_num_type block_num) {
      lib = block_num;
   }

   void process_vote_message(uint32_t connection_id, const vote_message& msg) {
      vote_ptr vptr = std::make_shared<vote>(vote{.connection_id = connection_id, .msg = msg});
      boost::asio::post(thread_pool.get_executor(), [this, connection_id, msg] {
         std::unique_lock g(mtx);
         if (++num_messages[connection_id] > max_votes_per_connection) {
            // consider the connection invalid, remove all votes of connection
            // don't clear num_messages[connection_id] so we keep reporting max_exceeded until index is drained
            remove_connection(connection_id);
            g.unlock();

            elog("Exceeded max votes per connection for ${c}", ("c", connection_id));
            emit(connection_id, vote_status::max_exceeded, msg);
         } else if (block_header::num_from_id(msg.block_id) < lib.load(std::memory_order_relaxed)) {
            // ignore
         } else {
            index.insert(std::make_shared<vote>(vote{.connection_id = connection_id, .msg = msg}));
            cv.notify_one();
         }
      });
   }

};

} } //eosio::chain
