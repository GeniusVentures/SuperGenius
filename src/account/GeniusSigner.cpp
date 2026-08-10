#include "account/GeniusSigner.hpp"

#include <openssl/rand.h>
#include <secp256k1.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>

#include "base/hexutil.hpp"
#include "base/logger.hpp"
#include "crypto/sha/sha256.hpp"

namespace
{
    sgns::base::Logger genius_signer_logger()
    {
        return sgns::base::createLogger( "GeniusSigner" );
    }

    const secp256k1_context *GetSecp256k1Context()
    {
        using Context = std::unique_ptr<secp256k1_context, decltype( &secp256k1_context_destroy )>;
        static const Context context( secp256k1_context_create( SECP256K1_CONTEXT_NONE ), &secp256k1_context_destroy );
        if ( context == nullptr )
        {
            genius_signer_logger()->critical( "Could not create the secp256k1 context" );
            std::abort();
        }
        return context.get();
    }

    /// Preserve the historical SHA256(SHA256(data)) Genius signing protocol.
    std::array<uint8_t, 32> GeniusMessageHash( const std::vector<uint8_t> &data )
    {
        const auto first  = sgns::crypto::sha256( gsl::make_span( data.data(), data.size() ) );
        const auto second = sgns::crypto::sha256( gsl::make_span( first.data(), first.size() ) );

        std::array<uint8_t, 32> hash{};
        std::copy( second.begin(), second.end(), hash.begin() );
        return hash;
    }
} // namespace

namespace sgns
{
    GeniusSigner GeniusSigner::Generate()
    {
        const auto *context = GetSecp256k1Context();

        PrivateKey private_key{};
        // A uniformly random 32-byte string lands outside the curve order with
        // negligible probability, but retry so the result is always usable.
        do
        {
            if ( RAND_bytes( private_key.data(), static_cast<int>( private_key.size() ) ) != 1 )
            {
                genius_signer_logger()->critical( "Could not obtain entropy for a new Genius key" );
                std::abort();
            }
        } while ( secp256k1_ec_seckey_verify( context, private_key.data() ) == 0 );

        return GeniusSigner( private_key );
    }

    GeniusSigner::GeniusSigner( const PrivateKey &private_key ) : private_key_( private_key )
    {
        const auto *context = GetSecp256k1Context();

        secp256k1_pubkey public_key;
        if ( secp256k1_ec_pubkey_create( context, &public_key, private_key_.data() ) == 0 )
        {
            genius_signer_logger()->error( "Could not derive a public key from the supplied secret key" );
            return;
        }

        std::array<uint8_t, 65> uncompressed_public_key{};
        size_t                  length = uncompressed_public_key.size();
        secp256k1_ec_pubkey_serialize( context,
                                       uncompressed_public_key.data(),
                                       &length,
                                       &public_key,
                                       SECP256K1_EC_UNCOMPRESSED );

        // Drop the 0x04 uncompressed tag: the Genius address is just X || Y.
        address_ = base::hex_lower( gsl::make_span( uncompressed_public_key.data() + 1, 64 ) );
    }

    std::string GeniusSigner::GetAddress() const
    {
        return address_;
    }

    bool GeniusSigner::VerifySignature( const std::string          &address,
                                        std::string_view            signature,
                                        const std::vector<uint8_t> &data )
    {
        if ( signature.size() != SIGNATURE_SIZE )
        {
            genius_signer_logger()->error( "Incorrect signature size {}, expected {}",
                                           signature.size(),
                                           SIGNATURE_SIZE );
            return false;
        }

        auto public_key_bytes = base::unhex( address );
        if ( public_key_bytes.has_error() || public_key_bytes.value().size() != 64 )
        {
            return false;
        }

        const auto *context = GetSecp256k1Context();

        std::array<uint8_t, 65> uncompressed_public_key{};
        uncompressed_public_key.front() = SECP256K1_TAG_PUBKEY_UNCOMPRESSED;
        std::copy( public_key_bytes.value().begin(),
                   public_key_bytes.value().end(),
                   uncompressed_public_key.begin() + 1 );

        secp256k1_pubkey public_key;
        if ( secp256k1_ec_pubkey_parse( context,
                                        &public_key,
                                        uncompressed_public_key.data(),
                                        uncompressed_public_key.size() ) == 0 )
        {
            return false;
        }

        // Genius signatures store each scalar least-significant byte first; libsecp256k1 uses big endian.
        std::array<uint8_t, SIGNATURE_SIZE> compact_signature{};
        std::reverse_copy( signature.begin(), signature.begin() + 32, compact_signature.begin() );
        std::reverse_copy( signature.begin() + 32, signature.end(), compact_signature.begin() + 32 );

        secp256k1_ecdsa_signature parsed_signature;
        if ( secp256k1_ecdsa_signature_parse_compact( context, &parsed_signature, compact_signature.data() ) == 0 )
        {
            return false;
        }
        secp256k1_ecdsa_signature_normalize( context, &parsed_signature, &parsed_signature );

        const std::array<uint8_t, 32> message_hash = GeniusMessageHash( data );

        return secp256k1_ecdsa_verify( context, &parsed_signature, message_hash.data(), &public_key ) == 1;
    }

    bool GeniusSigner::VerifySignature( const std::string          &address,
                                        const std::vector<uint8_t> &signature,
                                        const std::vector<uint8_t> &data )
    {
        const std::string_view signature_view = signature.empty()
                                                    ? std::string_view{}
                                                    : std::string_view(
                                                          reinterpret_cast<const char *>( signature.data() ),
                                                          signature.size() );
        return VerifySignature( address, signature_view, data );
    }

    std::vector<uint8_t> GeniusSigner::Sign( const std::vector<uint8_t> &data ) const
    {
        const auto *context = GetSecp256k1Context();

        const std::array<uint8_t, 32> message_hash = GeniusMessageHash( data );

        secp256k1_ecdsa_signature signature;
        if ( secp256k1_ecdsa_sign( context, &signature, message_hash.data(), private_key_.data(), nullptr, nullptr ) ==
             0 )
        {
            genius_signer_logger()->error( "Could not sign data with the account key" );
            return {};
        }

        std::array<uint8_t, SIGNATURE_SIZE> compact_signature{};
        secp256k1_ecdsa_signature_serialize_compact( context, compact_signature.data(), &signature );

        std::vector<uint8_t> signed_vector( SIGNATURE_SIZE );
        std::reverse_copy( compact_signature.begin(), compact_signature.begin() + 32, signed_vector.begin() );
        std::reverse_copy( compact_signature.begin() + 32, compact_signature.end(), signed_vector.begin() + 32 );
        return signed_vector;
    }
} // namespace sgns
