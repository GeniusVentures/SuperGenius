// globaldb_integration_gtest.cpp
//
// This file contains integration tests for GlobalDB.
// It mimics the initialization style of GeniusNode for setting up
// the logging system, configuration paths, pubsub, and GlobalDB.
// Note: Each test adds its specific broadcast topic. A new test
// demonstrates using more than one topic for publication.

#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <boost/dll.hpp>
#include <boost/format.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <chrono>
#include <thread>
#include <cstring>
#include <vector>
#include <iostream>
#include <fstream>
#include <openssl/sha.h>
#include <random>

// GeniusNode-related and GlobalDB headers.
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/hierarchical_key.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "base/buffer.hpp"
#include "base/logger.hpp"
#include "base/sgns_version.hpp"

// Logging system headers.
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

// --- Dummy Constants ---
#define GNUS_NETWORK_PATH "SuperGNUSNode.TestNet.2a.%02d.%s"
#define PROCESSING_CHANNEL "SGNUS.TestNet.Channel.2a.%02d"

// --- Helper Function: Generate Random Port ---
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

// --- Helper Function: Get Logging YAML Configuration ---
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

// --- Global Context Structure ---
struct GlobalContext
{
    std::string                                      baseWritePath;   // Equivalent to DEV_CONFIG.BaseWritePath
    std::string                                      networkFullPath; // Computed network full path
    std::shared_ptr<boost::asio::io_context>         io;
    std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub;
    std::shared_ptr<sgns::crdt::GlobalDB>            globaldb;
    std::thread                                      ioThread;
};

class GlobalDBIntegrationTest : public ::testing::Test
{
protected:
    // Consolidate shared variables into one context structure.
    static GlobalContext context;

    // SetUpTestSuite: Initialize GlobalDB and related components (mimicking GeniusNode).
    static void SetUpTestSuite()
    {
        // Determine binary path and set base write path using "node1" folder.
        auto binaryPath       = boost::dll::program_location().parent_path().string();
        context.baseWritePath = binaryPath + "/node1/";
        boost::filesystem::create_directories( context.baseWritePath );
        std::cout << "[DEBUG] Base write path: " << context.baseWritePath << std::endl;

        // Set up dummy account and network parameters.
        const std::string accountAddress = "test_account_address";
        const int         tokenID        = 1;
        uint16_t          basePort       = 4000;
        auto              pubsubPort     = GenerateRandomPort( basePort, accountAddress + std::to_string( tokenID ) );
        auto              graphsyncPort  = pubsubPort + 10;
        std::string       lanIP          = "127.0.0.1";
        std::vector<std::string> addresses{ lanIP };

        // Generate base58 key from SHA256 hash of the account address.
        std::vector<unsigned char> inputBytes( accountAddress.begin(), accountAddress.end() );
        std::vector<unsigned char> hash( SHA256_DIGEST_LENGTH );
        SHA256( inputBytes.data(), inputBytes.size(), hash.data() );
        libp2p::protocol::kademlia::ContentId cid( hash );
        auto                                  accCid = libp2p::multi::ContentIdentifierCodec::decode( cid.data );
        auto maybeBase58 = libp2p::multi::ContentIdentifierCodec::toString( accCid.value() );
        if ( !maybeBase58 )
        {
            throw std::runtime_error( "Could not convert the account to base58" );
        }
        auto base58Key = maybeBase58.value();

        // Compute the network full path.
        int superGeniusVersionMajor = sgns::version::SuperGeniusVersionMajor();
        context.networkFullPath = ( boost::format( GNUS_NETWORK_PATH ) % superGeniusVersionMajor % base58Key ).str();
        std::cout << "[DEBUG] GNUS network full path: " << context.networkFullPath << std::endl;

        // Prepare pubsub key path (similar to GeniusNode's "/pubs_processor").
        std::string pubsubKeyPath     = context.networkFullPath + "/pubs_processor";
        std::string pubsubStoragePath = context.baseWritePath + pubsubKeyPath;
        if ( !boost::filesystem::exists( pubsubStoragePath ) )
        {
            std::ofstream ofs( pubsubStoragePath );
            ofs << "dummy_key";
            ofs.close();
            std::cout << "[DEBUG] Created dummy pubsub key file at: " << pubsubStoragePath << std::endl;
        }

        // --- Logger Initialization ---
        auto loggingYAML        = GetLoggingSystem( context.baseWritePath );
        auto loggerConfigurator = std::make_shared<libp2p::log::Configurator>();
        auto configFromYAML     = std::make_shared<soralog::ConfiguratorFromYAML>( loggerConfigurator, loggingYAML );
        auto loggingSystem      = std::make_shared<soralog::LoggingSystem>( configFromYAML );
        auto logResult          = loggingSystem->configure();
        if ( logResult.has_error )
        {
            std::cerr << "[ERROR] Logging configuration error: " << logResult.message << std::endl;
            throw std::runtime_error( "Could not configure logger" );
        }
        std::cout << "[DEBUG] Logger configured: " << logResult.message << std::endl;
        libp2p::log::setLoggingSystem( loggingSystem );
        std::string logFile    = context.baseWritePath + "/sgnslog2.log";
        auto        nodeLogger = sgns::base::createLogger( "SuperGeniusDemo", logFile );
        nodeLogger->set_level( spdlog::level::err );
        std::cout << "[DEBUG] Log file: " << logFile << std::endl;
        // --- End Logger Initialization ---

        // Create pubsub instance using the key file.
        sgns::crdt::KeyPairFileStorage keyStorage( pubsubStoragePath );
        auto                           pubsubKeyPair = keyStorage.GetKeyPair().value();
        context.pubsub = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( pubsubKeyPair );
        context.pubsub->Start(
            pubsubPort,
            { "/dns4/sg-fullnode-1.gnus.ai/tcp/40052/p2p/12D3KooWBqcxStdb4f9s4zT3bQiTDXYB56VJ77Rt7eEdjw4kXTwi" },
            lanIP,
            addresses );
        std::cout << "[DEBUG] PubSub instance started on port " << pubsubPort << std::endl;

        // Create an io_context for GlobalDB.
        context.io = std::make_shared<boost::asio::io_context>();
        boost::filesystem::create_directories( context.baseWritePath + context.networkFullPath );
        std::cout << "[DEBUG] Database directory: " << ( context.baseWritePath + context.networkFullPath ) << std::endl;

        // Create GlobalDB instance.
        context.globaldb = std::make_shared<sgns::crdt::GlobalDB>( context.io,
                                                                   context.baseWritePath + context.networkFullPath,
                                                                   graphsyncPort,
                                                                   context.pubsub );
        auto options     = sgns::crdt::CrdtOptions::DefaultOptions();
        auto initResult  = context.globaldb->Init( options );
        if ( initResult.has_error() )
        {
            std::cerr << "[ERROR] GlobalDB::Init failed: " << initResult.error().message() << std::endl;
            throw std::runtime_error( initResult.error().message() );
        }
        std::cout << "[DEBUG] GlobalDB::Init succeeded." << std::endl;

        // Note: Do not add any broadcast topic here.
        // Each test will add its own topic as needed.

        // Start the io_context thread.
        context.ioThread = std::thread( []() { context.io->run(); } );
    }

    // TearDownTestSuite: Clean up GlobalDB and remove the database directory.
    static void TearDownTestSuite()
    {
        context.globaldb.reset();
        if ( context.io )
        {
            context.io->stop();
        }
        if ( context.ioThread.joinable() )
        {
            context.ioThread.join();
        }
        boost::filesystem::remove_all( context.baseWritePath + context.networkFullPath );
    }

    void SetUp() override {}

    void TearDown() override {}
};

// --- Define the static GlobalContext instance ---
GlobalContext GlobalDBIntegrationTest::context;

// --- Test Cases ---

TEST_F( GlobalDBIntegrationTest, PutAndGetWithoutTopic )
{
    std::cout << "[TEST] Starting PutAndGetWithoutTopic test" << std::endl;
    sgns::crdt::HierarchicalKey key( "/test/key1" );
    sgns::base::Buffer          value;
    value.put( "Hello from GlobalDB Integration Test!" );

    // No broadcast topic added here, so we expect the put operation to fail.
    auto putResult = context.globaldb->Put( key, value );
    EXPECT_FALSE( putResult.has_value() ) << "Put operation should fail when no topic is inserted";
}

TEST_F( GlobalDBIntegrationTest, BroadcastTopicInsertion )
{
    std::cout << "[TEST] Starting BroadcastTopicInsertion test" << std::endl;
    // Add a specific broadcast topic for this test.
    std::string testTopic = ( boost::format( PROCESSING_CHANNEL ) % 1 ).str();
    context.globaldb->AddBroadcastTopic( testTopic );

    sgns::crdt::HierarchicalKey key( "/broadcast/test" );
    sgns::base::Buffer          value;
    value.put( "Broadcast test value" );

    auto putResult = context.globaldb->Put( key, value, testTopic );
    EXPECT_TRUE( putResult.has_value() ) << "Put with broadcast topic failed";

    auto getResult = context.globaldb->Get( key );
    EXPECT_TRUE( getResult.has_value() ) << "Get after broadcast insertion failed";
    EXPECT_EQ( getResult.value().toString(), "Broadcast test value" );
}

TEST_F( GlobalDBIntegrationTest, MultipleTopicsInsertion )
{
    std::cout << "[TEST] Starting MultipleTopicsInsertion test" << std::endl;
    // Define two distinct topics.
    std::string topic1 = ( boost::format( PROCESSING_CHANNEL ) % 10 ).str();
    std::string topic2 = ( boost::format( PROCESSING_CHANNEL ) % 20 ).str();
    context.globaldb->AddBroadcastTopic( topic1 );
    context.globaldb->AddBroadcastTopic( topic2 );

    // Put a value under a key using topic1.
    sgns::crdt::HierarchicalKey key1( "/multiple/topic1" );
    sgns::base::Buffer          value1;
    value1.put( "Value for topic1" );
    auto putResult1 = context.globaldb->Put( key1, value1, topic1 );
    EXPECT_TRUE( putResult1.has_value() ) << "Put operation for topic1 failed";

    // Put a value under a different key using topic2.
    sgns::crdt::HierarchicalKey key2( "/multiple/topic2" );
    sgns::base::Buffer          value2;
    value2.put( "Value for topic2" );
    auto putResult2 = context.globaldb->Put( key2, value2, topic2 );
    EXPECT_TRUE( putResult2.has_value() ) << "Put operation for topic2 failed";

    // Retrieve and verify both values.
    auto getResult1 = context.globaldb->Get( key1 );
    EXPECT_TRUE( getResult1.has_value() ) << "Get operation for topic1 failed";
    EXPECT_EQ( getResult1.value().toString(), "Value for topic1" );

    auto getResult2 = context.globaldb->Get( key2 );
    EXPECT_TRUE( getResult2.has_value() ) << "Get operation for topic2 failed";
    EXPECT_EQ( getResult2.value().toString(), "Value for topic2" );
}
