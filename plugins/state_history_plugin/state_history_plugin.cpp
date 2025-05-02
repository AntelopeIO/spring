#include "eosio/chain/block_header.hpp"
#include <eosio/chain/config.hpp>
#include <eosio/chain/thread_utils.hpp>
#include <eosio/resource_monitor_plugin/resource_monitor_plugin.hpp>
#include <eosio/state_history/create_deltas.hpp>
#include <eosio/state_history/log_config.hpp>
#include <eosio/state_history/log_catalog.hpp>
#include <eosio/state_history/serialization.hpp>
#include <eosio/state_history/trace_converter.hpp>
#include <eosio/state_history_plugin/session.hpp>
#include <eosio/state_history_plugin/state_history_plugin.hpp>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/ip/host_name.hpp>

#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>

#include <boost/signals2/connection.hpp>
#include <mutex>

#include <fc/network/listener.hpp>

namespace eosio {
using namespace chain;
using namespace state_history;
using boost::signals2::scoped_connection;
namespace bio = boost::iostreams;

static auto _state_history_plugin = application::register_plugin<state_history_plugin>();

const std::string logger_name("state_history");
fc::logger        _log;

template <typename F>
auto catch_and_log(F f) {
   try {
      return f();
   } catch(const fc::exception& e) {
      fc_elog(_log, "${e}", ("e", e.to_detail_string()));
   } catch(const std::exception& e) {
      fc_elog(_log, "${e}", ("e", e.what()));
   } catch(...) {
      fc_elog(_log, "unknown exception");
   }
}

struct state_history_plugin_impl {
private:
   chain_plugin*                    chain_plug = nullptr;
   std::optional<log_catalog>       trace_log;
   std::optional<log_catalog>       chain_state_log;
   std::optional<log_catalog>       finality_data_log;
   uint32_t                         first_available_block = 0;
   bool                             trace_debug_mode = false;
   std::optional<scoped_connection> applied_transaction_connection;
   std::optional<scoped_connection> block_start_connection;
   std::optional<scoped_connection> accepted_block_connection;
   string                           endpoint_address;
   string                           unix_path;
   state_history::trace_converter   trace_converter;

   named_thread_pool<struct ship>   thread_pool;

   struct connection_map_key_less {
      using is_transparent = void;
      template<typename L, typename R> bool operator()(const L& lhs, const R& rhs) const {
         return std::to_address(lhs) < std::to_address(rhs);
      }
   };
   //connections must only be touched by the main thread because on_accepted_block() will iterate over it
   std::set<std::unique_ptr<session_base>, connection_map_key_less> connections; //gcc 11+ required for unordered_set

public:
   void plugin_initialize(const variables_map& options);
   void plugin_startup();
   void plugin_shutdown();

   std::optional<chain::block_id_type> get_block_id(block_num_type block_num) {
      if(trace_log) {
         if(std::optional<block_id_type> id = trace_log->get_block_id(block_num))
            return id;
      }
      if(chain_state_log) {
         if(std::optional<block_id_type> id = chain_state_log->get_block_id(block_num))
            return id;
      }
      if(finality_data_log) {
         if(std::optional<block_id_type> id = finality_data_log->get_block_id(block_num))
            return id;
      }
      try {
         // not thread safe, only call from main application thread
         return chain_plug->chain().chain_block_id_for_num(block_num);
      } catch(...) {
      }
      return {};
   }

   template <typename Protocol>
   void create_listener(const std::string& address) {
      const boost::posix_time::milliseconds accept_timeout(200);
      // run listener on ship thread so that thread_pool.stop() will shutdown the listener since this captures `this`
      fc::create_listener<Protocol>(thread_pool.get_executor(), _log, accept_timeout, address, "",
         [this](const auto&) { return boost::asio::make_strand(thread_pool.get_executor()); },
         [this](Protocol::socket&& socket) {
            // connections set must only be modified by the main thread
            app().executor().post(priority::high, exec_queue::read_write, [this, socket{std::move(socket)}]() mutable {
               catch_and_log([this, &socket]() {
                  connections.emplace(new session(std::move(socket), chain_plug->chain(),
                                                  trace_log, chain_state_log, finality_data_log,
                                                  [this](const chain::block_num_type block_num) {
                                                     return get_block_id(block_num);
                                                  },
                                                  [this](const chain::block_id_type& block_id) {
                                                     return chain_plug->chain().fetch_block_by_id(block_id);
                                                  },
                                                  [this](session_base* conn) {
                                                     app().executor().post(priority::high, exec_queue::read_write, [conn, this]() {
                                                        //Main thread may have post()s inflight to session strand (via block_applied() -> awake_if_idle()) that
                                                        // could execute during destruction. Drain any possible post() before destruction. This is in main
                                                        // thread now so guaranteed no new block_applied() will be called during these lines below, and the
                                                        // session has already indicated it is "done" so it will not be running any operations of its own
                                                        // on the strand
                                                        conn->drain_strand();
                                                        connections.erase(connections.find(conn));
                                                     });
                                                  }, _log));
               });
            });
         });
   }

   void listen(){
      try {
         if(!endpoint_address.empty())
            create_listener<boost::asio::ip::tcp>(endpoint_address);
         if(!unix_path.empty())
            create_listener<boost::asio::local::stream_protocol>(unix_path);
      } catch(std::exception&) {
         FC_THROW_EXCEPTION(plugin_exception, "unable to open listen socket");
      }
   }

   void on_applied_transaction(const transaction_trace_ptr& p, const packed_transaction_ptr& t) {
      if(trace_log)
         trace_converter.add_transaction(p, t);
   }

   void on_accepted_block(const signed_block_ptr& block, const block_id_type& id) {
      try {
         store_traces(block, id);
         store_chain_state(id, block->previous, block->block_num());
         store_finality_data(id, block->previous);
      } catch(const fc::exception& e) {
         fc_elog(_log, "fc::exception: ${details}", ("details", e.to_detail_string()));
         // Both app().quit() and exception throwing are required. Without app().quit(),
         // the exception would be caught and drop before reaching main(). The exception is
         // to ensure the block won't be committed.
         appbase::app().quit();
         EOS_THROW(
             chain::state_history_write_exception, // controller_emit_signal_exception, so it flow through emit()
             "State history encountered an Error which it cannot recover from.  Please resolve the error and relaunch "
             "the process");
      }

      for(const std::unique_ptr<session_base>& c : connections)
         c->block_applied(block->block_num());
   }

   void on_block_start(uint32_t block_num) {
      clear_caches();
   }

   void clear_caches() {
      trace_converter.cached_traces.clear();
      trace_converter.onblock_trace.reset();
   }

   void store_traces(const signed_block_ptr& block, const block_id_type& id) {
      if(!trace_log)
         return;

      trace_log->pack_and_write_entry(id, block->previous, [this, &block](bio::filtering_ostreambuf& buf) {
         trace_converter.pack(buf, trace_debug_mode, block);
      });
   }

   void store_chain_state(const block_id_type& id, const block_id_type& previous_id, uint32_t block_num) {
      if(!chain_state_log)
         return;
      bool fresh = chain_state_log->empty();
      if(fresh)
         fc_ilog(_log, "Placing initial state in block ${n}", ("n", block_num));

      chain_state_log->pack_and_write_entry(id, previous_id, [this, fresh](bio::filtering_ostreambuf& buf) {
         pack_deltas(buf, chain_plug->chain().db(), fresh);
      });
   } // store_chain_state

   void store_finality_data(const block_id_type& id, const block_id_type& previous_id) {
      if(!finality_data_log)
         return;

      std::optional<finality_data_t> finality_data = chain_plug->chain().head_finality_data();
      if(!finality_data.has_value()) {
         finality_data_log->clear();
         return;
      }

      finality_data_log->pack_and_write_entry(id, previous_id, [finality_data](bio::filtering_ostreambuf& buf) {
         fc::datastream<boost::iostreams::filtering_ostreambuf&> ds{buf};
         fc::raw::pack(ds, *finality_data);
      });
   }
}; // state_history_plugin_impl

state_history_plugin::state_history_plugin()
    : my(new state_history_plugin_impl()) {}

state_history_plugin::~state_history_plugin() = default;

void state_history_plugin::set_program_options(options_description& cli, options_description& cfg) {
   auto options = cfg.add_options();
   options("state-history-dir", bpo::value<std::filesystem::path>()->default_value("state-history"),
           "the location of the state-history directory (absolute path or relative to application data dir)");
   options("state-history-retained-dir", bpo::value<std::filesystem::path>(),
           "the location of the state history retained directory (absolute path or relative to state-history dir).");
   options("state-history-archive-dir", bpo::value<std::filesystem::path>(),
           "the location of the state history archive directory (absolute path or relative to state-history dir).\n"
           "If the value is empty string, blocks files beyond the retained limit will be deleted.\n"
           "All files in the archive directory are completely under user's control, i.e. they won't be accessed by nodeos anymore.");
   options("state-history-stride", bpo::value<uint32_t>(),
         "split the state history log files when the block number is the multiple of the stride\n"
         "When the stride is reached, the current history log and index will be renamed '*-history-<start num>-<end num>.log/index'\n"
         "and a new current history log and index will be created with the most recent blocks. All files following\n"
         "this format will be used to construct an extended history log.");
   options("max-retained-history-files", bpo::value<uint32_t>(),
          "the maximum number of history file groups to retain so that the blocks in those files can be queried.\n"
          "When the number is reached, the oldest history file would be moved to archive dir or deleted if the archive dir is empty.\n"
          "The retained history log files should not be manipulated by users." );
   cli.add_options()("delete-state-history", bpo::bool_switch()->default_value(false), "clear state history files");
   options("trace-history", bpo::bool_switch()->default_value(false), "enable trace history");
   options("chain-state-history", bpo::bool_switch()->default_value(false), "enable chain state history");
   options("finality-data-history", bpo::bool_switch()->default_value(false), "enable finality data history");
   options("state-history-endpoint", bpo::value<string>()->default_value("127.0.0.1:8080"),
           "the endpoint upon which to listen for incoming connections. Caution: only expose this port to "
           "your internal network.");
   options("state-history-unix-socket-path", bpo::value<string>(),
           "the path (relative to data-dir) to create a unix socket upon which to listen for incoming connections.");
   options("trace-history-debug-mode", bpo::bool_switch()->default_value(false), "enable debug mode for trace history");
   options("state-history-log-retain-blocks", bpo::value<uint32_t>(), "if set, periodically prune the state history files to store only configured number of most recent blocks");
}

void state_history_plugin_impl::plugin_initialize(const variables_map& options) {
   try {
      chain_plug = app().find_plugin<chain_plugin>();
      EOS_ASSERT(chain_plug, chain::missing_chain_plugin_exception, "");
      auto& chain = chain_plug->chain();

      if(!options.at("disable-replay-opts").as<bool>() && options.at("chain-state-history").as<bool>()) {
         ilog("Setting disable-replay-opts=true required by state_history_plugin chain-state-history=true option");
         chain.set_disable_replay_opts(true);
      }

      applied_transaction_connection.emplace(chain.applied_transaction().connect(
          [&](std::tuple<const transaction_trace_ptr&, const packed_transaction_ptr&> t) {
             on_applied_transaction(std::get<0>(t), std::get<1>(t));
          }));
      accepted_block_connection.emplace(
          chain.accepted_block().connect([&](const block_signal_params& t) {
             const auto& [ block, id ] = t;
             on_accepted_block(block, id);
          }));
      block_start_connection.emplace(
          chain.block_start().connect([&](uint32_t block_num) { on_block_start(block_num); }));

      auto                    dir_option = options.at("state-history-dir").as<std::filesystem::path>();
      std::filesystem::path state_history_dir;
      if(dir_option.is_relative())
         state_history_dir = app().data_dir() / dir_option;
      else
         state_history_dir = dir_option;
      if(auto resmon_plugin = app().find_plugin<resource_monitor_plugin>())
         resmon_plugin->monitor_directory(state_history_dir);

      endpoint_address = options.at("state-history-endpoint").as<string>();

      if(options.count("state-history-unix-socket-path")) {
         std::filesystem::path sock_path = options.at("state-history-unix-socket-path").as<string>();
         if(sock_path.is_relative())
            sock_path = app().data_dir() / sock_path;
         unix_path = sock_path.generic_string();
      }

      if(options.at("delete-state-history").as<bool>()) {
         fc_ilog(_log, "Deleting state history");
         std::filesystem::remove_all(state_history_dir);
      }
      std::filesystem::create_directories(state_history_dir);

      if(options.at("trace-history-debug-mode").as<bool>()) {
         trace_debug_mode = true;
      }

      bool has_state_history_partition_options =
          options.count("state-history-retained-dir") || options.count("state-history-archive-dir") ||
          options.count("state-history-stride") || options.count("max-retained-history-files");

      state_history_log_config ship_log_conf;
      if(options.count("state-history-log-retain-blocks")) {
         state_history::prune_config& ship_log_prune_conf = ship_log_conf.emplace<state_history::prune_config>();
         ship_log_prune_conf.prune_blocks = options.at("state-history-log-retain-blocks").as<uint32_t>();
         //the arbitrary limit of 1000 here is mainly so that there is enough buffer for newly applied forks to be delivered to clients
         // before getting pruned out. ideally pruning would have been smart enough to know not to prune reversible blocks
         EOS_ASSERT(ship_log_prune_conf.prune_blocks >= 1000, plugin_exception, "state-history-log-retain-blocks must be 1000 blocks or greater");
         EOS_ASSERT(!has_state_history_partition_options, plugin_exception, "state-history-log-retain-blocks cannot be used together with state-history-retained-dir,"
                  " state-history-archive-dir, state-history-stride or max-retained-history-files");
      } else if(has_state_history_partition_options){
         state_history::partition_config& config = ship_log_conf.emplace<state_history::partition_config>();
         if(options.count("state-history-retained-dir"))
            config.retained_dir       = options.at("state-history-retained-dir").as<std::filesystem::path>();
         if(options.count("state-history-archive-dir"))
            config.archive_dir        = options.at("state-history-archive-dir").as<std::filesystem::path>();
         if(options.count("state-history-stride"))
            config.stride             = options.at("state-history-stride").as<uint32_t>();
         if(options.count("max-retained-history-files"))
            config.max_retained_files = options.at("max-retained-history-files").as<uint32_t>();
      }

      if(options.at("trace-history").as<bool>())
         trace_log.emplace(state_history_dir, ship_log_conf, "trace_history", [this](chain::block_num_type bn) {return get_block_id(bn);});
      if(options.at("chain-state-history").as<bool>())
         chain_state_log.emplace(state_history_dir, ship_log_conf, "chain_state_history", [this](chain::block_num_type bn) {return get_block_id(bn);});
      if(options.at("finality-data-history").as<bool>())
         finality_data_log.emplace(state_history_dir, ship_log_conf, "finality_data_history", [this](chain::block_num_type bn) {return get_block_id(bn);});
   }
   FC_LOG_AND_RETHROW()
} // state_history_plugin::plugin_initialize

void state_history_plugin::plugin_initialize(const variables_map& options) {
   handle_sighup(); // setup logging
   my->plugin_initialize(options);
}
   
void state_history_plugin_impl::plugin_startup() {
   const auto& chain = chain_plug->chain();

   uint32_t block_num = chain.head().block_num();
   if( block_num > 0 && chain_state_log && chain_state_log->empty() ) {
      fc_ilog( _log, "Storing initial state on startup, this can take a considerable amount of time" );
      store_chain_state( chain.head().id(), chain.head().header().previous, block_num );
      fc_ilog( _log, "Done storing initial state on startup" );
   }
   first_available_block = chain.earliest_available_block_num();
   if(trace_log) {
      auto first_trace_block = trace_log->block_range().first;
      if( first_trace_block > 0 )
         first_available_block = std::min( first_available_block, first_trace_block );
   }
   if(chain_state_log) {
      auto first_state_block = chain_state_log->block_range().first;
      if( first_state_block > 0 )
         first_available_block = std::min( first_available_block, first_state_block );
   }
   if(finality_data_log) {
      auto first_state_block = finality_data_log->block_range().first;
      if( first_state_block > 0 )
         first_available_block = std::min( first_available_block, first_state_block );
   }
   fc_ilog(_log, "First available block for SHiP ${b}", ("b", first_available_block));
   listen();
   thread_pool.start(1, [](const fc::exception& e) {
      fc_elog( _log, "Exception in SHiP thread pool, exiting: ${e}", ("e", e.to_detail_string()) );
      app().quit();
   });
}

void state_history_plugin::plugin_startup() {
   my->plugin_startup();
}

void state_history_plugin_impl::plugin_shutdown() {
   fc_dlog(_log, "stopping");
   thread_pool.stop();
   fc_dlog(_log, "exit shutdown");
}

void state_history_plugin::plugin_shutdown() {
   my->plugin_shutdown();
}

void state_history_plugin::handle_sighup() {
   fc::logger::update(logger_name, _log);
}

} // namespace eosio
