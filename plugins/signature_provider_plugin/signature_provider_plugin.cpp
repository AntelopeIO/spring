#include <eosio/signature_provider_plugin/signature_provider_plugin.hpp>
#include <eosio/chain/exceptions.hpp>

#include <fc/time.hpp>
#include <fc/network/url.hpp>

namespace eosio {
   static auto _signature_provider_plugin = application::register_plugin<signature_provider_plugin>();

class signature_provider_plugin_impl {
   public:
      fc::microseconds  _keosd_provider_timeout_us;

      signature_provider_plugin::signature_provider_type
      make_key_signature_provider(const chain::private_key_type& key) const {
         return [key]( const chain::digest_type& digest ) {
            return key.sign(digest);
         };
      }

      signature_provider_plugin::signature_provider_type
      make_keosd_signature_provider(const string& url_str, const chain::public_key_type pubkey) const {
         fc::url keosd_url;
         if(url_str.starts_with("unix://"))
            //send the entire string after unix:// to http_plugin. It'll auto-detect which part
            // is the unix socket path, and which part is the url to hit on the server
            keosd_url = fc::url("unix", url_str.substr(7), fc::ostring(), fc::ostring(), fc::ostring(), fc::ostring(), fc::ovariant_object(), std::optional<uint16_t>());
         else
            keosd_url = fc::url(url_str);

         return [to=_keosd_provider_timeout_us, keosd_url, pubkey](const chain::digest_type& digest) {
            fc::variant params;
            fc::to_variant(std::make_pair(digest, pubkey), params);
            auto deadline = to.count() >= 0 ? fc::time_point::now() + to : fc::time_point::maximum();
            return app().get_plugin<http_client_plugin>().get_client().post_sync(keosd_url, params, deadline).as<chain::signature_type>();
         };
      }

      std::optional<std::pair<chain::public_key_type,signature_provider_plugin::signature_provider_type>>
      signature_provider_for_specification(const std::string& spec) const {
         auto [pub_key_str, spec_type_str, spec_data] = signature_provider_plugin::parse_signature_provider_spec(spec);
         if( pub_key_str.starts_with("PUB_BLS") && spec_type_str == "KEY" )
            return {};

         auto pubkey = chain::public_key_type(pub_key_str);

         if(spec_type_str == "KEY") {
            chain::private_key_type priv(spec_data);
            EOS_ASSERT(pubkey == priv.get_public_key(), chain::plugin_config_exception, "Private key does not match given public key for ${pub}", ("pub", pubkey));
            return std::make_pair(pubkey, make_key_signature_provider(priv));
         }
         else if(spec_type_str == "KEOSD")
            return std::make_pair(pubkey, make_keosd_signature_provider(spec_data, pubkey));
         EOS_THROW(chain::plugin_config_exception, "Unsupported key provider type \"${t}\"", ("t", spec_type_str));
      }
};

signature_provider_plugin::signature_provider_plugin():my(new signature_provider_plugin_impl()){}
signature_provider_plugin::~signature_provider_plugin(){}

void signature_provider_plugin::set_program_options(options_description&, options_description& cfg) {
   cfg.add_options()
         ("keosd-provider-timeout", boost::program_options::value<int32_t>()->default_value(5),
          "Limits the maximum time (in milliseconds) that is allowed for sending requests to a keosd provider for signing")
         ;
}

const char* const signature_provider_plugin::signature_provider_help_text() const {
   return "Key=Value pairs in the form <public-key>=<provider-spec>\n"
          "Where:\n"
          "   <public-key>    \tis a string form of a valid Antelope public key, including BLS finalizer key\n"
          "   <provider-spec> \tis a string in the form <provider-type>:<data>\n"
          "   <provider-type> \tis KEY, KEOSD, or SE\n"
          "   KEY:<data>      \tis a string form of a valid Antelope private key which maps to the provided public key\n"
          "   KEOSD:<data>    \tis the URL where keosd is available and the appropriate wallet(s) are unlocked\n\n"
          ;

}

void signature_provider_plugin::plugin_initialize(const variables_map& options) {
   my->_keosd_provider_timeout_us = fc::milliseconds( options.at("keosd-provider-timeout").as<int32_t>() );
}


std::optional<std::pair<chain::public_key_type,signature_provider_plugin::signature_provider_type>>
signature_provider_plugin::signature_provider_for_specification(const std::string& spec) const {
   return my->signature_provider_for_specification(spec);
}

signature_provider_plugin::signature_provider_type
signature_provider_plugin::signature_provider_for_private_key(const chain::private_key_type& priv) const {
   return my->make_key_signature_provider(priv);
}

std::optional<std::pair<fc::crypto::blslib::bls_public_key, fc::crypto::blslib::bls_private_key>>
signature_provider_plugin::bls_public_key_for_specification(const std::string& spec) const {
   auto [pub_key_str, spec_type_str, spec_data] = parse_signature_provider_spec(spec);
   if( pub_key_str.starts_with("PUB_BLS") && spec_type_str == "KEY" ) {
      return std::make_pair(fc::crypto::blslib::bls_public_key{pub_key_str}, fc::crypto::blslib::bls_private_key{spec_data});
   }
   return {};
}

//         public_key   spec_type    spec_data
std::tuple<std::string, std::string, std::string> signature_provider_plugin::parse_signature_provider_spec(const std::string& spec) {
   auto delim = spec.find("=");
   EOS_ASSERT(delim != std::string::npos, chain::plugin_config_exception, "Missing \"=\" in the key spec pair");
   // public_key can be base64 encoded with trailing `=`
   // e.g. --signature-provider PUB_BLS_Fmgk<snip>iuA===KEY:PVT_BLS_NZhJ<snip>ZHFu
   while( spec.size() > delim+1 && spec[delim+1] == '=' )
      ++delim;
   EOS_ASSERT(delim < spec.size() + 1, chain::plugin_config_exception, "Missing spec data in the key spec pair");
   auto pub_key_str = spec.substr(0, delim);
   auto spec_str = spec.substr(delim + 1);

   auto spec_delim = spec_str.find(":");
   EOS_ASSERT(spec_delim != std::string::npos, chain::plugin_config_exception, "Missing \":\" in the key spec pair");
   auto spec_type_str = spec_str.substr(0, spec_delim);
   auto spec_data = spec_str.substr(spec_delim + 1);
   return {std::move(pub_key_str), std::move(spec_type_str), std::move(spec_data)};
}

} // namespace eosio
