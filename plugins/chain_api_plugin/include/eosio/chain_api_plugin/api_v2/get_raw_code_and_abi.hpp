#pragma once
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <apiv2/get_raw_code_and_abi.pb.h>

#include "type_map.hpp"

namespace eosio {

template <>
struct type_map<chain_apis::read_only::get_raw_code_and_abi_params> {
   using pb_type_req = apiv2::get_raw_code_and_abi_request;
   using pb_type_res = apiv2::get_raw_code_and_abi_response;

   static chain_apis::read_only::get_raw_code_and_abi_params convert(const pb_type_req& request) {
      chain_apis::read_only::get_raw_code_and_abi_params res;
      res.account_name = account_name{request.account_name()};
      return res;
   }

   static pb_type_res convert(const chain_apis::read_only::get_raw_code_and_abi_results& results) {
      pb_type_res res;
      res.set_account_name(results.account_name.to_uint64_t());
      res.set_wasm(std::string(results.wasm.data.data(), results.wasm.data.size()));
      res.set_abi(std::string(results.abi.data.data(), results.abi.data.size()));
      return res;
   }

};

} //namespace eosio