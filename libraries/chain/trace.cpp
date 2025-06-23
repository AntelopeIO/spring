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
                           size_t                               call_trace_idx,
                           fc::unsigned_int                     sender_ordinal,
                           const std::string&                   sender_name,
                           const std::string&                   console,
                           const std::vector<fc::unsigned_int>& console_markers) {
   if (console.empty() && console_markers.empty()) { // no console output in the current action/call and no sync calls made by it
      return {};
   }

   // no sync calls but has console
   if (console_markers.empty()) {
      std::string output{};
      output += header;
      output += "\n";
      output += console;
      output += trailer;

      return output;
   }

   // has sync calls. expand their consoles
   std::string expanded{};
   size_t last_marker = 0;

   for (fc::unsigned_int marker : console_markers) {
      // if current marker is greater last marker, need to output the current
      // segment in the console
      if (marker > last_marker) {
         expanded += "\n";
         expanded += console.substr(last_marker, marker);
         last_marker = marker;
      }

      // find the call_trace corresponding to current marker
      // note: call_trace entries and markers are arranged in the same order
      while (call_trace_idx < call_traces.size()) {
         if (call_traces[call_trace_idx].sender_ordinal == sender_ordinal) {
            break;
         } else {
            ++call_trace_idx;
         }
      }
      assert(call_trace_idx < call_traces.size()); // there must be a call_trace entry for every marker

      const call_trace& ct = call_traces[call_trace_idx];

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

      // The traces of a child sync call in `call_traces` are after the call trace
      // of the current call, that's why we use `call_trace_idx + 1` to avoid
      // searching from the beginning every time.
      expanded += expand_console(header, trailer, call_traces, call_trace_idx + 1, ct.sender_ordinal, ct.receiver.to_string(), ct.console, ct.console_markers); // append expanded console of `ct`'s console (recursively)
   }

   if (console.size() > console_markers.back()) {
      expanded += "\n";
      // append the portion of console after the last marker to `expanded`
      expanded += console.substr(console_markers.back());
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
