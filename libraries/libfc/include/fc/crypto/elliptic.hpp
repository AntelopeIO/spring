#pragma once
#include <fc/crypto/bigint.hpp>
#include <fc/crypto/common.hpp>
#include <fc/crypto/openssl.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/crypto/sha512.hpp>
#include <fc/fwd.hpp>
#include <fc/array.hpp>
#include <fc/io/raw_fwd.hpp>

namespace fc {

  namespace ecc {
    namespace detail
    {
      class public_key_impl;
      class private_key_impl;
    }

    typedef fc::sha256                  blind_factor_type;
    typedef fc::array<char,33>          commitment_type;
    typedef fc::array<char,33>          public_key_data;
    typedef fc::sha256                  private_key_secret;
    typedef fc::array<char,65>          public_key_point_data; ///< the full non-compressed version of the ECC point
    typedef fc::array<char,72>          signature;
    typedef fc::array<unsigned char,65> compact_signature;
    typedef fc::array<char,78>          extended_key_data;
    typedef fc::sha256                  blinded_hash;
    typedef fc::sha256                  blind_signature;

    /**
     *  @class public_key
     *  @brief contains only the public point of an elliptic curve key.
     */
    class public_key
    {
        public:
           public_key();
           public_key(const public_key& k);
           ~public_key();
//           bool verify( const fc::sha256& digest, const signature& sig );
           public_key_data serialize()const;
           public_key_point_data serialize_ecc_point()const;

           operator public_key_data()const { return serialize(); }


           public_key( const public_key_data& v );
           public_key( const public_key_point_data& v );
           public_key( const compact_signature& c, const fc::sha256& digest );

           bool valid()const;

           public_key( public_key&& pk );
           public_key& operator=( public_key&& pk );
           public_key& operator=( const public_key& pk );

           inline friend bool operator==( const public_key& a, const public_key& b )
           {
            return a.serialize() == b.serialize();
           }
           inline friend bool operator!=( const public_key& a, const public_key& b )
           {
            return a.serialize() != b.serialize();
           }

           unsigned int fingerprint() const;

           static bool is_canonical( const compact_signature& c );

        private:
          friend class private_key;
          static public_key from_key_data( const public_key_data& v );
          fc::fwd<detail::public_key_impl,33> my;
    };

    /**
     *  @class private_key
     *  @brief an elliptic curve private key.
     */
    class private_key
    {
        public:
           private_key();
           private_key( private_key&& pk );
           private_key( const private_key& pk );
           ~private_key();

           private_key& operator=( private_key&& pk );
           private_key& operator=( const private_key& pk );

           static private_key generate();
           static private_key regenerate( const fc::sha256& secret );

           private_key child( const fc::sha256& offset )const;

           /**
            *  This method of generation enables creating a new private key in a deterministic manner relative to
            *  an initial seed.   A public_key created from the seed can be multiplied by the offset to calculate
            *  the new public key without having to know the private key.
            */
           static private_key generate_from_seed( const fc::sha256& seed, const fc::sha256& offset = fc::sha256() );

           private_key_secret get_secret()const; // get the private key secret

           operator private_key_secret ()const { return get_secret(); }

           /**
            *  Given a public key, calculatse a 512 bit shared secret between that
            *  key and this private key.
            */
           fc::sha512 get_shared_secret( const public_key& pub )const;

//           signature         sign( const fc::sha256& digest )const;
           compact_signature sign_compact( const fc::sha256& digest,
                                           require_canonical_t require_canonical = require_canonical_t::yes )const;
//           bool              verify( const fc::sha256& digest, const signature& sig );

           public_key get_public_key()const;

           inline friend bool operator==( const private_key& a, const private_key& b )
           {
            return a.get_secret() == b.get_secret();
           }
           inline friend std::strong_ordering operator<=>( const private_key& a, const private_key& b ) {
              return a.get_secret() <=> b.get_secret();
           }

           unsigned int fingerprint() const { return get_public_key().fingerprint(); }

        private:
           private_key( EC_KEY* k );
           static fc::sha256 get_secret( const EC_KEY * const k );
           fc::fwd<detail::private_key_impl,32> my;
    };

      /**
       * Shims
       */
      struct public_key_shim : public crypto::shim<public_key_data> {
         using crypto::shim<public_key_data>::shim;

         bool valid()const {
            return public_key(_data).valid();
         }
      };

      struct signature_shim : public crypto::shim<compact_signature> {
         using public_key_type = public_key_shim;
         using crypto::shim<compact_signature>::shim;

         public_key_type recover(const sha256& digest) const {
            return public_key_type(public_key(_data, digest).serialize());
         }
      };

      struct private_key_shim : public crypto::shim<private_key_secret> {
         using crypto::shim<private_key_secret>::shim;
         using signature_type = signature_shim;
         using public_key_type = public_key_shim;

         signature_type sign( const sha256& digest, require_canonical_t require_canonical = require_canonical_t::yes ) const
         {
           return signature_type(private_key::regenerate(_data).sign_compact(digest, require_canonical));
         }

         public_key_type get_public_key( ) const
         {
           return public_key_type(private_key::regenerate(_data).get_public_key().serialize());
         }

         sha512 generate_shared_secret( const public_key_type &pub_key ) const
         {
           return private_key::regenerate(_data).get_shared_secret(public_key(pub_key.serialize()));
         }

         static private_key_shim generate()
         {
            return private_key_shim(private_key::generate().get_secret());
         }

      };

  } // namespace ecc

} // namespace fc
#include <fc/reflect/reflect.hpp>

FC_REFLECT_TYPENAME( fc::ecc::private_key )
FC_REFLECT_TYPENAME( fc::ecc::public_key )
FC_REFLECT_DERIVED( fc::ecc::public_key_shim, (fc::crypto::shim<fc::ecc::public_key_data>), BOOST_PP_SEQ_NIL )
FC_REFLECT_DERIVED( fc::ecc::signature_shim, (fc::crypto::shim<fc::ecc::compact_signature>), BOOST_PP_SEQ_NIL )
FC_REFLECT_DERIVED( fc::ecc::private_key_shim, (fc::crypto::shim<fc::ecc::private_key_secret>), BOOST_PP_SEQ_NIL )
