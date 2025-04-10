#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <memory>
#include <iostream>
#include <cstdint>

#ifdef _WIN32
//#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

#include <functional>
#include <thread>

#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include <boost/dll.hpp>
#include <boost/format.hpp>
#include <crdt/globaldb/globaldb.hpp>
#include <crdt/dagsyncer.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/log/configurator.hpp>

std::string GetLoggingSystem( const std::string &  )
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

class PubsubGraphsyncTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        auto logSystem = std::make_shared<soralog::LoggingSystem>( std::make_shared<soralog::ConfiguratorFromYAML>(
            // Original LibP2P logging config
            std::make_shared<libp2p::log::Configurator>(),
            // Additional logging config for application
            GetLoggingSystem( "" ) ) );
        auto result    = logSystem->configure();

        libp2p::log::setLoggingSystem( logSystem );
        libp2p::log::setLevelOfGroup( "gossip_pubsub_test", soralog::Level::DEBUG );
        auto loggerGlobalDB    = sgns::base::createLogger( "GlobalDB", "" );
        auto loggerDAGSyncer   = sgns::base::createLogger( "GraphsyncDAGSyncer", "" );
       
        auto loggerBroadcaster = sgns::base::createLogger( "PubSubBroadcasterExt", "" );
        auto loggerDataStore   = sgns::base::createLogger( "CrdtDatastore", "" );
        loggerGlobalDB->set_level( spdlog::level::trace );
        loggerDAGSyncer->set_level( spdlog::level::trace );
        
        loggerBroadcaster->set_level( spdlog::level::trace );
        loggerDataStore->set_level( spdlog::level::trace );
    }

    static void TearDownTestSuite()
    {
    }
};

// Static member initialization

TEST_F(PubsubGraphsyncTest, MultiGlobalDBTest )
{
    std::string binaryPath = boost::dll::program_location().parent_path().string();
    std::string basePath1  = binaryPath + "/pubsub_graphsync_pubs1";
    std::string basePath2  = binaryPath + "/pubsub_graphsync_pubs2";
    auto io_context = std::make_shared<boost::asio::io_context>();
    auto pubs1 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();
    pubs1->Start( 40001, {} );
    auto pubs2 = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();
    pubs2->Start( 40002, {} );
    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

    //pubs2->AddPeers( { pubs1->GetLocalAddress() } );
    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
    auto gdb1 = std::make_shared<sgns::crdt::GlobalDB>(
        io_context,
        basePath1,
        40001,
        std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>( pubs1, "test" ) );
    auto gdb2 = std::make_shared<sgns::crdt::GlobalDB>(
        io_context,
        basePath2,
        40002,
        std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>( pubs2, "test" ) );

    gdb1->Init( sgns::crdt::CrdtOptions::DefaultOptions() );
    gdb2->Init( sgns::crdt::CrdtOptions::DefaultOptions() );
    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
    pubs1->AddPeers( { pubs2->GetLocalAddress() } );
    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
    auto loggerGraphsync = sgns::base::createLogger( "graphsync", "" );
    loggerGraphsync->set_level( spdlog::level::trace );
    //Dummy Transaction Data
    auto transaction = gdb1->BeginTransaction();
    sgns::crdt::HierarchicalKey  tx_key( "/test/test" );
    sgns::crdt::GlobalDB::Buffer data_transaction;
    std::vector<uint8_t>         dummy_data = { 0xde, 0xad, 0xbe, 0xef };
    data_transaction.put( gsl::span<const uint8_t>( dummy_data ) );

    transaction->Put( tx_key, data_transaction );
    transaction->Commit();
    std::this_thread::sleep_for( std::chrono::milliseconds( 20000 ) );
    EXPECT_NE(1, 2) << "Addresses are equal even though they should not be";
}
