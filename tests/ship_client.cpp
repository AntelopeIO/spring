#include <boost/asio.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/beast.hpp>
#include <boost/program_options.hpp>

#include <eosio/chain/abi_def.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <fc/io/json.hpp>

#include <iostream>
#include <regex>
#include <string>

using mvo = fc::mutable_variant_object;
using tcp = boost::asio::ip::tcp;
using unixs = boost::asio::local::stream_protocol;
namespace ws = boost::beast::websocket;

namespace bpo = boost::program_options;

static const eosio::chain::abi_serializer::yield_function_t null_yield_function{};

int main(int argc, char* argv[]) {
   boost::asio::io_context ctx;
   boost::asio::ip::tcp::resolver resolver(ctx);
   ws::stream<boost::asio::ip::tcp::socket> tcp_stream(ctx);
   eosio::chain::abi_serializer abi;

   unixs::socket unix_socket(ctx);
   ws::stream<unixs::socket> unix_stream(ctx);

   bpo::options_description cli("ship_client command line options");
   bool help = false;
   std::string socket_address = "127.0.0.1:8080";
   uint32_t num_requests = 1;

   cli.add_options()
      ("help,h", bpo::bool_switch(&help)->default_value(false), "Print this help message and exit.")
      ("socket-address,a", bpo::value<std::string>(&socket_address)->default_value(socket_address), "Websocket address and port.")
      ("num-requests,n", bpo::value<uint32_t>(&num_requests)->default_value(num_requests), "number of requests to make");
   bpo::variables_map varmap;
   bpo::store(bpo::parse_command_line(argc, argv, cli), varmap);
   bpo::notify(varmap);

   if(help) {
      cli.print(std::cout);
      return 0;
   }

   std::string statehistory_server, statehistory_port;

   // unix socket
   if(socket_address.starts_with("ws+unix://") ||
      socket_address.starts_with("unix://")) {
      statehistory_port = "";
      statehistory_server = socket_address.substr(socket_address.find("unix://") + strlen("unix://") + 1);
   } else {
      std::string::size_type colon = socket_address.find(':');
      FC_ASSERT(colon != std::string::npos, "Missing ':' seperator in Websocket address and port");
      statehistory_server = socket_address.substr(0, colon);
      statehistory_port = socket_address.substr(colon + 1);
   }

   std::cerr << "[\n{\n   \"status\": \"construct\",\n   \"time\": " << time(NULL) << "\n},\n";

   try {
      auto run = [&]<typename SocketStream>(SocketStream& stream) {
         {
            boost::beast::flat_buffer abi_buffer;
            stream.read(abi_buffer);
            std::string abi_string = boost::beast::buffers_to_string(abi_buffer.data());
            //remove all tables since their names are invalid; tables not needed for this test
            std::regex scrub_all_tables(R"(\{ "name": "[^"]+", "type": "[^"]+", "key_names": \[[^\]]*\] \},?)");
            abi_string = std::regex_replace(abi_string, scrub_all_tables, "");

            abi = eosio::chain::abi_serializer(fc::json::from_string(abi_string).as<eosio::chain::abi_def>(), null_yield_function);
         }
         stream.binary(true);

         std::cerr << "{\n   \"status\": \"set_abi\",\n   \"time\": " << time(NULL) << "\n},\n";

         bool is_first = true;
         uint32_t first_block_num = 0;
         uint32_t last_block_num = 0;

         struct {
            std::string get_status_request;
            std::string get_status_result;
         } request_result_types[] = {
            {"get_status_request_v0", "get_status_result_v0"},
            {"get_status_request_v1", "get_status_result_v1"},
         };

         while(num_requests--) {
            const eosio::chain::bytes get_status_bytes = abi.variant_to_binary("request",
               fc::variants{request_result_types[num_requests%2].get_status_request, mvo()}, null_yield_function);
            stream.write(boost::asio::buffer(get_status_bytes));

            boost::beast::flat_buffer buffer;
            stream.read(buffer);

            fc::datastream<const char*> ds((const char*)buffer.data().data(), buffer.data().size());
            const fc::variant result = abi.binary_to_variant("result", ds, null_yield_function);

            FC_ASSERT(result.is_array(),                                                           "result should have been an array (variant) but it's not");
            FC_ASSERT(result.size() == 2,                                                          "result was an array but did not contain 2 items like a variant should");
            FC_ASSERT(result[0ul] == request_result_types[num_requests%2].get_status_result, "result type doesn't look like expected get_status_result_vX");
            const fc::variant_object& resultobj = result[1ul].get_object();
            FC_ASSERT(resultobj.contains("head"),                                                  "cannot find 'head' in result");
            FC_ASSERT(resultobj["head"].is_object(),                                               "'head' is not an object");
            FC_ASSERT(resultobj["head"].get_object().contains("block_num"),                        "'head' does not contain 'block_num'");
            FC_ASSERT(resultobj["head"].get_object()["block_num"].is_integer(),                    "'head.block_num' isn't a number");
            FC_ASSERT(resultobj["head"].get_object().contains("block_id"),                         "'head' does not contain 'block_id'");
            FC_ASSERT(resultobj["head"].get_object()["block_id"].is_string(),                      "'head.block_id' isn't a string");

            uint32_t this_block_num = resultobj["head"]["block_num"].as_uint64();

            if(is_first) {
               std::cout << "[" << std::endl;
               first_block_num = this_block_num;
               is_first = false;
            }
            else {
               std::cout << "," << std::endl;
            }
            std::cout << "{ \"" << result[0ul].as_string() << "\":" << std::endl;
            std::cout << fc::json::to_pretty_string(resultobj) << std::endl << "}" << std::endl;

            last_block_num = this_block_num;
         }

         std::cout << "]" << std::endl;

         std::cerr << fc::json::to_pretty_string(mvo()("status", "done")
                                                      ("time", time(NULL))
                                                      ("first_block_num", first_block_num)
                                                      ("last_block_num", last_block_num)) << std::endl << "]" << std::endl;
      };

      // unix socket
      if(statehistory_port.empty()) {
         boost::system::error_code ec;
         auto check_ec = [&](const char* what) {
            if(!ec)
               return;
            std::cerr << "{\n   \"status\": socket error - " << ec.message() << ",\n   \"time\": " << time(NULL) << "\n},\n";
         };

         unix_stream.next_layer().connect(unixs::endpoint(statehistory_server),
                                          ec);
         if(ec == boost::system::errc::success) {
            std::cerr << "{\n   \"status\": \"successfully connected to unix socket\",\n   \"time\": " << time(NULL) << "\n},\n";
         } else {
            check_ec("connect");
         }
         unix_stream.handshake("", "/");
         run(unix_stream);
      }
      // tcp socket
      else {
         boost::asio::connect(tcp_stream.next_layer(), resolver.resolve(statehistory_server, statehistory_port));
         tcp_stream.handshake(statehistory_server, "/");
         run(tcp_stream);
      }
   } catch (std::exception& e) {
      std::cerr << "Caught exception: " << e.what() << std::endl;
      return 1;
   }
   return 0;
}
