#include <eosio/chain_plugin/account_query_db.hpp>

#include <eosio/chain/contract_types.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/permission_object.hpp>

#include <fc/crypto/base64.hpp>
#include <fc/io/raw.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/member.hpp>

#include <boost/bimap.hpp>
#include <boost/bimap/multiset_of.hpp>
#include <boost/bimap/set_of.hpp>

#include <algorithm>
#include <shared_mutex>

using namespace eosio;
using namespace eosio::chain::literals;
using namespace boost::multi_index;
using namespace boost::bimaps;

namespace eosio::chain_apis {
   struct account_query_cursor {
      static constexpr uint8_t current_version = 1;

      uint8_t version = current_version;
      std::variant<chain::permission_level, chain::public_key_type> input;
      std::optional<chain::permission_level> last_authorizing_account;
      chain::weight_type weight = 0;
      chain::name owner;
      chain::name permission;
   };
}

FC_REFLECT( eosio::chain_apis::account_query_cursor,
            (version)(input)(last_authorizing_account)(weight)(owner)(permission) )

namespace {
   /**
    * Structure to hold indirect reference to a `property_object` via {owner,name} as well as a non-standard
    * index over `last_updated_height` (which is truncated at the LIB during initialization) for roll-back support
    */
   struct permission_info {
      // indexed data
      chain::name    owner;
      chain::name    name;
      uint32_t       last_updated_height;

      // un-indexed data
      uint32_t       threshold;

      using cref = std::reference_wrapper<const permission_info>;
   };

   struct by_owner_name;
   struct by_last_updated_height;

   /**
    * Multi-index providing fast lookup for {owner,name} as well as {last_updated_height}
    */
   using permission_info_index_t = multi_index_container<
      permission_info,
      indexed_by<
         ordered_unique<
            tag<by_owner_name>,
            composite_key<permission_info,
               member<permission_info, chain::name, &permission_info::owner>,
               member<permission_info, chain::name, &permission_info::name>
            >
         >,
         ordered_non_unique<
            tag<by_last_updated_height>,
            member<permission_info, uint32_t, &permission_info::last_updated_height>
         >
      >
   >;

   /**
    * Utility function to identify on-block action
    * @param p
    * @return
    */
   bool is_onblock(const chain::transaction_trace_ptr& p) {
      if (p->action_traces.empty())
         return false;
      const auto& act = p->action_traces[0].act;
      if (act.account != eosio::chain::config::system_account_name || act.name != "onblock"_n ||
          act.authorization.size() != 1)
         return false;
      const auto& auth = act.authorization[0];
      return auth.actor == eosio::chain::config::system_account_name &&
             auth.permission == eosio::chain::config::active_name;
   }

   template<typename T>
   struct weighted {
      T                   value;
      chain::weight_type  weight;
      chain::name         owner;
      chain::name         permission;

      static weighted lower_bound_for( const T& value ) {
         return {value, std::numeric_limits<chain::weight_type>::min(), {}, {}};
      }

      static weighted upper_bound_for( const T& value ) {
         const auto max_name = chain::name{std::numeric_limits<uint64_t>::max()};
         return {value, std::numeric_limits<chain::weight_type>::max(), max_name, max_name};
      }
   };

   template<typename Output, typename Input>
   auto make_optional_authorizer(const Input& authorizer) -> std::optional<Output> {
      if constexpr (std::is_same_v<Input, Output>) {
         return authorizer;
      } else {
         return {};
      }
   }

   std::string encode_cursor( const eosio::chain_apis::account_query_cursor& cursor ) {
      const auto packed = fc::raw::pack(cursor);
      return fc::base64url_encode(packed.data(), packed.size());
   }

   eosio::chain_apis::account_query_cursor decode_cursor( const std::string& encoded ) {
      try {
         auto cursor = fc::raw::unpack<eosio::chain_apis::account_query_cursor>(fc::base64url_decode(encoded));
         EOS_ASSERT(cursor.version == eosio::chain_apis::account_query_cursor::current_version,
                    eosio::chain::invalid_http_request, "Unsupported cursor version");
         return cursor;
      } catch (...) {
         EOS_THROW(eosio::chain::invalid_http_request, "Invalid cursor");
      }
   }
}

namespace std {
   /**
    * support for using `permission_info::cref` in ordered containers
    */
   template<>
   struct less<permission_info::cref> {
      bool operator()( const permission_info::cref& lhs, const permission_info::cref& rhs ) const {
         return std::uintptr_t(&lhs.get()) < std::uintptr_t(&rhs.get());
      }
   };

   /**
    * support for using `weighted<T>` in ordered containers
    */
   template<typename T>
   struct less<weighted<T>> {
      bool operator()( const weighted<T>& lhs, const weighted<T>& rhs ) const {
         return std::tie(lhs.value, lhs.weight, lhs.owner, lhs.permission) <
                std::tie(rhs.value, rhs.weight, rhs.owner, rhs.permission);
      }
   };

}

namespace eosio::chain_apis {
   /**
    * Implementation details of the account query DB
    */
   struct account_query_db_impl {
      account_query_db_impl(const chain::controller& controller)
      :controller(controller)
      {}

      /**
       * Build the initial database from the chain controller by extracting the information contained in the
       * blockchain state at the current HEAD
       */
      void build_account_query_map() {
         std::unique_lock write_lock(rw_mutex);

         ilog("Building account query DB");
         auto start = fc::time_point::now();
         const auto& index = controller.db().get_index<chain::permission_index>().indices().get<by_id>();

         // build an initial time to block number map
         const auto fork_db_root_num = controller.fork_db_root().block_num();
         const auto head_num         = controller.head().block_num();

         for (uint32_t block_num = fork_db_root_num + 1; block_num <= head_num; block_num++) {
            const auto block_p = controller.fetch_block_by_number(block_num);
            EOS_ASSERT(block_p, chain::plugin_exception, "cannot fetch reversible block ${block_num}, required for account_db initialization", ("block_num", block_num));
            time_to_block_num.emplace(block_p->timestamp.to_time_point(), block_num);
         }

         for (const auto& po : index ) {
            uint32_t last_updated_height = last_updated_time_to_height(po.last_updated);
            const auto& pi = permission_info_index.emplace( permission_info{ po.owner, po.name, last_updated_height, po.auth.threshold } ).first;
            add_to_bimaps(*pi, po);
         }
         auto duration = fc::time_point::now() - start;
         ilog("Finished building account query DB in ${sec}", ("sec", (duration.count() / 1'000'000.0 )));
      }

      /**
       * Add a permission to the bimaps for keys and accounts
       * @param pi - the ephemeral permission info structure being added
       * @param po - the chain data associted with this permission
       */
      void add_to_bimaps( const permission_info& pi, const chain::permission_object& po ) {
         // For each account, add this permission info's non-owning reference to the bimap for accounts
         for (const auto& a : po.auth.accounts) {
            name_bimap.insert(name_bimap_t::value_type {{a.permission, a.weight, pi.owner, pi.name}, pi});
         }

         // for each key, add this permission info's non-owning reference to the bimap for keys
         for (const auto& k: po.auth.keys) {
            key_bimap.insert(key_bimap_t::value_type {{k.key.to_public_key(), k.weight, pi.owner, pi.name}, pi});
         }
      }

      /**
       * Remove a permission from the bimaps for keys and accounts
       * @param pi - the ephemeral permission info structure being removed
       */
      void remove_from_bimaps( const permission_info& pi ) {
         // remove all entries from the name bimap that refer to this permission_info's reference
         const auto name_range = name_bimap.right.equal_range(pi);
         name_bimap.right.erase(name_range.first, name_range.second);

         // remove all entries from the key bimap that refer to this permission_info's reference
         const auto key_range = key_bimap.right.equal_range(pi);
         key_bimap.right.erase(key_range.first, key_range.second);
      }

      bool is_rollback_required( const chain::signed_block_ptr& block ) const {
         std::shared_lock read_lock(rw_mutex);
         const auto bnum = block->block_num();
         const auto& index = permission_info_index.get<by_last_updated_height>();

         if (index.empty()) {
            return false;
         } else {
            const auto& pi = (*index.rbegin());
            if (pi.last_updated_height < bnum) {
               return false;
            }
         }

         return true;
      }

      uint32_t last_updated_time_to_height( const fc::time_point& last_updated) {
         const auto fork_db_root = controller.fork_db_root();
         const auto fork_db_root_num  = fork_db_root.block_num();
         const auto fork_db_root_time = fork_db_root.block_time();

         uint32_t last_updated_height = fork_db_root_num;
         if (last_updated > fork_db_root_time) {
            const auto iter = time_to_block_num.find(last_updated);
            EOS_ASSERT(iter != time_to_block_num.end(), chain::plugin_exception, "invalid block time encountered in on-chain accounts ${time}", ("time", last_updated));
            last_updated_height = iter->second;
         }

         return last_updated_height;
      }

      /**
       * Given a block number, remove all permissions that were last updated at or after that block number
       * this will effectively roll back the database to just before the incoming block
       *
       * For each removed entry, this will create a new entry if there exists an equivalent {owner, name} permission
       * at the HEAD state of the chain.
       * @param block - the block to rollback before
       */
      void rollback_to_before( const chain::signed_block_ptr& block ) {
         const auto bnum = block->block_num();
         auto& index = permission_info_index.get<by_last_updated_height>();
         const auto& permission_by_owner = controller.db().get_index<chain::permission_index>().indices().get<chain::by_owner>();

         // roll back time-map
         auto time_iter = time_to_block_num.rbegin();
         while (time_iter != time_to_block_num.rend() && time_iter->second >= bnum) {
            time_iter = decltype(time_iter){time_to_block_num.erase( std::next(time_iter).base() )};
         }

         auto curr_iter = index.rbegin();
         while (!index.empty()) {
            if (curr_iter == index.rend()) {
               break;
            }

            const auto& pi = (*curr_iter);
            if (pi.last_updated_height < bnum) {
               break;
            }

            // remove this entry from the bimaps
            remove_from_bimaps(pi);

            auto itr = permission_by_owner.find(std::make_tuple(pi.owner, pi.name));
            if (itr == permission_by_owner.end()) {
               // this permission does not exist at this point in the chains history
               curr_iter = decltype(curr_iter)( index.erase(index.iterator_to(pi)) );
            } else {
               const auto& po = *itr;

               uint32_t last_updated_height = chain::block_timestamp_type(po.last_updated) == block->timestamp ?
                  bnum : last_updated_time_to_height(po.last_updated);

               index.modify(index.iterator_to(pi), [&po, last_updated_height](auto& mutable_pi) {
                  mutable_pi.last_updated_height = last_updated_height;
                  mutable_pi.threshold = po.auth.threshold;
               });
               add_to_bimaps(pi, po);
               ++curr_iter;
            }
         }
      }

      /**
       * Store a potentially relevant transaction trace in a short lived cache so that it can be processed if its
       * committed to by a block
       * @param trace
       */
      void cache_transaction_trace( const chain::transaction_trace_ptr& trace ) {
         if( !trace->receipt ) return;
         // include only executed transactions; soft_fail included so that onerror (and any inlines via onerror) are included
         if((trace->receipt->status != chain::transaction_receipt_header::executed &&
             trace->receipt->status != chain::transaction_receipt_header::soft_fail)) {
            return;
         }
         if( is_onblock( trace )) {
            onblock_trace.emplace( trace );
         } else if( trace->failed_dtrx_trace ) {
            cached_trace_map[trace->failed_dtrx_trace->id] = trace;
         } else {
            cached_trace_map[trace->id] = trace;
         }
      }

      using permission_set_t = std::set<chain::permission_level>;
      /**
       * Pre-Commit step with const qualifier to guarantee it does not mutate
       * the thread-safe data set
       * @param block
       */
      auto commit_block_prelock( const chain::signed_block_ptr& block ) const {
         permission_set_t updated;
         permission_set_t deleted;

         /**
          * process traces to find `updateauth` and `deleteauth` calls maintaining a final set of
          * permissions to either update or delete.  Intra-block changes are discarded
          */
         auto process_trace = [&](const chain::transaction_trace_ptr& trace) {
            for( const auto& at : trace->action_traces ) {
               if (std::tie(at.receiver, at.act.account) != std::tie(chain::config::system_account_name,chain::config::system_account_name)) {
                  continue;
               }

               if (at.act.name == chain::updateauth::get_name()) {
                  auto data = at.act.data_as<chain::updateauth>();
                  auto itr = updated.emplace(chain::permission_level{data.account, data.permission}).first;
                  deleted.erase(*itr);
               } else if (at.act.name == chain::deleteauth::get_name()) {
                  auto data = at.act.data_as<chain::deleteauth>();
                  auto itr = deleted.emplace(chain::permission_level{data.account, data.permission}).first;
                  updated.erase(*itr);
               } else if (at.act.name == chain::newaccount::get_name()) {
                  auto data = at.act.data_as<chain::newaccount>();
                  updated.emplace(chain::permission_level{data.name, "owner"_n});
                  updated.emplace(chain::permission_level{data.name, "active"_n});
               }
            }
         };

         if( onblock_trace )
            process_trace(*onblock_trace);

         for( const auto& r : block->transactions ) {
            chain::transaction_id_type id;
            if( std::holds_alternative<chain::transaction_id_type>(r.trx)) {
               id = std::get<chain::transaction_id_type>(r.trx);
            } else {
               id = std::get<chain::packed_transaction>(r.trx).id();
            }

            const auto it = cached_trace_map.find( id );
            if( it != cached_trace_map.end() ) {
               process_trace( it->second );
            }
         }

         return std::make_tuple(std::move(updated), std::move(deleted), is_rollback_required(block));
      }

      /**
       * Commit a block of transactions to the account query DB
       * transaction traces need to be in the cache prior to this call
       * @param block
       */
      void commit_block(const chain::signed_block_ptr& block) {
         permission_set_t updated;
         permission_set_t deleted;
         bool rollback_required = false;

         std::tie(updated, deleted, rollback_required) = commit_block_prelock(block);

         // optimistic skip of locking section if there is nothing to do
         if (!updated.empty() || !deleted.empty() || rollback_required) {
            std::unique_lock write_lock(rw_mutex);
            auto block_num = block->block_num();

            rollback_to_before(block);

            // insert this blocks time into the time map
            time_to_block_num.emplace(block->timestamp, block_num);

            const auto bnum = block_num;
            auto& index = permission_info_index.get<by_owner_name>();
            const auto& permission_by_owner = controller.db().get_index<chain::permission_index>().indices().get<chain::by_owner>();

            // for each updated permission, find the new values and update the account query db
            for (const auto& up: updated) {
               auto key = std::make_tuple(up.actor, up.permission);
               auto source_itr = permission_by_owner.find(key);
               EOS_ASSERT(source_itr != permission_by_owner.end(), chain::plugin_exception, "chain data is missing");
               auto itr = index.find(key);
               if (itr == index.end()) {
                  const auto& po = *source_itr;
                  itr = index.emplace(permission_info{ po.owner, po.name, bnum, po.auth.threshold }).first;
               } else {
                  remove_from_bimaps(*itr);
                  index.modify(itr, [&](auto& mutable_pi){
                     mutable_pi.last_updated_height = bnum;
                     mutable_pi.threshold = source_itr->auth.threshold;
                  });
               }

               add_to_bimaps(*itr, *source_itr);
            }

            // for all deleted permissions, process their removal from the account query DB
            for (const auto& dp: deleted) {
               auto key = std::make_tuple(dp.actor, dp.permission);
               auto itr = index.find(key);
               if (itr != index.end()) {
                  remove_from_bimaps(*itr);
                  index.erase(itr);
               }
            }
         }

         // drop any unprocessed cached traces
         cached_trace_map.clear();
         onblock_trace.reset();
      }

      account_query_db::get_accounts_by_authorizers_result
      get_accounts_by_authorizers( const account_query_db::get_accounts_by_authorizers_params& args,
                                  const fc::time_point& deadline) const {
         std::shared_lock read_lock(rw_mutex);

         using result_t = account_query_db::get_accounts_by_authorizers_result;
         result_t result;
         const auto limit = std::min(args.limit, account_query_db::max_results);

         // deduplicate inputs
         std::set<chain::permission_level> account_set;
         for (const auto& account : args.accounts) {
            FC_CHECK_DEADLINE(deadline);
            account_set.emplace(account);
         }

         std::set<chain::public_key_type> key_set;
         for (const auto& key : args.keys) {
            FC_CHECK_DEADLINE(deadline);
            key_set.emplace(key);
         }

         std::optional<account_query_cursor> cursor;
         if (!args.cursor.empty()) {
            cursor = decode_cursor(args.cursor);
         }

         auto account_itr = account_set.begin();
         auto key_itr = key_set.begin();
         bool resume_account = false;
         bool resume_key = false;

         if (cursor) {
            if (std::holds_alternative<chain::permission_level>(cursor->input)) {
               const auto& input = std::get<chain::permission_level>(cursor->input);
               account_itr = account_set.find(input);
               EOS_ASSERT(account_itr != account_set.end(), chain::invalid_http_request,
                          "Cursor account is not present in the request");
               EOS_ASSERT(cursor->last_authorizing_account.has_value(), chain::invalid_http_request,
                          "Invalid account cursor");
               const auto& last_authorizer = *cursor->last_authorizing_account;
               EOS_ASSERT(last_authorizer.actor == input.actor &&
                          (input.permission.empty() || last_authorizer.permission == input.permission),
                          chain::invalid_http_request, "Invalid account cursor");
               resume_account = true;
            } else {
               const auto& input = std::get<chain::public_key_type>(cursor->input);
               key_itr = key_set.find(input);
               EOS_ASSERT(key_itr != key_set.end(), chain::invalid_http_request,
                          "Cursor key is not present in the request");
               account_itr = account_set.end();
               resume_key = true;
            }
         }

         std::string last_cursor;

         /**
          * Add a range of results
          */
         auto push_results = [&result, &last_cursor, limit, &deadline](const auto& begin, const auto& end,
                                                                      const auto& input) {
            FC_CHECK_DEADLINE(deadline);
            auto itr = begin;
            for (; itr != end && result.accounts.size() < limit; ++itr) {
               FC_CHECK_DEADLINE(deadline);
               const auto& pi = itr->second.get();
               const auto& authorizer = itr->first.value;
               auto weight = itr->first.weight;

               result.accounts.emplace_back(result_t::account_result{
                     pi.owner,
                     pi.name,
                     make_optional_authorizer<chain::permission_level>(authorizer),
                     make_optional_authorizer<chain::public_key_type>(authorizer),
                     weight,
                     pi.threshold
               });

               account_query_cursor next;
               next.input = input;
               if constexpr (std::is_same_v<std::decay_t<decltype(input)>, chain::permission_level>) {
                  next.last_authorizing_account = authorizer;
               }
               next.weight = weight;
               next.owner = pi.owner;
               next.permission = pi.name;
               last_cursor = encode_cursor(next);
            }
            if (itr != end) {
               result.more = true;
               result.next_cursor = last_cursor;
               return false;
            }
            return true;
         };

         for (; account_itr != account_set.end(); ++account_itr) {
            FC_CHECK_DEADLINE(deadline);
            const auto& a = *account_itr;
            if (a.permission.empty()) {
               // empty permission is a wildcard
               // construct a range between the lower bound of the given account and the lower bound of the
               // next possible account name
               auto begin = name_bimap.left.lower_bound(weighted<chain::permission_level>::lower_bound_for({a.actor, a.permission}));
               const auto next_account_name = chain::name(a.actor.to_uint64_t() + 1);
               const auto end = name_bimap.left.lower_bound(weighted<chain::permission_level>::lower_bound_for({next_account_name, a.permission}));
               if (resume_account) {
                  const auto& last_authorizer = *cursor->last_authorizing_account;
                  begin = name_bimap.left.upper_bound(weighted<chain::permission_level>{
                        last_authorizer, cursor->weight, cursor->owner, cursor->permission});
                  resume_account = false;
               }
               if (!push_results(begin, end, a))
                  return result;
            } else {
               // construct a range of all possible weights for an account/permission pair
               const auto p = chain::permission_level{a.actor, a.permission};
               auto begin = name_bimap.left.lower_bound(weighted<chain::permission_level>::lower_bound_for(p));
               const auto end = name_bimap.left.upper_bound(weighted<chain::permission_level>::upper_bound_for(p));
               if (resume_account) {
                  const auto& last_authorizer = *cursor->last_authorizing_account;
                  begin = name_bimap.left.upper_bound(weighted<chain::permission_level>{
                        last_authorizer, cursor->weight, cursor->owner, cursor->permission});
                  resume_account = false;
               }
               if (!push_results(begin, end, a))
                  return result;
            }
         }

         for (; key_itr != key_set.end(); ++key_itr) {
            FC_CHECK_DEADLINE(deadline);
            const auto& k = *key_itr;
            // construct a range of all possible weights for a key
            auto begin = key_bimap.left.lower_bound(weighted<chain::public_key_type>::lower_bound_for(k));
            const auto end = key_bimap.left.upper_bound(weighted<chain::public_key_type>::upper_bound_for(k));
            if (resume_key) {
               begin = key_bimap.left.upper_bound(weighted<chain::public_key_type>{
                     k, cursor->weight, cursor->owner, cursor->permission});
               resume_key = false;
            }
            if (!push_results(begin, end, k))
               return result;
         }

         return result;
      }

      /**
       * Convenience aliases
       */
      using cached_trace_map_t = std::map<chain::transaction_id_type, chain::transaction_trace_ptr>;
      using onblock_trace_t = std::optional<chain::transaction_trace_ptr>;

      const chain::controller&   controller;               ///< the controller to read data from
      cached_trace_map_t         cached_trace_map;         ///< temporary cache of uncommitted traces
      onblock_trace_t            onblock_trace;            ///< temporary cache of on_block trace

      using time_map_t = std::map<fc::time_point, uint32_t>;
      time_map_t                 time_to_block_num;



      using name_bimap_t = bimap<multiset_of<weighted<chain::permission_level>>, multiset_of<permission_info::cref>>;
      using key_bimap_t = bimap<multiset_of<weighted<chain::public_key_type>>, multiset_of<permission_info::cref>>;

      /*
       * The structures below are shared between the writing thread and the reading thread(s) and must be protected
       * by the `rw_mutex`
       */
      permission_info_index_t    permission_info_index;    ///< multi-index that holds ephemeral indices
      name_bimap_t               name_bimap;               ///< many:many bimap of names:permission_infos
      key_bimap_t                key_bimap;                ///< many:many bimap of keys:permission_infos

      mutable std::shared_mutex  rw_mutex;                 ///< mutex for read/write locking on the Multi-index and bimaps
   };

   account_query_db::account_query_db( const chain::controller& controller )
   :_impl(std::make_unique<account_query_db_impl>(controller))
   {
      _impl->build_account_query_map();
   }

   account_query_db::~account_query_db() = default;
   account_query_db & account_query_db::operator=(account_query_db &&) = default;

   void account_query_db::cache_transaction_trace( const chain::transaction_trace_ptr& trace ) {
      try {
         _impl->cache_transaction_trace(trace);
      } FC_LOG_AND_DROP(("ACCOUNT DB cache_transaction_trace ERROR"));
   }

   void account_query_db::commit_block( const chain::signed_block_ptr& block ) {
      try {
         _impl->commit_block(block);
      } FC_LOG_AND_DROP(("ACCOUNT DB commit_block ERROR"));
   }

   account_query_db::get_accounts_by_authorizers_result account_query_db::get_accounts_by_authorizers(
         const account_query_db::get_accounts_by_authorizers_params& args, const fc::time_point& deadline) const {
      return _impl->get_accounts_by_authorizers(args, deadline);
   }

}
