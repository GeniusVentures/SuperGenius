#include <crdt/crdt_datastore.hpp>
#include <boost/program_options.hpp>
#include <storage/rocksdb/rocksdb.hpp>
#include <boost/filesystem.hpp>
#include <boost/random.hpp>
#include <ipfs_lite/ipfs/merkledag/impl/merkledag_service_impl.hpp>
#include <ipfs_lite/ipfs/impl/datastore_rocksdb.hpp>
#include <libp2p/crypto/key_marshaller/key_marshaller_impl.hpp>
#include <libp2p/crypto/key_validator/key_validator_impl.hpp>
#include <libp2p/multi/multiaddress.hpp>
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

namespace sgns
{
  namespace crdt
  {
    class PubSubBroadcaster;
    class CustomDagSyncer;
  }
}

using RocksDB = sgns::storage::rocksdb;
using Buffer = sgns::base::Buffer;
using CryptoProvider = libp2p::crypto::CryptoProviderImpl;
using PrivateKey = libp2p::crypto::PrivateKey;
using PublicKey = libp2p::crypto::PublicKey;
using KeyMarshaller = libp2p::crypto::marshaller::KeyMarshallerImpl;
using KeyValidator = libp2p::crypto::validator::KeyValidatorImpl;
using PeerId = libp2p::peer::PeerId;
using PeerAddress = libp2p::peer::PeerAddress;
using CrdtOptions = sgns::crdt::CrdtOptions;
using CrdtDatastore = sgns::crdt::CrdtDatastore;
using HierarchicalKey = sgns::crdt::HierarchicalKey;
using PubSubBroadcaster = sgns::crdt::PubSubBroadcaster;
using CustomDagSyncer = sgns::crdt::CustomDagSyncer;
using RocksdbDatastore = sgns::ipfs_lite::ipfs::RocksdbDatastore;
using IpfsRocksDb = sgns::ipfs_lite::rocksdb;

namespace po = boost::program_options;

int main(int argc, char** argv)
{

  std::string strDatabasePath;
  po::options_description desc("Input arguments:");
  try
  {
    desc.add_options()
      ("help,h", "print help")
      ("databasePath,p", po::value<std::string>(&strDatabasePath)->default_value("CRDT.Datastore"),
        "Path to CRDT datastore")
      ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
      std::cout << desc << "\n";
      return 1;
    }
  }
  catch (std::exception& e)
  {
    std::cerr << "Error parsing arguments: " << e.what() << "\n";
    std::cout << desc << "\n";
    return 1;
  }
  auto logger = sgns::base::createLogger("globaldb");

  boost::filesystem::path databasePath = strDatabasePath;

  auto strDatabasePathAbsolute = boost::filesystem::absolute(databasePath).string();

  // Create new database
  logger->info("Opening database " + strDatabasePathAbsolute);
  RocksDB::Options options;
  options.create_if_missing = true;  // intentionally
  auto dataStoreResult = RocksDB::create(boost::filesystem::absolute(databasePath).string(), options);
  auto dataStore = dataStoreResult.value();

  PrivateKey privateKey;
  PublicKey publicKey;

  auto cryptoProvider = std::make_shared<CryptoProvider>(
    std::make_shared<libp2p::crypto::random::BoostRandomGenerator>(),
    std::make_shared<libp2p::crypto::ed25519::Ed25519ProviderImpl>(),
    std::make_shared<libp2p::crypto::rsa::RsaProviderImpl>(),
    std::make_shared<libp2p::crypto::ecdsa::EcdsaProviderImpl>(),
    std::make_shared<libp2p::crypto::secp256k1::Secp256k1ProviderImpl>(),
    std::make_shared<libp2p::crypto::hmac::HmacProviderImpl>());

  auto keyValidator = std::make_shared<KeyValidator>(cryptoProvider);
  auto keyMarshaller = std::make_shared<KeyMarshaller>(keyValidator);

  boost::filesystem::path keyPath = strDatabasePathAbsolute + "/key";
  logger->info("Path to keypairs " + keyPath.string());
  if (!boost::filesystem::exists(keyPath))
  {
    auto keyPairResult = cryptoProvider->generateKeys(libp2p::crypto::Key::Type::Ed25519, 
      libp2p::crypto::common::RSAKeyType::RSA1024);
    
    if (keyPairResult.has_failure())
    {
      logger->error("Unable to generate key pair");
      return 1;
    }

    privateKey = keyPairResult.value().privateKey;
    publicKey = keyPairResult.value().publicKey;

    auto marshalPrivateKeyResult = keyMarshaller->marshal(privateKey);
    auto marshalPublicKeyResult = keyMarshaller->marshal(privateKey);

    // TODO: save to file

  }
  else
  {
    // TODO: Read from file
  }

  auto protobufKeyResult = keyMarshaller->marshal(publicKey);
  if (protobufKeyResult.has_failure())
  {
    logger->error("Unable to marshal public key");
    return 1;
  }

  auto peerIDResult = PeerId::fromPublicKey(protobufKeyResult.value());
  if (peerIDResult.has_failure())
  {
    logger->error("Unable to get peer ID from public key");
    return 1;
  }

  auto peerID = peerIDResult.value();

  // TODO: Create pubsub gossip node

  // TODO: Create pubsub broadcaster 
  auto broadcaster = std::make_shared<PubSubBroadcaster>();

  // Create new DAGSyncer
  IpfsRocksDb::Options rdbOptions;
  rdbOptions.create_if_missing = true;  // intentionally
  auto ipfsDBResult = IpfsRocksDb::create(dataStore->getDB());
  if (ipfsDBResult.has_error())
  {
    logger->error("Unable to create database for IPFS datastore");
    return 1;
  }

  auto ipfsDataStore = std::make_shared<RocksdbDatastore>(ipfsDBResult.value());
  auto dagSyncer = std::make_shared<CustomDagSyncer>(ipfsDataStore);

  auto crdtOptions = CrdtOptions::DefaultOptions();
  crdtOptions->logger = logger;

  auto crdtDatastore = std::make_shared<CrdtDatastore>(dataStore, HierarchicalKey("crdt"), dagSyncer, broadcaster, CrdtOptions::DefaultOptions());
  if (crdtDatastore == nullptr)
  {
    logger->error("Unable to create CRDT datastore");
    return 1;
  }

  logger->info("Bootstrapping...");

  //auto bstr = libp2p::multi::Multiaddress::create("/ip4/127.0.0.1/tcp/33123/ipfs/12D3KooWFta2AE7oiK1ioqjVAKajUJauZWfeM7R413K7ARtHRDAu");
  //if (bstr.has_failure())
  //{
  //  logger->error("Unable to create multi address");
  //  return 1;
  //}

  //auto inf = PeerAddress::create(peerID, bstr.value());

  // TODO: continue implementation 
  // https://github.com/ipfs/go-ds-crdt/blob/master/examples/globaldb/globaldb.go

  return 0;
}

namespace sgns::crdt
{

  // TODO: Need to implement it based on new pubsub 
  class PubSubBroadcaster : public Broadcaster
  {
  public:

    PubSubBroadcaster() = default;

    /**
    * Send {@param buff} payload to other replicas.
    * @return outcome::success on success or outcome::failure on error
    */
    virtual outcome::result<void> PubSubBroadcaster::Broadcast(const base::Buffer& buff) override
    {
      if (!buff.empty())
      {
        const std::string bCastData(buff.toString());
        listOfBroadcasts_.push(bCastData);
      }
      return outcome::success();
    }

    /**
    * Obtain the next {@return} payload received from the network.
    * @return buffer value or outcome::failure on error
    */
    virtual outcome::result<base::Buffer> PubSubBroadcaster::Next() override
    {
      if (listOfBroadcasts_.empty())
      {
        //Broadcaster::ErrorCode::ErrNoMoreBroadcast
        return outcome::failure(boost::system::error_code{});
      }

      std::string strBuffer = listOfBroadcasts_.front();
      listOfBroadcasts_.pop();

      base::Buffer buffer;
      buffer.put(strBuffer);
      return buffer;
    }

    std::queue<std::string> listOfBroadcasts_;
  };

  class CustomDagSyncer : public DAGSyncer
  {
  public:
    using IpfsDatastore = ipfs_lite::ipfs::IpfsDatastore;
    using MerkleDagServiceImpl = ipfs_lite::ipfs::merkledag::MerkleDagServiceImpl;

    CustomDagSyncer(std::shared_ptr<IpfsDatastore> service)
      : DAGSyncer(service)
    {
    }

    virtual outcome::result<bool> HasBlock(const CID& cid) const override
    {
      auto getNodeResult = this->getNode(cid);
      return getNodeResult.has_value();
    }
  };
}
