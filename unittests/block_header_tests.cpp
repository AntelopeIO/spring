#include <eosio/chain/block_header.hpp>
#include <boost/test/unit_test.hpp>

using namespace eosio::chain;

BOOST_AUTO_TEST_SUITE(block_header_tests)

// test for block header without extension
BOOST_AUTO_TEST_CASE(block_header_without_extension_test)
{
   block_header header;
   std::optional<block_header_extension> ext = header.extract_header_extension(finality_extension::extension_id());
   BOOST_REQUIRE(!ext);
}

// test for empty finality_extension
BOOST_AUTO_TEST_CASE(finality_extension_with_empty_values_test)
{
   block_header       header;
   constexpr uint32_t last_qc_block_num {0};
   constexpr bool     is_last_strong_qc {false};

   emplace_extension(
      header.header_extensions,
      finality_extension::extension_id(),
      fc::raw::pack( finality_extension{qc_claim_t{last_qc_block_num, is_last_strong_qc},
                                                std::optional<finalizer_policy_diff>{}, std::optional<proposer_policy_diff>{}} )
   );

   std::optional<block_header_extension> ext = header.extract_header_extension(finality_extension::extension_id());
   BOOST_REQUIRE( !!ext );

   const auto& f_ext = std::get<finality_extension>(*ext);
   BOOST_REQUIRE_EQUAL( f_ext.qc_claim.block_num, last_qc_block_num );
   BOOST_REQUIRE_EQUAL( f_ext.qc_claim.is_strong_qc, is_last_strong_qc );
   BOOST_REQUIRE( !f_ext.new_finalizer_policy_diff );
   BOOST_REQUIRE( !f_ext.new_proposer_policy_diff );
}

// test for finality_extension uniqueness
BOOST_AUTO_TEST_CASE(finality_extension_uniqueness_test)
{
   block_header header;

   emplace_extension(
      header.header_extensions,
      finality_extension::extension_id(),
      fc::raw::pack( finality_extension{qc_claim_t{0, false}, {std::nullopt},
                                                std::optional<proposer_policy_diff>{}} )
   );

   std::vector<finalizer_authority> finalizers { {"test description", 50, fc::crypto::blslib::bls_public_key{"PUB_BLS_qVbh4IjYZpRGo8U_0spBUM-u-r_G0fMo4MzLZRsKWmm5uyeQTp74YFaMN9IDWPoVVT5rj_Tw1gvps6K9_OZ6sabkJJzug3uGfjA6qiaLbLh5Fnafwv-nVgzzzBlU2kwRrcHc8Q" }} };
   auto fin_policy = std::make_shared<finalizer_policy>();
   finalizer_policy_diff new_finalizer_policy_diff = fin_policy->create_diff(finalizer_policy{.generation = 1, .threshold = 100, .finalizers = finalizers});

   proposer_policy_diff new_proposer_policy_diff = proposer_policy_diff{.version = 1, .proposal_time = block_timestamp_type{200}, .producer_auth_diff = {}};

   emplace_extension(
      header.header_extensions,
      finality_extension::extension_id(),
      fc::raw::pack( finality_extension{qc_claim_t{100, true}, new_finalizer_policy_diff, new_proposer_policy_diff} )
   );
   
   BOOST_CHECK_THROW(header.validate_and_extract_header_extensions(), invalid_block_header_extension);
}

// test for finality_extension with values
BOOST_AUTO_TEST_CASE(finality_extension_with_values_test)
{
   block_header       header;
   constexpr uint32_t last_qc_block_num {10};
   constexpr bool     is_strong_qc {true};
   
   std::vector<finalizer_authority> finalizers { {"test description", 50, fc::crypto::blslib::bls_public_key{"PUB_BLS_qVbh4IjYZpRGo8U_0spBUM-u-r_G0fMo4MzLZRsKWmm5uyeQTp74YFaMN9IDWPoVVT5rj_Tw1gvps6K9_OZ6sabkJJzug3uGfjA6qiaLbLh5Fnafwv-nVgzzzBlU2kwRrcHc8Q" }} };
   auto fin_policy = std::make_shared<finalizer_policy>();
   finalizer_policy_diff new_finalizer_policy_diff = fin_policy->create_diff(finalizer_policy{.generation = 1, .threshold = 100, .finalizers = finalizers});

   proposer_policy_diff new_proposer_policy_diff = proposer_policy_diff{.version = 1, .proposal_time = block_timestamp_type{200}, .producer_auth_diff = {}};

   emplace_extension(
      header.header_extensions,
      finality_extension::extension_id(),
      fc::raw::pack( finality_extension{qc_claim_t{last_qc_block_num, is_strong_qc}, new_finalizer_policy_diff, new_proposer_policy_diff} )
   );

   std::optional<block_header_extension> ext = header.extract_header_extension(finality_extension::extension_id());
   BOOST_REQUIRE( !!ext );

   const auto& f_ext = std::get<finality_extension>(*ext);

   BOOST_REQUIRE_EQUAL( f_ext.qc_claim.block_num, last_qc_block_num );
   BOOST_REQUIRE_EQUAL( f_ext.qc_claim.is_strong_qc, is_strong_qc );

   BOOST_REQUIRE( !!f_ext.new_finalizer_policy_diff );
   BOOST_REQUIRE_EQUAL(f_ext.new_finalizer_policy_diff->generation, 1u);
   BOOST_REQUIRE_EQUAL(f_ext.new_finalizer_policy_diff->threshold, 100u);
   BOOST_REQUIRE_EQUAL(f_ext.new_finalizer_policy_diff->finalizers_diff.insert_indexes[0].second.description, "test description");
   BOOST_REQUIRE_EQUAL(f_ext.new_finalizer_policy_diff->finalizers_diff.insert_indexes[0].second.weight, 50u);
   BOOST_REQUIRE_EQUAL(f_ext.new_finalizer_policy_diff->finalizers_diff.insert_indexes[0].second.public_key.to_string(), "PUB_BLS_qVbh4IjYZpRGo8U_0spBUM-u-r_G0fMo4MzLZRsKWmm5uyeQTp74YFaMN9IDWPoVVT5rj_Tw1gvps6K9_OZ6sabkJJzug3uGfjA6qiaLbLh5Fnafwv-nVgzzzBlU2kwRrcHc8Q");

   BOOST_REQUIRE( !!f_ext.new_proposer_policy_diff );
   fc::time_point t = (fc::time_point)(f_ext.new_proposer_policy_diff->proposal_time);
   BOOST_REQUIRE_EQUAL(t.time_since_epoch().to_seconds(), 946684900ll);
}

BOOST_AUTO_TEST_SUITE_END()
