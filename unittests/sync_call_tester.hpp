#pragma once

#include <test_contracts.hpp>
#include <eosio/testing/tester.hpp>

// tester::set_code uses `std::vector<uint8_t>` for code in WASM format and
// `char*` for code in WAST format. That's why we have account_and_wasm_code and 
// account_and_wast_code.
struct account_and_wasm_code {
   eosio::chain::account_name acct{};
   std::vector<uint8_t>       code{};
   std::string                 abi{};
};

struct account_and_wast_code {
   eosio::chain::account_name acct{};
   const char*                code{nullptr};
   std::string                abi{};
};

// The first account in the accounts vector is the account initiating the sync call
struct call_tester: eosio::testing::validating_tester {
   template<typename T>
   call_tester(const std::vector<T>& accounts) {
      // Create accounts, and set up code and abi
      for (auto i = 0u; i < accounts.size(); ++i) {
         create_account(accounts[i].acct);
         set_code(accounts[i].acct, accounts[i].code);
         if (!accounts[i].abi.empty()) { // set_abi does not work for empty string for code in WAST format
            set_abi(accounts[i].acct, accounts[i].abi);
         }
      }

      produce_block();
   }
};
