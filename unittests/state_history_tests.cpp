#include <eosio/state_history/serialization.hpp>
#include <eosio/chain/authorization_manager.hpp>
#include <boost/test/unit_test.hpp>
#include <contracts.hpp>
#include <test_contracts.hpp>
#include <eosio/state_history/abi.hpp>
#include <eosio/state_history/create_deltas.hpp>
#include <eosio/state_history/log_catalog.hpp>
#include <eosio/state_history/trace_converter.hpp>
#include <eosio/testing/tester.hpp>
#include <fc/io/json.hpp>
#include <fc/io/cfile.hpp>
#include <eosio/chain/global_property_object.hpp>

#include "test_cfd_transaction.hpp"

#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/copy.hpp>

using namespace eosio::chain;
using namespace eosio::testing;
using namespace std::literals;

static const abi_serializer::yield_function_t null_yield_function{};

namespace eosio::state_history {

template <typename ST>
datastream<ST>& operator>>(datastream<ST>& ds, row_pair& rp) {
   fc::raw::unpack(ds, rp.first);
   fc::unsigned_int sz;
   fc::raw::unpack(ds, sz);
   if(sz) {
      rp.second.resize(sz);
      ds.read(rp.second.data(), sz);
   }
   return ds;
}

template <typename ST, typename T>
datastream<ST>& operator>>(datastream<ST>& ds, eosio::state_history::big_vector_wrapper<T>& obj) {
   fc::unsigned_int sz;
   fc::raw::unpack(ds, sz);
   obj.obj.resize(sz);
   for (auto& x : obj.obj)
      fc::raw::unpack(ds, x);
   return ds;
}

std::vector<table_delta> create_deltas(const chainbase::database& db, bool full_snapshot) {
   namespace bio = boost::iostreams;
   std::vector<char> buf;
   bio::filtering_ostreambuf obuf;
   obuf.push(bio::back_inserter(buf));
   pack_deltas(obuf, db, full_snapshot);

   fc::datastream<const char*> is{buf.data(), buf.size()};
   std::vector<table_delta> result;
   fc::raw::unpack(is, result);
   return result;
}
}

BOOST_AUTO_TEST_SUITE(test_state_history)

template<typename T>
class table_deltas_tester : public T {
public:
   using T::T;
   using deltas_vector = vector<eosio::state_history::table_delta>;

   pair<bool, deltas_vector::iterator> find_table_delta(const std::string &name, bool full_snapshot = false) {
      v = eosio::state_history::create_deltas(T::control->db(), full_snapshot);;

      auto find_by_name = [&name](const auto& x) {
         return x.name == name;
      };

      auto it = std::find_if(v.begin(), v.end(), find_by_name);

      return make_pair(it != v.end(), it);
   }

   variants deserialize_data(deltas_vector::iterator &it, const std::string& type, const std::string& in_variant_type) {
      variants result;
      for(size_t i=0; i < it->rows.obj.size(); i++) {
         fc::variant v = shipabi.binary_to_variant(in_variant_type, it->rows.obj[i].second, null_yield_function);
         BOOST_REQUIRE(v.is_array() && v.size() == 2 && v[0ul].is_string());
         BOOST_REQUIRE_EQUAL(v[0ul].get_string(), type);
         result.push_back(std::move(v[1ul]));
      }
      return result;
   }

private:
   deltas_vector v;
   abi_serializer shipabi = abi_serializer(json::from_string(eosio::state_history::ship_abi_without_tables()).as<abi_def>(), null_yield_function);
};

using table_deltas_testers = boost::mpl::list<table_deltas_tester<legacy_tester>,
                                              table_deltas_tester<savanna_tester>>;
using testers = boost::mpl::list<legacy_tester, savanna_tester>;

BOOST_AUTO_TEST_CASE_TEMPLATE(test_deltas_not_empty, T, table_deltas_testers) {
   T chain;

   auto deltas = eosio::state_history::create_deltas(chain.control->db(), false);

   for(const auto &delta: deltas) {
      BOOST_REQUIRE(!delta.rows.obj.empty());
   }
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_deltas_account_creation, T, table_deltas_testers) {
   T chain;
   chain.produce_block();

   // Check that no account table deltas are present
   BOOST_REQUIRE_EQUAL(chain.find_table_delta("account").first, false);

   // Create new account
   chain.create_account("newacc"_n);

   // Verify that a new record for the new account in the state delta of the block
   auto result = chain.find_table_delta("account");
   BOOST_REQUIRE(result.first);
   auto &it_account = result.second;
   BOOST_REQUIRE_EQUAL(it_account->rows.obj.size(), 1u);

   const variants accounts = chain.deserialize_data(it_account, "account_v0", "account");
   BOOST_REQUIRE_EQUAL(accounts[0]["name"].get_string(), "newacc");
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_deltas_account_metadata, T, table_deltas_testers) {
   T chain;
   chain.produce_block();

   chain.create_account("newacc"_n);

   // Spot onto account metadata
   auto result = chain.find_table_delta("account_metadata");
   BOOST_REQUIRE(result.first);
   auto &it_account_metadata = result.second;
   BOOST_REQUIRE_EQUAL(it_account_metadata->rows.obj.size(), 1u);

   const variants accounts_metadata = chain.deserialize_data(it_account_metadata, "account_metadata_v0", "account_metadata");
   BOOST_REQUIRE_EQUAL(accounts_metadata[0]["name"].get_string(), "newacc");
   BOOST_REQUIRE_EQUAL(accounts_metadata[0]["privileged"].as_bool(), false);
}


BOOST_AUTO_TEST_CASE_TEMPLATE(test_deltas_account_permission, T, table_deltas_testers) {
   T chain;
   chain.produce_block();

   chain.create_account("newacc"_n);

   // Check that the permissions of this new account are in the delta
   vector<string> expected_permission_names{ "owner", "active" };
   auto result = chain.find_table_delta("permission");
   BOOST_REQUIRE(result.first);
   auto &it_permission = result.second;
   BOOST_REQUIRE_EQUAL(it_permission->rows.obj.size(), 2u);
   const variants accounts_permissions = chain.deserialize_data(it_permission, "permission_v0", "permission");
   for(size_t i = 0; i < accounts_permissions.size(); i++)
   {
      BOOST_REQUIRE_EQUAL(it_permission->rows.obj[i].first, true);
      BOOST_REQUIRE_EQUAL(accounts_permissions[i]["owner"].get_string(), "newacc");
      BOOST_REQUIRE_EQUAL(accounts_permissions[i]["name"].get_string(), expected_permission_names[i]);
   }
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_deltas_account_permission_creation_and_deletion, T, table_deltas_testers) {
   T chain;
   chain.produce_block();

   chain.create_account("newacc"_n);

   auto& authorization_manager = chain.control->get_authorization_manager();
   const permission_object* ptr = authorization_manager.find_permission( {"newacc"_n, "active"_n} );
   BOOST_REQUIRE(ptr != nullptr);

   // Create new permission
   chain.set_authority("newacc"_n, "mypermission"_n, ptr->auth.to_authority(),  "active"_n);

   const permission_object* ptr_sub = authorization_manager.find_permission( {"newacc"_n, "mypermission"_n} );
   BOOST_REQUIRE(ptr_sub != nullptr);

   // Verify that the new permission is present in the state delta
   std::vector<std::string> expected_permission_names{ "owner", "active", "mypermission" };
   auto result = chain.find_table_delta("permission");
   BOOST_REQUIRE(result.first);
   auto &it_permission = result.second;
   BOOST_REQUIRE_EQUAL(it_permission->rows.obj.size(), 3u);
   BOOST_REQUIRE_EQUAL(it_permission->rows.obj[2].first, true);
   variants accounts_permissions = chain.deserialize_data(it_permission, "permission_v0", "permission");
   BOOST_REQUIRE_EQUAL(accounts_permissions[2]["owner"].get_string(), "newacc");
   BOOST_REQUIRE_EQUAL(accounts_permissions[2]["name"].get_string(), "mypermission");
   BOOST_REQUIRE_EQUAL(accounts_permissions[2]["parent"].get_string(), "active");

   chain.produce_block();

   // Delete the permission
   chain.delete_authority("newacc"_n, "mypermission"_n);

   result = chain.find_table_delta("permission");
   BOOST_REQUIRE(result.first);
   auto &it_permission_del = result.second;
   BOOST_REQUIRE_EQUAL(it_permission_del->rows.obj.size(), 1u);
   BOOST_REQUIRE_EQUAL(it_permission_del->rows.obj[0].first, false);
   accounts_permissions = chain.deserialize_data(it_permission_del, "permission_v0", "permission");
   BOOST_REQUIRE_EQUAL(accounts_permissions[0]["owner"].get_string(), "newacc");
   BOOST_REQUIRE_EQUAL(accounts_permissions[0]["name"].get_string(), "mypermission");
   BOOST_REQUIRE_EQUAL(accounts_permissions[0]["parent"].get_string(), "active");
}


BOOST_AUTO_TEST_CASE_TEMPLATE(test_deltas_account_permission_modification, T, table_deltas_testers) {
   T chain;
   chain.produce_block();

   chain.create_account("newacc"_n);
   chain.produce_block();
   public_key_type keys[] = {
         public_key_type("PUB_WA_WdCPfafVNxVMiW5ybdNs83oWjenQXvSt1F49fg9mv7qrCiRwHj5b38U3ponCFWxQTkDsMC"s), // Test for correct serialization of WA key, see issue #9087
         public_key_type("PUB_K1_12wkBET2rRgE8pahuaczxKbmv7ciehqsne57F9gtzf1PVb7Rf7o"s),
         public_key_type("PUB_R1_6FPFZqw5ahYrR9jD96yDbbDNTdKtNqRbze6oTDLntrsANgQKZu"s)};

   for(auto &key: keys) {
      // Modify the permission authority
      auto wa_authority = authority(1, {key_weight{key, 1}}, {});
      chain.set_authority("newacc"_n, "active"_n, wa_authority, "owner"_n);

      auto result = chain.find_table_delta("permission");
      BOOST_REQUIRE(result.first);

      auto &it_permission = result.second;
      BOOST_REQUIRE_EQUAL(it_permission->rows.obj.size(), 1u);
      const variants accounts_permissions = chain.deserialize_data(it_permission, "permission_v0", "permission");
      BOOST_REQUIRE_EQUAL(accounts_permissions[0]["owner"].get_string(), "newacc");
      BOOST_REQUIRE_EQUAL(accounts_permissions[0]["name"].get_string(), "active");
      BOOST_REQUIRE_EQUAL(accounts_permissions[0]["auth"]["keys"].size(), 1u);
      BOOST_REQUIRE_EQUAL(accounts_permissions[0]["auth"]["keys"][0ul]["key"].get_string(), key.to_string({}));

      chain.produce_block();
   }
}


BOOST_AUTO_TEST_CASE_TEMPLATE(test_deltas_permission_link, T, table_deltas_testers) {
   T chain;
   chain.produce_block();

   chain.create_account("newacc"_n);

   // Spot onto permission_link
   const auto spending_priv_key = chain.get_private_key("newacc"_n, "spending");
   const auto spending_pub_key = spending_priv_key.get_public_key();

   chain.set_authority("newacc"_n, "spending"_n, authority{spending_pub_key}, "active"_n);
   chain.link_authority("newacc"_n, "eosio"_n, "spending"_n, "reqauth"_n);
   chain.push_reqauth("newacc"_n, { permission_level{"newacc"_n, "spending"_n} }, { spending_priv_key });


   auto result = chain.find_table_delta("permission_link");
   BOOST_REQUIRE(result.first);
   auto &it_permission_link = result.second;
   BOOST_REQUIRE_EQUAL(it_permission_link->rows.obj.size(), 1u);
   const variants permission_links = chain.deserialize_data(it_permission_link, "permission_link_v0", "permission_link");
   BOOST_REQUIRE_EQUAL(permission_links[0]["account"].get_string(), "newacc");
   BOOST_REQUIRE_EQUAL(permission_links[0]["message_type"].get_string(), "reqauth");
   BOOST_REQUIRE_EQUAL(permission_links[0]["required_permission"].get_string(), "spending");
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_deltas_global_property_history, T, table_deltas_testers) {
   // Assuming max transaction delay is 45 days (default in config.hpp)
   T chain;

   // Change max_transaction_delay to 60 sec
   auto params = chain.control->get_global_properties().configuration;
   params.max_transaction_delay = 60;
   chain.push_action( config::system_account_name, "setparams"_n, config::system_account_name,
                             mutable_variant_object()
                             ("params", params) );

   // Deserialize and spot onto some data
   auto result = chain.find_table_delta("global_property");
   BOOST_REQUIRE(result.first);
   auto &it_global_property = result.second;
   BOOST_REQUIRE_EQUAL(it_global_property->rows.obj.size(), 1u);
   const variants global_properties = chain.deserialize_data(it_global_property, "global_property_v1", "global_property");
   BOOST_REQUIRE_EQUAL(global_properties[0]["configuration"][0ul].get_string(), "chain_config_v1");
   BOOST_REQUIRE_EQUAL(global_properties[0]["configuration"][1ul]["max_transaction_delay"].as_uint64(), 60u);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_deltas_protocol_feature_history, T, table_deltas_testers) {
   T chain(setup_policy::none);
   const auto &pfm = chain.control->get_protocol_feature_manager();

   chain.produce_block();

   auto d = pfm.get_builtin_digest(builtin_protocol_feature_t::preactivate_feature);
   BOOST_REQUIRE(d);

   // Activate PREACTIVATE_FEATURE.
   chain.schedule_protocol_features_wo_preactivation({*d});

   chain.produce_block();

   // Now the latest bios contract can be set.
   chain.set_before_producer_authority_bios_contract();

   // Spot onto some data of the protocol state table delta
   auto result = chain.find_table_delta("protocol_state");
   BOOST_REQUIRE(result.first);
   auto &it_protocol_state = result.second;
   BOOST_REQUIRE_EQUAL(it_protocol_state->rows.obj.size(), 1u);
   const variants protocol_states = chain.deserialize_data(it_protocol_state, "protocol_state_v0", "protocol_state");
   BOOST_REQUIRE_EQUAL(protocol_states[0]["activated_protocol_features"][0ul][0ul].get_string(), "activated_protocol_feature_v0");
   const digest_type digest_in_delta = protocol_states[0]["activated_protocol_features"][0ul][1ul]["feature_digest"].as<digest_type>();
   BOOST_REQUIRE_EQUAL(digest_in_delta, *d);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_deltas_contract, T, table_deltas_testers) {
   T chain;
   chain.produce_block();

   chain.create_account("tester"_n);

   chain.set_code("tester"_n, test_contracts::get_table_test_wasm());
   chain.set_abi("tester"_n, test_contracts::get_table_test_abi());

   chain.produce_block();

   auto trace = chain.push_action("tester"_n, "addhashobj"_n, "tester"_n, mutable_variant_object()("hashinput", "hello" ));

   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   trace = chain.push_action("tester"_n, "addnumobj"_n, "tester"_n, mutable_variant_object()("input", 2));

   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   // Spot onto contract_table
   auto result = chain.find_table_delta("contract_table");
   BOOST_REQUIRE(result.first);
   auto &it_contract_table = result.second;
   BOOST_REQUIRE_EQUAL(it_contract_table->rows.obj.size(), 6u);
   const variants contract_tables = chain.deserialize_data(it_contract_table, "contract_table_v0", "contract_table");
   BOOST_REQUIRE_EQUAL(contract_tables[0]["table"].get_string(), "hashobjs");
   BOOST_REQUIRE_EQUAL(contract_tables[1]["table"].get_string(), "hashobjs....1");
   BOOST_REQUIRE_EQUAL(contract_tables[2]["table"].get_string(), "numobjs");
   BOOST_REQUIRE_EQUAL(contract_tables[3]["table"].get_string(), "numobjs.....1");
   BOOST_REQUIRE_EQUAL(contract_tables[4]["table"].get_string(), "numobjs.....2");
   BOOST_REQUIRE_EQUAL(contract_tables[5]["table"].get_string(), "numobjs.....3");

   // Spot onto contract_row
   result = chain.find_table_delta("contract_row");
   BOOST_REQUIRE(result.first);
   auto &it_contract_row = result.second;
   BOOST_REQUIRE_EQUAL(it_contract_row->rows.obj.size(), 2u);
   const variants contract_rows = chain.deserialize_data(it_contract_row, "contract_row_v0", "contract_row");
   BOOST_REQUIRE_EQUAL(contract_rows[0]["table"].get_string(), "hashobjs");
   BOOST_REQUIRE_EQUAL(contract_rows[1]["table"].get_string(), "numobjs");

   // Spot onto contract_index256
   result = chain.find_table_delta("contract_index256");
   BOOST_REQUIRE(result.first);
   auto &it_contract_index256 = result.second;
   BOOST_REQUIRE_EQUAL(it_contract_index256->rows.obj.size(), 2u);
   const variants contract_indices = chain.deserialize_data(it_contract_index256, "contract_index256_v0", "contract_index256");
   BOOST_REQUIRE_EQUAL(contract_indices[0]["table"].get_string(), "hashobjs");
   BOOST_REQUIRE_EQUAL(contract_indices[1]["table"].get_string(), "hashobjs....1");
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_deltas_resources_history, T, table_deltas_testers) {
   T chain;
   chain.produce_block();

   chain.create_accounts({ "eosio.token"_n, "eosio.ram"_n, "eosio.ramfee"_n, "eosio.stake"_n, "eosio.rex"_n});

   chain.produce_block();

   chain.set_code( "eosio.token"_n, test_contracts::eosio_token_wasm() );
   chain.set_abi( "eosio.token"_n, test_contracts::eosio_token_abi() );

   chain.produce_block();

   chain.push_action("eosio.token"_n, "create"_n, "eosio.token"_n, mutable_variant_object()
      ("issuer", "eosio.token" )
      ("maximum_supply", core_from_string("1000000000.0000") )
   );

   chain.push_action("eosio.token"_n, "issue"_n, "eosio.token"_n, fc::mutable_variant_object()
      ("to",       "eosio")
      ("quantity", core_from_string("90.0000"))
      ("memo", "for stuff")
   );

   chain.produce_block();

   chain.set_code( config::system_account_name, test_contracts::eosio_system_wasm() );
   chain.set_abi( config::system_account_name, test_contracts::eosio_system_abi() );

   chain.push_action(config::system_account_name, "init"_n, config::system_account_name,
                        mutable_variant_object()
                        ("version", 0)
                        ("core", symbol(CORE_SYMBOL).to_string()));

   signed_transaction trx;
   chain.set_transaction_headers(trx);

   authority owner_auth;
   owner_auth =  authority( chain.get_public_key( "alice"_n, "owner" ) );

   trx.actions.emplace_back( vector<permission_level>{{config::system_account_name,config::active_name}},
                                newaccount{
                                    .creator  = config::system_account_name,
                                    .name     =  "alice"_n,
                                    .owner    = owner_auth,
                                    .active   = authority( chain.get_public_key( "alice"_n, "active" ) )});

   trx.actions.emplace_back( chain.get_action( config::system_account_name, "buyram"_n, vector<permission_level>{{config::system_account_name,config::active_name}},
                                                  mutable_variant_object()
                                                      ("payer", config::system_account_name)
                                                      ("receiver",  "alice"_n)
                                                      ("quant", core_from_string("1.0000"))));

   trx.actions.emplace_back( chain.get_action( config::system_account_name, "delegatebw"_n, vector<permission_level>{{config::system_account_name,config::active_name}},
                                                  mutable_variant_object()
                                                      ("from", config::system_account_name)
                                                      ("receiver",  "alice"_n)
                                                      ("stake_net_quantity", core_from_string("10.0000") )
                                                      ("stake_cpu_quantity", core_from_string("10.0000") )
                                                      ("transfer", 0 )));

   chain.set_transaction_headers(trx);
   trx.sign( chain.get_private_key( config::system_account_name, "active" ), chain.get_chain_id()  );
   chain.push_transaction( trx );
}

   BOOST_AUTO_TEST_CASE_TEMPLATE(test_deltas, T, testers) {
      T main;

      auto v = eosio::state_history::create_deltas(main.control->db(), false);

      std::string name="permission";
      auto find_by_name = [&name](const auto& x) {
         return x.name == name;
      };

      auto it = std::find_if(v.begin(), v.end(), find_by_name);
      BOOST_REQUIRE(it==v.end());

      name="resource_limits";
      it = std::find_if(v.begin(), v.end(), find_by_name);
      BOOST_REQUIRE(it==v.end());

      main.create_account("newacc"_n);

      v = eosio::state_history::create_deltas(main.control->db(), false);

      name="permission";
      it = std::find_if(v.begin(), v.end(), find_by_name);
      BOOST_REQUIRE(it!=v.end());

      name="resource_limits";
      it = std::find_if(v.begin(), v.end(), find_by_name);
      BOOST_REQUIRE(it!=v.end());

      main.produce_block();

      v = eosio::state_history::create_deltas(main.control->db(), false);

      name="permission";
      it = std::find_if(v.begin(), v.end(), find_by_name);
      BOOST_REQUIRE(it==v.end());

      name="resource_limits";
      it = std::find_if(v.begin(), v.end(), find_by_name);
      BOOST_REQUIRE(it==v.end());
   }

   BOOST_AUTO_TEST_CASE_TEMPLATE(test_deltas_contract_several_rows, T, table_deltas_testers) {
      T chain;

      chain.produce_block();
      chain.create_account("tester"_n);

      chain.set_code("tester"_n, test_contracts::get_table_test_wasm());
      chain.set_abi("tester"_n, test_contracts::get_table_test_abi());

      chain.produce_block();

      auto trace = chain.push_action("tester"_n, "addhashobj"_n, "tester"_n, mutable_variant_object()("hashinput", "hello"));
      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

      trace = chain.push_action("tester"_n, "addhashobj"_n, "tester"_n, mutable_variant_object()("hashinput", "world"));
      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

      trace = chain.push_action("tester"_n, "addhashobj"_n, "tester"_n, mutable_variant_object()("hashinput", "!"));
      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

      trace = chain.push_action("tester"_n, "addnumobj"_n, "tester"_n, mutable_variant_object()("input", 2));
      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

      trace = chain.push_action("tester"_n, "addnumobj"_n, "tester"_n, mutable_variant_object()("input", 3));
      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

      trace = chain.push_action("tester"_n, "addnumobj"_n, "tester"_n, mutable_variant_object()("input", 4));
      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

      // Spot onto contract_row with full snapshot
      auto result = chain.find_table_delta("contract_row", true);
      BOOST_REQUIRE(result.first);
      auto &it_contract_row = result.second;
      BOOST_REQUIRE_EQUAL(it_contract_row->rows.obj.size(), 8u);
      variants contract_rows = chain.deserialize_data(it_contract_row, "contract_row_v0", "contract_row");

      std::multiset<std::string> expected_contract_row_table_names {"abihash", "abihash", "hashobjs", "hashobjs", "hashobjs", "numobjs", "numobjs", "numobjs"};

      std::multiset<uint64_t> expected_contract_row_table_primary_keys {6138663577826885632U,14605619288908759040U, 0, 1 ,2, 0, 1, 2};
      std::multiset<std::string> result_contract_row_table_names;
      std::multiset<uint64_t> result_contract_row_table_primary_keys;
      for(auto &contract_row : contract_rows) {
         result_contract_row_table_names.insert(contract_row["table"].get_string());
         result_contract_row_table_primary_keys.insert(contract_row["primary_key"].as_uint64());
      }
      BOOST_REQUIRE(expected_contract_row_table_names == result_contract_row_table_names);
      BOOST_REQUIRE(expected_contract_row_table_primary_keys == result_contract_row_table_primary_keys);

      chain.produce_block();

      trace = chain.push_action("tester"_n, "erasenumobj"_n, "tester"_n, mutable_variant_object()("id", 1));
      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

      trace = chain.push_action("tester"_n, "erasenumobj"_n, "tester"_n, mutable_variant_object()("id", 0));
      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

      result = chain.find_table_delta("contract_row");
      BOOST_REQUIRE(result.first);
      BOOST_REQUIRE_EQUAL(it_contract_row->rows.obj.size(), 2u);
      contract_rows = chain.deserialize_data(it_contract_row, "contract_row_v0", "contract_row");

      for(size_t i=0; i < contract_rows.size(); i++) {
         BOOST_REQUIRE_EQUAL(it_contract_row->rows.obj[i].first, 0);
         BOOST_REQUIRE_EQUAL(contract_rows[i]["table"].get_string(), "numobjs");
      }

      result = chain.find_table_delta("contract_index_double");
      BOOST_REQUIRE(result.first);
      auto &it_contract_index_double = result.second;
      BOOST_REQUIRE_EQUAL(it_contract_index_double->rows.obj.size(), 2u);
      const variants contract_index_double_elems = chain.deserialize_data(it_contract_index_double, "contract_index_double_v0", "contract_index_double");

      for(size_t i=0; i < contract_index_double_elems.size(); i++) {
         BOOST_REQUIRE_EQUAL(it_contract_index_double->rows.obj[i].first, 0);
         BOOST_REQUIRE_EQUAL(contract_index_double_elems[i]["table"].get_string(), "numobjs.....2");
      }

   }

   std::vector<shared_ptr<eosio::state_history::partial_transaction>> get_partial_txns(eosio::state_history::trace_converter& log) {
      std::vector<shared_ptr<eosio::state_history::partial_transaction>> partial_txns;

      for (auto ct : log.cached_traces) {
         partial_txns.push_back(std::get<1>(ct).partial);
      }

      return partial_txns;
   }

   BOOST_AUTO_TEST_CASE(test_trace_log_with_transaction_extensions) {
      tester_no_disable_deferred_trx c;

      fc::temp_directory state_history_dir;
      eosio::state_history::trace_converter log;

      c.control->applied_transaction().connect(
            [&](std::tuple<const transaction_trace_ptr&, const packed_transaction_ptr&> t) {
               log.add_transaction(std::get<0>(t), std::get<1>(t));
            });

      c.create_accounts({"alice"_n, "test"_n});
      c.set_code("test"_n, test_contracts::deferred_test_wasm());
      c.set_abi("test"_n, test_contracts::deferred_test_abi());
      c.produce_block();

      c.push_action("test"_n, "defercall"_n, "alice"_n,
                    fc::mutable_variant_object()("payer", "alice")("sender_id", 1)("contract", "test")("payload", 40));

      auto block  = c.produce_block();
      auto partial_txns = get_partial_txns(log);

      auto contains_transaction_extensions = [](shared_ptr<eosio::state_history::partial_transaction> txn) {
         return txn->transaction_extensions.size() > 0;
      };

      BOOST_CHECK(std::any_of(partial_txns.begin(), partial_txns.end(), contains_transaction_extensions));
   }

struct state_history_tester_logs  {
   state_history_tester_logs(const std::filesystem::path& dir, const eosio::state_history::state_history_log_config& config)
      : traces_log(dir, config, "trace_history") , chain_state_log(dir, config, "chain_state_history") {}

   eosio::state_history::log_catalog traces_log;
   eosio::state_history::log_catalog chain_state_log;
   eosio::state_history::trace_converter trace_converter;
};

template<typename T>
struct state_history_tester : state_history_tester_logs, T {
   state_history_tester(const std::filesystem::path& dir, const eosio::state_history::state_history_log_config& config)
   : state_history_tester_logs(dir, config), T ([this](eosio::chain::controller& control) {
      control.applied_transaction().connect(
       [&](std::tuple<const transaction_trace_ptr&, const packed_transaction_ptr&> t) {
          trace_converter.add_transaction(std::get<0>(t), std::get<1>(t));
       });

      control.accepted_block().connect([&](block_signal_params t) {
         const auto& [ block, id ] = t;

         traces_log.pack_and_write_entry(id, block->previous, [this, &block](auto&& buf) {
            trace_converter.pack(buf, false, block);
         });

         chain_state_log.pack_and_write_entry(id, block->previous, [&control](auto&& buf) {
            eosio::state_history::pack_deltas(buf, control.db(), true);
         });
      });
      control.block_start().connect([this](uint32_t block_num) {
         trace_converter.cached_traces.clear();
         trace_converter.onblock_trace.reset();
      });
   }) {}
};

using state_history_testers = boost::mpl::list<state_history_tester<legacy_tester>,
                                               state_history_tester<savanna_tester>>;

static std::vector<char> get_decompressed_entry(eosio::state_history::log_catalog& log, block_num_type block_num) {
   std::optional<eosio::state_history::ship_log_entry> entry = log.get_entry(block_num);
   if(!entry) //existing tests expect failure to find a block returns an empty vector here
      return {};

   namespace bio = boost::iostreams;
   bio::filtering_istreambuf istream = entry->get_stream();
   std::vector<char> bytes;
   bio::copy(istream, bio::back_inserter(bytes));
   return bytes;
}

static variants get_traces(eosio::state_history::log_catalog& log, block_num_type            block_num) {
   auto     entry = get_decompressed_entry(log, block_num);

   if (entry.size()) {
      abi_serializer shipabi = abi_serializer(json::from_string(eosio::state_history::ship_abi_without_tables()).as<abi_def>(), null_yield_function);
      return shipabi.binary_to_variant("transaction_trace[]", entry, null_yield_function).get_array();
   }
   return variants();
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_splitted_log, T, state_history_testers) {
   fc::temp_directory state_history_dir;

   eosio::state_history::partition_config config{
      .retained_dir = "retained",
      .archive_dir = "archive",
      .stride  = 20,
      .max_retained_files = 5
   };

   T chain(state_history_dir.path(), config);
   chain.produce_block();
   chain.produce_blocks(49, true);

   deploy_test_api(chain);
   auto cfd_trace = push_test_cfd_transaction(chain);

   chain.produce_block();
   chain.produce_blocks(99, true);

   auto log_dir = state_history_dir.path();
   auto archive_dir  = log_dir / "archive";
   auto retained_dir = log_dir / "retained";

   BOOST_CHECK(std::filesystem::exists( archive_dir / "trace_history-2-20.log" ));
   BOOST_CHECK(std::filesystem::exists( archive_dir / "trace_history-2-20.index" ));
   BOOST_CHECK(std::filesystem::exists( archive_dir / "trace_history-21-40.log" ));
   BOOST_CHECK(std::filesystem::exists( archive_dir / "trace_history-21-40.index" ));

   BOOST_CHECK(std::filesystem::exists( archive_dir / "chain_state_history-2-20.log" ));
   BOOST_CHECK(std::filesystem::exists( archive_dir / "chain_state_history-2-20.index" ));
   BOOST_CHECK(std::filesystem::exists( archive_dir / "chain_state_history-21-40.log" ));
   BOOST_CHECK(std::filesystem::exists( archive_dir / "chain_state_history-21-40.index" ));

   if constexpr (std::is_same_v<T, state_history_tester<savanna_tester>>) {
      // Under Savanna, logs are archived earlier because LIB advances faster.
      BOOST_CHECK(std::filesystem::exists( archive_dir / "trace_history-41-60.log" ));
      BOOST_CHECK(std::filesystem::exists( archive_dir / "trace_history-41-60.index" ));
   } else {
      BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-41-60.log" ));
      BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-41-60.index" ));
   }

   BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-61-80.log" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-61-80.index" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-81-100.log" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-81-100.index" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-101-120.log" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-101-120.index" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-121-140.log" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-121-140.index" ));
   if constexpr (std::is_same_v<T, state_history_tester<savanna_tester>>) {
      BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-141-160.log" ));
      BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-141-160.index" ));
   }

   if constexpr (std::is_same_v<T, state_history_tester<savanna_tester>>) {
      BOOST_CHECK_EQUAL(chain.traces_log.block_range().first, 61u);
   }
   else {
      BOOST_CHECK_EQUAL(chain.traces_log.block_range().first, 41u);
   }

   if constexpr (std::is_same_v<T, state_history_tester<legacy_tester>>) {
      BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-41-60.log" ));
      BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-41-60.index" ));
   }
   BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-61-80.log" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-61-80.index" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-81-100.log" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-81-100.index" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-101-120.log" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-101-120.index" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-121-140.log" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-121-140.index" ));
   if constexpr (std::is_same_v<T, state_history_tester<savanna_tester>>) {
      BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-141-160.log" ));
      BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-141-160.index" ));
   }

   if constexpr (std::is_same_v<T, state_history_tester<savanna_tester>>) {
     BOOST_CHECK_EQUAL(chain.chain_state_log.block_range().first, 61u);
   } else {
     BOOST_CHECK_EQUAL(chain.chain_state_log.block_range().first, 41u);
   }

   BOOST_CHECK(get_traces(chain.traces_log, 10).empty());
   BOOST_CHECK(get_traces(chain.traces_log, 100).size());
   BOOST_CHECK(get_traces(chain.traces_log, 140).size());
   BOOST_CHECK(get_traces(chain.traces_log, 150).size());
   if constexpr (std::is_same_v<T, state_history_tester<savanna_tester>>) {
      BOOST_CHECK(get_traces(chain.traces_log, 160).size());
   } else {
      BOOST_CHECK(get_traces(chain.traces_log, 160).empty());
   }

   BOOST_CHECK(get_decompressed_entry(chain.chain_state_log, 10).empty());
   BOOST_CHECK(get_decompressed_entry(chain.chain_state_log, 100).size());
   BOOST_CHECK(get_decompressed_entry(chain.chain_state_log, 140).size());
   BOOST_CHECK(get_decompressed_entry(chain.chain_state_log, 150).size());
   if constexpr (std::is_same_v<T, state_history_tester<savanna_tester>>) {
      BOOST_CHECK(get_decompressed_entry(chain.chain_state_log, 160).size());
   } else {
      BOOST_CHECK(get_decompressed_entry(chain.chain_state_log, 160).empty());
   }
}

void push_blocks( tester& from, tester& to ) {
   while( to.fork_db_head().block_num()
            < from.fork_db_head().block_num() )
   {
      auto fb = from.fetch_block_by_number( to.fork_db_head().block_num()+1 );
      to.push_block( fb );
   }
}

template<typename T>
bool test_fork(uint32_t stride, uint32_t max_retained_files) {
   fc::temp_directory state_history_dir;

   eosio::state_history::partition_config config{
      .retained_dir = "retained",
      .archive_dir = "archive",
      .stride  = stride,
      .max_retained_files = max_retained_files
   };

   T chain1(state_history_dir.path(), config);
   chain1.produce_blocks(2, true);

   chain1.create_accounts( {"dan"_n,"sam"_n,"pam"_n} );
   chain1.produce_block();
   chain1.set_producers( {"dan"_n,"sam"_n,"pam"_n} );
   chain1.produce_block();
   chain1.produce_blocks(30, true);

   if constexpr (std::is_same_v<T, state_history_tester<savanna_tester>>) {
      // Produce one more block; do not vote it such that it won't become final when
      // the first block from chain2 is pushed to chain1. This is to ensure LIBs
      // on chain1 and chain2 are the same, and further blocks from chain2 can be
      // pushed into chain1's fork_db.
      chain1.control->testing_allow_voting(false);
      chain1.produce_block();
   }

   tester chain2(setup_policy::none);
   push_blocks(chain1, chain2);

   auto fork_block_num = chain1.head().block_num();

   chain1.produce_blocks(12, true);
   auto create_account_traces = chain2.create_accounts( {"adam"_n} );
   auto create_account_trace_id = create_account_traces[0]->id;

   if constexpr (std::is_same_v<T, state_history_tester<savanna_tester>>) {
      // Disable voting on chain2 such that chain2's blocks can form a fork when
      // pushed to chain1
      chain2.control->testing_allow_voting(false);
   }

   auto b = chain2.produce_block();
   chain2.produce_blocks(11+12, true);

   // Merge blocks from chain2 to chain1 and make the chain from chain2 as the best chain.
   // Specifically in Savanna, as voting is disabled on both chains, block timestamps
   // are used to decide best chain. chain2 is selected because its last block's
   // timestamp is bigger than chain1's last block's.
   for( uint32_t start = fork_block_num + 1, end = chain2.head().block_num(); start <= end; ++start ) {
      auto fb = chain2.fetch_block_by_number( start );
      chain1.push_block( fb );
   }

   auto traces = get_traces(chain1.traces_log, b->block_num());
   bool trace_found = std::find_if(traces.begin(), traces.end(), [create_account_trace_id](const auto& v) {
                         return v[1ul]["id"].template as<digest_type>() == create_account_trace_id;
                      }) != traces.end();

   return trace_found;
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_fork_no_stride, T, state_history_testers) {
   // In this case, the chain fork would NOT trunk the trace log across the stride boundary.
   BOOST_CHECK(test_fork<T>(UINT32_MAX, 10));
}
BOOST_AUTO_TEST_CASE_TEMPLATE(test_fork_with_stride1, T, state_history_testers) {
   // In this case, the chain fork would trunk the trace log across the stride boundary.
   // However, there are still some traces remains after the truncation.
   BOOST_CHECK(test_fork<T>(10, 10));
}
BOOST_AUTO_TEST_CASE_TEMPLATE(test_fork_with_stride2, T, state_history_testers) {
   // In this case, the chain fork would trunk the trace log across the stride boundary.
   // However, no existing trace remain after the truncation. Because we only keep a very
   // short history, the create_account_trace is not available to be found. We just need
   // to make sure no exception is throw.
   BOOST_CHECK_NO_THROW(test_fork<T>(5, 1));
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_corrupted_log_recovery, T, state_history_testers) {
   fc::temp_directory state_history_dir;

   eosio::state_history::partition_config config{
      .archive_dir = "archive",
      .stride  = 100,
      .max_retained_files = 5
   };

   T chain(state_history_dir.path(), config);
   chain.produce_block();
   chain.produce_blocks(49, true);
   chain.close();

   // write a few random bytes to block log indicating the last block entry is incomplete
   fc::cfile logfile;
   logfile.set_file_path(state_history_dir.path() / "trace_history.log");
   logfile.open("ab");
   const char random_data[] = "12345678901231876983271649837";
   logfile.write(random_data, sizeof(random_data));
   logfile.close();

   std::filesystem::remove_all(chain.get_config().blocks_dir/"reversible");

   T new_chain(state_history_dir.path(), config);
   new_chain.produce_block();
   new_chain.produce_blocks(49, true);

   BOOST_CHECK(get_traces(new_chain.traces_log, 10).size());
   BOOST_CHECK(get_decompressed_entry(new_chain.chain_state_log,10).size());
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_no_index_recovery, T, state_history_testers) {
   fc::temp_directory state_history_dir;

   eosio::state_history::partition_config config{};

   T chain(state_history_dir.path(), config);
   chain.produce_block();
   chain.produce_blocks(21, true);
   chain.close();

   std::filesystem::remove(state_history_dir.path() / "trace_history.index");

   T new_chain(state_history_dir.path(), config);
   new_chain.produce_block();

   BOOST_CHECK(get_traces(new_chain.traces_log, 10).size());
   BOOST_CHECK(get_decompressed_entry(new_chain.chain_state_log,10).size());
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_curropted_index_recovery, T, state_history_testers) {
   fc::temp_directory state_history_dir;

   eosio::state_history::partition_config config{};

   T chain(state_history_dir.path(), config);
   chain.produce_block();
   chain.produce_blocks(21, true);
   chain.close();

   // write a few random bytes to end of index log, size will not match and it will be auto-recreated
   fc::cfile indexfile;
   indexfile.set_file_path(state_history_dir.path() / "trace_history.index");
   indexfile.open("ab");
   const char random_data[] = "12345678901231876983271649837";
   indexfile.seek_end(0);
   indexfile.write(random_data, sizeof(random_data));
   indexfile.close();

   T new_chain(state_history_dir.path(), config);
   new_chain.produce_block();

   BOOST_CHECK(get_traces(new_chain.traces_log, 10).size());
   BOOST_CHECK(get_decompressed_entry(new_chain.chain_state_log,10).size());
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_curropted_index_error, T, state_history_testers) {
   fc::temp_directory state_history_dir;

   eosio::state_history::partition_config config{};

   T chain(state_history_dir.path(), config);
   chain.produce_block();
   chain.produce_blocks(21, true);
   chain.close();

   // write a few random bytes to end of index log, size will not match and it will be auto-recreated
   fc::cfile indexfile;
   indexfile.set_file_path(state_history_dir.path() / "trace_history.index");
   indexfile.open("rb+");
   const char random_data[] = "12345678901231876983271649837";
   indexfile.seek_end(-(sizeof(random_data)));
   indexfile.write(random_data, sizeof(random_data));
   indexfile.close();

   BOOST_CHECK_EXCEPTION(T(state_history_dir.path(), config),
                         plugin_exception, [](const plugin_exception& e) {
                            return e.to_detail_string().find("trace_history.index is corrupted and cannot be repaired, will be automatically regenerated if removed") != std::string::npos;
                         });

   // remove index for auto recovery
   std::filesystem::remove(state_history_dir.path() / "trace_history.index");

   T new_chain(state_history_dir.path(), config);
   new_chain.produce_block();

   BOOST_CHECK(get_traces(new_chain.traces_log, 10).size());
   BOOST_CHECK(get_decompressed_entry(new_chain.chain_state_log,10).size());
}

BOOST_AUTO_TEST_SUITE_END()
