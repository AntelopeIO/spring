#pragma once

#include <eosio/chain/action.hpp>
#include <eosio/chain/action_receipt.hpp>
#include <eosio/chain/block.hpp>

namespace eosio::chain {

   struct account_delta {
      account_delta( const account_name& n, int64_t d):account(n),delta(d){}
      account_delta(){}

      account_name account;
      int64_t delta = 0;

      friend bool operator<( const account_delta& lhs, const account_delta& rhs ) { return lhs.account < rhs.account; }
   };

   struct transaction_trace;
   using transaction_trace_ptr = std::shared_ptr<transaction_trace>;

   struct action_trace {
      action_trace(  const transaction_trace& trace, const action& act, account_name receiver, bool context_free,
                     uint32_t action_ordinal, uint32_t creator_action_ordinal,
                     uint32_t closest_unnotified_ancestor_action_ordinal );
      action_trace(  const transaction_trace& trace, action&& act, account_name receiver, bool context_free,
                     uint32_t action_ordinal, uint32_t creator_action_ordinal,
                     uint32_t closest_unnotified_ancestor_action_ordinal );
      action_trace(){}

      fc::unsigned_int                action_ordinal;
      fc::unsigned_int                creator_action_ordinal;
      fc::unsigned_int                closest_unnotified_ancestor_action_ordinal;
      std::optional<action_receipt>   receipt;
      action_name                     receiver;
      action                          act;
      bool                            context_free = false;
      fc::microseconds                elapsed;
      string                          console;
      transaction_id_type             trx_id; ///< the transaction that generated this action
      uint32_t                        block_num = 0;
      block_timestamp_type            block_time;
      std::optional<block_id_type>    producer_block_id;
      flat_set<account_delta>         account_ram_deltas;
      std::optional<fc::exception>    except;
      std::optional<uint64_t>         error_code;
      std::vector<char>               return_value;

      //savanna_witness_hash can be computed separately, since it is not relevant to IBC action proofs
      digest_type savanna_witness_hash() const {
         assert(!!receipt);
         const action_receipt& r = *receipt;

         digest_type::encoder e;
         fc::raw::pack(e, r.global_sequence);
         fc::raw::pack(e, r.auth_sequence);
         fc::raw::pack(e, r.code_sequence);
         fc::raw::pack(e, r.abi_sequence);
   
         return e.result();
      }

      digest_type digest_savanna() const {
         assert(!!receipt);
         const action_receipt& r = *receipt;

         digest_type::encoder e;
         fc::raw::pack(e, r.receiver);
         fc::raw::pack(e, r.recv_sequence);
         fc::raw::pack(e, act.account);
         fc::raw::pack(e, act.name);
         fc::raw::pack(e, r.act_digest);
         fc::raw::pack(e, savanna_witness_hash());

         return e.result();
      }

      digest_type digest_legacy()const {
         assert(!!receipt);
         const action_receipt& r = *receipt;

         digest_type::encoder e;
         fc::raw::pack(e, r.receiver);
         fc::raw::pack(e, r.act_digest);
         fc::raw::pack(e, r.global_sequence);
         fc::raw::pack(e, r.recv_sequence);
         fc::raw::pack(e, r.auth_sequence);
         fc::raw::pack(e, r.code_sequence);
         fc::raw::pack(e, r.abi_sequence);
         return e.result();
      }
   };

   struct transaction_trace {
      transaction_id_type                        id;
      uint32_t                                   block_num = 0;
      block_timestamp_type                       block_time;
      std::optional<block_id_type>               producer_block_id;
      std::optional<transaction_receipt_header>  receipt;
      fc::microseconds                           elapsed{};
      uint64_t                                   net_usage = 0;
      bool                                       scheduled = false;
      vector<action_trace>                       action_traces;
      std::optional<account_delta>               account_ram_delta;

      transaction_trace_ptr                      failed_dtrx_trace;
      std::optional<fc::exception>               except;
      std::optional<uint64_t>                    error_code;
      std::exception_ptr                         except_ptr;
   };

   /**
    * Deduce if transaction_trace is the trace of an onblock system transaction
    */
   inline bool is_onblock( const transaction_trace& tt ) {
      if (tt.action_traces.empty())
         return false;
      const auto& act = tt.action_traces[0].act;
      if (act.account != eosio::chain::config::system_account_name || act.name != "onblock"_n ||
          act.authorization.size() != 1)
         return false;
      const auto& auth = act.authorization[0];
      return auth.actor == eosio::chain::config::system_account_name &&
             auth.permission == eosio::chain::config::active_name;
   }

}  /// namespace eosio::chain

FC_REFLECT( eosio::chain::account_delta,
            (account)(delta) )

FC_REFLECT( eosio::chain::action_trace,
               (action_ordinal)(creator_action_ordinal)(closest_unnotified_ancestor_action_ordinal)(receipt)
               (receiver)(act)(context_free)(elapsed)(console)(trx_id)(block_num)(block_time)
               (producer_block_id)(account_ram_deltas)(except)(error_code)(return_value) )

// @ignore except_ptr
FC_REFLECT( eosio::chain::transaction_trace, (id)(block_num)(block_time)(producer_block_id)
                                             (receipt)(elapsed)(net_usage)(scheduled)
                                             (action_traces)(account_ram_delta)(failed_dtrx_trace)(except)(error_code) )
