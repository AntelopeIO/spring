#include <eosio/chain/controller.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/permission_object.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/transaction.hpp>
#include <boost/test/unit_test.hpp>
#include <eosio/testing/tester.hpp>

#include "fork_test_utilities.hpp"
#include "test_cfd_transaction.hpp"

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;

BOOST_AUTO_TEST_SUITE(chain_tests)

BOOST_AUTO_TEST_CASE( replace_producer_keys ) try {
   legacy_validating_tester tester;

   const auto new_key = get_public_key(name("newkey"), config::active_name.to_string());

   // make sure new keys is not used
   for(const auto& prod : tester.control->active_producers().producers) {
      for(const auto& key : std::get<block_signing_authority_v0>(prod.authority).keys){  
         BOOST_REQUIRE(key.key != new_key);
      }
   }

   // TODO: Add test with instant-finality enabled
   BOOST_REQUIRE(tester.control->pending_producers_legacy());
   const auto old_pending_version = tester.control->pending_producers_legacy()->version;
   const auto old_version = tester.control->active_producers().version;
   BOOST_REQUIRE_NO_THROW(tester.control->replace_producer_keys(new_key));
   const auto new_version = tester.control->active_producers().version;
   BOOST_REQUIRE(tester.control->pending_producers_legacy());
   const auto pending_version = tester.control->pending_producers_legacy()->version;
   // make sure version not been changed
   BOOST_REQUIRE(old_version == new_version);
   BOOST_REQUIRE(old_version == pending_version);
   BOOST_REQUIRE(pending_version == old_pending_version);

   const auto& gpo = tester.control->db().template get<global_property_object>();
   BOOST_REQUIRE(!gpo.proposed_schedule_block_num);
   BOOST_REQUIRE(gpo.proposed_schedule.version == 0);
   BOOST_REQUIRE(gpo.proposed_schedule.producers.empty());

   const uint32_t expected_threshold = 1;
   const weight_type expected_key_weight = 1;
   BOOST_REQUIRE(tester.control->pending_producers_legacy());
   for(const auto& prod : tester.control->pending_producers_legacy()->producers) {
      BOOST_REQUIRE_EQUAL(std::get<block_signing_authority_v0>(prod.authority).threshold, expected_threshold);
      for(const auto& key : std::get<block_signing_authority_v0>(prod.authority).keys){
         BOOST_REQUIRE_EQUAL(key.key, new_key);
         BOOST_REQUIRE_EQUAL(key.weight, expected_key_weight);
       }
   }
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( replace_account_keys, T, validating_testers ) try {
   T tester;
   const name usr = config::system_account_name;
   const name active_permission = config::active_name;
   const auto& rlm = tester.control->get_resource_limits_manager();
   const auto* perm = tester.control->db().template find<permission_object, by_owner>(boost::make_tuple(usr, active_permission));
   BOOST_REQUIRE(perm != NULL);

   const int64_t old_size = (int64_t)(chain::config::billable_size_v<permission_object> + perm->auth.get_billable_size());
   const auto old_usr_auth = perm->auth;
   const auto new_key = get_public_key(name("newkey"), "active");
   const authority expected_authority(new_key);
   BOOST_REQUIRE(old_usr_auth != expected_authority);
   const auto old_ram_usg = rlm.get_account_ram_usage(usr);

   BOOST_REQUIRE_NO_THROW(tester.control->replace_account_keys(usr, active_permission, new_key));
   const int64_t new_size = (int64_t)(chain::config::billable_size_v<permission_object> + perm->auth.get_billable_size());
   const auto new_ram_usg = rlm.get_account_ram_usage(usr);
   BOOST_REQUIRE_EQUAL(old_ram_usg + (new_size - old_size), new_ram_usg);
   const auto new_usr_auth = perm->auth;
   BOOST_REQUIRE(new_usr_auth == expected_authority);

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( decompressed_size_over_limit, T, testers ) try {
   T chain;

   // build a transaction, add cf data, sign
   cf_action                        cfa;
   eosio::chain::signed_transaction trx;
   eosio::chain::action             act({}, cfa);
   trx.context_free_actions.push_back(act);
   // this is a over limit size (4+4)*129*1024 = 1032*1024 > 1M
   for(int i = 0; i < 129*1024; ++i){
      trx.context_free_data.emplace_back(fc::raw::pack<uint32_t>(100));
      trx.context_free_data.emplace_back(fc::raw::pack<uint32_t>(200));
   }
   // add a normal action along with cfa
   dummy_action         da = {DUMMY_ACTION_DEFAULT_A, DUMMY_ACTION_DEFAULT_B, DUMMY_ACTION_DEFAULT_C};
   eosio::chain::action act1(
       std::vector<eosio::chain::permission_level>{{"testapi"_n, eosio::chain::config::active_name}}, da);
   trx.actions.push_back(act1);
   chain.set_transaction_headers(trx);
   auto sig = trx.sign(chain.get_private_key("testapi"_n, "active"), chain.get_chain_id());

   // pack
   packed_transaction pt(trx, packed_transaction::compression_type::zlib);
   // try unpack and throw
   bytes packed_txn = pt.get_packed_transaction();
   bytes pcfd = pt.get_packed_context_free_data();
   vector<signature_type>  sigs;
   sigs.push_back(sig);
   BOOST_REQUIRE_EXCEPTION(packed_transaction copy( std::move(packed_txn), std::move(sigs), std::move(pcfd), packed_transaction::compression_type::zlib ),
                           tx_decompression_error,
                           [](const tx_decompression_error& e) {
                              return e.to_detail_string().find("Exceeded maximum decompressed transaction size") != std::string::npos;
                           });
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE_TEMPLATE( decompressed_size_under_limit, T, testers ) try {
   T chain;

   // build a transaction, add cf data, sign
   cf_action                        cfa;
   eosio::chain::signed_transaction trx;
   eosio::chain::action             act({}, cfa);
   trx.context_free_actions.push_back(act);
   // this is a under limit size  (4+4)*128*1024 = 1024*1024
   for(int i = 0; i < 100*1024; ++i){
      trx.context_free_data.emplace_back(fc::raw::pack<uint32_t>(100));
      trx.context_free_data.emplace_back(fc::raw::pack<uint32_t>(200));
   }
   // add a normal action along with cfa
   dummy_action         da = {DUMMY_ACTION_DEFAULT_A, DUMMY_ACTION_DEFAULT_B, DUMMY_ACTION_DEFAULT_C};
   eosio::chain::action act1(
       std::vector<eosio::chain::permission_level>{{"testapi"_n, eosio::chain::config::active_name}}, da);
   trx.actions.push_back(act1);
   chain.set_transaction_headers(trx);
   auto sig = trx.sign(chain.get_private_key("testapi"_n, "active"), chain.get_chain_id());

   // pack
   packed_transaction pt(trx, packed_transaction::compression_type::zlib);
   // try unpack
   bytes packed_txn = pt.get_packed_transaction();
   bytes pcfd = pt.get_packed_context_free_data();
   vector<signature_type>  sigs;
   sigs.push_back(sig);
   packed_transaction copy( std::move(packed_txn), std::move(sigs), std::move(pcfd), packed_transaction::compression_type::zlib );
   //passes if no exception is thrown

} FC_LOG_AND_RETHROW()

// verify accepted_block signals validated blocks
BOOST_AUTO_TEST_CASE_TEMPLATE( signal_validated_blocks, T, testers ) try {
   T chain;
   T validator;

   signed_block_ptr accepted_block;
   block_id_type accepted_id;
   auto c = chain.control->accepted_block().connect([&](block_signal_params t) {
      const auto& [ block, id ] = t;
      auto block_num = block->block_num();
      BOOST_CHECK(block);
      BOOST_CHECK(chain.fetch_block_by_id(id) == block);
      BOOST_CHECK(chain.block_exists(id));
      BOOST_CHECK(chain.fetch_block_by_number(block_num) == block);
      BOOST_REQUIRE(chain.control->fetch_block_header_by_number(block_num));
      BOOST_CHECK(chain.control->fetch_block_header_by_number(block_num)->calculate_id() == id);
      BOOST_REQUIRE(chain.control->fetch_block_header_by_id(id));
      BOOST_CHECK(chain.control->fetch_block_header_by_id(id)->calculate_id() == id);
      accepted_block = block;
      accepted_id = id;
   });
   signed_block_ptr validated_block;
   block_id_type validated_id;
   auto c2 = validator.control->accepted_block().connect([&](block_signal_params t) {
      const auto& [ block, id ] = t;
      auto block_num = block->block_num();
      BOOST_CHECK(block);
      BOOST_CHECK(validator.fetch_block_by_id(id) == block);
      BOOST_CHECK(validator.block_exists(id));
      BOOST_CHECK(validator.fetch_block_by_number(block_num) == block);
      BOOST_REQUIRE(validator.control->fetch_block_header_by_number(block_num));
      BOOST_CHECK(validator.control->fetch_block_header_by_number(block_num)->calculate_id() == id);
      BOOST_REQUIRE(validator.control->fetch_block_header_by_id(id));
      BOOST_CHECK(validator.control->fetch_block_header_by_id(id)->calculate_id() == id);
      validated_block = block;
      validated_id = id;
   });

   chain.produce_block();
   validator.push_block(accepted_block);

   chain.create_account("hello"_n);
   auto produced_block = chain.produce_block();
   validator.push_block(accepted_block);
   BOOST_CHECK(produced_block->calculate_id() == accepted_id);
   BOOST_CHECK(accepted_id == validated_id);

} FC_LOG_AND_RETHROW()

// verify applied_transaction signals trx in blocks
BOOST_AUTO_TEST_CASE_TEMPLATE( signal_applied_transaction, T, testers ) try {
   T chain;

   chain.produce_block();

   transaction_trace_ptr last_trace;
   auto c = chain.control->applied_transaction().connect([&](std::tuple<const transaction_trace_ptr&, const packed_transaction_ptr&> x) {
      auto& t = std::get<0>(x);
      if (std::get<1>(x)->get_transaction().actions.at(0).name != "onblock"_n)
         last_trace = t;
   } );

   {
      transaction_trace_ptr create_account_trace = chain.create_account("hello"_n);
      BOOST_REQUIRE(last_trace);
      BOOST_REQUIRE(create_account_trace);
      BOOST_TEST(create_account_trace->id == last_trace->id);
      BOOST_TEST(create_account_trace->elapsed.count() == last_trace->elapsed.count());
      BOOST_TEST(create_account_trace->block_num == last_trace->block_num);
      signed_block_ptr block = chain.produce_block();
      BOOST_REQUIRE(block);
      bool found = false;
      for (auto& r : block->transactions) {
         std::visit(overloaded{
            [](const transaction_id_type& id) {},
            [&](const packed_transaction& x) {
            found = true;
            BOOST_TEST(x.get_transaction().actions.at(0).name == "newaccount"_n);
         }}, r.trx);
      }
      BOOST_TEST(found);
   }
   // abort block with create account
   {
      last_trace = nullptr;
      transaction_trace_ptr create_account_trace = chain.create_account("hello2"_n);
      BOOST_REQUIRE(last_trace);
      BOOST_REQUIRE(create_account_trace);
      BOOST_TEST(create_account_trace->id == last_trace->id);
      BOOST_TEST(create_account_trace->elapsed.count() == last_trace->elapsed.count());
      BOOST_TEST(create_account_trace->block_num == last_trace->block_num);
      signed_block_ptr empty_block = chain.produce_empty_block(); // aborts block, places in unapplied trx queue
      auto trx_meta = chain.get_unapplied_transaction_queue().get_trx(create_account_trace->id);
      BOOST_REQUIRE(trx_meta);
      BOOST_REQUIRE(empty_block);
      BOOST_TEST(empty_block->transactions.empty());
      last_trace = nullptr;
      signed_block_ptr block = chain.produce_block();
      BOOST_REQUIRE(block);
      BOOST_REQUIRE(last_trace);
      BOOST_TEST(create_account_trace->id == last_trace->id);
      BOOST_TEST(create_account_trace->block_num < last_trace->block_num); // different block
      BOOST_TEST(trx_meta->elapsed_time_us == std::max(last_trace->elapsed.count(), create_account_trace->elapsed.count())); // verify trx_meta updated
      BOOST_TEST(block->block_num() == last_trace->block_num);
      bool found = false;
      for (auto& r : block->transactions) {
         std::visit(overloaded{
            [](const transaction_id_type& id) {},
            [&](const packed_transaction& x) {
            found = true;
            BOOST_TEST(x.get_transaction().actions.at(0).name == "newaccount"_n);
         }}, r.trx);
      }
      BOOST_TEST(found);
   }

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
