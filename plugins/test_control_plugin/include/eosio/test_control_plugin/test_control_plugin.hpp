#pragma once
#include <eosio/chain/application.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <eosio/net_plugin/net_plugin.hpp>
#include <fc/variant.hpp>
#include <memory>

namespace fc { class variant; }

namespace eosio {
   using namespace appbase;
   typedef std::shared_ptr<class test_control_plugin_impl> test_control_ptr;

namespace test_control_apis {
struct empty{};

class read_write {

   public:
      read_write(const test_control_ptr& test_control)
         : my(test_control) {}

      struct kill_node_on_producer_params {
         name      producer;
         uint32_t  where_in_sequence;
         bool      based_on_lib;
      };
      using kill_node_on_producer_results = empty;
      kill_node_on_producer_results kill_node_on_producer(const kill_node_on_producer_params& params) const;

      struct throw_on_params {
         std::string  signal;    // which signal handler to throw exception from
         std::string  exception; // exception to throw
      };
      empty throw_on(const throw_on_params& params) const;

      // produce a next block with `from` action replaced with `to` action
      // requires Savanna to be active, this assumes blocks are is_proper_svnn_block
      struct swap_action_params {
         chain::name from;  // replace from action in block to `to` action
         chain::name to;
         fc::crypto::private_key trx_priv_key;
         fc::crypto::private_key blk_priv_key;
         bool shutdown = false; // shutdown node before next block
      };
      empty swap_action(const swap_action_params& params) const;

private:
      test_control_ptr my;
};


} // namespace test_control_apis


class test_control_plugin : public plugin<test_control_plugin> {
public:
   APPBASE_PLUGIN_REQUIRES((chain_plugin)(net_plugin))

   test_control_plugin();
   test_control_plugin(const test_control_plugin&) = delete;
   test_control_plugin(test_control_plugin&&) = delete;
   test_control_plugin& operator=(const test_control_plugin&) = delete;
   test_control_plugin& operator=(test_control_plugin&&) = delete;
   virtual ~test_control_plugin() override = default;

   virtual void set_program_options(options_description& cli, options_description& cfg) override;
   void plugin_initialize(const variables_map& options);
   void plugin_startup();
   void plugin_shutdown();

   test_control_apis::read_write get_read_write_api() const { return test_control_apis::read_write(my); }

private:
   test_control_ptr my;
};

}

FC_REFLECT(eosio::test_control_apis::empty, )
FC_REFLECT(eosio::test_control_apis::read_write::kill_node_on_producer_params, (producer)(where_in_sequence)(based_on_lib) )
FC_REFLECT(eosio::test_control_apis::read_write::throw_on_params, (signal)(exception) )
FC_REFLECT(eosio::test_control_apis::read_write::swap_action_params, (from)(to)(trx_priv_key)(blk_priv_key)(shutdown) )
