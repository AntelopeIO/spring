#include <eosio/chain/webassembly/interface.hpp>
#include <eosio/chain/apply_context.hpp>

namespace eosio::chain::webassembly {
void interface::emit_event(std::span<const char> tags_and_data) {
   //constructs a non-copied view over tags & data before sending off to more high level handlers. maybe overkill idk
   fc::datastream<const char*> ds(tags_and_data.data(), tags_and_data.size());

   fc::unsigned_int num_tags;
   fc::raw::unpack(ds, num_tags);
   EOS_ASSERT(num_tags.value <= chain::config::maximum_explicit_event_tags, event_too_many_tags, "Event has ${num} explicit tags but the maximum allowed is ${max}",
                                                                                                 ("num", num_tags)("max",chain::config::maximum_explicit_event_tags));

   size_t tagbytes = num_tags * sizeof(uint64_t);
   std::span<const char> u64tags = tags_and_data.subspan(ds.tellp(), tagbytes);
   ds.skip(tagbytes);
   EOS_ASSERT(ds.valid(), fc::out_of_range_exception, "Overread when parsing event tags");

   fc::unsigned_int event_data_sz;
   fc::raw::unpack(ds, event_data_sz);
   std::span<const char> eventdata = tags_and_data.subspan(ds.tellp(), event_data_sz);
   ds.skip(event_data_sz);
   EOS_ASSERT(ds.valid(), fc::out_of_range_exception, "Overread when parsing event data");
   context.emit_event(u64tags, eventdata);
}
}