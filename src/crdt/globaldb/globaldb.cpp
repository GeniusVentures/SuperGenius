#include "globaldb.hpp"
#include "pubsub_broadcaster.hpp"
#include "pubsub_broadcaster_ext.hpp"
#include "keypair_file_storage.hpp"

#include <crdt/crdt_datastore.hpp>
#include <crdt/graphsync_dagsyncer.hpp>

#include <ipfs_lite/ipfs/merkledag/impl/merkledag_service_impl.hpp>
#include <ipfs_lite/ipfs/impl/datastore_rocksdb.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/graphsync_impl.hpp>

#include <libp2p/multi/multiaddress.hpp>
#include <libp2p/host/host.hpp>
#include <libp2p/injector/host_injector.hpp>
#include <libp2p/protocol/common/asio/asio_scheduler.hpp>
#include <libp2p/common/literals.hpp>

#include <boost/format.hpp>

namespace sgns::crdt
{
using RocksDB = sgns::storage::rocksdb;
using CrdtOptions = sgns::crdt::CrdtOptions;
using CrdtDatastore = sgns::crdt::CrdtDatastore;
using HierarchicalKey = sgns::crdt::HierarchicalKey;
using PubSubBroadcaster = sgns::crdt::PubSubBroadcaster;
using GraphsyncDAGSyncer = sgns::crdt::GraphsyncDAGSyncer;
using RocksdbDatastore = sgns::ipfs_lite::ipfs::RocksdbDatastore;
using IpfsRocksDb = sgns::ipfs_lite::rocksdb;
using GossipPubSub = sgns::ipfs_pubsub::GossipPubSub;
using GraphsyncImpl = sgns::ipfs_lite::ipfs::graphsync::GraphsyncImpl;
using GossipPubSubTopic = sgns::ipfs_pubsub::GossipPubSubTopic;

GlobalDB::GlobalDB(
    std::shared_ptr<boost::asio::io_context> context,
    std::string databasePath,
    int dagSyncPort,
    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSubTopic> broadcastChannel)
    : m_context(std::move(context))
    , m_databasePath(std::move(databasePath))
    , m_dagSyncPort(dagSyncPort)
    , m_broadcastChannel(std::move(broadcastChannel))
{
}

outcome::result<void> GlobalDB::Init(std::shared_ptr<CrdtOptions> crdtOptions)
{
    std::shared_ptr<RocksDB> dataStore = nullptr;
    auto databasePathAbsolute = boost::filesystem::absolute(m_databasePath).string();

    // Create new database
    m_logger->info("Opening database " + databasePathAbsolute);
    RocksDB::Options options;
    options.create_if_missing = true;  // intentionally
    try
    {
        auto dataStoreResult = RocksDB::create(databasePathAbsolute, options);
        dataStore = dataStoreResult.value();
    }
    catch (std::exception& e)
    {
        m_logger->error("Unable to open database: " + std::string(e.what()));
        return outcome::failure(boost::system::error_code{});
    }

    boost::filesystem::path keyPath = databasePathAbsolute + "/key";
    KeyPairFileStorage keyPairStorage(keyPath);
    auto keyPair = keyPairStorage.GetKeyPair();
    // injector creates and ties dependent objects
    auto injector = libp2p::injector::makeHostInjector<BOOST_DI_CFG>(
        boost::di::bind<boost::asio::io_context>.to(m_context)[boost::di::override],
        boost::di::bind<libp2p::crypto::KeyPair>.to(keyPair.value())[boost::di::override]);

    // create asio context
    auto io = injector.create<std::shared_ptr<boost::asio::io_context>>();

    // Create new DAGSyncer
    IpfsRocksDb::Options rdbOptions;
    rdbOptions.create_if_missing = true;  // intentionally
    auto ipfsDBResult = IpfsRocksDb::create(dataStore->getDB());
    if (ipfsDBResult.has_error())
    {
        m_logger->error("Unable to create database for IPFS datastore");
        return outcome::failure(boost::system::error_code{});
    }

    auto ipfsDataStore = std::make_shared<RocksdbDatastore>(ipfsDBResult.value());

    auto dagSyncerHost = injector.create<std::shared_ptr<libp2p::Host>>();

    auto listen_to = libp2p::multi::Multiaddress::create(
        (boost::format("/ip4/127.0.0.1/tcp/%d/ipfs/%s") % m_dagSyncPort % dagSyncerHost->getId().toBase58()).str()).value();
    m_logger->debug(listen_to.getStringAddress());

    auto scheduler = std::make_shared<libp2p::protocol::AsioScheduler>(*io, libp2p::protocol::SchedulerConfig{});
    auto graphsync = std::make_shared<GraphsyncImpl>(dagSyncerHost, std::move(scheduler));
    auto dagSyncer = std::make_shared<GraphsyncDAGSyncer>(ipfsDataStore, graphsync, dagSyncerHost);

    // Start DagSyner listener 
    auto listenResult = dagSyncer->Listen(listen_to);
    if (listenResult.has_failure())
    {
        m_logger->warn("DAG syncer failed to listen " + std::string(listen_to.getStringAddress()));
        // @todo Check if the error is not fatal
    }

    // Create pubsub broadcaster
    //auto broadcaster = std::make_shared<PubSubBroadcaster>(m_broadcastChannel);
    auto broadcaster = std::make_shared<PubSubBroadcasterExt>(m_broadcastChannel, dagSyncer, listen_to);
    broadcaster->SetLogger(m_logger);

    m_crdtDatastore = std::make_shared<CrdtDatastore>(
        dataStore, HierarchicalKey("crdt"), dagSyncer, broadcaster, crdtOptions);
    if (m_crdtDatastore == nullptr)
    {
        m_logger->error("Unable to create CRDT datastore");
        return outcome::failure(boost::system::error_code{});
    }

    // TODO: bootstrapping
    //m_logger->info("Bootstrapping...");
    //bstr, _ : = multiaddr.NewMultiaddr("/ip4/94.130.135.167/tcp/33123/ipfs/12D3KooWFta2AE7oiK1ioqjVAKajUJauZWfeM7R413K7ARtHRDAu");
    //inf, _ : = peer.AddrInfoFromP2pAddr(bstr);
    //list: = append(ipfslite.DefaultBootstrapPeers(), *inf);
    //ipfs.Bootstrap(list);
    //h.ConnManager().TagPeer(inf.ID, "keep", 100);

    //if (daemonMode && echoProtocol)
    //{
    //  // set a handler for Echo protocol
    //  libp2p::protocol::Echo echo{ libp2p::protocol::EchoConfig{} };
    //  host->setProtocolHandler(
    //    echo.getProtocolId(),
    //    [&echo](std::shared_ptr<libp2p::connection::Stream> received_stream) {
    //      echo.handle(std::move(received_stream));
    //    });
    //}
    return outcome::success();
}

outcome::result<void> GlobalDB::Put(const HierarchicalKey& key, const Buffer& value)
{
    if (!m_crdtDatastore)
    {
        m_logger->error("CRDT datastore is not initialized yet");
        return outcome::failure(boost::system::error_code{});
    }

    return m_crdtDatastore->PutKey(key, value);
}

outcome::result<GlobalDB::Buffer> GlobalDB::Get(const HierarchicalKey& key)
{
    if (!m_crdtDatastore)
    {
        m_logger->error("CRDT datastore is not initialized yet");
        return outcome::failure(boost::system::error_code{});
    }

    return m_crdtDatastore->GetKey(key);
}

outcome::result<void> GlobalDB::Remove(const HierarchicalKey& key)
{
    if (!m_crdtDatastore)
    {
        m_logger->error("CRDT datastore is not initialized yet");
        return outcome::failure(boost::system::error_code{});
    }

    return m_crdtDatastore->DeleteKey(key);
}

outcome::result<GlobalDB::QueryResult> GlobalDB::QueryKeyValues(const std::string& keyPrefix)
{
    if (!m_crdtDatastore)
    {
        m_logger->error("CRDT datastore is not initialized yet");
        return outcome::failure(boost::system::error_code{});
    }

    return m_crdtDatastore->QueryKeyValues(keyPrefix);
}

outcome::result<std::string> GlobalDB::KeyToString(const Buffer& key) const
{
    // @todo cache the prefix and suffix
    auto keysPrefix = m_crdtDatastore->GetKeysPrefix();
    if (!keysPrefix.has_value())
    {
        return outcome::failure(boost::system::error_code{});
    }
    auto valueSuffix = m_crdtDatastore->GetValueSuffix();
    if (!valueSuffix.has_value())
    {
        return outcome::failure(boost::system::error_code{});
    }

    auto sKey = std::string(key.toString());

    size_t prefixPos = (keysPrefix.value().size() != 0) ? sKey.find(keysPrefix.value(), 0) : 0;
    if (prefixPos != 0)
    {
        return outcome::failure(boost::system::error_code{});
    }

    size_t keyPos = keysPrefix.value().size();
    auto suffixPos = (valueSuffix.value().size() != 0) ? sKey.rfind(valueSuffix.value(), std::string::npos) : sKey.size();
    if ((suffixPos == std::string::npos) || (suffixPos < keyPos))
    {
        return outcome::failure(boost::system::error_code{});
    }

    return sKey.substr(keyPos, suffixPos - keyPos);
}

std::shared_ptr<CrdtDataStoreTransaction> GlobalDB::BeginTransaction()
{
    return m_crdtDatastore->BeginTransaction();
}

}