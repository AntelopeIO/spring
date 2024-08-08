#pragma once
#include <eosio/chain/application.hpp>
#include <eosio/http_client_plugin/http_client_plugin.hpp>
#include <eosio/chain/types.hpp>
#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_public_key.hpp>

namespace eosio {

using namespace appbase;

class signature_provider_plugin : public appbase::plugin<signature_provider_plugin> {
public:
   signature_provider_plugin();
   virtual ~signature_provider_plugin();

   APPBASE_PLUGIN_REQUIRES((http_client_plugin))
   virtual void set_program_options(options_description&, options_description& cfg) override;

   void plugin_initialize(const variables_map& options);
   void plugin_startup() {}
   void plugin_shutdown() {}

   const char* const signature_provider_help_text() const;

   ///               public_key   spec_type    spec_data
   /// Note: spec_data is private_key if spec_type is KEY
   static std::tuple<std::string, std::string, std::string> parse_signature_provider_spec(const std::string& spec);


   using signature_provider_type = std::function<chain::signature_type(chain::digest_type)>;

   // @return empty optional for BLS specs
   std::optional<std::pair<chain::public_key_type,signature_provider_type>> signature_provider_for_specification(const std::string& spec) const;
   signature_provider_type signature_provider_for_private_key(const chain::private_key_type& priv) const;

   // @return empty optional for non-BLS specs
   std::optional<std::pair<fc::crypto::blslib::bls_public_key, fc::crypto::blslib::bls_private_key>> bls_public_key_for_specification(const std::string& spec) const;

private:
   std::unique_ptr<class signature_provider_plugin_impl> my;
};

}
