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
#include <libp2p/basic/scheduler.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
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
#include "testutil/wait_condition.hpp"
#include "testutil/remove_all.hpp"

#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/local_requests.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>

namespace
{
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

        void addNode( const std::string &dbName )
        {
            const std::string testName   = ::testing::UnitTest::GetInstance()->current_test_info()->name();
            const std::string binaryPath = boost::dll::program_location().parent_path().string();
            const std::string basePath   = binaryPath + "/" + dbName + "_" + testName;
            sgns::test::removeAllWithRetry( basePath );
            boost::filesystem::create_directories( basePath );

            sgns::crdt::KeyPairFileStorage keyStore( basePath + "/key" );
            auto                           keyPair  = keyStore.GetKeyPair().value();
            auto                           pubsub   = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( keyPair );
            const std::string              listenIp = "0.0.0.0";
            const auto startError = pubsub->Start( 0, {}, listenIp, {} ).get();
            ASSERT_FALSE( startError ) << "Could not start GlobalDB test node: " << startError.message();

            auto io        = std::make_shared<boost::asio::io_context>();
            auto scheduler = std::make_shared<libp2p::basic::SchedulerImpl>(
                std::make_shared<libp2p::basic::AsioSchedulerBackend>( io ),
                libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
            auto graphsyncnetwork = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubsub->GetHost(),
                                                                                                 scheduler );
            auto generator        = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();

            auto globaldb_ret = sgns::crdt::GlobalDB::New( io,
                                                           basePath + "/CommonKey",
                                                           pubsub,
                                                           sgns::crdt::CrdtOptions::DefaultOptions(),
                                                           graphsyncnetwork,
                                                           scheduler,
                                                           generator );
            if ( globaldb_ret.has_error() )
            {
                return;
            }
            auto db = std::move( globaldb_ret.value() );

            db->Start();
            std::thread t( [io]() { io->run(); } );
            TestNode    node{ basePath, io, pubsub, db, std::move( t ) };
            nodes_.push_back( std::move( node ) );
        }

        void connectNodes()
        {
            for ( size_t i = 0; i < nodes_.size(); ++i )
            {
                for ( size_t j = i + 1; j < nodes_.size(); ++j )
                {
                    std::cout << "Connecting to: " << nodes_[j].pubsub->GetInterfaceAddress() << std::endl;
                    nodes_[i].pubsub->AddPeers( { nodes_[j].pubsub->GetInterfaceAddress() } );
                }
            }

            const auto expectedPeerCount = nodes_.empty() ? 0 : nodes_.size() - 1;
            sgns::test::assertWaitForCondition(
                [&]()
                {
                    for ( const auto &node : nodes_ )
                    {
                        if ( node.pubsub->GetHost()->getNetwork().getConnectionManager().getConnections().size() <
                             expectedPeerCount )
                        {
                            return false;
                        }
                    }
                    return true;
                },
                WAIT_TIMEOUT,
                "GlobalDB test nodes did not connect" );
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
                if ( node.db )
                {
                    node.db->ShutdownNow();
                }
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
        const auto loggerGlobalDB    = sgns::base::createLogger( "GlobalDB" );
        const auto loggerBroadcaster = sgns::base::createLogger( "PubSubBroadcasterExt" );
        const auto loggerDataStore   = sgns::base::createLogger( "CrdtDatastore" );
        loggerGlobalDB->set_level( spdlog::level::trace );
        loggerBroadcaster->set_level( spdlog::level::trace );
        loggerDataStore->set_level( spdlog::level::off );
    }
};

TEST_F( GlobalDBIntegrationTest, OperationsAreRejectedAfterShutdown )
{
    auto testNodes = std::make_unique<TestNodeCollection>();
    testNodes->addNode( "globaldb_shutdown_node" );
    ASSERT_EQ( testNodes->getNodes().size(), 1 );

    auto db = testNodes->getNodes().front().db;
    ASSERT_NE( db, nullptr );
    db->ShutdownNow();

    sgns::base::Buffer value;
    value.put( "value" );
    const sgns::crdt::HierarchicalKey key( "/shutdown/rejected" );

    EXPECT_TRUE( db->Put( key, value, {} ).has_error() );
    EXPECT_TRUE( db->Get( key ).has_error() );
    EXPECT_TRUE( db->QueryKeyValues( "/shutdown" ).has_error() );
    EXPECT_EQ( db->BeginTransaction(), nullptr );
    EXPECT_EQ( db->GetCRDTDataStore(), nullptr );
    EXPECT_EQ( db->GetBroadcaster(), nullptr );
}

TEST_F( GlobalDBIntegrationTest, ReplicationWithoutTopicSuccessfulTest )
{
    auto testNodes = std::make_unique<TestNodeCollection>();
    testNodes->addNode( "globaldb_node1" );
    testNodes->addNode( "globaldb_node2" );
    testNodes->addNode( "globaldb_node3" );

    for ( auto &node : testNodes->getNodes() )
    {
        ASSERT_FALSE( node.db->AddBroadcastTopic( "firstTopic" ).has_error() );
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
    const auto commitRes = tx->Commit( { "test" } );
    ASSERT_TRUE( commitRes.has_value() );

    sgns::test::assertWaitForCondition(
        [&]() -> bool
        {
            const auto res2 = testNodes->getNodes()[1].db->Get( key );
            const auto res3 = testNodes->getNodes()[2].db->Get( key );
            return res2.has_value() && res3.has_value();
        },
        WAIT_TIMEOUT,
        "Value was not replicated to both peers" );
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
        ASSERT_FALSE( node.db->AddBroadcastTopic( "test_topic" ).has_error() );
        node.db->AddListenTopic( "test_topic" );
    }

    testNodes->connectNodes();
    using sgns::crdt::HierarchicalKey;
    sgns::base::Buffer value;
    value.put( "Value via test_topic" );
    const HierarchicalKey key( "/topic/test1" );
    const auto            tx = testNodes->getNodes()[0].db->BeginTransaction();
    ASSERT_NE( tx, nullptr );
    const auto putRes = tx->Put( key, value );
    ASSERT_TRUE( putRes.has_value() );
    const auto commitRes = tx->Commit( { "test" } );
    ASSERT_TRUE( commitRes.has_value() );

    sgns::test::assertWaitForCondition(
        [&]() -> bool
        {
            const auto res2 = testNodes->getNodes()[1].db->Get( key );
            const auto res3 = testNodes->getNodes()[2].db->Get( key );
            return res2.has_value() && res3.has_value();
        },
        WAIT_TIMEOUT,
        "Topic value was not replicated to both peers" );
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
        ASSERT_FALSE( node.db->AddBroadcastTopic( "firstTopic" ).has_error() );
        node.db->AddListenTopic( "firstTopic" );

        ASSERT_FALSE( node.db->AddBroadcastTopic( "topic_A" ).has_error() );
        node.db->AddListenTopic( "topic_A" );

        ASSERT_FALSE( node.db->AddBroadcastTopic( "topic_B" ).has_error() );
        node.db->AddListenTopic( "topic_B" );
    }

    testNodes->connectNodes();
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
    const auto commitResA = txA->Commit( { "test" } );
    ASSERT_TRUE( commitResA.has_value() );

    const auto txB = testNodes->getNodes()[1].db->BeginTransaction();
    ASSERT_NE( txB, nullptr );
    const auto putResB = txB->Put( keyB, valueB );
    ASSERT_TRUE( putResB.has_value() );
    const auto commitResB = txB->Commit( { "test" } );
    ASSERT_TRUE( commitResB.has_value() );

    sgns::test::assertWaitForCondition(
        [&]() -> bool
        {
            const auto resA = testNodes->getNodes()[2].db->Get( keyA );
            const auto resB = testNodes->getNodes()[2].db->Get( keyB );
            return resA.has_value() && resB.has_value();
        },
        WAIT_TIMEOUT,
        "Values from both topics were not replicated" );
    {
        const auto resA = testNodes->getNodes()[2].db->Get( keyA );
        const auto resB = testNodes->getNodes()[2].db->Get( keyB );
        ASSERT_TRUE( resA.has_value() );
        ASSERT_TRUE( resB.has_value() );
        EXPECT_EQ( resA.value().toString(), "Data from topic A" );
        EXPECT_EQ( resB.value().toString(), "Data from topic B" );
    }
}

TEST_F( GlobalDBIntegrationTest, PreventDoubleCommitTest )
{
    auto testNodes = std::make_unique<TestNodeCollection>();
    testNodes->addNode( "globaldb_node1" );
    ASSERT_FALSE( testNodes->getNodes()[0].db->AddBroadcastTopic( "firstTopic" ).has_error() );
    testNodes->connectNodes();
    using sgns::crdt::HierarchicalKey;
    sgns::base::Buffer value;
    value.put( "Double commit test value" );
    const HierarchicalKey key( "/double/commit" );
    const auto            tx = testNodes->getNodes()[0].db->BeginTransaction();
    ASSERT_NE( tx, nullptr );
    const auto putRes = tx->Put( key, value );
    ASSERT_TRUE( putRes.has_value() );
    const auto commitRes = tx->Commit( { "test" } );
    ASSERT_TRUE( commitRes.has_value() );
    const auto secondCommit = tx->Commit( { "test" } );
    EXPECT_FALSE( secondCommit.has_value() );
}

TEST_F( GlobalDBIntegrationTest, DISABLED_CommitFailsForNonexistentTopicTest )
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
    const auto commitRes = tx->Commit( { "test" } );
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
        ASSERT_FALSE( node.db->AddBroadcastTopic( "firstTopic" ).has_error() );
        ASSERT_FALSE( node.db->AddBroadcastTopic( "direct_topic" ).has_error() );
        node.db->AddListenTopic( "direct_topic" );
    }
    testNodes->connectNodes();
    using sgns::crdt::HierarchicalKey;
    sgns::base::Buffer value;
    value.put( "Direct put with topic value" );
    const HierarchicalKey key( "/direct/with_topic" );

    const auto putRes = testNodes->getNodes()[0].db->Put( key, value, { "topic" } );
    ASSERT_TRUE( putRes.has_value() );

    sgns::test::assertWaitForCondition(
        [&]() -> bool
        {
            const auto res2 = testNodes->getNodes()[1].db->Get( key );
            const auto res3 = testNodes->getNodes()[2].db->Get( key );
            return res2.has_value() && res3.has_value();
        },
        WAIT_TIMEOUT,
        "Direct value was not replicated to both peers" );
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
        ASSERT_FALSE( node.db->AddBroadcastTopic( "firstTopic" ).has_error() );
        node.db->AddListenTopic( "firstTopic" );
    }
    testNodes->connectNodes();
    using sgns::crdt::HierarchicalKey;
    sgns::base::Buffer value;
    value.put( "Direct put without topic value" );
    const HierarchicalKey key( "/direct/without_topic" );

    const auto putRes = testNodes->getNodes()[0].db->Put( key, value, { "topic" } );
    ASSERT_TRUE( putRes.has_value() );

    sgns::test::assertWaitForCondition(
        [&]() -> bool
        {
            const auto res2 = testNodes->getNodes()[1].db->Get( key );
            const auto res3 = testNodes->getNodes()[2].db->Get( key );
            return res2.has_value() && res3.has_value();
        },
        WAIT_TIMEOUT,
        "Direct value was not replicated to both peers" );
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
        ASSERT_FALSE( node.db->AddBroadcastTopic( "first_topic" ).has_error() );
    }
    ASSERT_FALSE( testNodes->getNodes()[0].db->AddBroadcastTopic( "test_topic" ).has_error() );
    testNodes->getNodes()[0].db->AddListenTopic( "test_topic" );
    ASSERT_FALSE( testNodes->getNodes()[1].db->AddBroadcastTopic( "test_topic" ).has_error() );
    testNodes->getNodes()[1].db->AddListenTopic( "test_topic" );
    testNodes->connectNodes();
    using sgns::crdt::HierarchicalKey;
    sgns::base::Buffer value;
    value.put( "Message for test_topic" );
    const HierarchicalKey key( "/nonsubscriber/test" );
    const auto            tx = testNodes->getNodes()[0].db->BeginTransaction();
    ASSERT_NE( tx, nullptr );
    const auto putRes = tx->Put( key, value );
    ASSERT_TRUE( putRes.has_value() );
    const auto commitRes = tx->Commit( { "test" } );
    ASSERT_TRUE( commitRes.has_value() );

    sgns::test::assertWaitForCondition( [&]() -> bool { return testNodes->getNodes()[0].db->Get( key ).has_value(); },
                                        WAIT_TIMEOUT,
                                        "Publishing node did not retain its value" );

    sgns::test::assertWaitForCondition( [&]() -> bool { return testNodes->getNodes()[1].db->Get( key ).has_value(); },
                                        WAIT_TIMEOUT,
                                        "Subscribed peer did not receive the value" );

    const bool node2Received = ::waitForCondition( [&]() -> bool
                                                   { return testNodes->getNodes()[2].db->Get( key ).has_value(); },
                                                   WAIT_TIMEOUT,
                                                   nullptr,
                                                   std::chrono::milliseconds( 100 ) );
    EXPECT_FALSE( node2Received );
}

TEST_F( GlobalDBIntegrationTest, UnconnectedNodeDoesNotReplicateBroadcastMessageTest )
{
    auto testNodes = std::make_unique<TestNodeCollection>();
    testNodes->addNode( "globaldb_node1" );
    testNodes->addNode( "globaldb_node2" );
    testNodes->connectNodes();

    testNodes->addNode( "globaldb_node3" );

    for ( auto &node : testNodes->getNodes() )
    {
        ASSERT_FALSE( node.db->AddBroadcastTopic( "isolated_topic" ).has_error() );
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
    const auto commitRes = tx->Commit( { "test" } );
    ASSERT_TRUE( commitRes.has_value() );

    sgns::test::assertWaitForCondition( [&]() -> bool { return testNodes->getNodes()[0].db->Get( key ).has_value(); },
                                        WAIT_TIMEOUT,
                                        "Publishing node did not retain its value" );

    sgns::test::assertWaitForCondition( [&]() -> bool { return testNodes->getNodes()[1].db->Get( key ).has_value(); },
                                        WAIT_TIMEOUT,
                                        "Connected peer did not receive the value" );

    const bool node3Replicated = ::waitForCondition( [&]() -> bool
                                                     { return testNodes->getNodes()[2].db->Get( key ).has_value(); },
                                                     WAIT_TIMEOUT,
                                                     nullptr,
                                                     std::chrono::milliseconds( 100 ) );
    EXPECT_FALSE( node3Replicated );
}
