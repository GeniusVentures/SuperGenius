/**
 * @file       globaldb_integration_gtest.cpp
 * @brief      Integration tests for GlobalDB.
 *
 * This file creates GlobalDB instances for each test independently using dynamic
 * broadcast topics and verifies replication and transaction behavior.
 * The logger is initialized once in SetUpTestSuite.
 */

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
        static uint16_t currentGraphsyncPort;

        void addNode( const std::string &dbName )
        {
            const std::string testName   = ::testing::UnitTest::GetInstance()->current_test_info()->name();
            const std::string binaryPath = boost::dll::program_location().parent_path().string();
            const std::string basePath   = binaryPath + "/" + dbName + "_" + testName;
            boost::filesystem::create_directories( basePath );

            sgns::crdt::KeyPairFileStorage keyStore( basePath + "/key" );
            auto                           keyPair  = keyStore.GetKeyPair().value();
            auto                           pubsub   = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( keyPair );
            const std::string              listenIp = "127.0.0.1";
            pubsub->Start( currentPubsubPort, {}, listenIp, {} );

            auto io = std::make_shared<boost::asio::io_context>();
            auto db = std::make_shared<sgns::crdt::GlobalDB>( io,
                                                              basePath + "/CommonKey",
                                                              pubsub );

            ++currentPubsubPort;
            ++currentGraphsyncPort;
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
            TestNode    node{ basePath, io, pubsub, db, std::move( t ) };
            nodes_.push_back( std::move( node ) );
        }

        void connectNodes( std::chrono::milliseconds delay = std::chrono::seconds( 3 ) )
        {
            for ( size_t i = 0; i < nodes_.size(); ++i )
            {
                std::vector<std::string> peers;
                for ( size_t j = 0; j < nodes_.size(); ++j )
                {
                    if ( i != j )
                    {
                        peers.push_back( nodes_[j].pubsub->GetLocalAddress() );
                    }
                }
                nodes_[i].pubsub->AddPeers( peers );
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
                node.db.reset();
                node.pubsub.reset();
                node.io.reset();
                boost::filesystem::remove_all( node.basePath );
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
        const auto loggerGlobalDB    = sgns::base::createLogger( "GlobalDB" );
        const auto loggerBroadcaster = sgns::base::createLogger( "PubSubBroadcasterExt" );
        const auto loggerDataStore   = sgns::base::createLogger( "CrdtDatastore" );
        loggerGlobalDB->set_level( spdlog::level::trace );
        loggerBroadcaster->set_level( spdlog::level::trace );
        loggerDataStore->set_level( spdlog::level::trace );
    }
};

uint16_t GlobalDBIntegrationTest::TestNodeCollection::currentPubsubPort    = 50501;
uint16_t GlobalDBIntegrationTest::TestNodeCollection::currentGraphsyncPort = 50541;

TEST_F( GlobalDBIntegrationTest, ReplicationWithoutTopicSuccessfulTest )
{
    auto testNodes = std::make_unique<TestNodeCollection>();
    testNodes->addNode( "globaldb_node1" );
    testNodes->addNode( "globaldb_node2" );
    testNodes->addNode( "globaldb_node3" );

    for ( auto &node : testNodes->getNodes() )
    {
        node.db->AddBroadcastTopic( "firstTopic" );
        node.db->AddListenTopic( "firstTopic" );
    }

    testNodes->connectNodes();
    using sgns::crdt::HierarchicalKey;
    sgns::base::Buffer value;
    value.put( "Replication Value without topic" );
    const HierarchicalKey key( "/replication/basic_test" );
    const auto            tx = testNodes->getNodes()[0].db->BeginTransaction();
    ASSERT_NE( tx, nullptr );
    const auto putRes = tx->Put( key, value );
    ASSERT_TRUE( putRes.has_value() );
    const auto commitRes = tx->Commit();
    ASSERT_TRUE( commitRes.has_value() );

    const bool replicated = waitForCondition(
        [&]() -> bool
        {
            const auto res2 = testNodes->getNodes()[1].db->Get( key );
            const auto res3 = testNodes->getNodes()[2].db->Get( key );
            return res2.has_value() && res3.has_value();
        },
        WAIT_TIMEOUT );
    EXPECT_TRUE( replicated );
    {
        const auto res2 = testNodes->getNodes()[1].db->Get( key );
        const auto res3 = testNodes->getNodes()[2].db->Get( key );
        ASSERT_TRUE( res2.has_value() );
        ASSERT_TRUE( res3.has_value() );
        EXPECT_EQ( res2.value().toString(), "Replication Value without topic" );
        EXPECT_EQ( res3.value().toString(), "Replication Value without topic" );
    }
}

TEST_F( GlobalDBIntegrationTest, ReplicationViaTopicBroadcastTest )
{
    auto testNodes = std::make_unique<TestNodeCollection>();
    testNodes->addNode( "globaldb_node1" );
    testNodes->addNode( "globaldb_node2" );
    testNodes->addNode( "globaldb_node3" );

    for ( auto &node : testNodes->getNodes() )
    {
        node.db->AddBroadcastTopic( "test_topic" );
        node.db->AddListenTopic( "test_topic" );
    }

    testNodes->connectNodes( std::chrono::seconds( 5 ) );
    using sgns::crdt::HierarchicalKey;
    sgns::base::Buffer value;
    value.put( "Value via test_topic" );
    const HierarchicalKey key( "/topic/test1" );
    const auto            tx = testNodes->getNodes()[0].db->BeginTransaction();
    ASSERT_NE( tx, nullptr );
    const auto putRes = tx->Put( key, value );
    ASSERT_TRUE( putRes.has_value() );
    const auto commitRes = tx->Commit();
    ASSERT_TRUE( commitRes.has_value() );

    const bool replicated = waitForCondition(
        [&]() -> bool
        {
            const auto res2 = testNodes->getNodes()[1].db->Get( key );
            const auto res3 = testNodes->getNodes()[2].db->Get( key );
            return res2.has_value() && res3.has_value();
        },
        WAIT_TIMEOUT );
    EXPECT_TRUE( replicated );
    {
        const auto res2 = testNodes->getNodes()[1].db->Get( key );
        const auto res3 = testNodes->getNodes()[2].db->Get( key );
        EXPECT_TRUE( res2.has_value() );
        EXPECT_TRUE( res3.has_value() );
        EXPECT_EQ( res2.value().toString(), "Value via test_topic" );
        EXPECT_EQ( res3.value().toString(), "Value via test_topic" );
    }
}

TEST_F( GlobalDBIntegrationTest, ReplicationAcrossMultipleTopicsTest )
{
    auto testNodes = std::make_unique<TestNodeCollection>();
    testNodes->addNode( "globaldb_node1" );
    testNodes->addNode( "globaldb_node2" );
    testNodes->addNode( "globaldb_node3" );

    for ( auto &node : testNodes->getNodes() )
    {
        node.db->AddBroadcastTopic( "firstTopic" );
        node.db->AddListenTopic( "firstTopic" );

        node.db->AddBroadcastTopic( "topic_A" );
        node.db->AddListenTopic( "topic_A" );

        node.db->AddBroadcastTopic( "topic_B" );
        node.db->AddListenTopic( "topic_B" );
    }

    testNodes->connectNodes( std::chrono::seconds( 5 ) );
    using sgns::crdt::HierarchicalKey;
    sgns::base::Buffer valueA, valueB;
    valueA.put( "Data from topic A" );
    valueB.put( "Data from topic B" );
    const HierarchicalKey keyA( "/multiple/topicA" );
    const HierarchicalKey keyB( "/multiple/topicB" );

    const auto txA = testNodes->getNodes()[0].db->BeginTransaction();
    ASSERT_NE( txA, nullptr );
    const auto putResA = txA->Put( keyA, valueA );
    ASSERT_TRUE( putResA.has_value() );
    const auto commitResA = txA->Commit();
    ASSERT_TRUE( commitResA.has_value() );

    const auto txB = testNodes->getNodes()[1].db->BeginTransaction();
    ASSERT_NE( txB, nullptr );
    const auto putResB = txB->Put( keyB, valueB );
    ASSERT_TRUE( putResB.has_value() );
    const auto commitResB = txB->Commit();
    ASSERT_TRUE( commitResB.has_value() );

    const bool replicated = waitForCondition(
        [&]() -> bool
        {
            const auto resA = testNodes->getNodes()[2].db->Get( keyA );
            const auto resB = testNodes->getNodes()[2].db->Get( keyB );
            return resA.has_value() && resB.has_value();
        },
        WAIT_TIMEOUT );
    EXPECT_TRUE( replicated );
    {
        const auto resA = testNodes->getNodes()[2].db->Get( keyA );
        const auto resB = testNodes->getNodes()[2].db->Get( keyB );
        EXPECT_TRUE( resA.has_value() );
        EXPECT_TRUE( resB.has_value() );
        EXPECT_EQ( resA.value().toString(), "Data from topic A" );
        EXPECT_EQ( resB.value().toString(), "Data from topic B" );
    }
}

TEST_F( GlobalDBIntegrationTest, PreventDoubleCommitTest )
{
    auto testNodes = std::make_unique<TestNodeCollection>();
    testNodes->addNode( "globaldb_node1" );
    testNodes->getNodes()[0].db->AddBroadcastTopic( "firstTopic" );
    testNodes->connectNodes( std::chrono::seconds( 1 ) );
    using sgns::crdt::HierarchicalKey;
    sgns::base::Buffer value;
    value.put( "Double commit test value" );
    const HierarchicalKey key( "/double/commit" );
    const auto            tx = testNodes->getNodes()[0].db->BeginTransaction();
    ASSERT_NE( tx, nullptr );
    const auto putRes = tx->Put( key, value );
    ASSERT_TRUE( putRes.has_value() );
    const auto commitRes = tx->Commit();
    ASSERT_TRUE( commitRes.has_value() );
    const auto secondCommit = tx->Commit();
    EXPECT_FALSE( secondCommit.has_value() );
}

TEST_F( GlobalDBIntegrationTest, OperationsWithoutInitializationTest )
{
    const std::string binaryPath  = boost::dll::program_location().parent_path().string();
    const std::string tmpBasePath = binaryPath + "/globaldb_no_init_ops";
    boost::filesystem::create_directories( tmpBasePath );

    sgns::crdt::KeyPairFileStorage keyStore( tmpBasePath + "/key" );
    const auto                     keyPair = keyStore.GetKeyPair().value();
    auto                           pubsub  = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( keyPair );
    pubsub->Start( 50800, {}, "127.0.0.1", {} );

    auto io = std::make_shared<boost::asio::io_context>();
    auto db = std::make_shared<sgns::crdt::GlobalDB>( io,
                                                      tmpBasePath + "/CommonKey",
                                                      pubsub );
    ++GlobalDBIntegrationTest::TestNodeCollection::currentGraphsyncPort;

    using sgns::crdt::HierarchicalKey;
    const HierarchicalKey queryKey( "/nonexistent/query" );
    const auto            queryResult = db->QueryKeyValues( queryKey.GetKey() );
    EXPECT_FALSE( queryResult.has_value() );

    const HierarchicalKey getKey( "/nonexistent/get" );
    const auto            getResult = db->Get( getKey );
    EXPECT_FALSE( getResult.has_value() );

    const HierarchicalKey removeKey( "/nonexistent/remove" );
    const auto            removeResult = db->Remove( removeKey );
    EXPECT_FALSE( removeResult.has_value() );

    boost::filesystem::remove_all( tmpBasePath );
}

TEST_F( GlobalDBIntegrationTest, CommitFailsForNonexistentTopicTest )
{
    auto testNodes = std::make_unique<TestNodeCollection>();
    testNodes->addNode( "globaldb_no_topic" );

    using sgns::crdt::HierarchicalKey;
    sgns::base::Buffer value;
    value.put( "Test value without topic" );
    const HierarchicalKey key( "/error/put_without_topic" );

    const auto tx = testNodes->getNodes()[0].db->BeginTransaction();
    ASSERT_NE( tx, nullptr );
    const auto putRes = tx->Put( key, value );
    ASSERT_TRUE( putRes.has_value() );
    const auto commitRes = tx->Commit();
    EXPECT_FALSE( commitRes.has_value() );
}

TEST_F( GlobalDBIntegrationTest, DirectPutWithTopicBroadcastTest )
{
    auto testNodes = std::make_unique<TestNodeCollection>();
    testNodes->addNode( "globaldb_node1" );
    testNodes->addNode( "globaldb_node2" );
    testNodes->addNode( "globaldb_node3" );

    for ( auto &node : testNodes->getNodes() )
    {
        node.db->AddBroadcastTopic( "firstTopic" );
        node.db->AddBroadcastTopic( "direct_topic" );
        node.db->AddListenTopic( "direct_topic" );
    }
    testNodes->connectNodes( std::chrono::seconds( 3 ) );
    using sgns::crdt::HierarchicalKey;
    sgns::base::Buffer value;
    value.put( "Direct put with topic value" );
    const HierarchicalKey key( "/direct/with_topic" );

    const auto putRes = testNodes->getNodes()[0].db->Put( key, value );
    ASSERT_TRUE( putRes.has_value() );

    const bool replicated = waitForCondition(
        [&]() -> bool
        {
            const auto res2 = testNodes->getNodes()[1].db->Get( key );
            const auto res3 = testNodes->getNodes()[2].db->Get( key );
            return res2.has_value() && res3.has_value();
        },
        WAIT_TIMEOUT );
    EXPECT_TRUE( replicated );
    {
        const auto res2 = testNodes->getNodes()[1].db->Get( key );
        const auto res3 = testNodes->getNodes()[2].db->Get( key );
        EXPECT_TRUE( res2.has_value() );
        EXPECT_TRUE( res3.has_value() );
        EXPECT_EQ( res2.value().toString(), "Direct put with topic value" );
        EXPECT_EQ( res3.value().toString(), "Direct put with topic value" );
    }
}

TEST_F( GlobalDBIntegrationTest, DirectPutWithoutTopicBroadcastTest )
{
    auto testNodes = std::make_unique<TestNodeCollection>();
    testNodes->addNode( "globaldb_node1" );
    testNodes->addNode( "globaldb_node2" );
    testNodes->addNode( "globaldb_node3" );

    for ( auto &node : testNodes->getNodes() )
    {
        node.db->AddBroadcastTopic( "firstTopic" );
        node.db->AddListenTopic( "firstTopic" );
    }
    testNodes->connectNodes( std::chrono::seconds( 3 ) );
    using sgns::crdt::HierarchicalKey;
    sgns::base::Buffer value;
    value.put( "Direct put without topic value" );
    const HierarchicalKey key( "/direct/without_topic" );

    const auto putRes = testNodes->getNodes()[0].db->Put( key, value );
    ASSERT_TRUE( putRes.has_value() );

    const bool replicated = waitForCondition(
        [&]() -> bool
        {
            const auto res2 = testNodes->getNodes()[1].db->Get( key );
            const auto res3 = testNodes->getNodes()[2].db->Get( key );
            return res2.has_value() && res3.has_value();
        },
        WAIT_TIMEOUT );
    EXPECT_TRUE( replicated );
    {
        const auto res2 = testNodes->getNodes()[1].db->Get( key );
        const auto res3 = testNodes->getNodes()[2].db->Get( key );
        ASSERT_TRUE( res2.has_value() );
        ASSERT_TRUE( res3.has_value() );
        EXPECT_EQ( res2.value().toString(), "Direct put without topic value" );
        EXPECT_EQ( res3.value().toString(), "Direct put without topic value" );
    }
}

TEST_F( GlobalDBIntegrationTest, NonSubscriberDoesNotReceiveTopicMessageTest )
{
    auto testNodes = std::make_unique<TestNodeCollection>();
    testNodes->addNode( "globaldb_node1" );
    testNodes->addNode( "globaldb_node2" );
    testNodes->addNode( "globaldb_node3" );

    for ( auto &node : testNodes->getNodes() )
    {
        node.db->AddBroadcastTopic( "first_topic" );
    }
    testNodes->getNodes()[0].db->AddBroadcastTopic( "test_topic" );
    testNodes->getNodes()[0].db->AddListenTopic( "test_topic" );
    testNodes->getNodes()[1].db->AddBroadcastTopic( "test_topic" );
    testNodes->getNodes()[1].db->AddListenTopic( "test_topic" );
    testNodes->connectNodes( std::chrono::seconds( 3 ) );
    using sgns::crdt::HierarchicalKey;
    sgns::base::Buffer value;
    value.put( "Message for test_topic" );
    const HierarchicalKey key( "/nonsubscriber/test" );
    const auto            tx = testNodes->getNodes()[0].db->BeginTransaction();
    ASSERT_NE( tx, nullptr );
    const auto putRes = tx->Put( key, value );
    ASSERT_TRUE( putRes.has_value() );
    const auto commitRes = tx->Commit();
    ASSERT_TRUE( commitRes.has_value() );

    bool node0Received = waitForCondition( [&]() -> bool
                                           { return testNodes->getNodes()[0].db->Get( key ).has_value(); },
                                           WAIT_TIMEOUT );
    EXPECT_TRUE( node0Received );

    bool node1Received = waitForCondition( [&]() -> bool
                                           { return testNodes->getNodes()[1].db->Get( key ).has_value(); },
                                           WAIT_TIMEOUT );
    EXPECT_TRUE( node1Received );

    bool node2Received = waitForCondition( [&]() -> bool
                                           { return testNodes->getNodes()[2].db->Get( key ).has_value(); },
                                           WAIT_TIMEOUT );
    EXPECT_FALSE( node2Received );
}

TEST_F( GlobalDBIntegrationTest, UnconnectedNodeDoesNotReplicateBroadcastMessageTest )
{
    auto testNodes = std::make_unique<TestNodeCollection>();
    testNodes->addNode( "globaldb_node1" );
    testNodes->addNode( "globaldb_node2" );
    testNodes->connectNodes( std::chrono::seconds( 3 ) );

    testNodes->addNode( "globaldb_node3" );

    for ( auto &node : testNodes->getNodes() )
    {
        node.db->AddBroadcastTopic( "isolated_topic" );
        node.db->AddListenTopic( "isolated_topic" );
    }

    using sgns::crdt::HierarchicalKey;
    sgns::base::Buffer value;
    value.put( "Test message for isolated node" );
    const HierarchicalKey key( "/isolated/test" );
    const auto            tx = testNodes->getNodes()[0].db->BeginTransaction();
    ASSERT_NE( tx, nullptr );
    const auto putRes = tx->Put( key, value );
    ASSERT_TRUE( putRes.has_value() );
    const auto commitRes = tx->Commit();
    ASSERT_TRUE( commitRes.has_value() );

    bool node1Replicated = waitForCondition( [&]() -> bool
                                             { return testNodes->getNodes()[0].db->Get( key ).has_value(); },
                                             WAIT_TIMEOUT );
    EXPECT_TRUE( node1Replicated );

    bool node2Replicated = waitForCondition( [&]() -> bool
                                             { return testNodes->getNodes()[1].db->Get( key ).has_value(); },
                                             WAIT_TIMEOUT );
    EXPECT_TRUE( node2Replicated );

    bool node3Replicated = waitForCondition( [&]() -> bool
                                             { return testNodes->getNodes()[2].db->Get( key ).has_value(); },
                                             WAIT_TIMEOUT );
    EXPECT_FALSE( node3Replicated );
}
