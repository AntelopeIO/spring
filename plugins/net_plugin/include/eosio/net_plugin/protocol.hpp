#pragma once
#include <eosio/chain/block.hpp>
#include <eosio/chain/vote_message.hpp>
#include <eosio/chain/types.hpp>

namespace eosio {
   using namespace chain;
   using namespace fc;

   constexpr auto message_header_size = sizeof(uint32_t);

   struct chain_size_message {
      uint32_t                   last_irreversible_block_num = 0;
      block_id_type              last_irreversible_block_id;
      uint32_t                   head_num = 0;
      block_id_type              head_id;
   };

   struct handshake_message {
      uint16_t                   network_version = 0; ///< incremental value above a computed base
      chain_id_type              chain_id; ///< used to identify chain
      fc::sha256                 node_id; ///< used to identify peers and prevent self-connect
      chain::public_key_type     key; ///< authentication key; may be a producer or peer key, or empty
      int64_t                    time{0}; ///< time message created in nanoseconds from epoch
      fc::sha256                 token; ///< digest of time to prove we own the private key of the key above
      chain::signature_type      sig; ///< signature for the digest
      string                     p2p_address;
      uint32_t                   fork_db_root_num = 0;
      block_id_type              fork_db_root_id;
      uint32_t                   fork_db_head_num = 0;
      block_id_type              fork_db_head_id;
      string                     os;
      string                     agent;
      int16_t                    generation = 0;
   };


  enum class go_away_reason {
    no_reason, ///< no reason to go away
    self, ///< the connection is to itself
    duplicate, ///< the connection is redundant
    wrong_chain, ///< the peer's chain id doesn't match
    wrong_version, ///< the peer's network version doesn't match
    forked, ///< the peer's irreversible blocks are different
    unlinkable, ///< the peer sent a block we couldn't use
    bad_transaction, ///< the peer sent a transaction that failed verification
    validation, ///< the peer sent a block that failed validation
    benign_other, ///< reasons such as a timeout. not fatal but warrant resetting
    fatal_other, ///< a catch-all for errors we don't have discriminated
    authentication ///< peer failed authenicatio
  };

  constexpr auto reason_str( go_away_reason rsn ) {
    switch (rsn ) {
    case go_away_reason::no_reason : return "no reason";
    case go_away_reason::self : return "self connect";
    case go_away_reason::duplicate : return "duplicate";
    case go_away_reason::wrong_chain : return "wrong chain";
    case go_away_reason::wrong_version : return "wrong version";
    case go_away_reason::forked : return "chain is forked";
    case go_away_reason::unlinkable : return "unlinkable block received";
    case go_away_reason::bad_transaction : return "bad transaction";
    case go_away_reason::validation : return "invalid block";
    case go_away_reason::authentication : return "authentication failure";
    case go_away_reason::fatal_other : return "some other failure";
    case go_away_reason::benign_other : return "some other non-fatal condition, possibly unknown block";
    default : return "some crazy reason";
    }
  }

  struct go_away_message {
    go_away_reason reason{go_away_reason::no_reason};
    fc::sha256 node_id; ///< for duplicate notification
  };

  struct time_message {
            int64_t  org{0};       //!< origin timestamp, in nanoseconds
            int64_t  rec{0};       //!< receive timestamp, in nanoseconds
            int64_t  xmt{0};       //!< transmit timestamp, in nanoseconds
    mutable int64_t  dst{0};       //!< destination timestamp, in nanoseconds
  };

  enum id_list_modes {
    none,
    catch_up,
    last_irr_catch_up,
    normal
  };

  constexpr auto modes_str( id_list_modes m ) {
    switch( m ) {
    case none : return "none";
    case catch_up : return "catch up";
    case last_irr_catch_up : return "last irreversible";
    case normal : return "normal";
    default: return "undefined mode";
    }
  }

  template<typename T>
  struct select_ids {
    select_ids() : mode(none),pending(0),ids() {}
    id_list_modes  mode{none};
    uint32_t       pending{0};
    vector<T>      ids;
    bool           empty () const { return (mode == none || ids.empty()); }
    bool operator==(const select_ids&) const noexcept = default;
  };

  using ordered_txn_ids = select_ids<transaction_id_type>;
  using ordered_blk_ids = select_ids<block_id_type>;

  struct notice_message {
    notice_message() : known_trx(), known_blocks() {}
    ordered_txn_ids known_trx;
    ordered_blk_ids known_blocks;
  };

  struct request_message {
    request_message() : req_trx(), req_blocks() {}
    ordered_txn_ids req_trx;
    ordered_blk_ids req_blocks;
  };

   struct sync_request_message {
      uint32_t start_block{0};
      uint32_t end_block{0};
   };

   struct block_nack_message {
      block_id_type id;
   };

   struct block_notice_message {
      block_id_type previous;
      block_id_type id;
   };

   struct transaction_notice_message {
      transaction_id_type id;
   };

   struct gossip_bp_peers_message {
      struct bp_peer_info_v1 {
         std::string               server_endpoint;      // externally available address to connect to
         std::string               outbound_ip_address;  // outbound ip address for firewall
         block_timestamp_type      expiration;           // head block to remove bp_peer
      };
      // bp_peer_info_v2 can derive from bp_peer_info_v1, so old peers can still unpack bp_peer_info_v1 from bp_peer::bp_peer_info
      struct bp_peer {
         unsigned_int              version = 1;
         eosio::name               producer_name;
         std::vector<char>         bp_peer_info;         // serialized bp_peer_info

         digest_type digest(const chain_id_type& chain_id) const;
      };
      struct signed_bp_peer : bp_peer {
         signature_type  sig; // signature over bp_peer

         std::optional<bp_peer_info_v1> cached_bp_peer_info; // not serialized

         const std::string& server_endpoint() const     { assert(cached_bp_peer_info); return cached_bp_peer_info->server_endpoint; }
         const std::string& outbound_ip_address() const { assert(cached_bp_peer_info); return cached_bp_peer_info->outbound_ip_address; }
         block_timestamp_type expiration() const        { assert(cached_bp_peer_info); return cached_bp_peer_info->expiration; }
      };

      std::vector<signed_bp_peer> peers;
   };

   using net_message = std::variant<handshake_message,
                                    chain_size_message,
                                    go_away_message,
                                    time_message,
                                    notice_message,
                                    request_message,
                                    sync_request_message,
                                    signed_block,
                                    packed_transaction,
                                    vote_message,
                                    block_nack_message,
                                    block_notice_message,
                                    gossip_bp_peers_message,
                                    transaction_notice_message>;

   // see protocol net_message
   enum class msg_type_t {
      handshake_message      = fc::get_index<net_message, handshake_message>(),
      chain_size_message     = fc::get_index<net_message, chain_size_message>(),
      go_away_message        = fc::get_index<net_message, go_away_message>(),
      time_message           = fc::get_index<net_message, time_message>(),
      notice_message         = fc::get_index<net_message, notice_message>(),
      request_message        = fc::get_index<net_message, request_message>(),
      sync_request_message   = fc::get_index<net_message, sync_request_message>(),
      signed_block           = fc::get_index<net_message, signed_block>(),
      packed_transaction     = fc::get_index<net_message, packed_transaction>(),
      vote_message           = fc::get_index<net_message, vote_message>(),
      block_nack_message     = fc::get_index<net_message, block_nack_message>(),
      block_notice_message   = fc::get_index<net_message, block_notice_message>(),
      gossip_bp_peers_message    = fc::get_index<net_message, gossip_bp_peers_message>(),
      transaction_notice_message = fc::get_index<net_message, transaction_notice_message>(),
      unknown
   };

   constexpr uint32_t to_index(msg_type_t net_msg) {
      static_assert( std::variant_size_v<net_message> == static_cast<uint32_t>(msg_type_t::unknown));
      return static_cast<uint32_t>(net_msg);
   }

   constexpr msg_type_t to_msg_type_t(size_t v) {
      static_assert( std::variant_size_v<net_message> == static_cast<size_t>(msg_type_t::unknown));
      EOS_ASSERT(v < to_index(msg_type_t::unknown), plugin_exception, "Invalid net_message index: ${v}", ("v", v));
      return static_cast<msg_type_t>(v);
   }

} // namespace eosio

FC_REFLECT( eosio::select_ids<fc::sha256>, (mode)(pending)(ids) )
FC_REFLECT( eosio::chain_size_message,
            (last_irreversible_block_num)(last_irreversible_block_id)
            (head_num)(head_id))
FC_REFLECT( eosio::handshake_message,
            (network_version)(chain_id)(node_id)(key)
            (time)(token)(sig)(p2p_address)
            (fork_db_root_num)(fork_db_root_id)
            (fork_db_head_num)(fork_db_head_id)
            (os)(agent)(generation) )
FC_REFLECT( eosio::go_away_message, (reason)(node_id) )
FC_REFLECT( eosio::time_message, (org)(rec)(xmt)(dst) )
FC_REFLECT( eosio::notice_message, (known_trx)(known_blocks) )
FC_REFLECT( eosio::request_message, (req_trx)(req_blocks) )
FC_REFLECT( eosio::sync_request_message, (start_block)(end_block) )
FC_REFLECT( eosio::block_nack_message, (id) )
FC_REFLECT( eosio::block_notice_message, (previous)(id) )
FC_REFLECT( eosio::transaction_notice_message, (id) )
FC_REFLECT( eosio::gossip_bp_peers_message::bp_peer_info_v1, (server_endpoint)(outbound_ip_address)(expiration) )
FC_REFLECT( eosio::gossip_bp_peers_message::bp_peer, (version)(producer_name)(bp_peer_info) )
FC_REFLECT_DERIVED(eosio::gossip_bp_peers_message::signed_bp_peer, (eosio::gossip_bp_peers_message::bp_peer), (sig) )
FC_REFLECT( eosio::gossip_bp_peers_message, (peers) )

/**
 *
Goals of Network Code
1. low latency to minimize missed blocks and potentially reduce block interval
2. minimize redundant data between blocks and transactions.
3. enable rapid sync of a new node
4. update to new boost / fc



State:
   All nodes know which blocks and transactions they have
   All nodes know which blocks and transactions their peers have
   A node knows which blocks and transactions it has requested
   All nodes know when they learned of a transaction

   send hello message
   write loop (true)
      if peer knows the last irreversible block {
         if peer does not know you know a block or transactions
            send the ids you know (so they don't send it to you)
            yield continue
         if peer does not know about a block
            send transactions in block peer doesn't know then send block summary
            yield continue
         if peer does not know about new public endpoints that you have verified
            relay new endpoints to peer
            yield continue
         if peer does not know about transactions
            sends the oldest transactions that is not known by the remote peer
            yield continue
         wait for new validated block, transaction, or peer signal from network fiber
      } else {
         we assume peer is in sync mode in which case it is operating on a
         request / response basis

         wait for notice of sync from the read loop
      }


    read loop
      if hello message
         verify that peers Last Ir Block is in our state or disconnect, they are on fork
         verify peer network protocol

      if notice message update list of transactions known by remote peer
      if trx message then insert into global state as unvalidated
      if blk summary message then insert into global state *if* we know of all dependent transactions
         else close connection


    if my head block < the LIB of a peer and my head block age > block interval * round_size/2 then
    enter sync mode...
        divide the block numbers you need to fetch among peers and send fetch request
        if peer does not respond to request in a timely manner then make request to another peer
        ensure that there is a constant queue of requests in flight and everytime a request is filled
        send of another request.

     Once you have caught up to all peers, notify all peers of your head block so they know that you
     know the LIB and will start sending you real time transactions

parallel fetches, request in groups


only relay transactions to peers if we don't already know about it.

send a notification rather than a transaction if the txn is > 3mtu size.





*/
