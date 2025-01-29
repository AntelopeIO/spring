#pragma once
#include <eosio/chain/controller.hpp>
#include <eosio/chain/asset.hpp>
#include <eosio/chain/contract_table_objects.hpp>
#include <eosio/chain/account_object.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/unapplied_transaction_queue.hpp>
#include <fc/io/json.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/tuple/tuple_io.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <eosio/testing/bls_utils.hpp>

#include <iosfwd>
#include <optional>

#define REQUIRE_EQUAL_OBJECTS(left, right) { auto a = fc::variant( left ); auto b = fc::variant( right ); BOOST_REQUIRE_EQUAL( true, a.is_object() ); \
   BOOST_REQUIRE_EQUAL( true, b.is_object() ); \
   BOOST_REQUIRE_EQUAL_COLLECTIONS( a.get_object().begin(), a.get_object().end(), b.get_object().begin(), b.get_object().end() ); }

#define REQUIRE_MATCHING_OBJECT(left, right) { auto a = fc::variant( left ); auto b = fc::variant( right ); BOOST_REQUIRE_EQUAL( true, a.is_object() ); \
   BOOST_REQUIRE_EQUAL( true, b.is_object() ); \
   auto filtered = ::eosio::testing::filter_fields( a.get_object(), b.get_object() ); \
   BOOST_REQUIRE_EQUAL_COLLECTIONS( a.get_object().begin(), a.get_object().end(), filtered.begin(), filtered.end() ); }

std::ostream& operator<<( std::ostream& osm, const fc::variant& v );

std::ostream& operator<<( std::ostream& osm, const fc::variant_object& v );

std::ostream& operator<<( std::ostream& osm, const fc::variant_object::entry& e );

eosio::chain::asset core_from_string(const std::string& s);

namespace boost { namespace test_tools { namespace tt_detail {

   template<>
   struct print_log_value<fc::variant> {
      void operator()( std::ostream& osm, const fc::variant& v )
      {
         ::operator<<( osm, v );
      }
   };

   template<>
   struct print_log_value<fc::variant_object> {
      void operator()( std::ostream& osm, const fc::variant_object& v )
      {
         ::operator<<( osm, v );
      }
   };

   template<>
   struct print_log_value<fc::variant_object::entry> {
      void operator()( std::ostream& osm, const fc::variant_object::entry& e )
      {
         ::operator<<( osm, e );
      }
   };

} } }

namespace eosio::testing {
   enum class setup_policy {
      none,
      old_bios_only,
      preactivate_feature_only,
      preactivate_feature_and_new_bios,
      old_wasm_parser,
      full_except_do_not_disable_deferred_trx,
      full_except_do_not_transition_to_savanna,
      full
   };

   enum class call_startup_t {
      no, // tester does not call startup() during initialization. The user must call
          // `startup()` explicitly. See unittests/blocks_log_replay_tests.cpp for example.
      yes // tester calls startup() during initialization.
   };

   // Number of chains required for a block to become final.
   // Current protocol is 2: strong-strong or weak-strong.
   constexpr size_t num_chains_to_final = 2;

   std::ostream& operator<<(std::ostream& os, setup_policy p);

   std::vector<uint8_t> read_wasm( const char* fn );
   std::vector<char>    read_abi( const char* fn );
   std::string          read_wast( const char* fn );

   std::string          read_binary_snapshot( const char* fn );
   fc::variant          read_json_snapshot( const char* fn );

   using namespace eosio::chain;

   fc::variant_object filter_fields(const fc::variant_object& filter, const fc::variant_object& value);

   void copy_row(const chain::key_value_object& obj, vector<char>& data);

   bool expect_assert_message(const fc::exception& ex, string expected);

   using subjective_restriction_map = std::map<builtin_protocol_feature_t, protocol_feature_subjective_restrictions>;

   protocol_feature_set make_protocol_feature_set(const subjective_restriction_map& custom_subjective_restrictions = {});

   namespace mock {
      using namespace fc::crypto;
      struct webauthn_private_key {
         explicit webauthn_private_key(r1::private_key&& priv_key)
         :priv_key(std::move(priv_key))
         {
         }

         webauthn_private_key(webauthn_private_key&&) = default;
         webauthn_private_key(const webauthn_private_key&) = default;

         static auto regenerate(const fc::sha256& secret) {
            return webauthn_private_key(r1::private_key::regenerate(secret));
         }

         public_key get_public_key(webauthn::public_key::user_presence_t presence = webauthn::public_key::user_presence_t::USER_PRESENCE_NONE) const {
            return public_key_type(webauthn::public_key(priv_key.get_public_key().serialize(), presence, _origin));
         }

         signature sign( const sha256& digest, bool = true) const {
            auto json = std::string("{\"origin\":\"https://") +
                        _origin +
                        "\",\"type\":\"webauthn.get\",\"challenge\":\"" +
                        fc::base64url_encode(digest.data(), digest.data_size()) +
                        "\"}";
            std::vector<uint8_t> auth_data(37);
            memcpy(auth_data.data(), _origin_hash.data(), sizeof(_origin_hash));

            auto client_data_hash = fc::sha256::hash(json);
            fc::sha256::encoder e;
            e.write((char*)auth_data.data(), auth_data.size());
            e.write(client_data_hash.data(), client_data_hash.data_size());
            auto sig = priv_key.sign_compact(e.result());

            char serialized_sig[4096];
            datastream<char*> sig_ds(serialized_sig, sizeof(serialized_sig));
            fc::raw::pack(sig_ds, (uint8_t)get_index<signature::storage_type, webauthn::signature>());
            fc::raw::pack(sig_ds, sig);
            fc::raw::pack(sig_ds, auth_data);
            fc::raw::pack(sig_ds, json);
            sig_ds.seekp(0);

            signature ret;
            fc::raw::unpack(sig_ds, ret);
            return ret;
         }

         r1::private_key priv_key;
         static const std::string _origin;
         static const fc::sha256 _origin_hash;
      };
   }

   struct produce_block_result_t {
      signed_block_ptr                   block;
      transaction_trace_ptr              onblock_trace;
      std::vector<transaction_trace_ptr> unapplied_transaction_traces; // only traces of any unapplied transactions
   };

   /**
    *  @class tester
    *  @brief provides utility function to simplify the creation of unit tests
    */
   class base_tester {
      public:
         typedef string action_result;

         static const uint32_t DEFAULT_EXPIRATION_DELTA = 6;

         static const uint32_t DEFAULT_BILLED_CPU_TIME_US = 2000;
         static const fc::microseconds abi_serializer_max_time;
         static constexpr fc::microseconds default_skip_time = fc::milliseconds(config::block_interval_ms);

         virtual ~base_tester() {}
         base_tester() = default;
         base_tester(base_tester&&) = default;

         void              init(const setup_policy policy = setup_policy::full, db_read_mode read_mode = db_read_mode::HEAD, std::optional<uint32_t> genesis_max_inline_action_size = std::optional<uint32_t>{});
         void              init(controller::config config, const snapshot_reader_ptr& snapshot);
         void              init(controller::config config, const genesis_state& genesis, call_startup_t call_startup);
         void              init(controller::config config);
         void              init(controller::config config, protocol_feature_set&& pfs, const snapshot_reader_ptr& snapshot);
         void              init(controller::config config, protocol_feature_set&& pfs, const genesis_state& genesis);
         void              init(controller::config config, protocol_feature_set&& pfs);
         void              execute_setup_policy(const setup_policy policy);

         void              close();
         void              open( protocol_feature_set&& pfs, std::optional<chain_id_type> expected_chain_id, const std::function<void()>& lambda );
         void              open( protocol_feature_set&& pfs, const snapshot_reader_ptr& snapshot );
         void              open( protocol_feature_set&& pfs, const genesis_state& genesis, call_startup_t call_startup );
         void              open( protocol_feature_set&& pfs, std::optional<chain_id_type> expected_chain_id = {} );
         void              open( const snapshot_reader_ptr& snapshot );
         void              open( const genesis_state& genesis, call_startup_t call_startup );
         void              open( std::optional<chain_id_type> expected_chain_id = {} );
         bool              is_open() const;
         bool              is_same_chain( base_tester& other ) const;

         // `produce_block_ex` does the same thing as produce_block, but returns a struct including
         // the transaction traces in addition to the `signed_block_ptr`.
         virtual produce_block_result_t produce_block_ex( fc::microseconds skip_time = default_skip_time,
                                                          bool no_throw = false) = 0;

         virtual signed_block_ptr       produce_block( fc::microseconds skip_time = default_skip_time,
                                                       bool no_throw = false) = 0;
         virtual signed_block_ptr       produce_empty_block( fc::microseconds skip_time = default_skip_time ) = 0;
         virtual signed_block_ptr       finish_block() = 0;

         // produce one block and return traces for all applied transactions, both failed and executed
         signed_block_ptr     produce_blocks( uint32_t n = 1, bool empty = false );
         void                 produce_blocks_until_end_of_round();
         void                 produce_blocks_for_n_rounds(const uint32_t num_of_rounds = 1);
         // Produce minimal number of blocks as possible to spend the given time without having any
         // producer become inactive
         void                 produce_min_num_of_blocks_to_spend_time_wo_inactive_prod(const fc::microseconds target_elapsed_time = fc::microseconds());
         void                 push_block(const signed_block_ptr& b);
         void                 apply_blocks();

         /**
          * These transaction IDs represent transactions available in the head chain state as scheduled
          * or otherwise generated transactions.
          *
          * calling push_scheduled_transaction with these IDs will remove the associated transaction from
          * the chain state IFF it succeeds or objectively fails
          *
          * @return
          */
         vector<transaction_id_type> get_scheduled_transactions() const;
         unapplied_transaction_queue& get_unapplied_transaction_queue() { return unapplied_transactions; }

         transaction_trace_ptr    push_transaction( packed_transaction& trx, fc::time_point deadline = fc::time_point::maximum(), uint32_t billed_cpu_time_us = DEFAULT_BILLED_CPU_TIME_US );
         transaction_trace_ptr    push_transaction( signed_transaction& trx, fc::time_point deadline = fc::time_point::maximum(), uint32_t billed_cpu_time_us = DEFAULT_BILLED_CPU_TIME_US, bool no_throw = false, transaction_metadata::trx_type trx_type = transaction_metadata::trx_type::input );

         [[nodiscard]]
         action_result            push_action(action&& cert_act, uint64_t authorizer); // TODO/QUESTION: Is this needed?

         transaction_trace_ptr    push_action( const account_name& code,
                                               const action_name& acttype,
                                               const account_name& actor,
                                               const variant_object& data,
                                               uint32_t expiration = DEFAULT_EXPIRATION_DELTA,
                                               uint32_t delay_sec = 0 );
         transaction_trace_ptr    push_action( const account_name& code,
                                               const action_name& acttype,
                                               const vector<account_name>& actors,
                                               const variant_object& data,
                                               uint32_t expiration = DEFAULT_EXPIRATION_DELTA,
                                               uint32_t delay_sec = 0 );
         transaction_trace_ptr    push_action( const account_name& code,
                                               const action_name& acttype,
                                               const vector<permission_level>& auths,
                                               const variant_object& data,
                                               uint32_t expiration = DEFAULT_EXPIRATION_DELTA,
                                               uint32_t delay_sec = 0 );


         action get_action( account_name code, action_name acttype, vector<permission_level> auths,
                                         const variant_object& data )const;

         void  set_transaction_headers( transaction& trx,
                                        uint32_t expiration = DEFAULT_EXPIRATION_DELTA,
                                        uint32_t delay_sec = 0 )const;

         vector<transaction_trace_ptr>  create_accounts( const vector<account_name>& names,
                                                         bool multisig = false,
                                                         bool include_code = true
                                                       )
         {
            vector<transaction_trace_ptr> traces;
            traces.reserve(names.size());
            for( auto n : names ) traces.emplace_back( create_account( n, config::system_account_name, multisig, include_code ) );
            return traces;
         }

         void                  set_before_preactivate_bios_contract();
         void                  set_before_producer_authority_bios_contract();
         void                  set_bios_contract();

         vector<producer_authority>  get_producer_authorities( const vector<account_name>& producer_names )const;
         transaction_trace_ptr       set_producers(const vector<account_name>& producer_names);
         transaction_trace_ptr       set_producer_schedule(const vector<producer_authority>& schedule);
         transaction_trace_ptr       set_producers_legacy(const vector<account_name>& producer_names);

         // Finalizer policy input to set up a test: weights, threshold and local finalizers
         // which participate voting.
         struct finalizer_policy_input {
            struct finalizer_info {
               account_name name;
               uint64_t     weight;
            };

            std::vector<finalizer_info> finalizers;
            uint64_t                    threshold {0};
            std::vector<account_name>   local_finalizers;
         };

         struct set_finalizers_output_t {
            transaction_trace_ptr        setfinalizer_trace;
            std::vector<bls_private_key> privkeys;  // private keys of **local** finalizers
            std::vector<bls_public_key>  pubkeys;   // public keys of all finalizers in the policy
         };

         set_finalizers_output_t set_finalizers(const finalizer_policy_input& input);

         void set_node_finalizers(std::span<const account_name> finalizer_names);

         set_finalizers_output_t set_active_finalizers(std::span<const account_name> finalizer_names);

         // Useful when using a single node.
         // Set a finalizer policy with a few finalizers, all local to the current node.
         // All have weight == 1, threshold is `num_finalizers * 2 / 3 + 1`
         // -----------------------------------------------------------------------------
         set_finalizers_output_t set_finalizers(std::span<const account_name> finalizer_names);

         // Useful when using a single node.
         // Set a finalizer policy with a few finalizers, all local to the current node.
         // All have weight == 1, threshold is `num_finalizers * 2 / 3 + 1`
         // -----------------------------------------------------------------------------
         set_finalizers_output_t set_finalizers(const std::vector<account_name>& names) {
            return set_finalizers(std::span{names.begin(), names.end()});
         }

         std::optional<finalizer_policy> active_finalizer_policy(const block_id_type& id) const {
            return control->active_finalizer_policy(id);
         }

         finalizer_policy_ptr head_active_finalizer_policy() const {
            return control->head_active_finalizer_policy();
         }

         finalizer_policy_ptr head_pending_finalizer_policy() const {
            return control->head_pending_finalizer_policy();
         }

         void link_authority( account_name account, account_name code,  permission_name req, action_name type = {} );
         void unlink_authority( account_name account, account_name code, action_name type = {} );
         void set_authority( account_name account, permission_name perm, authority auth,
                                     permission_name parent, const vector<permission_level>& auths, const vector<private_key_type>& keys );
         void set_authority( account_name account, permission_name perm, authority auth,
                                     permission_name parent = config::owner_name );
         void delete_authority( account_name account, permission_name perm,  const vector<permission_level>& auths, const vector<private_key_type>& keys );
         void delete_authority( account_name account, permission_name perm );

         transaction_trace_ptr create_account( account_name name,
                                               account_name creator = config::system_account_name,
                                               bool multisig = false,
                                               bool include_code = true
                                             );

         transaction_trace_ptr push_reqauth( account_name from, const vector<permission_level>& auths, const vector<private_key_type>& keys );
         transaction_trace_ptr push_reqauth(account_name from, string role, bool multi_sig = false);
         // use when just want any old non-context free action
         transaction_trace_ptr push_dummy(account_name from, const string& v = "blah", uint32_t billed_cpu_time_us = DEFAULT_BILLED_CPU_TIME_US );
         transaction_trace_ptr transfer( account_name from, account_name to, asset amount, string memo, account_name currency );
         transaction_trace_ptr transfer( account_name from, account_name to, string amount, string memo, account_name currency );
         transaction_trace_ptr issue( account_name to, string amount, account_name currency , string memo);

         template<typename ObjectType>
         const auto& get(const chainbase::oid< ObjectType >& key) {
            return control->db().get<ObjectType>(key);
         }

         template<typename ObjectType, typename IndexBy, typename... Args>
         const auto& get( Args&&... args ) {
            return control->db().get<ObjectType,IndexBy>( std::forward<Args>(args)... );
         }

         template<typename ObjectType, typename IndexBy, typename... Args>
         const auto* find( Args&&... args ) {
            return control->db().find<ObjectType,IndexBy>( std::forward<Args>(args)... );
         }

         template< typename KeyType = fc::ecc::private_key_shim >
         static auto get_private_key( name keyname, string role = "owner" ) {
            auto secret = fc::sha256::hash(keyname.to_string() + role);
            if constexpr (std::is_same_v<KeyType, mock::webauthn_private_key>) {
               return mock::webauthn_private_key::regenerate(secret);
            } else {
               return private_key_type::regenerate<KeyType>(secret);
            }
         }

         template< typename KeyType = fc::ecc::private_key_shim >
         static auto get_public_key( name keyname, string role = "owner" ) {
            return get_private_key<KeyType>( keyname, role ).get_public_key();
         }

         void              set_code( account_name name, const char* wast, const private_key_type* signer = nullptr );
         void              set_code( account_name name, const vector<uint8_t> wasm, const private_key_type* signer = nullptr  );
         void              set_abi( account_name name, const std::string& abi_json, const private_key_type* signer = nullptr );

         bool is_code_cached( account_name name ) const;

         bool                          chain_has_transaction( const transaction_id_type& txid ) const;
         const transaction_receipt&    get_transaction_receipt( const transaction_id_type& txid ) const;

         asset                         get_currency_balance( const account_name& contract,
                                                             const symbol&       asset_symbol,
                                                             const account_name& account ) const;

         vector<char> get_row_by_account( name code, name scope, name table, const account_name& act ) const;
         vector<char> get_row_by_id( name code, name scope, name table, uint64_t id ) const;

         map<account_name, block_id_type> get_last_produced_block_map()const { return last_produced_block; };
         void set_last_produced_block_map( const map<account_name, block_id_type>& lpb ) { last_produced_block = lpb; }

         static vector<uint8_t> to_uint8_vector(const string& s);

         static vector<uint8_t> to_uint8_vector(uint64_t x);

         static uint64_t to_uint64(fc::variant x);

         static string to_string(fc::variant x);

         static action_result success() { return string(); }

         static action_result error( const string& msg ) { return msg; }

         static action_result wasm_assert_msg( const string& msg ) { return "assertion failure with message: " + msg; }

         static action_result wasm_assert_code( uint64_t error_code ) { return "assertion failure with error code: " + std::to_string(error_code); }

         auto get_resolver() {
            return [this]( const account_name& name ) -> std::optional<abi_serializer> {
               try {
                  const auto& accnt = control->db().get<account_object, by_name>( name );
                  if( abi_def abi; abi_serializer::to_abi( accnt.abi, abi )) {
                     return abi_serializer( std::move(abi), abi_serializer::create_yield_function( abi_serializer_max_time ) );
                  }
                  return std::optional<abi_serializer>();
               } FC_RETHROW_EXCEPTIONS( error, "Failed to find or parse ABI for ${name}", ("name", name))
            };
         }

         void sync_with(base_tester& other);

         const table_id_object* find_table( name code, name scope, name table );

         // method treats key as a name type, if this is not appropriate in your case, pass require == false and report the correct behavior
         template<typename Object>
         bool get_table_entry(Object& obj, account_name code, account_name scope, account_name table, uint64_t key, bool require = true) {
            auto* maybe_tid = find_table(code, scope, table);
            if( maybe_tid == nullptr ) {
               BOOST_FAIL( "table for code=\"" + code.to_string()
                            + "\" scope=\"" + scope.to_string()
                            + "\" table=\"" + table.to_string()
                            + "\" does not exist"                 );
            }

            auto* o = control->db().find<key_value_object, by_scope_primary>(boost::make_tuple(maybe_tid->id, key));
            if( o == nullptr ) {
               if( require )
                  BOOST_FAIL("object does not exist for primary_key=\"" + name(key).to_string() + "\"");

               return false;
            }

            fc::raw::unpack(o->value.data(), o->value.size(), obj);
            return true;
         }

         void allow_voting(bool val) {
            control->testing_allow_voting(val);
         }

         const controller::config& get_config() const {
            return cfg;
         }

         void schedule_protocol_features_wo_preactivation(const vector<digest_type>& feature_digests);
         void preactivate_protocol_features(const vector<digest_type>& feature_digests);
         void preactivate_builtin_protocol_features(const std::vector<builtin_protocol_feature_t>& features);
         void preactivate_all_builtin_protocol_features();
         void preactivate_all_but_disable_deferred_trx();
         void preactivate_savanna_protocol_features();

         static genesis_state default_genesis() {
            genesis_state genesis;
            genesis.initial_timestamp = fc::time_point::from_iso_string("2020-01-01T00:00:00.000");
            genesis.initial_key = get_public_key( config::system_account_name, "active" );

            return genesis;
         }

         static std::pair<controller::config, genesis_state> default_config(const fc::temp_directory& tempdir, std::optional<uint32_t> genesis_max_inline_action_size = std::optional<uint32_t>{}) {
            controller::config cfg;
            cfg.finalizers_dir = tempdir.path() / config::default_finalizers_dir_name;
            cfg.blocks_dir = tempdir.path() / config::default_blocks_dir_name;
            cfg.state_dir  = tempdir.path() / config::default_state_dir_name;
            cfg.state_size = 1024*1024*16;
            cfg.state_guard_size = 0;
            cfg.contracts_console = true;
            cfg.eosvmoc_config.cache_size = 1024*1024*8;
            cfg.vote_thread_pool_size = 3;

            // don't enforce OC compilation subject limits for tests,
            // particularly EOS EVM tests may run over those limits
            cfg.eosvmoc_config.non_whitelisted_limits.cpu_limit.reset();
            cfg.eosvmoc_config.non_whitelisted_limits.vm_limit.reset();
            cfg.eosvmoc_config.non_whitelisted_limits.stack_size_limit.reset();
            cfg.eosvmoc_config.non_whitelisted_limits.generated_code_size_limit.reset();

            // don't use auto tier up for tests, since the point is to test diff vms
            cfg.eosvmoc_tierup = chain::wasm_interface::vm_oc_enable::oc_none;

            for(int i = 0; i < boost::unit_test::framework::master_test_suite().argc; ++i) {
               if(boost::unit_test::framework::master_test_suite().argv[i] == std::string("--eos-vm"))
                  cfg.wasm_runtime = chain::wasm_interface::vm_type::eos_vm;
               else if(boost::unit_test::framework::master_test_suite().argv[i] == std::string("--eos-vm-jit"))
                  cfg.wasm_runtime = chain::wasm_interface::vm_type::eos_vm_jit;
               else if(boost::unit_test::framework::master_test_suite().argv[i] == std::string("--eos-vm-oc"))
                  cfg.wasm_runtime = chain::wasm_interface::vm_type::eos_vm_oc;
            }
            auto gen = default_genesis();
            if (genesis_max_inline_action_size) {
               gen.initial_configuration.max_inline_action_size = *genesis_max_inline_action_size;
            }
            return {cfg, gen};
         }

         static bool arguments_contains(const std::string& arg) {
            auto argc = boost::unit_test::framework::master_test_suite().argc;
            auto argv = boost::unit_test::framework::master_test_suite().argv;

            return std::find(argv, argv + argc, arg) != (argv + argc);
         }

         // ideally, users of `tester` should not access the controller directly,
         // so we provide APIs to access the chain head and fork_db head, and some
         // other commonly used APIs.
         // ----------------------------------------------------------------------
         block_handle     head() const { return control->head(); }
         block_handle     fork_db_head() const { return control->fork_db_head(); }

         chain_id_type    get_chain_id() const { return control->get_chain_id(); }
         block_id_type    last_irreversible_block_id() const { return control->fork_db_root().id(); }
         uint32_t         last_irreversible_block_num() const { return control->fork_db_root().block_num(); }
         bool             block_exists(const block_id_type& id) const { return  control->block_exists(id); }

         signed_block_ptr fetch_block_by_id(const block_id_type& id) const {
            return control->fetch_block_by_id(id);
         }

         signed_block_ptr fetch_block_by_number(uint32_t block_num) const {
            return control->fetch_block_by_number(block_num);
         }

         const account_object& get_account(account_name name) const {
            return control->get_account(name);
         }

         // checks that the active `finalizer_policy` for `block` matches the
         // passed `generation` and `keys_span`.
         // -----------------------------------------------------------------
         void check_head_finalizer_policy(uint32_t generation,
                                          std::span<const bls_public_key> keys_span) {
            auto finpol = active_finalizer_policy(head().id());
            BOOST_REQUIRE(!!finpol);
            BOOST_REQUIRE_EQUAL(finpol->generation, generation);
            BOOST_REQUIRE_EQUAL(keys_span.size(), finpol->finalizers.size());
            std::vector<bls_public_key> keys {keys_span.begin(), keys_span.end() };
            std::sort(keys.begin(), keys.end());

            std::vector<bls_public_key> active_keys;
            for (const auto& auth : finpol->finalizers)
               active_keys.push_back(auth.public_key);
            std::sort(active_keys.begin(), active_keys.end());
            for (size_t i=0; i<keys.size(); ++i)
               BOOST_REQUIRE_EQUAL(keys[i], active_keys[i]);
         }

         void set_produce_block_callback(std::function<void(const signed_block_ptr&)> cb) { _produce_block_callback = std::move(cb); }
         void set_open_callback(std::function<void()> cb) { _open_callback = std::move(cb); }
         void do_check_for_votes(bool val) { _expect_votes = val; }

      protected:
         signed_block_ptr       _produce_block( fc::microseconds skip_time, bool skip_pending_trxs );
         produce_block_result_t _produce_block( fc::microseconds skip_time, bool skip_pending_trxs, bool no_throw );

         transaction_trace_ptr  _start_block(fc::time_point block_time);
         signed_block_ptr       _finish_block();
         void                   _check_for_vote_if_needed(controller& c, const block_handle& bh);

         enum class block_signal { block_start, accepted_block_header, accepted_block, irreversible_block };
         bool                   _check_signal(const block_id_type& id, block_signal sig);

      // Fields:
      protected:
         bool                   _expect_votes {true};                          // if set, ensure the node votes on each block
         std::function<void(const signed_block_ptr&)> _produce_block_callback; // if set, called every time a block is produced
         std::function<void()>                        _open_callback;          // if set, called every time the tester is opened

         // tempdir field must come before control so that during destruction the tempdir is deleted only after controller finishes
         fc::temp_directory                            tempdir;
      public:
         unique_ptr<controller>                        control;
         std::map<chain::public_key_type, chain::private_key_type> block_signing_private_keys;
      protected:
         controller::config                            cfg;
         map<transaction_id_type, transaction_receipt> chain_transactions;
         map<account_name, block_id_type>              last_produced_block;
         unapplied_transaction_queue                   unapplied_transactions;
         boost::unordered_flat_map<block_id_type, block_signal> blocks_signaled;

      public:
         vector<digest_type>                           protocol_features_to_be_activated_wo_preactivation;
         signed_block_ptr                              lib_block; // updated via irreversible_block signal
         block_id_type                                 lib_id;    // updated via irreversible_block signal
         uint32_t                                      lib_number {0}; // updated via irreversible_block signal

      private:
         std::vector<builtin_protocol_feature_t> get_all_builtin_protocol_features();
   };

   class tester : public base_tester {
   public:
      tester(setup_policy policy = setup_policy::full, db_read_mode read_mode = db_read_mode::HEAD, std::optional<uint32_t> genesis_max_inline_action_size = std::optional<uint32_t>{}) {
         init(policy, read_mode, genesis_max_inline_action_size);
      }

      // If `call_startup` is `yes`, tester starts the chain during initialization.
      //
      // If `call_startup` is `no`, tester does NOT start the chain during initialization;
      // the user must call `startup()` explicitly.
      // Before calling `startup()`, the user can do additional setups like connecting
      // to a particular signal, and customizing shutdown conditions.
      // See blocks_log_replay_tests.cpp in unit_test for an example.
      tester(controller::config config, const genesis_state& genesis, call_startup_t call_startup = call_startup_t::yes) {
         init(std::move(config), genesis, call_startup);
      }

      tester(controller::config config) {
         init(std::move(config));
      }

      tester(controller::config config, protocol_feature_set&& pfs, const genesis_state& genesis) {
         init(std::move(config), std::move(pfs), genesis);
      }

      tester(const fc::temp_directory& tempdir, bool use_genesis) {
         auto def_conf = default_config(tempdir);
         cfg = def_conf.first;

         if (use_genesis) {
            init(cfg, def_conf.second, call_startup_t::yes);
         }
         else {
            init(cfg);
         }
      }

      template <typename Lambda>
      tester(const fc::temp_directory& tempdir, Lambda conf_edit, bool use_genesis) {
         auto def_conf = default_config(tempdir);
         cfg = def_conf.first;
         conf_edit(cfg);

         if (use_genesis) {
            init(cfg, def_conf.second, call_startup_t::yes);
         }
         else {
            init(cfg);
         }
      }

      tester(const std::function<void(controller&)>& control_setup, setup_policy policy = setup_policy::full,
             db_read_mode read_mode = db_read_mode::HEAD);

      using base_tester::produce_block;

      produce_block_result_t produce_block_ex( fc::microseconds skip_time = default_skip_time, bool no_throw = false ) override {
         return _produce_block(skip_time, false, no_throw);
      }

      signed_block_ptr produce_block( fc::microseconds skip_time = default_skip_time, bool no_throw = false ) override {
         return _produce_block(skip_time, false, no_throw).block;
      }

      signed_block_ptr produce_empty_block( fc::microseconds skip_time = default_skip_time ) override {
         unapplied_transactions.add_aborted( control->abort_block() );
         return _produce_block(skip_time, true);
      }

      signed_block_ptr finish_block()override {
         return _finish_block();
      }

      bool validate() { return true; }

   };

   // The behavior of legacy_tester is activating all the protocol features but not
   // transition to Savanna consensus.
   // If needed, the tester can be transitioned to Savanna by explicitly calling
   // set_finalizer host function only.
   class legacy_tester : public tester {
   public:
      legacy_tester(setup_policy policy = setup_policy::full_except_do_not_transition_to_savanna, db_read_mode read_mode = db_read_mode::HEAD, std::optional<uint32_t> genesis_max_inline_action_size = std::optional<uint32_t>{})
      : tester(policy == setup_policy::full ? setup_policy::full_except_do_not_transition_to_savanna
                                            : policy,
               read_mode, genesis_max_inline_action_size) {};

      legacy_tester(controller::config config, const genesis_state& genesis)
      : tester(config, genesis) {};

      legacy_tester(const fc::temp_directory& tempdir, bool use_genesis)
      : tester(tempdir, use_genesis) {};

      template <typename Lambda>
      legacy_tester(const fc::temp_directory& tempdir, Lambda conf_edit, bool use_genesis)
      : tester(tempdir, conf_edit, use_genesis) {};

      legacy_tester(const std::function<void(controller&)>& control_setup, setup_policy policy = setup_policy::full, db_read_mode read_mode = db_read_mode::HEAD)
      : tester(control_setup,
               policy == setup_policy::full ? setup_policy::full_except_do_not_transition_to_savanna
                                            : policy,
               read_mode) {};

      // setup_policy::full does not not transition to Savanna consensus.
      void execute_setup_policy(const setup_policy policy) {
         tester::execute_setup_policy(policy == setup_policy::full ? setup_policy::full_except_do_not_transition_to_savanna : policy);
      };
   };

   using savanna_tester = tester;
   using testers = boost::mpl::list<legacy_tester, savanna_tester>;

   class tester_no_disable_deferred_trx : public tester {
   public:
      tester_no_disable_deferred_trx(): tester(setup_policy::full_except_do_not_disable_deferred_trx) {
      }
   };

   class validating_tester : public base_tester {
   public:
      virtual ~validating_tester() {
         if( !validating_node ) {
            elog( "~validating_tester() called with empty validating_node; likely in the middle of failure" );
            return;
         }
         try {
            if (!skip_validate && std::uncaught_exceptions() == 0)
               BOOST_CHECK_EQUAL( validate(), true );
         } catch( const fc::exception& e ) {
            wdump((e.to_detail_string()));
         }
      }
      controller::config vcfg;

      validating_tester(const flat_set<account_name>& trusted_producers = flat_set<account_name>(), deep_mind_handler* dmlog = nullptr, setup_policy p = setup_policy::full) {
         auto def_conf = default_config(tempdir);

         vcfg = def_conf.first;
         config_validator(vcfg);
         vcfg.trusted_producers = trusted_producers;

         validating_node = create_validating_node(vcfg, def_conf.second, true, dmlog);

         init(def_conf.first, def_conf.second, call_startup_t::yes);
         execute_setup_policy(p);
      }

      static void config_validator(controller::config& vcfg) {
         FC_ASSERT( vcfg.blocks_dir.filename().generic_string() != "."
                    && vcfg.state_dir.filename().generic_string() != ".", "invalid path names in controller::config" );

         vcfg.finalizers_dir = vcfg.blocks_dir.parent_path() / std::string("v_").append( vcfg.finalizers_dir.filename().generic_string() );
         vcfg.blocks_dir = vcfg.blocks_dir.parent_path() / std::string("v_").append( vcfg.blocks_dir.filename().generic_string() );
         vcfg.state_dir  = vcfg.state_dir.parent_path() / std::string("v_").append( vcfg.state_dir.filename().generic_string() );

         vcfg.contracts_console = false;
      }

      static unique_ptr<controller> create_validating_node(controller::config vcfg, const genesis_state& genesis, bool use_genesis, deep_mind_handler* dmlog = nullptr);

      validating_tester(const fc::temp_directory& tempdir, bool use_genesis) {
         auto def_conf = default_config(tempdir);
         vcfg = def_conf.first;
         config_validator(vcfg);
         validating_node = create_validating_node(vcfg, def_conf.second, use_genesis);

         if (use_genesis) {
            init(def_conf.first, def_conf.second, call_startup_t::yes);
         } else {
            init(def_conf.first);
         }
      }

      template <typename Lambda>
      validating_tester(const fc::temp_directory& tempdir, Lambda conf_edit, bool use_genesis) {
         auto def_conf = default_config(tempdir);
         conf_edit(def_conf.first);
         vcfg = def_conf.first;
         config_validator(vcfg);
         validating_node = create_validating_node(vcfg, def_conf.second, use_genesis);

         if (use_genesis) {
            init(def_conf.first, def_conf.second, call_startup_t::yes);
         }  else {
            init(def_conf.first);
         }
      }

      produce_block_result_t produce_block_ex( fc::microseconds skip_time = default_skip_time, bool no_throw = false ) override {
         auto produce_block_result = _produce_block(skip_time, false, no_throw);
         validate_push_block(produce_block_result.block);
         return produce_block_result;
      }

      signed_block_ptr produce_block( fc::microseconds skip_time = default_skip_time, bool no_throw = false ) override {
         return produce_block_ex(skip_time, no_throw).block;
      }

      signed_block_ptr produce_block_no_validation( fc::microseconds skip_time = default_skip_time ) {
         return _produce_block(skip_time, false, false).block;
      }

      void validate_push_block(const signed_block_ptr& sb) {
         auto [best_head, obh] = validating_node->accept_block( sb->calculate_id(), sb );
         EOS_ASSERT(obh, unlinkable_block_exception, "block did not link ${b}", ("b", sb->calculate_id()));
         validating_node->apply_blocks( {}, trx_meta_cache_lookup{} );
         _check_for_vote_if_needed(*validating_node, *obh);
      }

      signed_block_ptr produce_empty_block( fc::microseconds skip_time = default_skip_time )override {
         unapplied_transactions.add_aborted( control->abort_block() );
         auto sb = _produce_block(skip_time, true);
         validate_push_block(sb);
         return sb;
      }

      signed_block_ptr finish_block()override {
         return _finish_block();
      }

      bool validate() {
        const block_header hbh = control->head().header();
        const block_header vn_hbh = validating_node->head().header();
        bool ok = control->head().id() == validating_node->head().id() &&
               hbh.previous == vn_hbh.previous &&
               hbh.timestamp == vn_hbh.timestamp &&
               hbh.transaction_mroot == vn_hbh.transaction_mroot &&
               hbh.action_mroot == vn_hbh.action_mroot &&
               hbh.producer == vn_hbh.producer;

        validating_node.reset();
        validating_node = std::make_unique<controller>(vcfg, make_protocol_feature_set(), control->get_chain_id());
        validating_node->add_indices();
        validating_node->startup( [](){}, []() { return false; } );

        return ok;
      }

      unique_ptr<controller>      validating_node;
      bool                        skip_validate = false;
   };

   class validating_tester_no_disable_deferred_trx : public validating_tester {
   public:
      validating_tester_no_disable_deferred_trx(): validating_tester({}, nullptr, setup_policy::full_except_do_not_disable_deferred_trx) {
      }
   };

   // The behavior of legacy_validating_tester is activating all the protocol features
   // but not transition to Savanna consensus.
   // If needed, the tester can be transitioned to Savanna by explicitly calling
   // set_finalizer host function only.
   class legacy_validating_tester : public validating_tester {
   public:
      legacy_validating_tester(const flat_set<account_name>& trusted_producers = flat_set<account_name>(), deep_mind_handler* dmlog = nullptr, setup_policy p = setup_policy::full_except_do_not_transition_to_savanna)
      : validating_tester(trusted_producers, dmlog, p == setup_policy::full ? setup_policy::full_except_do_not_transition_to_savanna : p) {};

      legacy_validating_tester(const fc::temp_directory& tempdir, bool use_genesis)
      : validating_tester(tempdir, use_genesis) {};

      template <typename Lambda>
      legacy_validating_tester(const fc::temp_directory& tempdir, Lambda conf_edit, bool use_genesis)
      : validating_tester(tempdir, conf_edit, use_genesis) {};
   };

   using savanna_validating_tester = validating_tester;
   using validating_testers = boost::mpl::list<legacy_validating_tester, savanna_validating_tester>;

   // -------------------------------------------------------------------------------------
   // creates and manages a set of `bls_public_key` used for finalizers voting and policies
   // Supports initial transition to Savanna.
   // -------------------------------------------------------------------------------------
   template <class Tester>
   struct finalizer_keys {
      explicit finalizer_keys(Tester& t, size_t num_keys = 0, size_t finalizer_policy_size = 0) : t(t) {
         if (num_keys)
            init_keys(num_keys, finalizer_policy_size);
      }

      void init_keys( size_t num_keys = 50, size_t finalizer_policy_size = 21) {
         fin_policy_size = finalizer_policy_size;
         key_names.clear(); pubkeys.clear(); privkeys.clear();
         key_names.reserve(num_keys);
         pubkeys.reserve(num_keys);
         privkeys.reserve(num_keys);
         for (size_t i=0; i<num_keys; ++i) {
            account_name name { std::string("finalizer") + (char)('a' + i/26) + (char)('a' + i%26) };
            key_names.push_back(name);

            auto [privkey, pubkey, pop] = get_bls_key(name);
            pubkeys.push_back(pubkey);
            privkeys.push_back(privkey);
         }
      }

      // configures local node finalizers - should be done only once after tester is `open`ed
      // different nodes should use different keys
      // OK to configure keys not used in a finalizer_policy
      // -------------------------------------------------------------
      void set_node_finalizers(size_t first_key_index, size_t num_keys) {
         node_first_key_idx = first_key_index;
         node_num_keys = num_keys;
         t.set_node_finalizers({&key_names.at(first_key_index), num_keys});
      }

      void set_node_finalizers() {
         if (node_num_keys)
            t.set_node_finalizers({&key_names.at(node_first_key_idx), node_num_keys});
      }

      // updates the finalizer_policy to the `fin_policy_size` keys starting at `first_key_idx`
      // --------------------------------------------------------------------------------------
      base_tester::set_finalizers_output_t set_finalizer_policy(size_t first_key_idx) {
         return t.set_active_finalizers({&key_names.at(first_key_idx), fin_policy_size});
      }

      base_tester::set_finalizers_output_t  set_finalizer_policy(std::span<const size_t> indices) {
         assert(indices.size() == fin_policy_size);
         vector<account_name> names;
         names.reserve(fin_policy_size);
         for (auto idx : indices)
            names.push_back(key_names.at(idx));
         return t.set_active_finalizers({names.begin(), fin_policy_size});
      }

      // Produce blocks until the transition to Savanna is completed.
      // This assumes `set_finalizer_policy` was called immediately
      // before this.
      // This should be done only once.
      // -----------------------------------------------------------
      finalizer_policy transition_to_savanna(const std::function<void(const signed_block_ptr&)>& block_callback = {}) {
         auto produce_block = [&]() {
            auto b = t.produce_block();
            if (block_callback)
               block_callback(b);
            return b;
         };

         // `genesis_block` is the first block where set_finalizers() was executed.
         // It is the genesis block.
         // It will include the first header extension for the instant finality.
         // -----------------------------------------------------------------------
         auto genesis_block = produce_block();

         // Do some sanity checks on the genesis block
         // ------------------------------------------
         const auto& ext = genesis_block->template extract_header_extension<finality_extension>();
         const auto& fin_policy_diff = ext.new_finalizer_policy_diff;
         BOOST_TEST(!!fin_policy_diff);
         BOOST_TEST(fin_policy_diff->finalizers_diff.insert_indexes.size() == fin_policy_size);
         BOOST_TEST(fin_policy_diff->generation == 1u);
         BOOST_TEST(fin_policy_diff->threshold == (fin_policy_size * 2) / 3 + 1);

         // wait till the genesis_block becomes irreversible.
         // The critical block is the block that makes the genesis_block irreversible
         // -------------------------------------------------------------------------
         signed_block_ptr critical_block = nullptr;  // last value of this var is the critical block
         auto genesis_block_num = genesis_block->block_num();
         while(genesis_block_num > t.lib_block->block_num())
            critical_block = produce_block();

         // Blocks after the critical block are proper IF blocks.
         // -----------------------------------------------------
         auto first_proper_block = produce_block();
         BOOST_REQUIRE(first_proper_block->is_proper_svnn_block());

         // wait till the first proper block becomes irreversible. Transition will be done then
         // -----------------------------------------------------------------------------------
         signed_block_ptr pt_block  = nullptr;  // last value of this var is the first post-transition block
         while(first_proper_block->block_num() > t.lib_block->block_num()) {
            pt_block = produce_block();
            BOOST_REQUIRE(pt_block->is_proper_svnn_block());
         }

         // lib must advance after num_chains_to_final blocks
         // -------------------------------
         for (size_t i=0; i<num_chains_to_final; ++i)
            auto b = produce_block();

         BOOST_REQUIRE_EQUAL(t.lib_block->block_num(), pt_block->block_num());
         return finalizer_policy{}.apply_diff(*fin_policy_diff);
      }

      void activate_savanna(size_t first_key_idx) {
         set_node_finalizers(first_key_idx, pubkeys.size());
         set_finalizer_policy(first_key_idx);
         transition_to_savanna();
      }

      Tester&                 t;
      vector<account_name>    key_names;
      vector<bls_public_key>  pubkeys;
      vector<bls_private_key> privkeys;
      size_t                  fin_policy_size {0};
      size_t                  node_first_key_idx{0};
      size_t                  node_num_keys{0};
   };


   /**
    * Utility predicate to check whether an fc::exception message is equivalent to a given string
    */
   struct fc_exception_message_is {
      fc_exception_message_is( const string& msg )
            : expected( msg ) {}

      bool operator()( const fc::exception& ex );

      string expected;
   };

  /**
   * Utility predicate to check whether an fc::exception message starts with a given string
   */
  struct fc_exception_message_starts_with {
     fc_exception_message_starts_with( const string& msg )
           : expected( msg ) {}

     bool operator()( const fc::exception& ex );

     string expected;
  };

  /**
   * Utility predicate to check whether an fc::exception message contains a given string
   */
  struct fc_exception_message_contains {
     explicit fc_exception_message_contains( string msg )
           : expected( std::move(msg) ) {}

     bool operator()( const fc::exception& ex );

     string expected;
  };

  /**
   * Utility predicate to check whether an fc::assert_exception message is equivalent to a given string
   */
  struct fc_assert_exception_message_is {
     fc_assert_exception_message_is( const string& msg )
           : expected( msg ) {}

     bool operator()( const fc::assert_exception& ex );

     string expected;
  };

  /**
   * Utility predicate to check whether an fc::assert_exception message starts with a given string
   */
  struct fc_assert_exception_message_starts_with {
     fc_assert_exception_message_starts_with( const string& msg )
           : expected( msg ) {}

     bool operator()( const fc::assert_exception& ex );

     string expected;
  };

  /**
   * Utility predicate to check whether an eosio_assert message is equivalent to a given string
   */
  struct eosio_assert_message_is {
     eosio_assert_message_is( const string& msg )
           : expected( "assertion failure with message: " ) {
        expected.append( msg );
     }

     bool operator()( const eosio_assert_message_exception& ex );

     string expected;
  };

  /**
   * Utility predicate to check whether an eosio_assert message starts with a given string
   */
  struct eosio_assert_message_starts_with {
     eosio_assert_message_starts_with( const string& msg )
           : expected( "assertion failure with message: " ) {
        expected.append( msg );
     }

     bool operator()( const eosio_assert_message_exception& ex );

     string expected;
  };

  /**
   * Utility predicate to check whether an eosio_assert_code error code is equivalent to a given number
   */
  struct eosio_assert_code_is {
     eosio_assert_code_is( uint64_t error_code )
           : expected( "assertion failure with error code: " ) {
        expected.append( std::to_string(error_code) );
     }

     bool operator()( const eosio_assert_code_exception& ex );

     string expected;
  };

} /// eosio::testing
