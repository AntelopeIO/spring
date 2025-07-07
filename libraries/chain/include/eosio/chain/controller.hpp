#pragma once
#include <eosio/chain/block_state_legacy.hpp>
#include <eosio/chain/block_state.hpp>
#include <eosio/chain/block_handle.hpp>
#include <eosio/chain/block_log.hpp>
#include <eosio/chain/trace.hpp>
#include <eosio/chain/genesis_state.hpp>
#include <eosio/chain/snapshot.hpp>
#include <eosio/chain/protocol_feature_manager.hpp>
#include <eosio/chain/webassembly/eos-vm-oc/config.hpp>
#include <eosio/chain/vote_message.hpp>
#include <eosio/chain/finalizer.hpp>
#include <eosio/chain/peer_keys_db.hpp>

#include <chainbase/pinnable_mapped_file.hpp>

#include <boost/signals2/signal.hpp>


namespace chainbase {
   class database;
}
namespace boost::asio {
   class thread_pool;
}

namespace savanna_cluster {
   class node_t;
}

namespace eosio::vm { class wasm_allocator; }

namespace eosio::chain {

   struct speculative_block_metrics {
      account_name block_producer{};
      uint32_t     block_num             = 0;
      int64_t      block_total_time_us   = 0;
      int64_t      block_idle_us         = 0;
      std::size_t  num_success_trx       = 0;
      int64_t      success_trx_time_us   = 0;
      std::size_t  num_fail_trx          = 0;
      int64_t      fail_trx_time_us      = 0;
      std::size_t  num_transient_trx     = 0;
      int64_t      transient_trx_time_us = 0;
      int64_t      block_other_time_us   = 0;
   };

   struct produced_block_metrics : public speculative_block_metrics {
      std::size_t unapplied_transactions_total       = 0;
      std::size_t subjective_bill_account_size_total = 0;
      std::size_t scheduled_trxs_total               = 0;
      std::size_t trxs_produced_total                = 0;
      uint64_t    cpu_usage_us                       = 0;
      int64_t     total_elapsed_time_us              = 0;
      int64_t     total_time_us                      = 0;
      uint64_t    net_usage_us                       = 0;

      uint32_t last_irreversible = 0;
      uint32_t head_block_num    = 0;
   };

   struct incoming_block_metrics {
      std::size_t trxs_incoming_total   = 0;
      uint64_t    cpu_usage_us          = 0;
      int64_t     total_elapsed_time_us = 0;
      int64_t     total_time_us         = 0;
      uint64_t    net_usage_us          = 0;
      int64_t     block_latency_us      = 0;

      uint32_t last_irreversible = 0;
      uint32_t head_block_num    = 0;
   };

   using bls_pub_priv_key_map_t = std::map<std::string, std::string>;
   struct finalizer_policy;

   class authorization_manager;

   namespace resource_limits {
      class resource_limits_manager;
   };

   struct controller_impl;
   using chainbase::database;
   using chainbase::pinnable_mapped_file;
   using boost::signals2::signal;

   class transaction_context;
   struct trx_block_context;
   class dynamic_global_property_object;
   class global_property_object;
   class permission_object;
   class account_object;
   class deep_mind_handler;
   class subjective_billing;
   using resource_limits::resource_limits_manager;
   using apply_handler = std::function<void(apply_context&)>;

   enum class fork_db_add_t;
   using forked_callback_t = std::function<void(const transaction_metadata_ptr&)>;

   // lookup transaction_metadata via supplied function to avoid re-creation
   using trx_meta_cache_lookup = std::function<transaction_metadata_ptr( const transaction_id_type&)>;

   using block_signal_params = std::tuple<const signed_block_ptr&, const block_id_type&>;
   using vote_signal_params =
      std::tuple<uint32_t,                         // connection_id
                 vote_result_t,                    // vote result status
                 const vote_message_ptr&,          // vote_message processed
                 const finalizer_authority_ptr&,   // active authority that voted  (nullptr if vote for pending or error)
                 const finalizer_authority_ptr&>;  // pending authority that voted (nullptr if no pending finalizer policy)
   using vote_signal_t = signal<void(const vote_signal_params&)>;

   enum class db_read_mode {
      HEAD,
      IRREVERSIBLE,
      SPECULATIVE
   };

   enum class validation_mode {
      FULL,
      LIGHT
   };

   class controller {
      public:
         struct config {
            flat_set<account_name>   sender_bypass_whiteblacklist;
            flat_set<account_name>   actor_whitelist;
            flat_set<account_name>   actor_blacklist;
            flat_set<account_name>   contract_whitelist;
            flat_set<account_name>   contract_blacklist;
            flat_set< pair<account_name, action_name> > action_blacklist;
            flat_set<public_key_type> key_blacklist;
            path                     finalizers_dir         =  chain::config::default_finalizers_dir_name;
            path                     blocks_dir             =  chain::config::default_blocks_dir_name;
            block_log_config         blog;
            path                     state_dir              =  chain::config::default_state_dir_name;
            uint64_t                 state_size             =  chain::config::default_state_size;
            uint64_t                 state_guard_size       =  chain::config::default_state_guard_size;
            uint32_t                 sig_cpu_bill_pct       =  chain::config::default_sig_cpu_bill_pct;
            uint16_t                 chain_thread_pool_size =  chain::config::default_controller_thread_pool_size;
            uint16_t                 vote_thread_pool_size  =  0;
            bool                     read_only              =  false;
            bool                     force_all_checks       =  false;
            bool                     disable_replay_opts    =  false;
            bool                     contracts_console      =  false;
            bool                     allow_ram_billing_in_notify = false;
            uint32_t                 maximum_variable_signature_length = chain::config::default_max_variable_signature_length;
            bool                     disable_all_subjective_mitigations = false; //< for developer & testing purposes, can be configured using `disable-all-subjective-mitigations` when `EOSIO_DEVELOPER` build option is provided
            uint32_t                 terminate_at_block     = 0;
            uint32_t                 truncate_at_block      = 0;
            uint32_t                 num_configured_p2p_peers = 0;
            bool                     integrity_hash_on_start= false;
            bool                     integrity_hash_on_stop = false;

            wasm_interface::vm_type  wasm_runtime = chain::config::default_wasm_runtime;
            eosvmoc::config          eosvmoc_config;
            wasm_interface::vm_oc_enable eosvmoc_tierup     = wasm_interface::vm_oc_enable::oc_auto;
            flat_set<account_name>   eos_vm_oc_whitelist_suffixes;

            db_read_mode             read_mode              = db_read_mode::HEAD;
            validation_mode          block_validation_mode  = validation_mode::FULL;

            pinnable_mapped_file::map_mode db_map_mode      = pinnable_mapped_file::map_mode::mapped;

            flat_set<account_name>   resource_greylist;
            flat_set<account_name>   trusted_producers;
            uint32_t                 greylist_limit         = chain::config::maximum_elastic_resource_multiplier;

            flat_set<account_name>   profile_accounts;
         };

         enum class block_status {
            irreversible = 0, ///< this block has already been applied before by this node and is considered irreversible
            validated   = 1, ///< this is a complete block signed by a valid producer and has been previously applied by this node and therefore validated but it is not yet irreversible
            complete   = 2, ///< this is a complete block signed by a valid producer but is not yet irreversible nor has it yet been applied by this node
            incomplete  = 3, ///< this is an incomplete block being produced by a producer
            ephemeral = 4  ///< this is an incomplete block created for speculative execution of trxs, will always be aborted
         };

         controller( const config& cfg, const chain_id_type& chain_id );
         controller( const config& cfg, protocol_feature_set&& pfs, const chain_id_type& chain_id );
         ~controller();

         void add_indices();
         void startup( std::function<void()> shutdown, std::function<bool()> check_shutdown, const snapshot_reader_ptr& snapshot);
         void startup( std::function<void()> shutdown, std::function<bool()> check_shutdown, const genesis_state& genesis);
         void startup( std::function<void()> shutdown, std::function<bool()> check_shutdown);

         void preactivate_feature( const digest_type& feature_digest, bool is_trx_transient );

         vector<digest_type> get_preactivated_protocol_features()const;

         void validate_protocol_features( const vector<digest_type>& features_to_activate )const;

         /**
          * Starts a new pending block session upon which new transactions can be pushed.
          * returns the trace for the on_block action
          */
         transaction_trace_ptr start_block( block_timestamp_type time,
                                            uint16_t confirm_block_count,
                                            const vector<digest_type>& new_protocol_feature_activations,
                                            block_status bs,
                                            const fc::time_point& deadline = fc::time_point::maximum() );

         /**
          * @return transactions applied in aborted block
          */
         deque<transaction_metadata_ptr> abort_block();

         /// Expected to be called from signal handler or producer_plugin
         enum class interrupt_t { all_trx, apply_block_trx, speculative_block_trx };
         void interrupt_transaction(interrupt_t interrupt);

       /**
        *
        */
         transaction_trace_ptr push_transaction( const transaction_metadata_ptr& trx,
                                                 fc::time_point deadline, fc::microseconds max_transaction_time,
                                                 uint32_t billed_cpu_time_us, bool explicit_billed_cpu_time,
                                                 int64_t subjective_cpu_bill_us );

         /**
          * Attempt to execute a specific transaction in our deferred trx database
          *
          */
         transaction_trace_ptr push_scheduled_transaction( const transaction_id_type& scheduled,
                                                           uint32_t billed_cpu_time_us, bool explicit_billed_cpu_time );

         void assemble_and_complete_block( const signer_callback_type& signer_callback );
         void sign_block( const signer_callback_type& signer_callback );
         void commit_block();
         void testing_allow_voting(bool val);
         bool get_testing_allow_voting_flag();
         void set_async_voting(async_t val);
         void set_async_aggregation(async_t val);

         struct accepted_block_result {
            const fork_db_add_t add_result;
            std::optional<block_handle> block;   // empty optional if block is unlinkable
         };
         // thread-safe
         accepted_block_result accept_block( const block_id_type& id, const signed_block_ptr& b ) const;

         /// Apply any blocks that are ready from the fork_db
         struct apply_blocks_result_t {
            enum class status_t {
               complete,   // all ready blocks in forkdb have been applied
               incomplete, // time limit reached, additional blocks may be available in forkdb to process
               paused      // apply blocks currently paused
            };
            status_t status = status_t::complete;
            size_t   num_blocks_applied = 0;
         };
         apply_blocks_result_t apply_blocks(const forked_callback_t& cb, const trx_meta_cache_lookup& trx_lookup);

         boost::asio::io_context& get_thread_pool();

         const chainbase::database& db()const;

         const account_object&                 get_account( account_name n )const;
         const global_property_object&         get_global_properties()const;
         const dynamic_global_property_object& get_dynamic_global_properties()const;
         const resource_limits_manager&        get_resource_limits_manager()const;
         resource_limits_manager&              get_mutable_resource_limits_manager();
         const authorization_manager&          get_authorization_manager()const;
         authorization_manager&                get_mutable_authorization_manager();
         const protocol_feature_manager&       get_protocol_feature_manager()const;
         const subjective_billing&             get_subjective_billing()const;
         subjective_billing&                   get_mutable_subjective_billing();

         const flat_set<account_name>&   get_actor_whitelist() const;
         const flat_set<account_name>&   get_actor_blacklist() const;
         const flat_set<account_name>&   get_contract_whitelist() const;
         const flat_set<account_name>&   get_contract_blacklist() const;
         const flat_set< pair<account_name, action_name> >& get_action_blacklist() const;
         const flat_set<public_key_type>& get_key_blacklist() const;

         void   set_actor_whitelist( const flat_set<account_name>& );
         void   set_actor_blacklist( const flat_set<account_name>& );
         void   set_contract_whitelist( const flat_set<account_name>& );
         void   set_contract_blacklist( const flat_set<account_name>& );
         void   set_action_blacklist( const flat_set< pair<account_name, action_name> >& );
         void   set_key_blacklist( const flat_set<public_key_type>& );

         void   set_disable_replay_opts( bool v );

         block_handle         head()const;
         block_handle         fork_db_head()const;

         [[deprecated("Use head().block_num().")]]  uint32_t             head_block_num()const;
         [[deprecated("Use head().block_time().")]] time_point           head_block_time()const;
         [[deprecated("Use head().timestamp().")]]  block_timestamp_type head_block_timestamp()const;
         [[deprecated("Use head().id().")]]         block_id_type        head_block_id()const;
         [[deprecated("Use head().producer().")]]   account_name         head_block_producer()const;
         [[deprecated("Use head().header().")]]     const block_header&  head_block_header()const;
         [[deprecated("Use head().block().")]]      const signed_block_ptr& head_block()const;

         // returns nullptr after instant finality enabled
         block_state_legacy_ptr head_block_state_legacy()const;
         // returns finality_data associated with chain head for SHiP when in Savanna,
         // std::nullopt in Legacy
         std::optional<finality_data_t> head_finality_data() const;

         [[deprecated("Use fork_db_head().block_num().")]] uint32_t      fork_db_head_block_num()const;
         [[deprecated("Use fork_db_head().id().")]]        block_id_type fork_db_head_block_id()const;

         time_point                     pending_block_time()const;
         block_timestamp_type           pending_block_timestamp()const;
         account_name                   pending_block_producer()const;
         const block_signing_authority& pending_block_signing_authority()const;
         std::optional<block_id_type>   pending_producer_block_id()const;
         uint32_t                       pending_block_num()const;

         // returns producer_authority_schedule for a next block built from head with
         // `next_block_timestamp`
         const producer_authority_schedule&         head_active_producers(block_timestamp_type next_block_timestamp)const;
         // active_producers() is legacy and may be deprecated in the future;
         // head_active_producers(block_timestamp_type next_block_timestamp)
         // is preferred.
         const producer_authority_schedule&         active_producers()const;
         const producer_authority_schedule&         head_active_producers()const;
         // pending for pre-instant-finality, next proposed that will take affect, null if none are pending/proposed
         const producer_authority_schedule*         pending_producers()const;
         // post-instant-finality this always returns empty std::optional
         std::optional<producer_authority_schedule> proposed_producers_legacy()const;
         // pre-instant-finality this always returns a valid producer_authority_schedule
         // post-instant-finality this always returns nullptr
         const producer_authority_schedule*         pending_producers_legacy()const;

         finalizer_policy_ptr   head_active_finalizer_policy()const; // returns nullptr pre-savanna
         finalizer_policy_ptr   head_pending_finalizer_policy()const; // returns nullptr pre-savanna

         /// Return the vote metrics for qc.block_num
         /// thread-safe
         /// @param id the block which contains the qc
         /// @param qc the qc from the block which refers to qc.block_num
         qc_vote_metrics_t vote_metrics(const block_id_type& id, const qc_t& qc) const;
         // return qc missing vote's finalizers, use instead of vote_metrics when only missing votes are needed
         // thread-safe
         qc_vote_metrics_t::fin_auth_set_t missing_votes(const block_id_type& id, const qc_t& qc) const;

         // not thread-safe
         bool is_head_descendant_of_pending_lib() const;

         // thread-safe
         void set_savanna_lib_id(const block_id_type& id);

         // thread-safe
         bool         fork_db_has_root() const;
         // thread-safe
         block_handle fork_db_root()const;
         // thread-safe
         size_t       fork_db_size() const;

         // thread-safe, retrieves block according to fork db best branch which can change at any moment
         signed_block_ptr fetch_block_by_number( uint32_t block_num )const;
         // thread-safe
         signed_block_ptr fetch_block_by_id( const block_id_type& id )const;
         // thread-safe, retrieves serialized signed block
         std::vector<char> fetch_serialized_block_by_number( uint32_t block_num)const;
         // thread-safe
         bool block_exists(const block_id_type& id) const;
         bool validated_block_exists(const block_id_type& id) const;
         // thread-safe, retrieves block according to fork db best branch which can change at any moment
         std::optional<signed_block_header> fetch_block_header_by_number( uint32_t block_num )const;
         // thread-safe
         std::optional<signed_block_header> fetch_block_header_by_id( const block_id_type& id )const;
         // thread-safe, retrieves block id according to fork db best branch which can change at any moment
         std::optional<block_id_type> fork_block_id_for_num( uint32_t block_num )const;
         // not thread-safe, retrieves block id according to applied chain head
         std::optional<block_id_type> chain_block_id_for_num( uint32_t block_num )const;
         // thread-safe
         digest_type get_strong_digest_by_id( const block_id_type& id ) const; // used in unittests

         fc::sha256 calculate_integrity_hash();
         void write_snapshot( const snapshot_writer_ptr& snapshot );
         // thread-safe
         bool is_writing_snapshot()const;

         bool sender_avoids_whitelist_blacklist_enforcement( account_name sender )const;
         void check_actor_list( const flat_set<account_name>& actors )const;
         void check_contract_list( account_name code )const;
         void check_action_list( account_name code, action_name action )const;
         void check_key_list( const public_key_type& key )const;
         bool is_building_block()const;
         bool is_speculative_block()const;

         bool is_ram_billing_in_notify_allowed()const;

         //This is only an accessor to the user configured subjective limit: i.e. it does not do a
         // check similar to is_ram_billing_in_notify_allowed() to check if controller is currently
         // producing a block
         uint32_t configured_subjective_signature_length_limit()const;

         void add_resource_greylist(const account_name &name);
         void remove_resource_greylist(const account_name &name);
         bool is_resource_greylisted(const account_name &name) const;
         const flat_set<account_name> &get_resource_greylist() const;

         void validate_expiration( const transaction& t )const;
         void validate_tapos( const transaction& t )const;
         void validate_db_available_size() const;

         bool is_protocol_feature_activated( const digest_type& feature_digest )const;
         bool is_builtin_activated( builtin_protocol_feature_t f )const;

         bool is_known_unexpired_transaction( const transaction_id_type& id) const;

         // called by host function
         int64_t set_proposed_producers( transaction_context& trx_context, vector<producer_authority> producers );

         void apply_trx_block_context( trx_block_context& trx_blk_context );

         // called from net threads
         void process_vote_message( uint32_t connection_id, const vote_message_ptr& msg );
         // thread safe, for testing
         bool is_block_missing_finalizer_votes(const block_handle& bh) const;

         // thread safe, for testing
         std::optional<finalizer_policy> active_finalizer_policy(const block_id_type& id) const;

         bool light_validation_allowed() const;
         bool skip_auth_check()const;
         bool skip_trx_checks()const;
         bool skip_db_sessions()const;
         bool skip_db_sessions( block_status bs )const;
         bool is_trusted_producer( const account_name& producer) const;

         bool contracts_console()const;

         bool is_profiling(account_name name) const;

         bool is_eos_vm_oc_whitelisted(const account_name& n) const;

         chain_id_type get_chain_id()const;

         void set_peer_keys_retrieval_active(name_set_t configured_bp_peers);
         std::optional<peer_info_t> get_peer_info(name n) const;  // thread safe
         bool configured_peer_keys_updated(); // thread safe
         // used for testing, only call with an active pending block from main thread
         getpeerkeys_res_t get_top_producer_keys();

         // thread safe
         db_read_mode get_read_mode()const;
         validation_mode get_validation_mode()const;

         /// @return true if terminate-at-block reached
         /// not-thread-safe
         bool should_terminate() const;

         void set_subjective_cpu_leeway(fc::microseconds leeway);
         std::optional<fc::microseconds> get_subjective_cpu_leeway() const;
         void set_greylist_limit( uint32_t limit );
         uint32_t get_greylist_limit()const;

         void add_to_ram_correction( account_name account, uint64_t ram_bytes );
         bool all_subjective_mitigations_disabled()const;

         deep_mind_handler* get_deep_mind_logger(bool is_trx_transient) const;
         void enable_deep_mind( deep_mind_handler* logger );
         uint32_t earliest_available_block_num() const;

#if defined(EOSIO_EOS_VM_RUNTIME_ENABLED) || defined(EOSIO_EOS_VM_JIT_RUNTIME_ENABLED)
         vm::wasm_allocator&  get_wasm_allocator();
#endif
#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
         bool is_eos_vm_oc_enabled() const;
#endif

         static std::optional<uint64_t> convert_exception_to_error_code( const fc::exception& e );

         signal<void(uint32_t)>&                    block_start();
         signal<void(const block_signal_params&)>&  accepted_block_header();
         signal<void(const block_signal_params&)>&  accepted_block();
         signal<void(const block_signal_params&)>&  irreversible_block();
         signal<void(std::tuple<const transaction_trace_ptr&, const packed_transaction_ptr&>)>& applied_transaction();

         // Unlike other signals, voted_block and aggregated_vote may be signaled from other
         // threads than the main thread.
         vote_signal_t&                             voted_block();
         vote_signal_t&                             aggregated_vote();

         const apply_handler* find_apply_handler( account_name contract, scope_name scope, action_name act )const;
         wasm_interface& get_wasm_interface();

      static chain_id_type extract_chain_id(snapshot_reader& snapshot);

      static std::optional<chain_id_type> extract_chain_id_from_db( const path& state_dir );

      void replace_producer_keys( const public_key_type& key );
      void replace_account_keys( name account, name permission, const public_key_type& key );

      void set_producer_node(bool is_producer_node);
      bool is_producer_node()const; // thread safe, set at program initialization

      void set_pause_at_block_num(block_num_type block_num);
      block_num_type get_pause_at_block_num()const;

      void set_db_read_only_mode();
      void unset_db_read_only_mode();
      void init_thread_local_data();
      void set_to_write_window();
      void set_to_read_window();
      bool is_write_window() const;

      platform_timer& get_thread_local_timer();

      void code_block_num_last_used(const digest_type& code_hash, uint8_t vm_type, uint8_t vm_version,
                                    block_num_type first_used_block_num, block_num_type block_num_last_used);
      void set_node_finalizer_keys(const bls_pub_priv_key_map_t& finalizer_keys);

      // is the bls key a registered finalizer key of this node, thread safe
      bool is_node_finalizer_key(const bls_public_key& key) const;

      void register_update_produced_block_metrics(std::function<void(produced_block_metrics)>&&);
      void register_update_speculative_block_metrics(std::function<void(speculative_block_metrics)>&&);
      void register_update_incoming_block_metrics(std::function<void(incoming_block_metrics)>&&);

      private:
         const my_finalizers_t& get_node_finalizers() const;  // used for tests (purpose is inspecting fsi).

         friend class apply_context;
         friend class transaction_context;
         friend class savanna_cluster::node_t;

         chainbase::database& mutable_db()const;

         std::unique_ptr<controller_impl> my;
   }; // controller

}  /// eosio::chain

FC_REFLECT(eosio::chain::peerkeys_t, (producer_name)(peer_key))