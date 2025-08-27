#pragma once

#define CALL_WITH_400(version, api_name, category, api_handle, call_name, http_response_code, request_deserializer, response_serializer, error_serializer) \
{std::string("/v" #version "/" #api_name "/" #call_name), \
   api_category::category,\
   [api_handle](string&&, string&& body, url_response_callback&& cb) mutable { \
          auto deadline = api_handle.start(); \
          try { \
             auto params = request_deserializer(body); \
             auto result( api_handle.call_name( std::move(params), deadline ) ); \
             cb(http_response_code, response_serializer(std::move(result))); \
          } catch (...) { \
             http_plugin::handle_exception(#api_name, #call_name, body, cb, error_serializer); \
          } \
       }}

#define CALL_ASYNC_WITH_400(version, api_name, category, api_handle, call_name, call_result, http_resp_code, request_deserializer, response_serializer, error_serializer) \
{ std::string("/v" #version "/" #api_name "/" #call_name),                                                      \
  api_category::category,                                                                                       \
  [api_handle, &_http_plugin](string&&, string&& body, url_response_callback&& cb) mutable {                    \
     api_handle.start();                                                                                        \
     try {                                                                                                      \
        auto params = request_deserializer(body);                                                               \
        using http_fwd_t = std::function<chain::t_or_exception<call_result>()>;                                 \
        api_handle.call_name( std::move(params), /* called on main application thread */                        \
           [&_http_plugin, cb=std::move(cb), body=std::move(body)]                                              \
           (const chain::next_function_variant<call_result>& result) mutable {                                  \
              if (std::holds_alternative<fc::exception_ptr>(result)) {                                          \
                 try {                                                                                          \
                    std::get<fc::exception_ptr>(result)->rethrow();                                             \
                 } catch (...) {                                                                                \
                    http_plugin::handle_exception(#api_name, #call_name, body, cb, error_serializer);           \
                 }                                                                                              \
              } else if (std::holds_alternative<call_result>(result)) {                                         \
                 cb(http_resp_code, response_serializer(std::get<call_result>(std::move(result))));             \
              } else {                                                                                          \
                 /* api returned a function to be processed on the http_plugin thread pool */                   \
                 assert(std::holds_alternative<http_fwd_t>(result));                                            \
                 _http_plugin.post_http_thread_pool([resp_code=http_resp_code, cb=std::move(cb),                \
                                                     body=std::move(body),                                      \
                                                     http_fwd = std::get<http_fwd_t>(std::move(result))]() {    \
                    chain::t_or_exception<call_result> result = http_fwd();                                     \
                    if (std::holds_alternative<fc::exception_ptr>(result)) {                                    \
                       try {                                                                                    \
                          std::get<fc::exception_ptr>(result)->rethrow();                                       \
                       } catch (...) {                                                                          \
                          http_plugin::handle_exception(#api_name, #call_name, body, cb, error_serializer);     \
                       }                                                                                        \
                    } else {                                                                                    \
                       cb(resp_code, response_serializer(std::get<call_result>(std::move(result))));                    \
                    }                                                                                           \
                 });                                                                                            \
              }                                                                                                 \
           });                                                                                                  \
     } catch (...) {                                                                                            \
        http_plugin::handle_exception(#api_name, #call_name, body, cb, error_serializer);                       \
     }                                                                                                          \
   }                                                                                                            \
}


// call an API which returns either fc::exception_ptr, or a function to be posted on the http thread pool
// for execution (typically doing the final serialization)
// ------------------------------------------------------------------------------------------------------
#define CALL_WITH_400_POST(version, api_name, category, api_handle, call_name, call_result, http_resp_code, request_deserializer, response_serializer, error_serializer) \
{std::string("/v" #version "/" #api_name "/" #call_name),                                                       \
      api_category::category,                                                                                   \
      [api_handle, &_http_plugin](string&&, string&& body, url_response_callback&& cb) {                        \
          auto deadline = api_handle.start();                                                                   \
          try {                                                                                                 \
             auto params = request_deserializer(body);                                                          \
             using http_fwd_t = std::function<chain::t_or_exception<call_result>()>;                            \
             /* called on main application thread */                                                            \
             http_fwd_t http_fwd(api_handle.call_name(std::move(params), deadline));                            \
             _http_plugin.post_http_thread_pool([resp_code=http_resp_code, cb=std::move(cb),                    \
                                                 body=std::move(body),                                          \
                                                 http_fwd = std::move(http_fwd)]() {                            \
                try {                                                                                           \
                   chain::t_or_exception<call_result> result = http_fwd();                                      \
                   if (std::holds_alternative<fc::exception_ptr>(result)) {                                     \
                      try {                                                                                     \
                         std::get<fc::exception_ptr>(result)->rethrow();                                        \
                      } catch (...) {                                                                           \
                         http_plugin::handle_exception(#api_name, #call_name, body, cb, error_serializer);      \
                      }                                                                                         \
                   } else {                                                                                     \
                      cb(resp_code, response_serializer(std::get<call_result>(std::move(result))));             \
                   }                                                                                            \
                } catch (...) {                                                                                 \
                   http_plugin::handle_exception(#api_name, #call_name, body, cb, error_serializer);            \
                }                                                                                               \
             });                                                                                                \
          } catch (...) {                                                                                       \
             http_plugin::handle_exception(#api_name, #call_name, body, cb, error_serializer);                  \
          }                                                                                                     \
       }}
