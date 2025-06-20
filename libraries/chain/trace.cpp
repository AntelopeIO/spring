#include <eosio/chain/trace.hpp>
#include <eosio/chain/abi_def.hpp>

namespace eosio { namespace chain {

action_trace::action_trace(
   const transaction_trace& trace, const action& act, account_name receiver, bool context_free,
   uint32_t action_ordinal, uint32_t creator_action_ordinal, uint32_t closest_unnotified_ancestor_action_ordinal
)
:action_ordinal( action_ordinal )
,creator_action_ordinal( creator_action_ordinal )
,closest_unnotified_ancestor_action_ordinal( closest_unnotified_ancestor_action_ordinal )
,receiver( receiver )
,act( act )
,context_free( context_free )
,trx_id( trace.id )
,block_num( trace.block_num )
,block_time( trace.block_time )
,producer_block_id( trace.producer_block_id )
{}

action_trace::action_trace(
   const transaction_trace& trace, action&& act, account_name receiver, bool context_free,
   uint32_t action_ordinal, uint32_t creator_action_ordinal, uint32_t closest_unnotified_ancestor_action_ordinal
)
:action_ordinal( action_ordinal )
,creator_action_ordinal( creator_action_ordinal )
,closest_unnotified_ancestor_action_ordinal( closest_unnotified_ancestor_action_ordinal )
,receiver( receiver )
,act( std::move(act) )
,context_free( context_free )
,trx_id( trace.id )
,block_num( trace.block_num )
,block_time( trace.block_time )
,producer_block_id( trace.producer_block_id )
{}

std::string expand_console(const std::string&                   header,
                           const std::string&                   trailer,
                           const std::vector<call_trace>&       call_traces,
                           fc::unsigned_int                     sender_ordinal,
                           const std::string&                   sender_name,
                           const std::string&                   console,
                           const std::vector<fc::unsigned_int>& console_markers) {
   ilog("entering expand_console");

   if (console.empty() && console_markers.empty()) { // no console output in the current action/call and no sync calls made by it
      return {};
   }

   std::string expanded{};

   if (console_markers.empty()) {
      if (!console.empty()) {
         expanded += "\n";
         expanded += console;
      }
   } else {
      size_t start = 0;
      size_t i = 0;
      ilog("console_markers size: ${s}", ("s", console_markers.size()));
      for (fc::unsigned_int m : console_markers) {
         if (m > start) {
            // append the portion of console between start and the current marker to expanded
            expanded += "\n";
            expanded += console.substr(start, m);
            start = m;
         }

         // find the call_trace corresponding to current marker
         // note: call_trace entries and markers are arranged in the same order
         while (i < call_traces.size()) {
            if (call_traces[i].sender_ordinal == sender_ordinal) {
               break;
            } else {
               ++i;
            }
         }
         assert(i < call_traces.size()); // there must be a call_trace entry for every marker

         const call_trace& ct = call_traces[i];

         std::string call_name{};
         call_data_header data_header;
         fc::datastream<const char*> ds(ct.data.data(), ct.data.size());
         try{
            fc::raw::unpack(ds, data_header);
            if (data_header.is_version_valid()) {
               call_name = eosio::chain::name(data_header.func_name).to_string();
            } else {
               call_name = "unknown_call_name";
            }
         } catch(...) {}

         std::string prefix = "\n[";
         prefix += sender_name;
         prefix += "->(";
         prefix += ct.receiver.to_string();
         prefix += ",";
         prefix += call_name;
         prefix += ")]";

         std::string header = prefix;
         header  += ": CALL BEGIN ======";

         std::string trailer = prefix;
         trailer += ": CALL END   ======";

         // append expanded console of `ct`'s console (recursively)
         expanded += expand_console(header, trailer, call_traces, ct.sender_ordinal, ct.receiver.to_string(), ct.console, ct.console_markers);
      }

      if (console.size() > console_markers.back()) {
         expanded += "\n";
         // append the portion of console after the last marker to `expanded`
         expanded += console.substr(console_markers.back());
      }
   }

   if (expanded.empty()) {
      return {};
   }

   std::string output;
   output += header;
   output += expanded;
   output += trailer;

   return output;
}
} } // eosio::chain
