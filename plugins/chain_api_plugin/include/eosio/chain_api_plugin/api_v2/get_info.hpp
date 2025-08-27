#pragma once
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <apiv2/get_info.pb.h>

#include "type_map.hpp"

namespace eosio {

template <>
struct type_map<chain_apis::read_only::get_info_params> {
   using pb_type_req = apiv2::get_info_request;
   using pb_type_res = apiv2::get_info_response;

   static chain_apis::read_only::get_info_params convert(const pb_type_req& request) {
      chain_apis::read_only::get_info_params res;
      return res;
   }

   static pb_type_res convert(const chain_apis::read_only::get_info_results& results) {
      pb_type_res res;
      res.set_server_version(results.server_version);
      res.mutable_chain_id()->set_data(results.chain_id.data(), results.chain_id.data_size());
      res.set_head_block_num(results.head_block_num);
      res.set_last_irreversible_block_num(results.last_irreversible_block_num);
      res.mutable_last_irreversible_block_id()->set_data(results.last_irreversible_block_id.data(), results.last_irreversible_block_id.data_size());
      res.mutable_head_block_id()->set_data(results.head_block_id.data(), results.head_block_id.data_size());
      res.set_head_block_time(results.head_block_time.time_since_epoch().count());
      res.set_head_block_producer(results.head_block_producer.to_uint64_t());
      res.set_virtual_block_cpu_limit(results.virtual_block_cpu_limit);
      res.set_virtual_block_net_limit(results.virtual_block_net_limit);
      res.set_block_cpu_limit(results.block_cpu_limit);
      res.set_block_net_limit(results.block_net_limit);
      if(results.server_version_string.has_value())
         res.set_server_version_string(results.server_version_string.value());
      if(results.fork_db_head_block_num.has_value())
         res.set_fork_db_head_block_num(results.fork_db_head_block_num.value());
      if(results.fork_db_head_block_id.has_value())
         res.mutable_fork_db_head_block_id()->set_data(results.fork_db_head_block_id.value().data(), results.fork_db_head_block_id.value().data_size());
      if(results.server_full_version_string.has_value())
         res.set_server_full_version_string(results.server_full_version_string.value());
      if(results.server_full_version_string.has_value())
         res.set_server_full_version_string(results.server_full_version_string.value());
      if(results.total_cpu_weight.has_value())
         res.set_total_cpu_weight(results.total_cpu_weight.value());
      if(results.total_net_weight.has_value())
         res.set_total_net_weight(results.total_net_weight.value());
      if(results.earliest_available_block_num.has_value())
         res.set_earliest_available_block_num(results.earliest_available_block_num.value());
      if(results.last_irreversible_block_time.has_value())
         res.set_last_irreversible_block_time(results.last_irreversible_block_time.value().time_since_epoch().count());
      return res;
   }

};

} //namespace eosio