#include <eosio/chain/qc.hpp>
#include <eosio/chain/types.hpp>
#include <eosio/chain/block_header.hpp>

#include <fc/exception/exception.hpp>
#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_utils.hpp>

#include <boost/test/unit_test.hpp>

// -----------------------------------------------------------------------------
//            Allow boost to print `aggregating_qc_sig_t::state_t`
// -----------------------------------------------------------------------------
namespace std {
   using state_t = eosio::chain::aggregating_qc_sig_t::state_t;
   std::ostream& operator<<(std::ostream& os, state_t s)
   {
      switch(s) {
      case state_t::unrestricted:   os << "unrestricted"; break;
      case state_t::restricted:     os << "restricted"; break;
      case state_t::weak_achieved:  os << "weak_achieved"; break;
      case state_t::weak_final:     os << "weak_final"; break;
      case state_t::strong:         os << "strong"; break;
      }
      return os;
   }
}

BOOST_AUTO_TEST_SUITE(finality_misc_tests)

BOOST_AUTO_TEST_CASE(qc_state_transitions) try {
   using namespace eosio::chain;
   using namespace fc::crypto::blslib;
   using state_t = aggregating_qc_sig_t::state_t;

   digest_type d(fc::sha256("0000000000000000000000000000001"));
   std::vector<uint8_t> digest(d.data(), d.data() + d.data_size());

   std::vector<bls_private_key> sk {
      bls_private_key("PVT_BLS_0d8dsux83r42Qg8CHgAqIuSsn9AV-QdCzx3tPj0K8yOJA_qb"),
      bls_private_key("PVT_BLS_Wfs3KzfTI2P5F85PnoHXLnmYgSbp-XpebIdS6BUCHXOKmKXK"),
      bls_private_key("PVT_BLS_74crPc__6BlpoQGvWjkHmUdzcDKh8QaiN_GtU4SD0QAi4BHY"),
      bls_private_key("PVT_BLS_foNjZTu0k6qM5ftIrqC5G_sim1Rg7wq3cRUaJGvNtm2rM89K"),
      bls_private_key("PVT_BLS_FWK1sk_DJnoxNvUNhwvJAYJFcQAFtt_mCtdQCUPQ4jN1K7eT"),
      bls_private_key("PVT_BLS_tNAkC5MnI-fjHWSX7la1CPC2GIYgzW5TBfuKFPagmwVVsOeW")
   };

   std::vector<bls_public_key> pubkey;
   pubkey.reserve(sk.size());
   for (const auto& k : sk)
      pubkey.push_back(k.get_public_key());

   auto weak_vote = [&](aggregating_qc_sig_t& qc, const std::vector<uint8_t>& digest_to_sign, size_t index, uint64_t weight) {
      return qc.add_vote(0, 0, false, index, sk[index].sign(digest_to_sign), weight);
   };

   auto strong_vote = [&](aggregating_qc_sig_t& qc, const std::vector<uint8_t>& digest_to_sign, size_t index, uint64_t weight) {
      return qc.add_vote(0, 0, true, index, sk[index].sign(digest_to_sign), weight);
   };

   constexpr uint64_t weight = 1;

   {
      constexpr uint64_t quorum = 1;
      constexpr uint64_t max_weak_sum_before_weak_final = 1;
      aggregating_qc_sig_t qc(2, quorum, max_weak_sum_before_weak_final); // 2 finalizers
      BOOST_CHECK_EQUAL(qc.state(), state_t::unrestricted);

      // add one weak vote
      // -----------------
      weak_vote(qc, digest, 0, weight);
      BOOST_CHECK_EQUAL(qc.state(), state_t::weak_achieved);
      BOOST_CHECK(qc.is_quorum_met());

      // add duplicate weak vote
      // -----------------------
      auto ok = weak_vote(qc, digest, 0, weight);
      BOOST_CHECK(ok != vote_result_t::success); // vote was a duplicate
      BOOST_CHECK_EQUAL(qc.state(), state_t::weak_achieved);
      BOOST_CHECK(qc.is_quorum_met());

      // add another weak vote
      // ---------------------
      weak_vote(qc, digest, 1, weight);
      BOOST_CHECK_EQUAL(qc.state(), state_t::weak_final);
   }

   {
      constexpr uint64_t quorum = 1;
      constexpr uint64_t max_weak_sum_before_weak_final = 1;
      aggregating_qc_sig_t qc(2, quorum, max_weak_sum_before_weak_final); // 2 finalizers
      BOOST_CHECK_EQUAL(qc.state(), state_t::unrestricted);

      // add a weak vote
      // ---------------
      weak_vote(qc, digest, 0, weight);
      BOOST_CHECK_EQUAL(qc.state(), state_t::weak_achieved);
      BOOST_CHECK(qc.is_quorum_met());

      // add a strong vote
      // -----------------
      strong_vote(qc, digest, 1, weight);
      BOOST_CHECK_EQUAL(qc.state(), state_t::strong);
      BOOST_CHECK(qc.is_quorum_met());
   }

   {
      constexpr uint64_t quorum = 1;
      constexpr uint64_t max_weak_sum_before_weak_final = 1;
      aggregating_qc_sig_t qc(2, quorum, max_weak_sum_before_weak_final); // 2 finalizers, weight_sum_minus_quorum = 1
      BOOST_CHECK_EQUAL(qc.state(), state_t::unrestricted);

      // add a strong vote
      // -----------------
      strong_vote(qc, digest, 1, weight);
      BOOST_CHECK_EQUAL(qc.state(), state_t::strong);
      BOOST_CHECK(qc.is_quorum_met());

      // add a strong vote
      // -----------------
      strong_vote(qc, digest, 1, weight);
      BOOST_CHECK_EQUAL(qc.state(), state_t::strong);
      BOOST_CHECK(qc.is_quorum_met());
   }

   {
      constexpr uint64_t quorum = 2;
      constexpr uint64_t max_weak_sum_before_weak_final = 1;
      aggregating_qc_sig_t qc(3, quorum, max_weak_sum_before_weak_final); // 3 finalizers

      // add a weak vote
      // ---------------
      weak_vote(qc, digest, 0, weight);
      BOOST_CHECK_EQUAL(qc.state(), state_t::unrestricted);
      BOOST_CHECK(!qc.is_quorum_met());

      // add a strong vote
      // -----------------
      strong_vote(qc, digest, 1, weight);
      BOOST_CHECK_EQUAL(qc.state(), state_t::weak_achieved);
      BOOST_CHECK(qc.is_quorum_met());

      {
         aggregating_qc_sig_t qc2(std::move(qc));

         // add a weak vote
         // ---------------
         weak_vote(qc2, digest, 2, weight);
         BOOST_CHECK_EQUAL(qc2.state(), state_t::weak_final);
         BOOST_CHECK(qc2.is_quorum_met());
      }
   }

   {
      constexpr uint64_t quorum = 2;
      constexpr uint64_t max_weak_sum_before_weak_final = 1;
      aggregating_qc_sig_t qc(3, quorum, max_weak_sum_before_weak_final); // 3 finalizers, quorum = 2

      // add a weak vote
      // ---------------
      weak_vote(qc, digest, 0, weight);
      BOOST_CHECK_EQUAL(qc.state(), state_t::unrestricted);
      BOOST_CHECK(!qc.is_quorum_met());

      // add a strong vote
      // -----------------
      strong_vote(qc, digest, 1, weight);
      BOOST_CHECK_EQUAL(qc.state(), state_t::weak_achieved);
      BOOST_CHECK(qc.is_quorum_met());

      {
         aggregating_qc_sig_t qc2(std::move(qc));

         // add a strong vote
         // -----------------
         strong_vote(qc2, digest, 2, weight);
         BOOST_CHECK_EQUAL(qc2.state(), state_t::strong);
         BOOST_CHECK(qc2.is_quorum_met());
      }
   }

   {
      constexpr uint64_t quorum = 2;
      constexpr uint64_t max_weak_sum_before_weak_final = 1;
      aggregating_qc_sig_t qc(3, quorum, max_weak_sum_before_weak_final); // 3 finalizers, quorum = 2

      // add a weak vote
      // ---------------
      weak_vote(qc, digest, 0, weight);
      BOOST_CHECK_EQUAL(qc.state(), state_t::unrestricted);
      BOOST_CHECK(!qc.is_quorum_met());

      // add a weak vote
      // ---------------
      weak_vote(qc, digest, 1, weight);
      BOOST_CHECK_EQUAL(qc.state(), state_t::weak_final);
      BOOST_CHECK(qc.is_quorum_met());

      {
         aggregating_qc_sig_t qc2(std::move(qc));

         // add a weak vote
         // ---------------
         weak_vote(qc2, digest, 2, weight);
         BOOST_CHECK_EQUAL(qc2.state(), state_t::weak_final);
         BOOST_CHECK(qc2.is_quorum_met());
      }
   }

   {
      constexpr uint64_t quorum = 2;
      constexpr uint64_t max_weak_sum_before_weak_final = 1;
      aggregating_qc_sig_t qc(3, quorum, max_weak_sum_before_weak_final); // 3 finalizers, quorum = 2

      // add a weak vote
      // ---------------
      weak_vote(qc, digest, 0, weight);
      BOOST_CHECK_EQUAL(qc.state(), state_t::unrestricted);
      BOOST_CHECK(!qc.is_quorum_met());

      // add a weak vote
      // ---------------
      weak_vote(qc, digest, 1, weight);
      BOOST_CHECK_EQUAL(qc.state(), state_t::weak_final);
      BOOST_CHECK(qc.is_quorum_met());

      {
         aggregating_qc_sig_t qc2(std::move(qc));

         // add a strong vote
         // -----------------
         strong_vote(qc2, digest, 2, weight);
         BOOST_CHECK_EQUAL(qc2.state(), state_t::weak_final);
         BOOST_CHECK(qc2.is_quorum_met());
      }
   }

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
