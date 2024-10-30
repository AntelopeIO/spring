#include <eosio/test_control_plugin/test_control_plugin.hpp>

namespace eosio {

   static auto _test_control_plugin = application::register_plugin<test_control_plugin>();

class test_control_plugin_impl {
public:
   explicit test_control_plugin_impl(chain::controller& c) : _chain(c) {}
   void connect();
   void kill_on_lib(account_name prod, uint32_t where_in_seq);
   void kill_on_head(account_name prod, uint32_t where_in_seq);

   void set_throw_on_options(const test_control_apis::read_write::throw_on_params& throw_options);
private:
   void block_start(chain::block_num_type block_num);
   void accepted_block_header(const chain::block_id_type& id);
   void accepted_block(const chain::block_id_type& id);
   void irreversible_block(const chain::block_id_type& id);
   void applied_transaction();
   void voted_block();
   void aggregated_vote();

   void throw_exception();
   void reset_throw();
   void process_next_block_state(const chain::block_id_type& id);

   chain::controller&  _chain;
   struct kill_options {
      account_name        _producer;
      uint32_t            _where_in_sequence{};
      bool                _clean_producer_sequence{false};
      bool                _started_production_round{false};
      bool                _track_lib{false};
      bool                _track_head{false};
   } _kill_options;

   test_control_apis::read_write::throw_on_params _throw_options;

   std::optional<boost::signals2::scoped_connection> _block_start_connection;
   std::optional<boost::signals2::scoped_connection> _accepted_block_header_connection;
   std::optional<boost::signals2::scoped_connection> _accepted_block_connection;
   std::optional<boost::signals2::scoped_connection> _irreversible_block_connection;
   std::optional<boost::signals2::scoped_connection> _applied_transaction_connection;
   std::optional<boost::signals2::scoped_connection> _voted_block_connection;
   std::optional<boost::signals2::scoped_connection> _aggregated_vote_connection;
};

void test_control_plugin_impl::connect() {
   _block_start_connection.emplace(
         _chain.block_start().connect( [&]( chain::block_num_type block_num ) {
            block_start( block_num );
         } ));
   _accepted_block_header_connection =
         _chain.accepted_block_header().connect( [&]( const chain::block_signal_params& t ) {
            const auto& [ block, id ] = t;
            accepted_block_header( id );
         } );
   _accepted_block_connection =
         _chain.accepted_block().connect( [&]( const chain::block_signal_params& t ) {
            const auto& [ block, id ] = t;
            accepted_block( id );
         } );
   _irreversible_block_connection.emplace(
         _chain.irreversible_block().connect( [&]( const chain::block_signal_params& t ) {
            const auto& [ block, id ] = t;
            irreversible_block( id );
         } ));
   _applied_transaction_connection.emplace(
         _chain.applied_transaction().connect( [&]( std::tuple<const chain::transaction_trace_ptr&, const chain::packed_transaction_ptr&> t) {
            applied_transaction();
         } ));
   _voted_block_connection.emplace(
         _chain.voted_block().connect( [&]( const chain::vote_signal_params& p) {
            voted_block();
         } ));
   _aggregated_vote_connection.emplace(
         _chain.aggregated_vote().connect( [&]( const chain::vote_signal_params& p) {
            aggregated_vote();
         } ));
}

void test_control_plugin_impl::throw_exception() {
   if (_throw_options.exception == "controller_emit_signal_exception") {
      ilog("throwing controller_emit_signal_exception for signal ${s}", ("s", _throw_options.signal));
      reset_throw(); // throw only once
      EOS_ASSERT(false, chain::controller_emit_signal_exception, "");
   }

   ilog("throwing misc_exception for signal ${s}", ("s", _throw_options.signal));
   reset_throw(); // throw only once
   EOS_ASSERT(false, chain::misc_exception, "");
}

void test_control_plugin_impl::reset_throw() {
   _throw_options = test_control_apis::read_write::throw_on_params{};
}

void test_control_plugin_impl::block_start(chain::block_num_type block_num) {
   if (_throw_options.signal == "block_start")
      throw_exception();
}

void test_control_plugin_impl::accepted_block_header(const chain::block_id_type& id) {
   if (_throw_options.signal == "accepted_block_header")
      throw_exception();
}

void test_control_plugin_impl::accepted_block(const chain::block_id_type& id) {
   if (_kill_options._track_head)
      process_next_block_state(id);
   if (_throw_options.signal == "accepted_block")
      throw_exception();
}

void test_control_plugin_impl::irreversible_block(const chain::block_id_type& id) {
   if (_kill_options._track_lib)
      process_next_block_state(id);
   if (_throw_options.signal == "irreversible_block")
      throw_exception();
}

void test_control_plugin_impl::applied_transaction() {
   if (_throw_options.signal == "applied_transaction")
      throw_exception();
}

void test_control_plugin_impl::voted_block() {
   if (_throw_options.signal == "voted_block")
      throw_exception();
}

void test_control_plugin_impl::aggregated_vote() {
   if (_throw_options.signal == "aggregated_vote")
      throw_exception();
}

void test_control_plugin_impl::process_next_block_state(const chain::block_id_type& id) {
   // Tests expect the shutdown only after signaling a producer shutdown and seeing a full production cycle
   const auto block_time = _chain.head().block_time() + fc::microseconds(chain::config::block_interval_us);
   // have to fetch bsp due to get_scheduled_producer call

   const auto& producer_authority = _chain.active_producers().get_scheduled_producer(block_time);
   const auto producer_name = producer_authority.producer_name;
   const auto slot = _chain.head().timestamp().slot % chain::config::producer_repetitions;
   if (_kill_options._producer != account_name()) {
      if( _kill_options._producer != producer_name ) _kill_options._clean_producer_sequence = true;
      if( _kill_options._clean_producer_sequence ) {
         ilog( "producer ${cprod} slot ${pslot}, looking for ${lprod} slot ${slot}",
               ("cprod", producer_name)("pslot", slot)("lprod", _kill_options._producer)("slot", _kill_options._where_in_sequence) );
      } else {
         ilog( "producer ${cprod} slot ${pslot}, looking for start of ${lprod} production round",
               ("cprod", producer_name)("pslot", slot)("lprod", _kill_options._producer) );
      }
   }

   // check started_production_round in case where producer does not produce a full round, still want to shut down
   if( _kill_options._clean_producer_sequence && (producer_name == _kill_options._producer || _kill_options._started_production_round) ) {
      _kill_options._started_production_round = true;
      const auto current_slot = chain::block_timestamp_type( block_time ).slot % chain::config::producer_repetitions;
      ilog( "producer ${prod} slot: ${slot}", ("prod", producer_name)("slot", slot) );

      if( current_slot >= _kill_options._where_in_sequence || producer_name != _kill_options._producer ) {
         ilog("shutting down");
         app().quit();
      }
   }
}

void test_control_plugin_impl::kill_on_lib(account_name prod, uint32_t where_in_seq) {
   _kill_options._track_head = false;
   _kill_options._producer = prod;
   _kill_options._where_in_sequence = where_in_seq;
   _kill_options._clean_producer_sequence = false;
   _kill_options._started_production_round = false;
   _kill_options._track_lib = true;
}

void test_control_plugin_impl::kill_on_head(account_name prod, uint32_t where_in_seq) {
   _kill_options._track_lib = false;
   _kill_options._producer = prod;
   _kill_options._where_in_sequence = where_in_seq;
   _kill_options._clean_producer_sequence = false;
   _kill_options._started_production_round = false;
   _kill_options._track_head = true;
}

// ----------------- throw_on --------------------------------

void test_control_plugin_impl::set_throw_on_options(const test_control_apis::read_write::throw_on_params& throw_options) {
   _throw_options = throw_options;
}


test_control_plugin::test_control_plugin() = default;

void test_control_plugin::set_program_options(options_description& cli, options_description& cfg) {
}

void test_control_plugin::plugin_initialize(const variables_map& options) {
}

void test_control_plugin::plugin_startup() {
   dlog("test_control_plugin starting up");
   my.reset(new test_control_plugin_impl(app().get_plugin<chain_plugin>().chain()));
   my->connect();
}

void test_control_plugin::plugin_shutdown() {
   dlog("test_control_plugin shutting down");
}

namespace test_control_apis {

read_write::kill_node_on_producer_results read_write::kill_node_on_producer(const read_write::kill_node_on_producer_params& params) const {

   if (params.based_on_lib) {
      ilog("kill on lib for producer: ${p} at their ${s} slot in sequence", ("p", params.producer.to_string())("s", params.where_in_sequence));
      my->kill_on_lib(params.producer, params.where_in_sequence);
   } else {
      ilog("kill on head for producer: ${p} at their ${s} slot in sequence", ("p", params.producer.to_string())("s", params.where_in_sequence));
      my->kill_on_head(params.producer, params.where_in_sequence);
   }
   return read_write::kill_node_on_producer_results{};
}

empty read_write::throw_on(const read_write::throw_on_params& params) const {
   ilog("received throw on: ${p}", ("p", params));
   my->set_throw_on_options(params);
   return {};
}

} // namespace test_control_apis
} // namespace eosio
