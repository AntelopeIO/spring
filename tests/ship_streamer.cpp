#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/program_options.hpp>

#include <eosio/chain/abi_def.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <fc/io/json.hpp>

#include <map>
#include <set>
#include <iostream>
#include <regex>
#include <string>

using mvo = fc::mutable_variant_object;

namespace bpo = boost::program_options;

static const eosio::chain::abi_serializer::yield_function_t null_yield_function{};

int main(int argc, char* argv[]) {
   boost::asio::io_context ctx;
   boost::asio::ip::tcp::resolver resolver(ctx);
   boost::beast::websocket::stream<boost::asio::ip::tcp::socket> stream(ctx);
   eosio::chain::abi_serializer abi;

   bpo::options_description cli("ship_streamer command line options");
   bool help = false;
   std::string socket_address = "127.0.0.1:8080";
   uint32_t start_block_num = 1;
   uint32_t end_block_num = std::numeric_limits<u_int32_t>::max()-1;
   bool irreversible_only = false;
   bool fetch_block = false;
   bool fetch_traces = false;
   bool fetch_deltas = false;
   bool fetch_finality_data = false;

   cli.add_options()
      ("help,h", bpo::bool_switch(&help)->default_value(false), "Print this help message and exit.")
      ("socket-address,a", bpo::value<std::string>(&socket_address)->default_value(socket_address), "Websocket address and port.")
      ("start-block-num", bpo::value<uint32_t>(&start_block_num)->default_value(start_block_num), "Block to start streaming from")
      ("end-block-num", bpo::value<uint32_t>(&end_block_num)->default_value(end_block_num), "Block to stop streaming")
      ("irreversible-only", bpo::bool_switch(&irreversible_only)->default_value(irreversible_only), "Irreversible blocks only")
      ("fetch-block", bpo::bool_switch(&fetch_block)->default_value(fetch_block), "Fetch blocks")
      ("fetch-traces", bpo::bool_switch(&fetch_traces)->default_value(fetch_traces), "Fetch traces")
      ("fetch-deltas", bpo::bool_switch(&fetch_deltas)->default_value(fetch_deltas), "Fetch deltas")
      ("fetch-finality-data", bpo::bool_switch(&fetch_finality_data)->default_value(fetch_finality_data), "Fetch finality data")
      ;
   bpo::variables_map varmap;
   bpo::store(bpo::parse_command_line(argc, argv, cli), varmap);
   bpo::notify(varmap);

   if(help) {
      cli.print(std::cout);
      return 0;
   }

   std::string::size_type colon = socket_address.find(':');
   FC_ASSERT(colon != std::string::npos, "Missing ':' seperator in Websocket address and port");
   std::string statehistory_server = socket_address.substr(0, colon);
   std::string statehistory_port = socket_address.substr(colon+1);

   try {
      boost::asio::connect(stream.next_layer(), resolver.resolve(statehistory_server, statehistory_port));
      stream.handshake(statehistory_server, "/");

      {
         boost::beast::flat_buffer abi_buffer;
         stream.read(abi_buffer);
         std::string abi_string = boost::beast::buffers_to_string(abi_buffer.data());
         //remove all tables since their names are invalid; tables not needed for this test
         std::regex scrub_all_tables(R"(\{ "name": "[^"]+", "type": "[^"]+", "key_names": \[[^\]]*\] \},?)");
         abi_string = std::regex_replace(abi_string, scrub_all_tables, "");

         abi = eosio::chain::abi_serializer(fc::json::from_string(abi_string).as<eosio::chain::abi_def>(), null_yield_function);
         //state history may have 'bytes' larger than MAX_SIZE_OF_BYTE_ARRAYS, so divert 'bytes' to an impl that does not have that check
         abi.add_specialized_unpack_pack("bytes", std::make_pair<eosio::chain::abi_serializer::unpack_function, eosio::chain::abi_serializer::pack_function>(
            [](fc::datastream<const char*>& stream, bool is_array, bool is_optional, const eosio::chain::abi_serializer::yield_function_t& yield) {
               FC_ASSERT(!is_array, "sorry, this kludge doesn't support bytes[]");
               if(is_optional) {
                  bool present = false;
                  fc::raw::unpack(stream, present);
                  if(!present)
                     return fc::variant();
               }
               fc::unsigned_int sz;
               fc::raw::unpack(stream, sz);
               if(sz == 0u)
                  return fc::variant("");
               std::vector<char> data(sz);
               stream.read(data.data(), sz);
               return fc::variant(fc::to_hex(data.data(),data.size()));
         },
         [](const fc::variant&, fc::datastream<char*>&, bool, bool, const eosio::chain::abi_serializer::yield_function_t&) {
            FC_ASSERT(false, "sorry, this kludge can't write out bytes");
         }));
      }

      //struct get_blocks_request_v0 {
      //   uint32_t                    start_block_num        = 0;
      //   uint32_t                    end_block_num          = 0;
      //   uint32_t                    max_messages_in_flight = 0;
      //   std::vector<block_position> have_positions         = {};
      //   bool                        irreversible_only      = false;
      //   bool                        fetch_block            = false;
      //   bool                        fetch_traces           = false;
      //   bool                        fetch_deltas           = false;
      //};
      //struct get_blocks_request_v1 : get_blocks_request_v0 {
      //   bool                        fetch_finality_data    = false;
      //};

      stream.binary(true);
      const eosio::chain::bytes get_status_bytes = abi.variant_to_binary("request",
         fc::variants{"get_blocks_request_v1", mvo()("start_block_num", start_block_num)
                                                    ("end_block_num", std::to_string(end_block_num + 1)) // SHiP is (start-end] exclusive
                                                    ("max_messages_in_flight",std::to_string(std::numeric_limits<u_int32_t>::max()))
                                                    ("have_positions", fc::variants{})
                                                    ("irreversible_only", irreversible_only)
                                                    ("fetch_block", fetch_block)
                                                    ("fetch_traces", fetch_traces)
                                                    ("fetch_deltas", fetch_deltas)
                                                    ("fetch_finality_data", fetch_finality_data)}, null_yield_function);
      stream.write(boost::asio::buffer(get_status_bytes));
      stream.read_message_max(0);

      // Each block_num can have multiple block_ids since forks are possible
      //       block_num,         block_id
      std::map<uint32_t, std::set<std::string>> block_ids;
      bool is_first = true;
      for(;;) {
         boost::beast::flat_buffer buffer;
         stream.read(buffer);

         fc::datastream<const char*> ds((const char*)buffer.data().data(), buffer.data().size());
         const fc::variant result = abi.binary_to_variant("result", ds, null_yield_function);

         FC_ASSERT(result.is_array(),                                                        "result should have been an array (variant) but it's not");
         FC_ASSERT(result.size() == 2,                                                       "result was an array but did not contain 2 items like a variant should");
         FC_ASSERT(result[0ul] == "get_blocks_result_v1",                                    "result type doesn't look like get_blocks_result_v1");
         const fc::variant_object& resultobj = result[1ul].get_object();
         FC_ASSERT(resultobj.contains("head"),                                               "cannot find 'head' in result");
         FC_ASSERT(resultobj["head"].is_object(),                                            "'head' is not an object");
         FC_ASSERT(resultobj["head"].get_object().contains("block_num"),                     "'head' does not contain 'block_num'");
         FC_ASSERT(resultobj["head"].get_object()["block_num"].is_integer(),                 "'head.block_num' isn't a number");
         FC_ASSERT(resultobj["head"].get_object().contains("block_id"),                      "'head' does not contain 'block_id'");
         FC_ASSERT(resultobj["head"].get_object()["block_id"].is_string(),                   "'head.block_id' isn't a string");

         // stream what was received
         if(is_first) {
           std::cout << "[" << std::endl;
           is_first = false;
         } else {
           std::cout << "," << std::endl;
         }
         std::cout << "{ \"get_blocks_result_v1\":" << fc::json::to_pretty_string(resultobj) << std::endl << "}" << std::endl;

         // validate after streaming, so that invalid entry is included in the output
         uint32_t this_block_num = 0;
         if( resultobj.contains("this_block") && resultobj["this_block"].is_object() ) {
            const fc::variant_object& this_block = resultobj["this_block"].get_object();
            if( this_block.contains("block_num") && this_block["block_num"].is_integer() ) {
               this_block_num = this_block["block_num"].as_uint64();
            }
            std::string this_block_id;
            if( this_block.contains("block_id") && this_block["block_id"].is_string() ) {
               this_block_id = this_block["block_id"].get_string();
            }
            std::string prev_block_id;
            if( resultobj.contains("prev_block") && resultobj["prev_block"].is_object() ) {
               const fc::variant_object& prev_block = resultobj["prev_block"].get_object();
               if ( prev_block.contains("block_id") && prev_block["block_id"].is_string() ) {
                  prev_block_id = prev_block["block_id"].get_string();
               }
            }
            if( !irreversible_only && !this_block_id.empty() && !prev_block_id.empty() ) {
               // verify forks were sent
               if (block_ids.count(this_block_num-1)) {
                  if (block_ids[this_block_num-1].count(prev_block_id) == 0) {
                     std::cerr << "Received block: << " << this_block_num << " that does not link to previous: ";
                     std::copy(block_ids[this_block_num-1].begin(), block_ids[this_block_num-1].end(), std::ostream_iterator<std::string>(std::cerr, " "));
                     std::cerr << std::endl;
                     return 1;
                  }
               }
               block_ids[this_block_num].insert(this_block_id);

               if( resultobj["last_irreversible"].get_object().contains("block_num") && resultobj["last_irreversible"]["block_num"].is_integer() ) {
                  uint32_t lib_num = resultobj["last_irreversible"]["block_num"].as_uint64();
                  auto i = block_ids.lower_bound(lib_num);
                  if (i != block_ids.end()) {
                     block_ids.erase(block_ids.begin(), i);
                  }
               }
            }

         }

         if( this_block_num == end_block_num ) break;
      }

      std::cout << "]" << std::endl;
   }
   catch(std::exception& e) {
      std::cerr << "Caught exception: " << e.what() << std::endl;
      return 1;
   }

   return 0;
}
