/**
 * @file       globaldb_integration_gtest.cpp
 * @brief      Integration tests for GlobalDB.
 *
 * This file sets up three GlobalDB instances, uses dynamic broadcast topics,
 * and verifies replication and transaction behavior.
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

// Generate a random port using a seed.
uint16_t GenerateRandomPort( uint16_t base, const std::string &seed )
{
    uint32_t seed_hash = 0;
    for ( char c : seed )
    {
        seed_hash = seed_hash * 31 + static_cast<uint8_t>( c );
    }
    std::mt19937                            rng( seed_hash );
    std::uniform_int_distribution<uint16_t> dist( 0, 300 );
    return base + dist( rng );
}

// Return a YAML string used to configure logging.
// This configuration is similar to your attached configuration.
std::string GetLoggingSystem( const std::string & /*unused*/ )
{
    std::string config = R"(
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
    return config;
}

struct TestNode
{
    std::string                                      basePath;
    std::shared_ptr<boost::asio::io_context>         io;
    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub;
    std::shared_ptr<sgns::crdt::GlobalDB>            db;
    std::thread                                      ioThread;
};

struct GlobalContext
{
    std::vector<TestNode> nodes;
    std::string           commonTopic;
};

class GlobalDBIntegrationTest : public ::testing::Test
{
protected:
    static GlobalContext context;

    // Standard function to create a GlobalDB instance using the provided dbName.
    // PubSub and Graphsync ports are assigned via static counters and incremented with each call.
    static TestNode CreateTestNode( const std::string &dbName )
    {
        std::string binaryPath = boost::dll::program_location().parent_path().string();
        std::string basePath   = binaryPath + "/" + dbName;
        boost::filesystem::create_directories( basePath );

        // Create key store and generate the key pair.
        sgns::crdt::KeyPairFileStorage keyStore( basePath + "/key" );
        auto                           keyPair = keyStore.GetKeyPair().value();
        auto                           pubsub  = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( keyPair );

        // Use static counters to allocate ports.
        static uint16_t currentPubsubPort    = 50501;
        static uint16_t currentGraphsyncPort = 50511;
        std::string     listenIp             = "127.0.0.1";
        pubsub->Start( currentPubsubPort, {}, listenIp, {} );

        auto io = std::make_shared<boost::asio::io_context>();
        auto db = std::make_shared<sgns::crdt::GlobalDB>( io, basePath + "/CommonKey", currentGraphsyncPort, pubsub );

        // Increment ports for the next instance.
        currentPubsubPort++;
        currentGraphsyncPort++;

        auto initRes = db->Init( sgns::crdt::CrdtOptions::DefaultOptions() );
        if ( !initRes.has_value() )
        {
            return TestNode{};
        }
        std::thread t( [io]() { io->run(); } );
        return TestNode{ basePath, io, pubsub, db, std::move( t ) };
    }

    static void SetUpTestSuite()
    {
        std::string binaryPath         = boost::dll::program_location().parent_path().string();
        context.commonTopic            = "globaldb_test_topic";
        std::string loggingYAML        = GetLoggingSystem( binaryPath + "/globaldbtest" );
        auto        loggerConfigurator = std::make_shared<libp2p::log::Configurator>();
        auto        configFromYAML = std::make_shared<soralog::ConfiguratorFromYAML>( loggerConfigurator, loggingYAML );
        auto        loggingSystem  = std::make_shared<soralog::LoggingSystem>( configFromYAML );
        auto        confResult     = loggingSystem->configure();
        if ( confResult.has_error )
        {
            throw std::runtime_error( "Could not configure logger" );
        }
        libp2p::log::setLoggingSystem( loggingSystem );
        auto nodeLogger = sgns::base::createLogger( "SuperGeniusDemo", binaryPath + "/sgnslog2.log" );
        nodeLogger->set_level( spdlog::level::err );
        auto loggerGlobalDB    = sgns::base::createLogger( "GlobalDB" );
        auto loggerBroadcaster = sgns::base::createLogger( "PubSubBroadcasterExt" );
        auto loggerDataStore   = sgns::base::createLogger( "CrdtDatastore" );
        loggerGlobalDB->set_level( spdlog::level::debug );
        loggerBroadcaster->set_level( spdlog::level::debug );
        loggerDataStore->set_level( spdlog::level::debug );

        // Create three GlobalDB instances using only the db name.
        context.nodes.push_back( CreateTestNode( "globaldb_node1" ) );
        context.nodes.push_back( CreateTestNode( "globaldb_node2" ) );
        context.nodes.push_back( CreateTestNode( "globaldb_node3" ) );

        for ( auto &node : context.nodes )
        {
            node.db->AddBroadcastTopic( "firstTopic" );
        }

        // Connect the nodes by adding peers.
        for ( size_t i = 0; i < context.nodes.size(); i++ )
        {
            std::vector<std::string> peers;
            for ( size_t j = 0; j < context.nodes.size(); j++ )
            {
                if ( i != j )
                {
                    peers.push_back( context.nodes[j].pubsub->GetLocalAddress() );
                }
            }
            context.nodes[i].pubsub->AddPeers( peers );
        }
        std::this_thread::sleep_for( std::chrono::seconds( 3 ) );
    }

    static void TearDownTestSuite()
    {
        for ( auto &node : context.nodes )
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
        context.nodes.clear();
    }

    void SetUp() override {}

    void TearDown() override {}
};

GlobalContext GlobalDBIntegrationTest::context;

// Test basic replication without a broadcast of first topic.
TEST_F( GlobalDBIntegrationTest, BasicReplicationTest )
{
    using sgns::crdt::HierarchicalKey;
    sgns::base::Buffer value;
    value.put( "Replication Value without topic" );
    HierarchicalKey key( "/replication/basic_test" );
    auto            tx = context.nodes[0].db->BeginTransaction();
    ASSERT_NE( tx, nullptr );
    auto putRes = tx->Put( key, value );
    ASSERT_TRUE( putRes.has_value() );
    auto commitRes = tx->Commit();
    ASSERT_TRUE( commitRes.has_value() );
    std::this_thread::sleep_for( std::chrono::seconds( 5 ) );
    auto res_node2 = context.nodes[1].db->Get( key );
    auto res_node3 = context.nodes[2].db->Get( key );
    EXPECT_TRUE( res_node2.has_value() );
    EXPECT_TRUE( res_node3.has_value() );
    if ( res_node2.has_value() && res_node3.has_value() )
    {
        EXPECT_EQ( res_node2.value().toString(), "Replication Value without topic" );
        EXPECT_EQ( res_node3.value().toString(), "Replication Value without topic" );
    }
}

// Test replication using a specific broadcast topic.
TEST_F( GlobalDBIntegrationTest, TopicBroadcastReplicationTest )
{
    using sgns::crdt::HierarchicalKey;
    for ( auto &node : context.nodes )
    {
        node.db->AddBroadcastTopic( "test_topic" );
    }
    sgns::base::Buffer value;
    value.put( "Value via test_topic" );
    HierarchicalKey key( "/topic/test1" );
    auto            tx = context.nodes[0].db->BeginTransaction();
    ASSERT_NE( tx, nullptr );
    auto putRes = tx->Put( key, value );
    ASSERT_TRUE( putRes.has_value() );
    auto commitRes = tx->Commit( "test_topic" );
    ASSERT_TRUE( commitRes.has_value() );
    std::this_thread::sleep_for( std::chrono::seconds( 5 ) );
    auto res_node2 = context.nodes[1].db->Get( key );
    auto res_node3 = context.nodes[2].db->Get( key );
    EXPECT_TRUE( res_node2.has_value() );
    EXPECT_TRUE( res_node3.has_value() );
    if ( res_node2.has_value() && res_node3.has_value() )
    {
        EXPECT_EQ( res_node2.value().toString(), "Value via test_topic" );
        EXPECT_EQ( res_node3.value().toString(), "Value via test_topic" );
    }
}

// Test replication with two different broadcast topics.
TEST_F( GlobalDBIntegrationTest, MultipleTopicsTest )
{
    using sgns::crdt::HierarchicalKey;
    for ( auto &node : context.nodes )
    {
        node.db->AddBroadcastTopic( "topic_A" );
        node.db->AddBroadcastTopic( "topic_B" );
    }
    sgns::base::Buffer valueA, valueB;
    valueA.put( "Data from topic A" );
    valueB.put( "Data from topic B" );
    HierarchicalKey keyA( "/multiple/topicA" );
    HierarchicalKey keyB( "/multiple/topicB" );

    auto txA = context.nodes[0].db->BeginTransaction();
    ASSERT_NE( txA, nullptr );
    auto putResA = txA->Put( keyA, valueA );
    ASSERT_TRUE( putResA.has_value() );
    auto commitResA = txA->Commit( "topic_A" );
    ASSERT_TRUE( commitResA.has_value() );

    auto txB = context.nodes[1].db->BeginTransaction();
    ASSERT_NE( txB, nullptr );
    auto putResB = txB->Put( keyB, valueB );
    ASSERT_TRUE( putResB.has_value() );
    auto commitResB = txB->Commit( "topic_B" );
    ASSERT_TRUE( commitResB.has_value() );

    std::this_thread::sleep_for( std::chrono::seconds( 5 ) );
    auto resA_node2 = context.nodes[2].db->Get( keyA );
    auto resB_node2 = context.nodes[2].db->Get( keyB );
    EXPECT_TRUE( resA_node2.has_value() );
    EXPECT_TRUE( resB_node2.has_value() );
    if ( resA_node2.has_value() )
    {
        EXPECT_EQ( resA_node2.value().toString(), "Data from topic A" );
    }
    if ( resB_node2.has_value() )
    {
        EXPECT_EQ( resB_node2.value().toString(), "Data from topic B" );
    }
}

// Test that committing a transaction twice fails.
TEST_F( GlobalDBIntegrationTest, DoubleCommitTest )
{
    using sgns::crdt::HierarchicalKey;
    sgns::base::Buffer value;
    value.put( "Double commit test value" );
    HierarchicalKey key( "/double/commit" );

    auto tx = context.nodes[0].db->BeginTransaction();
    ASSERT_NE( tx, nullptr );
    auto putRes = tx->Put( key, value );
    ASSERT_TRUE( putRes.has_value() );
    auto commitRes = tx->Commit( "firstTopic" );
    ASSERT_TRUE( commitRes.has_value() );
    auto secondCommit = tx->Commit( "firstTopic" );
    EXPECT_FALSE( secondCommit.has_value() );
}

// Disabled test: Attempt a Put with a non-existent topic.
TEST_F( GlobalDBIntegrationTest, DISABLED_PutWithoutTopicTest )
{
    // Create a GlobalDB instance without adding any default topics.
    TestNode node = CreateTestNode( "globaldb_no_topic" );
    {
        sgns::crdt::HierarchicalKey key( "/error/test" );
        sgns::base::Buffer          value;
        value.put( "Test value without topic" );

        auto tx = node.db->BeginTransaction();
        ASSERT_NE( tx, nullptr );
        auto putRes = tx->Put( key, value );
        ASSERT_TRUE( putRes.has_value() );
        auto commitRes = tx->Commit( "nonexistent_topic" );
        EXPECT_FALSE( commitRes.has_value() );
    }
    std::this_thread::sleep_for( std::chrono::seconds( 5 ) );
}

// Test operations without initializing GlobalDB.
TEST_F( GlobalDBIntegrationTest, OperationsWithoutInitTest )
{
    std::string binaryPath  = boost::dll::program_location().parent_path().string();
    std::string tmpBasePath = binaryPath + "/globaldb_no_init_ops";
    boost::filesystem::create_directories( tmpBasePath );

    sgns::crdt::KeyPairFileStorage keyStore( tmpBasePath + "/key" );
    auto                           keyPair = keyStore.GetKeyPair().value();
    auto                           pubsub  = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( keyPair );
    pubsub->Start( 50800, {}, "127.0.0.1", {} );

    auto io = std::make_shared<boost::asio::io_context>();
    // Create GlobalDB without calling Init().
    auto db = std::make_shared<sgns::crdt::GlobalDB>( io, tmpBasePath + "/CommonKey", 50810, pubsub );

    sgns::crdt::HierarchicalKey queryKey( "/nonexistent/query" );
    auto                        queryResult = db->QueryKeyValues( queryKey.GetKey() );
    EXPECT_FALSE( queryResult.has_value() );

    sgns::crdt::HierarchicalKey getKey( "/nonexistent/get" );
    auto                        getResult = db->Get( getKey );
    EXPECT_FALSE( getResult.has_value() );

    sgns::crdt::HierarchicalKey removeKey( "/nonexistent/remove" );
    auto                        removeResult = db->Remove( removeKey );
    EXPECT_FALSE( removeResult.has_value() );

    boost::filesystem::remove_all( tmpBasePath );
}

// Test Put operation when no extra topics are added.
TEST_F( GlobalDBIntegrationTest, PutWithoutTopicTest )
{
    // Create a GlobalDB instance without any default topics.
    TestNode node = CreateTestNode( "globaldb_no_topic" );
    {
        sgns::crdt::HierarchicalKey key( "/error/put_without_topic" );
        sgns::base::Buffer          value;
        value.put( "Test value without topic" );

        auto tx = node.db->BeginTransaction();
        ASSERT_NE( tx, nullptr );
        auto putRes = tx->Put( key, value );
        ASSERT_TRUE( putRes.has_value() );
        auto commitRes = tx->Commit( "nonexistent_topic" );
        EXPECT_FALSE( commitRes.has_value() );
    }
    node.io->stop();
    if ( node.ioThread.joinable() )
    {
        node.ioThread.join();
    }
    boost::filesystem::remove_all( node.basePath );
}
