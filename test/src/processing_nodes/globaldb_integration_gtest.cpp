/**
 * @file       globaldb_integration_gtest.cpp
 * @brief      Integration tests for GlobalDB.
 *
 * Three GlobalDB instances are set up without initial broadcast topics.
 * Topics are added dynamically and then used during transaction commit.
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

std::string GetLoggingSystem( const std::string &base_path )
{
    std::string config = R"(
 sinks:
   - name: file
     type: file
     capacity: 1000
     path: [basepath]/sgnslog.log
 groups:
   - name: SuperGeniusDemo
     sink: file
     level: debug
     children:
       - name: libp2p
       - name: Gossip
 )";
    boost::algorithm::replace_all( config, "[basepath]", base_path );
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

    static TestNode CreateTestNode( const std::string &basePath, uint16_t pubsubPort, uint16_t graphsyncPort )
    {
        boost::filesystem::create_directories( basePath );
        sgns::crdt::KeyPairFileStorage keyStore( basePath + "/key" );
        auto                           keyPair  = keyStore.GetKeyPair().value();
        auto                           pubsub   = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( keyPair );
        std::string                    listenIp = "127.0.0.1";
        pubsub->Start( pubsubPort, {}, listenIp, {} );
        auto io      = std::make_shared<boost::asio::io_context>();
        auto db      = std::make_shared<sgns::crdt::GlobalDB>( io, basePath + "/CommonKey", graphsyncPort, pubsub );
        auto initRes = db->Init( sgns::crdt::CrdtOptions::DefaultOptions() );
        if ( !initRes.has_value() )
        {
            return TestNode{};
        }
        db->AddBroadcastTopic( "firstTopic" );
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

        uint16_t    pubsubPorts[3]    = { 50501, 50502, 50503 };
        uint16_t    graphsyncPorts[3] = { 50511, 50512, 50513 };
        std::string basePaths[3]      = { binaryPath + "/globaldb_node1",
                                          binaryPath + "/globaldb_node2",
                                          binaryPath + "/globaldb_node3" };

        for ( int i = 0; i < 3; i++ )
        {
            TestNode node = CreateTestNode( basePaths[i], pubsubPorts[i], graphsyncPorts[i] );
            context.nodes.push_back( std::move( node ) );
        }

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

// Attempt to commit the same transaction twice.
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
    // The second commit attempt should fail.
    auto secondCommit = tx->Commit( "firstTopic" );
    EXPECT_FALSE( secondCommit.has_value() );
}

// Create a GlobalDB instance without adding any broadcast topic and attempt a Put with a non-existent topic.
TEST_F( GlobalDBIntegrationTest, DISABLED_PutWithoutTopicTest )
{
    std::string binaryPath  = boost::dll::program_location().parent_path().string();
    std::string tmpBasePath = binaryPath + "/globaldb_no_topic";
    boost::filesystem::create_directories( tmpBasePath );
    {

    sgns::crdt::KeyPairFileStorage keyStore( tmpBasePath + "/key" );
    auto                           keyPair = keyStore.GetKeyPair().value();
    auto                           pubsub  = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( keyPair );
    pubsub->Start( 50510, {}, "127.0.0.1", {} );

    auto io = std::make_shared<boost::asio::io_context>();
    auto db      = std::make_shared<sgns::crdt::GlobalDB>( io, tmpBasePath + "/CommonKey", 50520, pubsub );
    auto initRes = db->Init( sgns::crdt::CrdtOptions::DefaultOptions() );
    ASSERT_TRUE( initRes.has_value() );

    std::thread t( [io]() { io->run(); } );

    sgns::crdt::HierarchicalKey key( "/error/test" );
    sgns::base::Buffer          value;
    value.put( "Test value without topic" );

    auto tx = db->BeginTransaction();
    ASSERT_NE( tx, nullptr );
    auto putRes = tx->Put( key, value );
    ASSERT_TRUE( putRes.has_value() );
    auto commitRes = tx->Commit( "nonexistent_topic" );
    EXPECT_FALSE( commitRes.has_value() );

    // io->stop();
    // t.join();
        
    }
        std::this_thread::sleep_for( std::chrono::seconds( 5 ) );
    // boost::filesystem::remove_all( tmpBasePath );
    
}
