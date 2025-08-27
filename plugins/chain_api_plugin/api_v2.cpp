#include <eosio/chain_api_plugin/api_v2.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <eosio/chain/application.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/http_plugin/http_plugin.hpp>
#include <eosio/http_plugin/macros.hpp>

#include <eosio/chain_api_plugin/api_v2/error_results.hpp>
#include <eosio/chain_api_plugin/api_v2/get_info.hpp>
#include <eosio/chain_api_plugin/api_v2/get_raw_code_and_abi.hpp>

namespace eosio {

using eosio::chain::controller;
using namespace appbase;

// v2 protobuf parser and serializer
#define PROTOBUF_PARAMS_PARSER(namespace, call_name, params_type) \
   [](const std::string& body) { \
      using m = type_map<namespace::call_name ## _params>; \
      m::pb_type_req request; \
      if (!request.ParseFromString(body)) { \
         throw std::runtime_error("Failed to parse protobuf type from binary data"); \
      } \
      return m::convert(request); \
   }

#define PROTOBUF_RESULT_SERIALIZER(namespace, call_name) \
   [](const namespace::call_name ## _results& result) { \
      using m = type_map<namespace::call_name ## _params>; \
      m::pb_type_res res = m::convert(result); \
      std::string data; \
      res.SerializeToString(&data); \
      return data; \
   }

#define PROTOBUF_ERROR_SERIALIZER() \
   [](const eosio::error_results& e) { \
      auto res = error_results_pb::convert(e); \
      std::string data; \
      res.SerializeToString(&data); \
      return data; \
   }

#define CHAIN_RO_V2_CALL(call_name, http_response_code, params_type) CALL_WITH_400(2, chain, chain_ro_v2, ro_api, call_name, http_response_code, PROTOBUF_PARAMS_PARSER(chain_apis::read_only, call_name, params_type), PROTOBUF_RESULT_SERIALIZER(chain_apis::read_only, call_name), PROTOBUF_ERROR_SERIALIZER())

void api_v2_manager::initialize() {
   auto& chain = app().get_plugin<chain_plugin>();
   auto& _http_plugin = app().get_plugin<http_plugin>();

   fc::microseconds max_response_time = _http_plugin.get_max_response_time();

   auto ro_api = chain.get_read_only_api(max_response_time);
   ro_api.set_shorten_abi_errors( !http_plugin::verbose_errors() );

   // Run get_info on http thread only
   _http_plugin.add_async_api({
      CHAIN_RO_V2_CALL(get_info, 200, http_params_types::no_params)
   }, http_content_type::protobuf );

   _http_plugin.add_api({
      CHAIN_RO_V2_CALL(get_raw_code_and_abi, 200, http_params_types::params_required)
   }, appbase::exec_queue::read_only, appbase::priority::medium_low, http_content_type::protobuf);

}

} 
