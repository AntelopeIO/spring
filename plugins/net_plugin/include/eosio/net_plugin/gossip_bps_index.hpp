#pragma once

#include <eosio/net_plugin/protocol.hpp>
#include <eosio/chain/thread_utils.hpp>

#include <fc/mutex.hpp>

#include <boost/multi_index_container.hpp>

namespace eosio {

struct gossip_bp_index_t {
   using gossip_bps_index_container_t = boost::multi_index_container<
      gossip_bp_peers_message::bp_peer,
      indexed_by<
         ordered_unique<
            tag<struct by_producer>,
            composite_key< gossip_bp_peers_message::bp_peer,
               member<gossip_bp_peers_message::bp_peer, name, &gossip_bp_peers_message::bp_peer::producer_name>,
               member<gossip_bp_peers_message::bp_peer, std::string, &gossip_bp_peers_message::bp_peer::server_address>
            >,
            composite_key_compare< std::less<>, std::less<> >
         >,
         ordered_unique<
            tag< struct by_sig >,
            member< gossip_bp_peers_message::bp_peer, chain::signature_type, &gossip_bp_peers_message::bp_peer::sig > >
         >
      >;

   alignas(hardware_destructive_interference_sz)
   mutable fc::mutex                mtx;
   gossip_bps_index_container_t     index GUARDED_BY(mtx);
};


} // namespace eosio