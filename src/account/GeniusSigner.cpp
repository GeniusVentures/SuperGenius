// Keep these includes before EthereumKeyGenerator to satisfy crypto3's headers.
#include <nil/crypto3/algebra/marshalling.hpp>
#include <nil/crypto3/pubkey/algorithm/sign.hpp>
#include <nil/crypto3/pubkey/algorithm/verify.hpp>

#include "account/GeniusSigner.hpp"

#include <secp256k1.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <utility>

#include "base/hexutil.hpp"
#include "base/logger.hpp"

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
} // namespace

namespace sgns
{
    GeniusSigner GeniusSigner::Generate()
    {
        return GeniusSigner( ethereum::EthereumKeyGenerator{} );
    }

    GeniusSigner::GeniusSigner( ethereum::EthereumKeyGenerator keypair ) : keypair_( std::move( keypair ) )
    {
    }

    std::string GeniusSigner::GetAddress() const
    {
        return keypair_.GetEntirePubValue();
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

        // Preserve the historical SHA256(SHA256(data)) Genius signing protocol.
        const std::array<uint8_t, 32> first_hash   = nil::crypto3::hash<nil::crypto3::hashes::sha2<256>>( data );
        const std::array<uint8_t, 32> message_hash = nil::crypto3::hash<nil::crypto3::hashes::sha2<256>>( first_hash );

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

        std::array<uint8_t, 32> secret_key{};
        const auto              private_key = keypair_.get_private_key();
        nil::marshalling::bincode::field<ethereum::scalar_field_type>::field_element_to_bytes<
            std::array<uint8_t, 32>::iterator>( private_key.private_key_data(), secret_key.begin(), secret_key.end() );
        std::reverse( secret_key.begin(), secret_key.end() );

        // Preserve the historical SHA256(SHA256(data)) Genius signing protocol.
        const std::array<uint8_t, 32> first_hash   = nil::crypto3::hash<nil::crypto3::hashes::sha2<256>>( data );
        const std::array<uint8_t, 32> message_hash = nil::crypto3::hash<nil::crypto3::hashes::sha2<256>>( first_hash );

        secp256k1_ecdsa_signature signature;
        if ( secp256k1_ecdsa_sign( context, &signature, message_hash.data(), secret_key.data(), nullptr, nullptr ) ==
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
