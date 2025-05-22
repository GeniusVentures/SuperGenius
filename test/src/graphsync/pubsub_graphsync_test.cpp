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
#include "testutil/wait_condition.hpp"
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/local_requests.hpp>
#include "libp2p/protocol/common/asio/asio_scheduler.hpp"

std::string GetLoggingSystem( const std::string & )
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
       - name: debug
 # ----------------
     )";
    return config;
}

using namespace sgns::test;

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
        libp2p::log::setLevelOfGroup( "gossip_pubsub_test", soralog::Level::TRACE );
        auto loggerGlobalDB  = sgns::base::createLogger( "GlobalDB", "" );
        auto loggerDAGSyncer = sgns::base::createLogger( "GraphsyncDAGSyncer", "" );

        auto loggerBroadcaster = sgns::base::createLogger( "PubSubBroadcasterExt", "" );
        auto loggerDataStore   = sgns::base::createLogger( "CrdtDatastore", "" );
        auto loggerGraphsync   = sgns::base::createLogger( "graphsync", "" );
        loggerGraphsync->set_level( spdlog::level::trace );
        loggerGlobalDB->set_level( spdlog::level::err );
        loggerDAGSyncer->set_level( spdlog::level::err );

        loggerBroadcaster->set_level( spdlog::level::err );
        loggerDataStore->set_level( spdlog::level::err );
    }

    static void TearDownTestSuite() {}
};

// Static member initialization

TEST_F( PubsubGraphsyncTest, MultiGlobalDBTest )
{
    std::string binaryPath = boost::dll::program_location().parent_path().string();
    std::string basePath1  = binaryPath + "/pubsub_graphsync_pubs1";
    std::string basePath2  = binaryPath + "/pubsub_graphsync_pubs2";
    std::string basePath3  = binaryPath + "/pubsub_graphsync_add1";
    std::string basePath4  = binaryPath + "/pubsub_graphsync_add2";
    std::string basePath5  = binaryPath + "/pubsub_graphsync_add3";
    std::string basePath6  = binaryPath + "/pubsub_graphsync_add4";
    std::filesystem::remove_all( basePath1 );
    std::filesystem::remove_all( basePath2 );
    std::filesystem::remove_all( basePath3 );
    std::filesystem::remove_all( basePath4 );
    std::filesystem::remove_all( basePath5 );
    std::filesystem::remove_all( basePath6 );

    auto io_context = std::make_shared<boost::asio::io_context>();

    auto pubs1        = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();
    auto start1Future = pubs1->Start( 40001, {} );
    auto pubs2        = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();
    auto start2Future = pubs2->Start( 40002, {} );

    std::chrono::milliseconds resultTime;
    assertWaitForCondition(

        [&start1Future, &start2Future]()
        {
            start1Future.get();

            start2Future.get();

            return true;
        },

        std::chrono::milliseconds( 2000 ),

        "Pubsub nodes initialization failed",

        &resultTime

    );
    auto scheduler         = std::make_shared<libp2p::protocol::AsioScheduler>( io_context,
                                                                        libp2p::protocol::SchedulerConfig{} );
    auto graphsyncnetwork  = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubs1->GetHost(), scheduler );
    auto generator         = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();
    auto scheduler2        = std::make_shared<libp2p::protocol::AsioScheduler>( io_context,
                                                                         libp2p::protocol::SchedulerConfig{} );
    auto graphsyncnetwork2 = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubs2->GetHost(),
                                                                                          scheduler2 );

    auto generator2 = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();

    auto globaldb_ret1 = sgns::crdt::GlobalDB::New( io_context,
                                                    basePath1,
                                                    pubs1,
                                                    sgns::crdt::CrdtOptions::DefaultOptions(),
                                                    graphsyncnetwork,
                                                    scheduler,
                                                    generator );
    auto globaldb_ret2 = sgns::crdt::GlobalDB::New( io_context,
                                                    basePath2,
                                                    pubs2,
                                                    sgns::crdt::CrdtOptions::DefaultOptions(),
                                                    graphsyncnetwork2,
                                                    scheduler2,
                                                    generator2 );
    auto globaldb_ret3 = sgns::crdt::GlobalDB::New( io_context,
                                                    basePath3,
                                                    pubs1,
                                                    sgns::crdt::CrdtOptions::DefaultOptions(),
                                                    graphsyncnetwork,
                                                    scheduler,
                                                    generator );
    auto globaldb_ret4 = sgns::crdt::GlobalDB::New( io_context,
                                                    basePath4,
                                                    pubs2,
                                                    sgns::crdt::CrdtOptions::DefaultOptions(),
                                                    graphsyncnetwork2,
                                                    scheduler2,
                                                    generator2 );

    auto globaldb_ret5 = sgns::crdt::GlobalDB::New( io_context,
                                                    basePath5,
                                                    pubs1,
                                                    sgns::crdt::CrdtOptions::DefaultOptions(),
                                                    graphsyncnetwork,
                                                    scheduler,
                                                    generator );
    auto globaldb_ret6 = sgns::crdt::GlobalDB::New( io_context,
                                                    basePath6,
                                                    pubs2,
                                                    sgns::crdt::CrdtOptions::DefaultOptions(),
                                                    graphsyncnetwork2,
                                                    scheduler2,
                                                    generator2 );

    EXPECT_TRUE( globaldb_ret1.has_value() );
    EXPECT_TRUE( globaldb_ret2.has_value() );
    EXPECT_TRUE( globaldb_ret3.has_value() );
    EXPECT_TRUE( globaldb_ret4.has_value() );
    EXPECT_TRUE( globaldb_ret5.has_value() );
    EXPECT_TRUE( globaldb_ret6.has_value() );

    auto gdb1 = std::move( globaldb_ret1.value() );
    auto gdb2 = std::move( globaldb_ret2.value() );
    auto gdb3 = std::move( globaldb_ret3.value() );
    auto gdb4 = std::move( globaldb_ret4.value() );
    auto gdb5 = std::move( globaldb_ret5.value() );
    auto gdb6 = std::move( globaldb_ret6.value() );
    gdb1->AddBroadcastTopic( "test1" );
    gdb2->AddBroadcastTopic( "test1" );
    gdb3->AddBroadcastTopic( "test2" );
    gdb4->AddBroadcastTopic( "test2" );
    gdb5->AddBroadcastTopic( "test3" );
    gdb6->AddBroadcastTopic( "test3" );
    gdb1->AddListenTopic( "test1" );
    gdb2->AddListenTopic( "test1" );
    gdb3->AddListenTopic( "test2" );
    gdb4->AddListenTopic( "test2" );
    gdb5->AddListenTopic( "test3" );
    gdb6->AddListenTopic( "test3" );
    gdb1->Start();
    gdb2->Start();
    gdb3->Start();
    gdb4->Start();
    gdb5->Start();
    gdb6->Start();
    pubs1->AddPeers( { pubs2->GetLocalAddress() } );
    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
    std::thread io_thread = std::thread( [io_context]() { io_context->run(); } );
    //Dummy Transaction Data
    auto                         transaction = gdb1->BeginTransaction();
    sgns::crdt::HierarchicalKey  tx_key( "/test/test" );
    sgns::crdt::GlobalDB::Buffer data_transaction;
    std::vector<uint8_t>         dummy_data;

    // Reserve space to avoid reallocations
    constexpr size_t target_size = 512 * 1024; // 512KB
    dummy_data.reserve( target_size );

    // Generate pattern: 0xde, 0xad, 0xbe, 0xef repeated
    const uint8_t pattern[] = { 0xde, 0xad, 0xbe, 0xef };

    while ( dummy_data.size() < target_size )
    {
        dummy_data.insert( dummy_data.end(), std::begin( pattern ), std::end( pattern ) );
    }

    data_transaction.put( gsl::span<const uint8_t>( dummy_data ) );

    transaction->Put( tx_key, data_transaction );
    transaction->Commit();

    auto                         transaction2 = gdb3->BeginTransaction();
    sgns::crdt::HierarchicalKey  tx_key2( "/test/test2" );
    sgns::crdt::GlobalDB::Buffer data_transaction2;
    std::vector<uint8_t>         dummy_data2 = { 0xde, 0xad, 0xbe, 0xef };
    data_transaction2.put( gsl::span<const uint8_t>( dummy_data2 ) );

    transaction2->Put( tx_key2, data_transaction2 );
    transaction2->Commit();

    bool                         getConfirmed = false;
    sgns::crdt::GlobalDB::Buffer retrieved_data;

    assertWaitForCondition(
        [&]()
        {
            auto result = gdb2->Get( tx_key );
            if ( result )
            {
                retrieved_data = std::move( result.value() );
                getConfirmed   = true;
                return true;
            }
            return false;
        },
        std::chrono::milliseconds( 20000 ),
        "Replication to gdb2 did not complete in time" );

    bool                         getConfirmed2 = false;
    sgns::crdt::GlobalDB::Buffer retrieved_data2;
    assertWaitForCondition(
        [&]()
        {
            auto result2 = gdb4->Get( tx_key2 );
            if ( result2 )
            {
                retrieved_data2 = std::move( result2.value() );
                getConfirmed2   = true;
                return true;
            }
            return false;
        },
        std::chrono::milliseconds( 20000 ),
        "Replication to gdb4 did not complete in time" );
    auto result2 = gdb3->Get( tx_key );
    auto result3 = gdb4->Get( tx_key );
    auto result4 = gdb5->Get( tx_key );
    auto result5 = gdb6->Get( tx_key );
    auto result6 = gdb1->Get( tx_key2 );
    auto result7 = gdb2->Get( tx_key2 );
    auto result8 = gdb5->Get( tx_key2 );
    auto result9 = gdb6->Get( tx_key2 );
    EXPECT_FALSE( result2 );
    EXPECT_FALSE( result3 );
    EXPECT_FALSE( result4 );
    EXPECT_FALSE( result5 );
    EXPECT_FALSE( result6 );
    EXPECT_FALSE( result7 );
    EXPECT_FALSE( result8 );
    EXPECT_FALSE( result9 );
    EXPECT_TRUE( getConfirmed );
    EXPECT_TRUE( getConfirmed2 );
    io_context->stop();
    if ( io_thread.joinable() )
    {
        io_thread.join();
        std::cout << "Join thread 1 " << std::endl;
    }
}
