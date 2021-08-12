#include <crdt/globaldb/keypair_file_storage.hpp>

#include <libp2p/crypto/key_marshaller/key_marshaller_impl.hpp>
#include <libp2p/crypto/key_validator/key_validator_impl.hpp>
#include <libp2p/peer/peer_id.hpp>
#include <libp2p/peer/peer_address.hpp>
#include <crypto/ed25519/ed25519_provider_impl.hpp>
#include <libp2p/crypto/random_generator/boost_generator.hpp>
#include <libp2p/crypto/crypto_provider/crypto_provider_impl.hpp>
#include <libp2p/crypto/ed25519_provider/ed25519_provider_impl.hpp>
#include <libp2p/crypto/rsa_provider/rsa_provider_impl.hpp>
#include <libp2p/crypto/secp256k1_provider/secp256k1_provider_impl.hpp>
#include <libp2p/crypto/ecdsa_provider/ecdsa_provider_impl.hpp>
#include <libp2p/crypto/hmac_provider/hmac_provider_impl.hpp>

namespace sgns::crdt
{
namespace
{
//using RocksDB = sgns::storage::rocksdb;
//using Buffer = sgns::base::Buffer;
using CryptoProvider = libp2p::crypto::CryptoProviderImpl;
//using IdentityManager = libp2p::peer::IdentityManagerImpl;
using KeyPair = libp2p::crypto::KeyPair;
using PrivateKey = libp2p::crypto::PrivateKey;
using PublicKey = libp2p::crypto::PublicKey;
using KeyMarshaller = libp2p::crypto::marshaller::KeyMarshallerImpl;
using KeyValidator = libp2p::crypto::validator::KeyValidatorImpl;
using PeerId = libp2p::peer::PeerId;
using PeerAddress = libp2p::peer::PeerAddress;
//using CrdtOptions = sgns::crdt::CrdtOptions;
//using CrdtDatastore = sgns::crdt::CrdtDatastore;
//using HierarchicalKey = sgns::crdt::HierarchicalKey;
//using PubSubBroadcaster = sgns::crdt::PubSubBroadcaster;
//using GraphsyncDAGSyncer = sgns::crdt::GraphsyncDAGSyncer;
//using RocksdbDatastore = sgns::ipfs_lite::ipfs::RocksdbDatastore;
//using IpfsRocksDb = sgns::ipfs_lite::rocksdb;
//using GossipPubSub = sgns::ipfs_pubsub::GossipPubSub;
//using GraphsyncImpl = sgns::ipfs_lite::ipfs::graphsync::GraphsyncImpl;
//using GossipPubSubTopic = sgns::ipfs_pubsub::GossipPubSubTopic;

/** Generate key pair or load it from file if available
*/
outcome::result<KeyPair> GenerateKeyPair(
    const boost::filesystem::path& pathToKey, 
    std::shared_ptr<KeyMarshaller>& keyMarshaller, 
    const sgns::base::Logger& logger)
{
    KeyPair keyPair;
    auto cryptoProvider = std::make_shared<CryptoProvider>(
        std::make_shared<libp2p::crypto::random::BoostRandomGenerator>(),
        std::make_shared<libp2p::crypto::ed25519::Ed25519ProviderImpl>(),
        std::make_shared<libp2p::crypto::rsa::RsaProviderImpl>(),
        std::make_shared<libp2p::crypto::ecdsa::EcdsaProviderImpl>(),
        std::make_shared<libp2p::crypto::secp256k1::Secp256k1ProviderImpl>(),
        std::make_shared<libp2p::crypto::hmac::HmacProviderImpl>());

    auto keyValidator = std::make_shared<KeyValidator>(cryptoProvider);
    keyMarshaller = std::make_shared<KeyMarshaller>(keyValidator);

    if (!boost::filesystem::exists(pathToKey))
    {
        auto keyPairResult = cryptoProvider->generateKeys(libp2p::crypto::Key::Type::Ed25519,
            libp2p::crypto::common::RSAKeyType::RSA1024);

        if (keyPairResult.has_failure())
        {
            logger->error("Unable to generate key pair");
            return outcome::failure(boost::system::error_code{});
        }

        keyPair = keyPairResult.value();

        auto marshalPrivateKeyResult = keyMarshaller->marshal(keyPair.privateKey);
        if (marshalPrivateKeyResult.has_failure())
        {
            logger->error("Unable to marshal private key");
            return outcome::failure(boost::system::error_code{});
        }
        auto marshalPublicKeyResult = keyMarshaller->marshal(keyPair.publicKey);
        if (marshalPublicKeyResult.has_failure())
        {
            logger->error("Unable to marshal public key");
            return outcome::failure(boost::system::error_code{});
        }

        std::ofstream fileKey(pathToKey.string(), std::ios::out | std::ios::binary);
        std::copy(marshalPrivateKeyResult.value().key.cbegin(), marshalPrivateKeyResult.value().key.cend(),
            std::ostreambuf_iterator<char>(fileKey));
        std::copy(marshalPublicKeyResult.value().key.cbegin(), marshalPublicKeyResult.value().key.cend(),
            std::ostreambuf_iterator<char>(fileKey));
        fileKey.close();
    }
    else
    {
        std::ifstream fileKey(pathToKey.string(), std::ios::in | std::ios::binary);
        if (!fileKey.is_open())
        {
            logger->error("Unable to open key file: " + pathToKey.string());
            return outcome::failure(boost::system::error_code{});
        }
        std::istreambuf_iterator<char> it{ fileKey }, end;
        std::string ss{ it, end };

        std::vector<uint8_t> key = std::vector<uint8_t>(ss.begin(), ss.begin() + ss.size() / 2);
        libp2p::crypto::ProtobufKey privateProtobufKey{ key };

        key.clear();
        key = std::vector<uint8_t>(ss.begin() + ss.size() / 2, ss.end());
        libp2p::crypto::ProtobufKey publicProtobufKey{ key };

        auto unmarshalPrivateKeyResult = keyMarshaller->unmarshalPrivateKey(privateProtobufKey);
        if (unmarshalPrivateKeyResult.has_failure())
        {
            logger->error("Unable to unmarshal private key");
            return outcome::failure(boost::system::error_code{});
        }
        keyPair.privateKey = unmarshalPrivateKeyResult.value();

        auto unmarshalPublicKeyResult = keyMarshaller->unmarshalPublicKey(publicProtobufKey);
        if (unmarshalPublicKeyResult.has_failure())
        {
            logger->error("Unable to unmarshal public key");
            return outcome::failure(boost::system::error_code{});
        }
        keyPair.publicKey = unmarshalPublicKeyResult.value();
    }

    return keyPair;
}
}

KeyPairFileStorage::KeyPairFileStorage(const boost::filesystem::path& keyPath)
    : m_keyPath(keyPath)
{
}

outcome::result<libp2p::crypto::KeyPair> KeyPairFileStorage::GetKeyPair() const
{
    m_logger->info("Path to keypairs " + m_keyPath.string());
    std::shared_ptr<KeyMarshaller> keyMarshaller;
    auto keyPairResult = GenerateKeyPair(m_keyPath, keyMarshaller, m_logger);
    if (keyPairResult.has_failure())
    {
        m_logger->error("Unable to get key pair");
        return outcome::failure(boost::system::error_code{});
    }

    PrivateKey privateKey = keyPairResult.value().privateKey;
    PublicKey publicKey = keyPairResult.value().publicKey;

    if (keyMarshaller == nullptr)
{
        m_logger->error("Unable to marshal keys, keyMarshaller is NULL");
        return outcome::failure(boost::system::error_code{});
    }

    auto protobufKeyResult = keyMarshaller->marshal(publicKey);
    if (protobufKeyResult.has_failure())
    {
        m_logger->error("Unable to marshal public key");
        return outcome::failure(boost::system::error_code{});
}

    auto peerIDResult = PeerId::fromPublicKey(protobufKeyResult.value());
    if (peerIDResult.has_failure())
{
        m_logger->error("Unable to get peer ID from public key");
        return outcome::failure(boost::system::error_code{});
    }

    auto peerID = peerIDResult.value();
    m_logger->info("Peer ID from public key: " + peerID.toBase58());

    return outcome::success(keyPairResult.value());
}
}