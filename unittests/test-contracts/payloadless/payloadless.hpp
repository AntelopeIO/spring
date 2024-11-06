#pragma once

#include <eosio/eosio.hpp>

class [[eosio::contract]] payloadless : public eosio::contract {
public:
   using eosio::contract::contract;

   [[eosio::action]]
   void doit();

   [[eosio::action]]
   void doitslow();

   [[eosio::action]]
   void doitforever();
};
