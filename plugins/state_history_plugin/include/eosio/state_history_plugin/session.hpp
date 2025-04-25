#pragma once
#include <eosio/state_history/log.hpp>
#include <eosio/state_history/serialization.hpp>
#include <eosio/state_history/types.hpp>

#include <eosio/chain/types.hpp>
#include <eosio/chain/controller.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/asio/error.hpp>
#include <boost/beast/websocket.hpp>
#include <memory>

extern const char* const state_history_plugin_abi;

namespace eosio::state_history {

class session_base {
public:
   session_base() = default;
   session_base(const session_base&) = delete;
   session_base& operator=(const session_base&) = delete;

   virtual void block_applied(const chain::block_num_type applied_block_num) = 0;

   virtual void drain_strand() = 0;

   virtual ~session_base() = default;
};

template<typename SocketType, typename GetBlockID, typename GetBlock, typename OnDone>
requires std::is_same_v<SocketType, boost::asio::ip::tcp::socket> || std::is_same_v<SocketType, boost::asio::local::stream_protocol::socket>
class session final : public session_base {
   using coro_throwing_stream = boost::asio::use_awaitable_t<>::as_default_on_t<boost::beast::websocket::stream<SocketType>>;
   using coro_nonthrowing_steadytimer = boost::asio::as_tuple_t<boost::asio::use_awaitable_t<>>::as_default_on_t<boost::asio::steady_timer>;

public:
   session(SocketType&& s, chain::controller& controller,
           std::optional<log_catalog>& trace_log, std::optional<log_catalog>& chain_state_log, std::optional<log_catalog>& finality_data_log,
           GetBlockID&& get_block_id, GetBlock&& get_block, OnDone&& on_done, fc::logger& logger) :
    strand(s.get_executor()), stream(std::move(s)), wake_timer(strand), controller(controller),
    trace_log(trace_log), chain_state_log(chain_state_log), finality_data_log(finality_data_log),
    get_block_id(get_block_id), get_block(get_block), on_done(on_done), logger(logger), remote_endpoint_string(get_remote_endpoint_string()) {
      fc_ilog(logger, "incoming state history connection from ${a}", ("a", remote_endpoint_string));

      boost::asio::co_spawn(strand, read_loop(), [&](std::exception_ptr e) {check_coros_done(e);});
   }

   void block_applied(const chain::block_num_type applied_block_num) {
      //indicates a fork being applied for already-sent blocks; rewind the cursor
      if(applied_block_num < next_block_cursor)
         next_block_cursor = applied_block_num;
      awake_if_idle();
   }

   // allow main thread to drain the strand before destruction -- some awake_if_idle() post()s may be inflight
   void drain_strand() {
      chain::post_async_task(strand, [](){}).get();
   }

private:
   std::string get_remote_endpoint_string() const {
      try {
         if constexpr(std::is_same_v<SocketType, boost::asio::ip::tcp::socket>)
            return boost::lexical_cast<std::string>(stream.next_layer().remote_endpoint());
         return "UNIX socket";
      } catch (...) {
         return "(unknown)";
      }
   }

   void awake_if_idle() {
      boost::asio::dispatch(strand, [this]() {
         wake_timer.cancel_one();
      });
   }

   void check_coros_done(std::exception_ptr e) {
      //the only exception that should have bubbled out of the coros is a bad_alloc, bubble it up further. No need to bother
      //  with the rest of the cleanup: we'll be shutting down soon anyway due to bad_alloc
      if(e)
         std::rethrow_exception(e);
      //coros always return on the session's strand
      if(--coros_running == 0)
         on_done(this);
   }

   template<typename F>
   void drop_exceptions(F&& f) {
      try{ f(); } catch(...) {}
   }

   template<typename F>
   boost::asio::awaitable<void> readwrite_coro_exception_wrapper(F&& f) {
      coros_running++;

      try {
         co_await f();
      }
      catch(std::bad_alloc&) {
         throw;
      }
      catch(fc::exception& e) {
         if(has_logged_exception.test_and_set() == false)
            fc_ilog(logger, "state history connection from ${a} failed: ${w}", ("a", remote_endpoint_string)("w", e.top_message()));
      }
      catch(boost::system::system_error& e) {
         if(has_logged_exception.test_and_set() == false)
            fc_ilog(logger, "state history connection from ${a} failed: ${w}", ("a", remote_endpoint_string)("w", e.code().message()));
      }
      catch(std::exception& e) {
         if(has_logged_exception.test_and_set() == false)
            fc_ilog(logger, "state history connection from ${a} failed: ${w}", ("a", remote_endpoint_string)("w", e.what()));
      }
      catch(...) {
         if(has_logged_exception.test_and_set() == false)
            fc_ilog(logger, "state history connection from ${a} failed", ("a", remote_endpoint_string));
      }

      drop_exceptions([this](){ stream.next_layer().close(); });
      drop_exceptions([this](){ awake_if_idle();             });
   }

   boost::asio::awaitable<void> read_loop() {
      co_await readwrite_coro_exception_wrapper([this]() -> boost::asio::awaitable<void> {
         wake_timer.expires_at(std::chrono::steady_clock::time_point::max());

         if constexpr(std::is_same_v<SocketType, boost::asio::ip::tcp::socket>)
            stream.next_layer().set_option(boost::asio::ip::tcp::no_delay(true));
         stream.next_layer().set_option(boost::asio::socket_base::send_buffer_size(1024*1024));
         stream.write_buffer_bytes(512*1024);
         stream.set_option(boost::beast::websocket::stream_base::decorator([](boost::beast::websocket::response_type& res) {
            res.set(boost::beast::http::field::server, "state_history/" + app().version_string());
         }));

         co_await stream.async_accept();
         co_await stream.async_write(boost::asio::const_buffer(state_history_plugin_abi, strlen(state_history_plugin_abi)));
         stream.binary(true);
         boost::asio::co_spawn(strand, write_loop(), [&](std::exception_ptr e) {check_coros_done(e);});

         while(true) {
            boost::beast::flat_buffer b;
            co_await stream.async_read(b);
            const state_request req = fc::raw::unpack<std::remove_const_t<decltype(req)>>(static_cast<const char*>(b.cdata().data()), b.size());

            auto& self = *this; //gcc10 ICE workaround wrt capturing 'this' in a coro
            co_await boost::asio::co_spawn(app().get_io_context(), [&]() -> boost::asio::awaitable<void> {
               /**
                * This lambda executes on the main thread; upon returning, the enclosing coroutine continues execution on the connection's strand
                */
               std::visit(chain::overloaded {
                  [&self]<typename GetStatusRequestV0orV1, typename = std::enable_if_t<std::is_base_of_v<get_status_request_v0, GetStatusRequestV0orV1>>>(const GetStatusRequestV0orV1&) {
                     self.queued_status_requests.emplace_back(std::is_same_v<GetStatusRequestV0orV1, get_status_request_v1>);
                  },
                  [&self]<typename GetBlocksRequestV0orV1, typename = std::enable_if_t<std::is_base_of_v<get_blocks_request_v0, GetBlocksRequestV0orV1>>>(const GetBlocksRequestV0orV1& gbr) {
                     self.current_blocks_request_v1_finality.reset();
                     self.current_blocks_request = gbr;
                     if constexpr(std::is_same_v<GetBlocksRequestV0orV1, get_blocks_request_v1>)
                        self.current_blocks_request_v1_finality = gbr.fetch_finality_data;

                     for(const block_position& haveit : self.current_blocks_request.have_positions) {
                        if(self.current_blocks_request.start_block_num <= haveit.block_num)
                           continue;
                        if(const std::optional<chain::block_id_type> id = self.get_block_id(haveit.block_num); !id || *id != haveit.block_id)
                           self.current_blocks_request.start_block_num = std::min(self.current_blocks_request.start_block_num, haveit.block_num);
                     }
                     self.current_blocks_request.have_positions.clear();
                  },
                  [&self](const get_blocks_ack_request_v0& gbar0) {
                     self.send_credits += gbar0.num_messages;
                  }
               }, req);
               co_return;
            }, boost::asio::use_awaitable);

            awake_if_idle();
         }
      });
   }

   get_status_result_v1 fill_current_status_result() {
      get_status_result_v1 ret;

      ret.head              = {controller.head().block_num(), controller.head().id()};
      ret.last_irreversible = {controller.fork_db_root().block_num(), controller.fork_db_root().id()};
      ret.chain_id          = controller.get_chain_id();
      if(trace_log)
         std::tie(ret.trace_begin_block, ret.trace_end_block) = trace_log->block_range();
      if(chain_state_log)
         std::tie(ret.chain_state_begin_block, ret.chain_state_end_block) = chain_state_log->block_range();
      if(finality_data_log)
         std::tie(ret.finality_data_begin_block, ret.finality_data_end_block) = finality_data_log->block_range();

      return ret;
   }

   boost::asio::awaitable<void> write_log_entry(std::optional<ship_log_entry>& log_stream) {
      if(!log_stream) { //will be unset if either request did not ask for this log entry, or the log isn't enabled
         co_await stream.async_write_some(false, boost::asio::buffer(fc::raw::pack(false)));
         co_return;
      }

      char buff[1024*1024];
      fc::datastream<char*> ds(buff, sizeof(buff));
      fc::raw::pack(ds, true);
      history_pack_varuint64(ds, log_stream->get_uncompressed_size());
      co_await stream.async_write_some(false, boost::asio::buffer(buff, ds.tellp()));

      bio::filtering_istreambuf decompression_stream = log_stream->get_stream();
      std::streamsize red = 0;
      while((red = bio::read(decompression_stream, buff, sizeof(buff))) != -1) {
         if(red == 0)
            continue;
         co_await stream.async_write_some(false, boost::asio::buffer(buff, red));
      }
   }

   boost::asio::awaitable<void> write_loop() {
      co_await readwrite_coro_exception_wrapper([this]() -> boost::asio::awaitable<void> {
         get_status_result_v1 current_status_result;
         struct block_package {
            get_blocks_result_base blocks_result_base;
            bool is_v1_request = false;
            std::optional<ship_log_entry> trace_entry;
            std::optional<ship_log_entry> state_entry;
            std::optional<ship_log_entry> finality_entry;
         };

         while(true) {
            if(!stream.is_open())
               break;

            std::deque<bool>             status_requests;
            std::optional<block_package> block_to_send;

            auto& self = *this; //gcc10 ICE workaround wrt capturing 'this' in a coro
            co_await boost::asio::co_spawn(app().get_io_context(), [&]() -> boost::asio::awaitable<void> {
               /**
                * This lambda executes on the main thread; upon returning, the enclosing coroutine continues execution on the connection's strand
                */
               status_requests = std::move(self.queued_status_requests);

               //decide what block -- if any -- to send out
               const chain::block_num_type latest_to_consider = self.current_blocks_request.irreversible_only ?
                                                                self.controller.fork_db_root().block_num() : self.controller.head().block_num();
               if(self.send_credits && self.next_block_cursor <= latest_to_consider && self.next_block_cursor < self.current_blocks_request.end_block_num) {
                  block_to_send.emplace( block_package{
                     .blocks_result_base = {
                        .head = {self.controller.head().block_num(), self.controller.head().id()},
                        .last_irreversible = {self.controller.fork_db_root().block_num(), self.controller.fork_db_root().id()}
                     },
                     .is_v1_request = self.current_blocks_request_v1_finality.has_value()
                  });
                  if(const std::optional<chain::block_id_type> this_block_id = self.get_block_id(self.next_block_cursor)) {
                     block_to_send->blocks_result_base.this_block  = {self.current_blocks_request.start_block_num, *this_block_id};
                     if(const std::optional<chain::block_id_type> last_block_id = self.get_block_id(self.next_block_cursor - 1))
                        block_to_send->blocks_result_base.prev_block = {self.next_block_cursor - 1, *last_block_id};
                     if (self.current_blocks_request.fetch_block) {
                        if (chain::signed_block_ptr sbp = get_block(*this_block_id)) {
                           block_to_send->blocks_result_base.block = fc::raw::pack(*sbp);
                        }
                     }
                     if(self.current_blocks_request.fetch_traces && self.trace_log)
                        block_to_send->trace_entry = self.trace_log->get_entry(self.next_block_cursor);
                     if(self.current_blocks_request.fetch_deltas && self.chain_state_log)
                        block_to_send->state_entry = self.chain_state_log->get_entry(self.next_block_cursor);
                     if(block_to_send->is_v1_request && *self.current_blocks_request_v1_finality && self.finality_data_log)
                        block_to_send->finality_entry = self.finality_data_log->get_entry(self.next_block_cursor);
                  }
                  // increment next_block_cursor even if unable to retrieve block to avoid tight busy loop
                  ++self.next_block_cursor;
                  --self.send_credits;
               }

               if(status_requests.size())
                  current_status_result = fill_current_status_result();
               co_return;
            }, boost::asio::use_awaitable);

            //if there is nothing to send, go to sleep
            if(status_requests.empty() && !block_to_send) {
               co_await wake_timer.async_wait();
               continue;
            }

            //send replies to all send status requests first
            for(const bool status_request_is_v1 : status_requests) {
               if(status_request_is_v1 == false) //v0 status request, gets a v0 status result
                  co_await stream.async_write(boost::asio::buffer(fc::raw::pack(state_result((get_status_result_v0)current_status_result))));
               else
                  co_await stream.async_write(boost::asio::buffer(fc::raw::pack(state_result(current_status_result))));
            }

            //and then send the block
            if(block_to_send) {
               const fc::unsigned_int get_blocks_result_variant_index = block_to_send->is_v1_request ?
                                                                        state_result(get_blocks_result_v1()).index() :
                                                                        state_result(get_blocks_result_v0()).index();
               co_await stream.async_write_some(false, boost::asio::buffer(fc::raw::pack(get_blocks_result_variant_index)));
               co_await stream.async_write_some(false, boost::asio::buffer(fc::raw::pack(block_to_send->blocks_result_base)));

               co_await write_log_entry(block_to_send->trace_entry);
               co_await write_log_entry(block_to_send->state_entry);
               if(block_to_send->is_v1_request)
                  co_await write_log_entry(block_to_send->finality_entry);

               co_await stream.async_write_some(true, boost::asio::const_buffer());
            }
         }
      });
   }

private:
   ///these items must only ever be touched by the session's strand
   SocketType::executor_type         strand;
   coro_throwing_stream              stream;
   coro_nonthrowing_steadytimer      wake_timer;
   unsigned                          coros_running = 0;
   std::atomic_flag                  has_logged_exception;  //left as atomic_flag for useful test_and_set() interface

   ///these items must only ever be touched on the main thread
   std::deque<bool>                  queued_status_requests;  //false for v0, true for v1

   get_blocks_request_v0             current_blocks_request;
   std::optional<bool>               current_blocks_request_v1_finality; //unset: current request is v0; set means v1; true/false is if finality requested
   //current_blocks_request is modified with the current state; bind some more descriptive names to items frequently used
   uint32_t&                         send_credits = current_blocks_request.max_messages_in_flight;
   chain::block_num_type&            next_block_cursor = current_blocks_request.start_block_num;

   chain::controller&                controller;
   std::optional<log_catalog>&       trace_log;
   std::optional<log_catalog>&       chain_state_log;
   std::optional<log_catalog>&       finality_data_log;

   GetBlockID                        get_block_id; // call from main app thread
   GetBlock                          get_block;

   ///these items might be used on either the strand or main thread
   OnDone                            on_done;
   fc::logger&                       logger;
   const std::string                 remote_endpoint_string;
};

} // namespace eosio
