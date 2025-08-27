#pragma once
#include <eosio/http_plugin/http_plugin.hpp>
#include <apiv2/error_results.pb.h>

namespace eosio {

struct error_results_pb {

   static apiv2::error_results convert(const error_results& results) {
      apiv2::error_results error;
      error.set_code(results.code);
      error.set_message(results.message);

      auto* info = error.mutable_error();
      info->set_code(results.error.code);
      info->set_name(results.error.name);
      info->set_what(results.error.what);

      for (const auto& d : results.error.details) {
         auto* detail = info->add_details();
         detail->set_message(d.message);
         detail->set_file(d.file);
         detail->set_line_number(d.line_number);
         detail->set_method(d.method);
      }

      return error;
   }
};

} //namespace eosio