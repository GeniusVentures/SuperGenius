#include "crdt/globaldb/keypair_file_storage.hpp"

#include <fstream>

#include <libp2p/crypto/key_marshaller/key_marshaller_impl.hpp>
#include <libp2p/crypto/key_validator/key_validator_impl.hpp>
#include <libp2p/crypto/protobuf/protobuf_key.hpp>
#include <libp2p/peer/peer_id.hpp>
#include <libp2p/peer/peer_address.hpp>
#include <libp2p/crypto/random_generator/boost_generator.hpp>
#include <libp2p/crypto/crypto_provider/crypto_provider_impl.hpp>
#include <libp2p/crypto/ed25519_provider/ed25519_provider_impl.hpp>
#include <libp2p/crypto/rsa_provider/rsa_provider_impl.hpp>
#include <libp2p/crypto/secp256k1_provider/secp256k1_provider_impl.hpp>
#include <libp2p/crypto/ecdsa_provider/ecdsa_provider_impl.hpp>
#include <libp2p/crypto/hmac_provider/hmac_provider_impl.hpp>

#include <crypto/ed25519/ed25519_provider_impl.hpp>

namespace sgns::crdt
{
    using CryptoProvider = libp2p::crypto::CryptoProviderImpl;
    using libp2p::crypto::KeyPair;
    using KeyMarshaller = libp2p::crypto::marshaller::KeyMarshallerImpl;
    using KeyValidator  = libp2p::crypto::validator::KeyValidatorImpl;
    using libp2p::peer::PeerId;

    KeyPairFileStorage::KeyPairFileStorage( boost::filesystem::path keyPath ) : m_keyPath( std::move( keyPath ) )
    {
        // Extract the directory path from the keyPath
        boost::filesystem::path directory = m_keyPath.parent_path();

        // Create the directory if it doesn't exist
        if ( !boost::filesystem::exists( directory ) )
        {
            boost::filesystem::create_directories( directory );
        }
    }

    outcome::result<libp2p::crypto::KeyPair> KeyPairFileStorage::GetKeyPair() const
    {
        KeyPair keyPair;

        m_logger->info( "Path to keypairs " + m_keyPath.string() );

        auto cryptoProvider =
            std::make_shared<CryptoProvider>( std::make_shared<libp2p::crypto::random::BoostRandomGenerator>(),
                                              std::make_shared<libp2p::crypto::ed25519::Ed25519ProviderImpl>(),
                                              std::make_shared<libp2p::crypto::rsa::RsaProviderImpl>(),
                                              std::make_shared<libp2p::crypto::ecdsa::EcdsaProviderImpl>(),
                                              std::make_shared<libp2p::crypto::secp256k1::Secp256k1ProviderImpl>(),
                                              std::make_shared<libp2p::crypto::hmac::HmacProviderImpl>() );

        auto          keyValidator = std::make_shared<KeyValidator>( cryptoProvider );
        KeyMarshaller keyMarshaller( keyValidator );

        if ( !boost::filesystem::exists( m_keyPath ) )
        {
            auto keyPairResult = cryptoProvider->generateKeys( libp2p::crypto::Key::Type::Ed25519,
                                                               libp2p::crypto::common::RSAKeyType::RSA1024 );

            if ( keyPairResult.has_failure() )
            {
                m_logger->error( "Unable to generate key pair" );
                return outcome::failure( boost::system::error_code{} );
            }

            keyPair = keyPairResult.value();

            auto marshalPrivateKeyResult = keyMarshaller.marshal( keyPair.privateKey );
            if ( marshalPrivateKeyResult.has_failure() )
            {
                m_logger->error( "Unable to marshal private key" );
                return outcome::failure( boost::system::error_code{} );
            }

            auto marshalPublicKeyResult = keyMarshaller.marshal( keyPair.publicKey );
            if ( marshalPublicKeyResult.has_failure() )
            {
                m_logger->error( "Unable to marshal public key" );
                return outcome::failure( boost::system::error_code{} );
            }

            std::ofstream fileKey( m_keyPath.string(), std::ios::out | std::ios::binary );
            std::copy( marshalPrivateKeyResult.value().key.cbegin(),
                       marshalPrivateKeyResult.value().key.cend(),
                       std::ostreambuf_iterator<char>( fileKey ) );
            std::copy( marshalPublicKeyResult.value().key.cbegin(),
                       marshalPublicKeyResult.value().key.cend(),
                       std::ostreambuf_iterator<char>( fileKey ) );
            fileKey.close();
        }
        else
        {
            std::ifstream fileKey( m_keyPath.string(), std::ios::in | std::ios::binary );
            if ( !fileKey.is_open() )
            {
                m_logger->error( "Unable to open key file: " + m_keyPath.string() );
                return outcome::failure( boost::system::error_code{} );
            }

            std::istreambuf_iterator<char> it{ fileKey };
            std::istreambuf_iterator<char> end;
            std::string                    ss{ it, end };

            std::vector<uint8_t>        privateKey( ss.begin(), ss.begin() + ss.size() / 2 );
            libp2p::crypto::ProtobufKey privateProtobufKey{ std::move( privateKey ) };

            std::vector<uint8_t>        publicKey( ss.begin() + ss.size() / 2, ss.end() );
            libp2p::crypto::ProtobufKey publicProtobufKey{ std::move( publicKey ) };

            auto unmarshalPrivateKeyResult = keyMarshaller.unmarshalPrivateKey( privateProtobufKey );
            if ( unmarshalPrivateKeyResult.has_failure() )
            {
                m_logger->error( "Unable to unmarshal private key" );
                return outcome::failure( boost::system::error_code{} );
            }
            keyPair.privateKey = unmarshalPrivateKeyResult.value();

            auto unmarshalPublicKeyResult = keyMarshaller.unmarshalPublicKey( publicProtobufKey );
            if ( unmarshalPublicKeyResult.has_failure() )
            {
                m_logger->error( "Unable to unmarshal public key" );
                return outcome::failure( boost::system::error_code{} );
            }
            keyPair.publicKey = unmarshalPublicKeyResult.value();
        }

        auto protobufKeyResult = keyMarshaller.marshal( keyPair.publicKey );
        if ( protobufKeyResult.has_failure() )
        {
            m_logger->error( "Unable to marshal public key" );
            return outcome::failure( boost::system::error_code{} );
        }

        auto peerIDResult = PeerId::fromPublicKey( protobufKeyResult.value() );
        if ( peerIDResult.has_failure() )
        {
            m_logger->error( "Unable to get peer ID from public key" );
            return outcome::failure( boost::system::error_code{} );
        }

        auto peerID = peerIDResult.value();
        m_logger->info( "Peer ID from public key: " + peerID.toBase58() );

        return outcome::success( keyPair );
    }
}
