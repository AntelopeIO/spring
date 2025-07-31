#pragma once

#include <eosio/testing/tester.hpp>

#include <fc/exception/exception.hpp>

#include <exception>

namespace eosio::test_utils {

using namespace eosio::chain;

inline bool is_access_violation(const fc::unhandled_exception& e) {
   try {
      std::rethrow_exception(e.get_inner_exception());
    }
    catch (const wasm_execution_error& e) {
       return true;
    } catch (...) {

    }
   return false;
}

inline bool is_assert_exception(const fc::assert_exception& e) { return true; }
inline bool is_page_memory_error(const page_memory_error& e) { return true; }
inline bool is_unsatisfied_authorization(const unsatisfied_authorization& e) { return true;}
inline bool is_wasm_execution_error(const wasm_execution_error& e) {return true;}
inline bool is_tx_net_usage_exceeded(const tx_net_usage_exceeded& e) { return true; }
inline bool is_block_net_usage_exceeded(const block_net_usage_exceeded& e) { return true; }
inline bool is_tx_cpu_usage_exceeded(const tx_cpu_usage_exceeded& e) { return true; }
inline bool is_block_cpu_usage_exceeded(const block_cpu_usage_exceeded& e) { return true; }
inline bool is_deadline_exception(const deadline_exception& e) { return true; }

template<uint64_t NAME>
struct test_api_action {
   static account_name get_account() {
      return "testapi"_n;
   }

   static action_name get_name() {
      return action_name(NAME);
   }
};


template<uint64_t NAME>
struct test_pause_action {
   static account_name get_account() {
      return "pause"_n;
   }

   static action_name get_name() {
      return action_name(NAME);
   }
};


template<uint64_t NAME>
struct test_chain_action {
   static account_name get_account() {
      return account_name(config::system_account_name);
   }

   static action_name get_name() {
      return action_name(NAME);
   }
};

template<class T, typename Tester>
void push_trx(Tester& test, T ac, uint32_t billed_cpu_time_us , uint32_t max_cpu_usage_ms, uint32_t max_block_cpu_ms,
              bool explicit_bill, std::vector<char> payload = {}, name account = "testapi"_n, transaction_metadata::trx_type trx_type = transaction_metadata::trx_type::input ) {
   signed_transaction trx;

   action act;
   act.account = ac.get_account();
   act.name = ac.get_name();
   if ( trx_type != transaction_metadata::trx_type::read_only ) {
      auto pl = vector<permission_level>{{account, config::active_name}};
      act.authorization = pl;
   }
   act.data = payload;

   trx.actions.push_back(act);
   test.set_transaction_headers(trx);
   if ( trx_type != transaction_metadata::trx_type::read_only ) {
      auto sigs = trx.sign(test.get_private_key(account, "active"), test.get_chain_id());
   }
   flat_set<public_key_type> keys;
   trx.get_signature_keys(test.get_chain_id(), fc::time_point::maximum(), keys);
   auto ptrx = std::make_shared<packed_transaction>( std::move(trx) );

   auto fut = transaction_metadata::start_recover_keys( std::move( ptrx ), test.control->get_thread_pool(),
                                                        test.get_chain_id(), fc::microseconds::maximum(),
                                                        trx_type );
   fc::microseconds max_trx_time = max_cpu_usage_ms == UINT32_MAX ? fc::microseconds::maximum() : fc::milliseconds(max_cpu_usage_ms);
   auto res = test.control->push_transaction( fut.get(), fc::time_point::now() + fc::milliseconds(max_block_cpu_ms),
                                              max_trx_time, billed_cpu_time_us, explicit_bill, 0 );
   if( res->except_ptr ) std::rethrow_exception( res->except_ptr );
   if( res->except ) throw *res->except;
};

static constexpr unsigned int DJBH(const char* cp) {
   unsigned int hash = 5381;
   while (*cp)
      hash = 33 * hash ^ (unsigned char) *cp++;
   return hash;
}

static constexpr unsigned long long WASM_TEST_ACTION(const char* cls, const char* method) {
   return static_cast<unsigned long long>(DJBH(cls)) << 32 | static_cast<unsigned long long>(DJBH(method));
}

template <typename T>
transaction_trace_ptr CallAction(testing::validating_tester& test, T ac, const vector<account_name>& scope = {"testapi"_n}) {
   signed_transaction trx;


   auto pl = vector<permission_level>{{scope[0], config::active_name}};
   if (scope.size() > 1)
      for (size_t i = 1; i < scope.size(); i++)
         pl.push_back({scope[i], config::active_name});

   action act(pl, ac);
   trx.actions.push_back(act);

   test.set_transaction_headers(trx);
   auto sigs = trx.sign(test.get_private_key(scope[0], "active"), test.get_chain_id());
   flat_set<public_key_type> keys;
   trx.get_signature_keys(test.get_chain_id(), fc::time_point::maximum(), keys);
   auto res = test.push_transaction(trx);
   BOOST_CHECK_EQUAL(res->receipt->status, transaction_receipt::executed);
   test.produce_block();
   return res;
}

template <typename T, typename Tester>
std::pair<transaction_trace_ptr, signed_block_ptr> _CallFunction(Tester& test, T ac, const vector<char>& data, const vector<account_name>& scope = {"testapi"_n}, bool no_throw = false) {
   {
      signed_transaction trx;

      auto pl = vector<permission_level>{{scope[0], config::active_name}};
      if (scope.size() > 1)
         for (unsigned int i = 1; i < scope.size(); i++)
            pl.push_back({scope[i], config::active_name});

      action act(pl, ac);
      act.data = data;
      act.authorization = {{"testapi"_n, config::active_name}};
      trx.actions.push_back(act);

      test.set_transaction_headers(trx, test.DEFAULT_EXPIRATION_DELTA);
      auto sigs = trx.sign(test.get_private_key(scope[0], "active"), test.get_chain_id());

      flat_set<public_key_type> keys;
      trx.get_signature_keys(test.get_chain_id(), fc::time_point::maximum(), keys);

      auto res = test.push_transaction(trx, fc::time_point::maximum(), Tester::DEFAULT_BILLED_CPU_TIME_US, no_throw);
      if (!no_throw) {
         BOOST_CHECK_EQUAL(res->receipt->status, transaction_receipt::executed);
      }
      auto block = test.produce_block();
      return { res, block };
   }
}

template <typename T, typename Tester>
transaction_trace_ptr CallFunction(Tester& test, T ac, const vector<char>& data, const vector<account_name>& scope = {"testapi"_n}, bool no_throw = false) {
   {
      return _CallFunction(test, ac, data, scope, no_throw).first;
   }
}

#define CALL_TEST_FUNCTION(_TESTER, CLS, MTH, DATA) CallFunction(_TESTER, test_api_action<WASM_TEST_ACTION(CLS, MTH)>{}, DATA)
#define CALL_TEST_FUNCTION_WITH_BLOCK(_TESTER, CLS, MTH, DATA) _CallFunction(_TESTER, test_api_action<WASM_TEST_ACTION(CLS, MTH)>{}, DATA)
#define CALL_TEST_FUNCTION_SYSTEM(_TESTER, CLS, MTH, DATA) CallFunction(_TESTER, test_chain_action<WASM_TEST_ACTION(CLS, MTH)>{}, DATA, {config::system_account_name} )
#define CALL_TEST_FUNCTION_SCOPE(_TESTER, CLS, MTH, DATA, ACCOUNT) CallFunction(_TESTER, test_api_action<WASM_TEST_ACTION(CLS, MTH)>{}, DATA, ACCOUNT)
#define CALL_TEST_FUNCTION_NO_THROW(_TESTER, CLS, MTH, DATA) CallFunction(_TESTER, test_api_action<WASM_TEST_ACTION(CLS, MTH)>{}, DATA, {"testapi"_n}, true)
#define CALL_TEST_FUNCTION_AND_CHECK_EXCEPTION(_TESTER, CLS, MTH, DATA, EXC, EXC_MESSAGE) \
BOOST_CHECK_EXCEPTION( \
   CALL_TEST_FUNCTION( _TESTER, CLS, MTH, DATA), \
                       EXC, \
                       [](const EXC& e) { \
                          return expect_assert_message(e, EXC_MESSAGE); \
                     } \
);

} // namespace eosio::test_utils

FC_REFLECT_TEMPLATE((uint64_t T), eosio::test_utils::test_api_action<T>, BOOST_PP_SEQ_NIL)
FC_REFLECT_TEMPLATE((uint64_t T), eosio::test_utils::test_pause_action<T>, BOOST_PP_SEQ_NIL)
FC_REFLECT_TEMPLATE((uint64_t T), eosio::test_utils::test_chain_action<T>, BOOST_PP_SEQ_NIL)
