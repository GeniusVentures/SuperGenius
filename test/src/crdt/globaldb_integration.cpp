#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <boost/dll.hpp>
#include <boost/format.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <chrono>
#include <thread>
#include <fstream>
#include <openssl/sha.h>
#include <random>

#include "crdt/globaldb/globaldb.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/hierarchical_key.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "base/buffer.hpp"
#include "base/logger.hpp"
#include "base/sgns_version.hpp"

#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/local_requests.hpp>
#include <libp2p/protocol/common/asio/asio_scheduler.hpp>

namespace
{
    template <typename Predicate>
    bool waitForCondition( Predicate                 pred,
                           std::chrono::milliseconds timeout,
                           std::chrono::milliseconds pollInterval = std::chrono::milliseconds( 100 ) )
    {
        const auto start = std::chrono::steady_clock::now();
        while ( std::chrono::steady_clock::now() - start < timeout )
        {
            if ( pred() )
            {
                return true;
            }
            std::this_thread::sleep_for( pollInterval );
        }
        return false;
    }

    std::string GetLoggingSystem( const std::string & )
    {
        return R"(
     # ----------------
     sinks:
       - name: console
         type: console
         color: true
     groups:
       - name: gossip_pubsub_test
         sink: console
         level: error
         children:
           - name: libp2p
           - name: Gossip
     # ----------------
        )";
    }

#define WAIT_TIMEOUT ( std::chrono::milliseconds( 25000 ) )
} // namespace

class GlobalDBIntegrationTest : public ::testing::Test
{
public:
    class TestNodeCollection
    {
    public:
        struct TestNode
        {
            std::string                                      basePath;
            std::shared_ptr<boost::asio::io_context>         io;
            std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub;
            std::shared_ptr<sgns::crdt::GlobalDB>            db;
            std::thread                                      ioThread;

            TestNode()                                  = default;
            TestNode( const TestNode & )                = delete;
            TestNode &operator=( const TestNode & )     = delete;
            TestNode( TestNode && ) noexcept            = default;
            TestNode &operator=( TestNode && ) noexcept = default;
        };

        static uint16_t currentPubsubPort;

        void addNode( const std::string &dbName )
        {
            const std::string testName   = ::testing::UnitTest::GetInstance()->current_test_info()->name();
            const std::string binaryPath = boost::dll::program_location().parent_path().string();
            const std::string basePath   = binaryPath + "/" + dbName + "_" + testName;
            boost::filesystem::remove_all( basePath );
            boost::filesystem::create_directories( basePath );

            sgns::crdt::KeyPairFileStorage keyStore( basePath + "/key" );
            auto                           keyPair  = keyStore.GetKeyPair().value();
            auto                           pubsub   = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( keyPair );
            const std::string              listenIp = "127.0.0.1";
            pubsub->Start( currentPubsubPort, {}, listenIp, {} );

            auto io = std::make_shared<boost::asio::io_context>();
            auto db = std::make_shared<sgns::crdt::GlobalDB>( io, basePath + "/CommonKey", pubsub );

            ++currentPubsubPort;
            auto scheduler        = std::make_shared<libp2p::protocol::AsioScheduler>( io,
                                                                                libp2p::protocol::SchedulerConfig{} );
            auto graphsyncnetwork = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubsub->GetHost(),
                                                                                                 scheduler );
            auto generator        = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();
            auto initRes          = db->Init( sgns::crdt::CrdtOptions::DefaultOptions(),
                                     graphsyncnetwork,
                                     scheduler,
                                     generator );
            if ( !initRes.has_value() )
            {
                return;
            }
            std::thread t( [io]() { io->run(); } );
            nodes_.push_back( TestNode{ basePath, io, pubsub, db, std::move( t ) } );
        }

        void connectNodes( std::chrono::milliseconds delay = std::chrono::seconds( 1 ) )
        {
            for ( size_t i = 0; i < nodes_.size(); ++i )
            {
                for ( size_t j = i + 1; j < nodes_.size(); ++j )
                {
                    nodes_[i].pubsub->AddPeers( { nodes_[j].pubsub->GetLocalAddress() } );
                }
            }
            std::this_thread::sleep_for( delay );
        }

        const std::vector<TestNode> &getNodes() const
        {
            return nodes_;
        }

        std::vector<TestNode> &getNodes()
        {
            return nodes_;
        }

        ~TestNodeCollection()
        {
            for ( auto &node : nodes_ )
            {
                if ( node.io )
                {
                    node.io->stop();
                }
                if ( node.ioThread.joinable() )
                {
                    node.ioThread.join();
                }
                node.pubsub->Stop();
                node.db.reset();
                node.pubsub.reset();
                node.io.reset();
            }

            nodes_.clear();
        }

    private:
        std::vector<TestNode> nodes_;
    };

    static void SetUpTestSuite()
    {
        const std::string binaryPath         = boost::dll::program_location().parent_path().string();
        const std::string loggingYAML        = GetLoggingSystem( binaryPath + "/globaldbtest" );
        auto              loggerConfigurator = std::make_shared<libp2p::log::Configurator>();
        auto       configFromYAML = std::make_shared<soralog::ConfiguratorFromYAML>( loggerConfigurator, loggingYAML );
        auto       loggingSystem  = std::make_shared<soralog::LoggingSystem>( configFromYAML );
        const auto confResult     = loggingSystem->configure();
        if ( confResult.has_error )
        {
            throw std::runtime_error( "Could not configure logger" );
        }
        libp2p::log::setLoggingSystem( loggingSystem );
        auto nodeLogger = sgns::base::createLogger( "SuperGeniusDemo", binaryPath + "/sgnslog2.log" );
        nodeLogger->set_level( spdlog::level::err );
        const auto loggerGlobalDB     = sgns::base::createLogger( "GlobalDB" );
        const auto loggerBroadcaster  = sgns::base::createLogger( "PubSubBroadcasterExt" );
        const auto loggerDataStore    = sgns::base::createLogger( "CrdtDatastore" );
        const auto loggerGossipPubsub = sgns::base::createLogger( "GossipPubSub" );
        loggerGlobalDB->set_level( spdlog::level::off );
        loggerBroadcaster->set_level( spdlog::level::off );
        loggerDataStore->set_level( spdlog::level::off );
        loggerGossipPubsub->set_level( spdlog::level::off );
    }
};

uint16_t GlobalDBIntegrationTest::TestNodeCollection::currentPubsubPort = 50501;

TEST_F( GlobalDBIntegrationTest, BroadcastsAcrossAllTopics )
{
    auto testNodes = std::make_unique<TestNodeCollection>();
    testNodes->addNode( "node1" );
    testNodes->addNode( "node2" );
    testNodes->addNode( "node3" );
    testNodes->addNode( "node4" );

    const std::vector<std::string> topics = { "alpha", "beta", "gamma" };

    // Configure broadcaster with all three topics
    for ( const auto &topic : topics )
    {
        testNodes->getNodes()[0].db->AddBroadcastTopic( topic );
    }

    // Configure each listener to subscribe to exactly one topic
    for ( size_t i = 0; i < topics.size(); ++i )
    {
        testNodes->getNodes()[i + 1].db->AddListenTopic( topics[i] );
    }

    testNodes->connectNodes();

    sgns::base::Buffer buffer;
    buffer.put( "hello topics" );
    const sgns::crdt::HierarchicalKey key( "/multi/topic" );
    auto                              tx = testNodes->getNodes()[0].db->BeginTransaction();
    ASSERT_TRUE( tx->Put( key, buffer ).has_value() );
    ASSERT_TRUE( tx->Commit().has_value() );

    for ( size_t i = 1; i < testNodes->getNodes().size(); ++i )
    {
        EXPECT_TRUE(
            waitForCondition( [&]() { return testNodes->getNodes()[i].db->Get( key ).has_value(); }, WAIT_TIMEOUT ) );
    }
}

TEST_F( GlobalDBIntegrationTest, TwoWayBroadcast )
{
    auto testNodes = std::make_unique<TestNodeCollection>();
    testNodes->addNode( "node1" );
    testNodes->addNode( "node2" );

    const std::string topic = "exchange";
    // Both nodes broadcast and listen on the same topic
    for ( auto &node : testNodes->getNodes() )
    {
        node.db->AddBroadcastTopic( topic );
        node.db->AddListenTopic( topic );
    }

    testNodes->connectNodes();

    // node1 -> node2
    {
        sgns::base::Buffer buf1;
        buf1.put( "hello node2" );
        const sgns::crdt::HierarchicalKey key1( "/first/topic" );
        auto                              tx1 = testNodes->getNodes()[0].db->BeginTransaction();
        ASSERT_TRUE( tx1->Put( key1, buf1 ).has_value() );
        ASSERT_TRUE( tx1->Commit().has_value() );

        EXPECT_TRUE(
            waitForCondition( [&]() { return testNodes->getNodes()[1].db->Get( key1 ).has_value(); }, WAIT_TIMEOUT ) );
    }

    // node2 -> node1
    {
        sgns::base::Buffer buf2;
        buf2.put( "hello node1" );
        const sgns::crdt::HierarchicalKey key2( "/second/topic" );
        auto                              tx2 = testNodes->getNodes()[1].db->BeginTransaction();
        ASSERT_TRUE( tx2->Put( key2, buf2 ).has_value() );
        ASSERT_TRUE( tx2->Commit().has_value() );

        EXPECT_TRUE(
            waitForCondition( [&]() { return testNodes->getNodes()[0].db->Get( key2 ).has_value(); }, WAIT_TIMEOUT ) );
    }
}

TEST_F( GlobalDBIntegrationTest, BroadcastLoopUpdates )
{
    auto       testNodes  = std::make_unique<TestNodeCollection>();
    const auto iterations = 50;

    testNodes->addNode( "node1" );
    testNodes->addNode( "node2" );

    for ( auto &node : testNodes->getNodes() )
    {
        node.db->AddBroadcastTopic( "topic" );
        node.db->AddListenTopic( "topic" );
    }
    testNodes->connectNodes();

    const sgns::crdt::HierarchicalKey key( "/loop/key" );
    for ( auto i = 0; i < iterations; ++i )
    {
        SCOPED_TRACE( "Iteration " + std::to_string( i ) );
        sgns::base::Buffer buf;
        buf.put( "value_" + std::to_string( i ) );

        auto tx = testNodes->getNodes()[0].db->BeginTransaction();
        ASSERT_TRUE( tx->Put( key, buf ).has_value() );
        ASSERT_TRUE( tx->Commit().has_value() );

        EXPECT_TRUE( waitForCondition(
            [&]()
            {
                auto res = testNodes->getNodes()[1].db->Get( key );
                return res.has_value() && res.value().toString() == ( "value_" + std::to_string( i ) );
            },
            WAIT_TIMEOUT ) );
    }
}

TEST_F( GlobalDBIntegrationTest, OneTopicManyNodes )
{
    auto       testNodes  = std::make_unique<TestNodeCollection>();
    const auto numNodes   = 20;
    const auto iterations = 20;

    for ( auto i = 0; i < numNodes; ++i )
    {
        testNodes->addNode( "node" + std::to_string( i ) );
    }

    // Configure a single topic for broadcast and listen on all nodes
    const std::string topic = "topic";
    for ( auto &node : testNodes->getNodes() )
    {
        node.db->AddBroadcastTopic( topic );
        node.db->AddListenTopic( topic );
    }

    testNodes->connectNodes();

    for ( auto i = 0; i < iterations; ++i )
    {
        auto senderIdx = i % numNodes;
        SCOPED_TRACE( "Iteration " + std::to_string( i ) + ", sender=node" + std::to_string( senderIdx ) );

        sgns::base::Buffer buffer;
        std::string        message = "i_" + std::to_string( i );
        buffer.put( message );

        // Use a fixed key namespace for synchronization
        const sgns::crdt::HierarchicalKey key( "/sync/key" );

        // Begin transaction on the sender node and publish the message
        auto tx = testNodes->getNodes()[senderIdx].db->BeginTransaction();
        ASSERT_TRUE( tx->Put( key, buffer ).has_value() );
        ASSERT_TRUE( tx->Commit().has_value() );

        // Verify that every other node receives the updated value
        for ( auto recvIdx = 0; recvIdx < numNodes; ++recvIdx )
        {
            if ( recvIdx == senderIdx )
            {
                continue;
            }
            EXPECT_TRUE( waitForCondition(
                [&]()
                {
                    auto res = testNodes->getNodes()[recvIdx].db->Get( key );
                    return res.has_value() && res.value().toString() == message;
                },
                WAIT_TIMEOUT ) );
        }
    }
}

TEST_F( GlobalDBIntegrationTest, NoListenerDoesNotReceive )
{
    auto testNodes = std::make_unique<TestNodeCollection>();
    testNodes->addNode( "nodeA" );
    testNodes->addNode( "nodeB" );
    testNodes->connectNodes();

    testNodes->getNodes()[0].db->AddBroadcastTopic( "topic" );
    testNodes->getNodes()[1].db->AddBroadcastTopic( "other" );

    const sgns::crdt::HierarchicalKey key( "/key" );
    sgns::base::Buffer                buffer;
    buffer.put( "value" );
    const auto tx = testNodes->getNodes()[0].db->BeginTransaction();
    ASSERT_TRUE( tx->Put( key, buffer ).has_value() );
    ASSERT_TRUE( tx->Commit().has_value() );

    EXPECT_TRUE(
        waitForCondition( [&]() { return testNodes->getNodes()[0].db->Get( key ).has_value(); }, WAIT_TIMEOUT ) );
    EXPECT_FALSE(
        waitForCondition( [&]() { return testNodes->getNodes()[1].db->Get( key ).has_value(); }, WAIT_TIMEOUT ) );
}

TEST_F( GlobalDBIntegrationTest, PreventDoubleCommit )
{
    auto testNodes = std::make_unique<TestNodeCollection>();
    testNodes->addNode( "single" );
    testNodes->getNodes()[0].db->AddBroadcastTopic( "topic" );
    testNodes->connectNodes();

    const sgns::crdt::HierarchicalKey key( "/key" );
    sgns::base::Buffer                buffer;
    buffer.put( "data" );
    const auto tx = testNodes->getNodes()[0].db->BeginTransaction();
    ASSERT_TRUE( tx->Put( key, buffer ).has_value() );
    ASSERT_TRUE( tx->Commit().has_value() );
    EXPECT_FALSE( tx->Commit().has_value() );
}

TEST_F( GlobalDBIntegrationTest, OperationsWithoutInitialization )
{
    const std::string binaryPath  = boost::dll::program_location().parent_path().string();
    const std::string tmpBasePath = binaryPath + "/globaldb_no_init_ops";
    boost::filesystem::create_directories( tmpBasePath );

    sgns::crdt::KeyPairFileStorage keyStore( tmpBasePath + "/key" );
    const auto                     keyPair = keyStore.GetKeyPair().value();
    auto                           pubsub  = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( keyPair );
    pubsub->Start( 50800, {}, "127.0.0.1", {} );

    auto io = std::make_shared<boost::asio::io_context>();
    auto db = std::make_shared<sgns::crdt::GlobalDB>( io, tmpBasePath + "/CommonKey", pubsub );

    const sgns::crdt::HierarchicalKey key( "/nonexistent" );
    EXPECT_FALSE( db->Get( key ).has_value() );
    EXPECT_FALSE( db->Remove( key ).has_value() );
    EXPECT_FALSE( db->QueryKeyValues( key.GetKey() ).has_value() );

    boost::filesystem::remove_all( tmpBasePath );
}

TEST_F( GlobalDBIntegrationTest, DISABLED_QueryKeyValuesWithRegex )
{
    auto testNodes = std::make_unique<TestNodeCollection>();
    // Node 0 = writer, Node 1 = reader
    testNodes->addNode( "node_writer" );
    testNodes->addNode( "node_reader" );

    // Configure both to broadcast/listen on default topic
    const std::string topic = "query_regex";
    for ( auto &node : testNodes->getNodes() )
    {
        node.db->AddBroadcastTopic( topic );
        node.db->AddListenTopic( topic );
    }
    testNodes->connectNodes();

    // Writer inserts multiple keys under the same prefix
    std::vector<std::pair<std::string, std::string>> entries = { { "/filter/alpha", "A" },
                                                                 { "/filter/beta", "B" },
                                                                 { "/filter/a123", "C" },
                                                                 { "/filter/gamma", "D" } };
    for ( auto &e : entries )
    {
        sgns::base::Buffer buf;
        buf.put( e.second );
        auto tx = testNodes->getNodes()[0].db->BeginTransaction();
        ASSERT_TRUE( tx->Put( { e.first }, buf ).has_value() );
        ASSERT_TRUE( tx->Commit().has_value() );
    }

    // Now wait on the reader node until it has the right regex query result
    EXPECT_TRUE( waitForCondition(
        [&]
        {
            auto res = testNodes->getNodes()[1].db->QueryKeyValues( "filter", "a[a-z]+", "" );
            if ( !res.has_value() )
            {
                return false;
            }
            return res.value().size() == 1;
        },
        WAIT_TIMEOUT ) );

    // Verify it really found only "/filter/alpha"
    auto finalRes = testNodes->getNodes()[1].db->QueryKeyValues( "filter", "a[a-z]+", "" );
    ASSERT_TRUE( finalRes.has_value() );
    EXPECT_NE( finalRes.value().begin()->first.toString().find( "alpha" ), std::string::npos );
}

TEST_F( GlobalDBIntegrationTest, DISABLED_QueryKeyValuesWithPrefixAndSuffix )
{
    auto testNodes = std::make_unique<TestNodeCollection>();
    testNodes->addNode( "node_writer_ps" );
    testNodes->addNode( "node_reader_ps" );

    const std::string topic = "query_ps";
    for ( auto &node : testNodes->getNodes() )
    {
        node.db->AddBroadcastTopic( topic );
        node.db->AddListenTopic( topic );
    }
    testNodes->connectNodes();

    // Writer inserts keys with different structures
    std::vector<std::pair<std::string, std::string>> entries = { { "/rx/one/end", "1" },
                                                                 { "/rx/two/end", "2" },
                                                                 { "/rx/one/foo", "3" },
                                                                 { "/rx/three/bar", "4" } };
    for ( auto &e : entries )
    {
        sgns::base::Buffer buf;
        buf.put( e.second );
        auto tx = testNodes->getNodes()[0].db->BeginTransaction();
        ASSERT_TRUE( tx->Put( { e.first }, buf ).has_value() );
        ASSERT_TRUE( tx->Commit().has_value() );
    }

    // Wait until reader returns exactly one entry for prefix="rx", middle="o.*", remainder="end"
    EXPECT_TRUE( waitForCondition(
        [&]
        {
            auto res = testNodes->getNodes()[1].db->QueryKeyValues( "rx", "o.*", "end" );
            if ( !res.has_value() )
            {
                return false;
            }
            return res.value().size() == 1;
        },
        WAIT_TIMEOUT ) );

    // Final sanity check
    auto finalRes = testNodes->getNodes()[1].db->QueryKeyValues( "rx", "o.*", "end" );
    ASSERT_TRUE( finalRes.has_value() );
    EXPECT_NE( finalRes.value().begin()->first.toString().find( "/one/end" ), std::string::npos );
}
