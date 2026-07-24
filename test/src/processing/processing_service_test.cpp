#include "processing_service_test.hpp"
#include "processing/processing_service.hpp"

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <gtest/gtest.h>
#include "testutil/wait_condition.hpp"

using namespace sgns::processing;
using namespace sgns::test;

const std::string logger_config( R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: processing_service_test
    sink: console
    level: trace
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )" );

void ProcessingServiceTest::SetUp()
{
    SetUp( "processing_service_test", logger_config );
    Initialize( 2, 50 );
}

void ProcessingServiceTest::SetUp( std::string name, std::string loggerConfig )
{
    // prepare log system
    auto logSystem = std::make_shared<soralog::LoggingSystem>( std::make_shared<soralog::ConfiguratorFromYAML>(
        // Original LibP2P logging config
        std::make_shared<libp2p::log::Configurator>(),
        // Additional logging config for application
        loggerConfig ) );
    if ( auto result = logSystem->configure(); result.has_error )
    {
        throw std::domain_error( "Unable to configure soralog" );
    }

    libp2p::log::setLoggingSystem( logSystem );

    m_Logger = logSystem->getLogger( "console", name );
#ifdef SGNS_DEBUGLOGS
    libp2p::log::setLevelOfGroup( name, soralog::Level::OFF );

    auto loggerProcQM = base::createLogger( "ProcessingSubTaskQueueManager" );
    loggerProcQM->set_level( spdlog::level::trace );

    loggerProcQM = base::createLogger( "ProcessingSubTaskQueue" );
    loggerProcQM->set_level( spdlog::level::off );

    loggerProcQM = base::createLogger( "ProcessingSubTaskQueueAccessorImpl" );
    loggerProcQM->set_level( spdlog::level::trace );
    auto loggerProcEngine = base::createLogger( "ProcessingEngine" );
    loggerProcEngine->set_level( spdlog::level::off );
    auto loggerQueueChannel = base::createLogger( "ProcessingSubTaskQueueChannelPubSub" );
    loggerQueueChannel->set_level( spdlog::level::off );
    auto loggerBroadcaster = base::createLogger( "PubSubBroadcasterExt" );
    loggerBroadcaster->set_level( spdlog::level::trace );
    auto loggerPubsub = base::createLogger( "GossipPubSub" );
    loggerPubsub->set_level( spdlog::level::trace );
#else
    libp2p::log::setLevelOfGroup( name, soralog::Level::OFF );
#endif
}

void ProcessingServiceTest::TearDown()
{
    // FIRST: Stop all ProcessingServiceImpl instances to shut down their background threads
    for ( auto &service : m_processing_services )
    {
        if ( service )
        {
            service->StopProcessing();
        }
    }

    // Allow time for ProcessingServiceImpl background threads to complete
    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

    // Second, stop all processing engines to ensure no background threads are running
    for ( auto &engine : m_processing_engines )
    {
        if ( engine )
        {
            engine->StopQueueProcessing();
        }
    }

    // Allow more time for any ongoing ProcessSubTask threads to complete
    // The crash shows threads are still running, so we need more time
    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

    // Now safely destroy engines after stopping them
    for ( auto &engine : m_processing_engines )
    {
        if ( engine )
        {
            engine.reset(); // Force cleanup
        }
    }

    // Additional wait to ensure engine destruction completed
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    // Destroy processing services after engines
    for ( auto &service : m_processing_services )
    {
        if ( service )
        {
            service.reset(); // Force cleanup
        }
    }

    // Next, destroy accessors (which engines might reference)
    for ( auto &accessor : m_processing_queues_accessors )
    {
        if ( accessor )
        {
            accessor.reset(); // Force cleanup
        }
    }

    // Then destroy managers (which accessors depend on)
    for ( auto &mgr : m_processing_queues_managers )
    {
        if ( mgr )
        {
            mgr.reset(); // Force cleanup
        }
    }

    // Destroy pubsub channels (which managers depend on)
    for ( auto &pubsub : m_processing_queues_channel_pub_subs )
    {
        if ( pubsub )
        {
            pubsub.reset(); // Force cleanup
        }
    }

    // Destroy processing cores (which engines used)
    for ( auto &core : m_processing_cores )
    {
        if ( core )
        {
            core.reset(); // Force cleanup
        }
    }

    // Finally, stop and destroy pubsub nodes (which everything else depends on)
    for ( auto &pubs : m_pubsub_nodes )
    {
        if ( pubs )
        {
            pubs->Stop();
        }
    }

    // On Linux, we need extra time for TCP sockets to fully release from TIME_WAIT state
    // The pubsub Stop() method waits for libp2p shutdown, but the OS may still hold the port
    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

    // Now reset the pubsub nodes
    for ( auto &pubs : m_pubsub_nodes )
    {
        if ( pubs )
        {
            pubs.reset();
        }
    }

    // Clear collections
    m_pubsub_nodes.clear();
    m_processing_queues_accessors.clear();
    m_processing_queues_managers.clear();
    m_processing_engines.clear();
    m_processing_queues_channel_pub_subs.clear();
    m_processing_cores.clear();
    m_pubsub_futures.clear();
    m_processing_services.clear();

    // Add extra delay for Linux socket cleanup
    // This ensures TCP sockets are fully released from TIME_WAIT state
    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
}

void ProcessingServiceTest::Initialize( uint64_t numNodes, size_t processingTime )
{
    // create 2 nodes default
    std::vector<std::string>         bootstrap_nodes = {};
    libp2p::protocol::gossip::Config config;
    config.echo_forward_mode       = true;
    config.sign_messages           = true;
    config.seen_cache_limit        = 10;
    config.heartbeat_interval_msec = std::chrono::milliseconds{ 100 };
    for ( size_t i = 0; i < numNodes; ++i )
    {
        auto pubsub_node = m_pubsub_nodes.emplace_back( std::make_shared<GossipPubSub>( config ) );

        Color::PrintInfo( "Attempting to start PubSub node ", i, " on an OS-assigned port" );
        for ( auto node : bootstrap_nodes )
        {
            Color::PrintInfo( "  with bootstrap node: ", node );
        }

        // Start the node and wait for it to complete before getting its address
        m_pubsub_futures.emplace_back( m_pubsub_nodes[i]->Start( 0, bootstrap_nodes ) );

        if ( auto result = m_pubsub_futures.back().get(); result )
        {
            throw std::runtime_error( "PubSub node " + std::to_string( i ) + " failed to start: " +
                                      result.message() );
        }
        Color::PrintInfo( "PubSub node ", i, " started successfully" );

        // Now it's safe to get the interface address and use it as bootstrap
        if ( i == 0 )
        {
            std::string interfaceAddr = pubsub_node->GetInterfaceAddress();
            Color::PrintInfo( "PubSub node 0 started on address ", interfaceAddr );
            bootstrap_nodes = { interfaceAddr };
        }
    }

    for ( size_t i = 0; i < numNodes; ++i )
    {
        std::string nodeId      = "NODE_" + std::to_string( i + 1 );
        auto        pubsub_node = m_pubsub_nodes[i];
        // Both nodes process at the same speed
        auto processingCore = m_processing_cores.emplace_back( std::make_shared<ProcessingCoreImpl>( processingTime ) );
        auto queuePubSubChannel = m_processing_queues_channel_pub_subs.emplace_back(
            std::make_shared<ProcessingSubTaskQueueChannelPubSub>( pubsub_node, "QUEUE_CHANNEL_ID" ) );
        auto processingQueueManager = m_processing_queues_managers.emplace_back(
            std::make_shared<ProcessingSubTaskQueueManager>( queuePubSubChannel,
                                                             pubsub_node->GetAsioContext(),
                                                             nodeId,
                                                             []( const std::string & ) {} ) );
        m_processing_engines.emplace_back(
            std::make_shared<ProcessingEngine>( nodeId, processingCore, []( const std::string & ) {}, [] {} ) );
        m_IsTaskFinalized.emplace_back( std::make_unique<std::atomic<bool>>( false ) );
        auto queueAccessor = m_processing_queues_accessors.emplace_back( std::make_shared<SubTaskQueueAccessorImpl>(
            pubsub_node,
            processingQueueManager,
            std::make_shared<SubTaskResultStorageMock>(),
            [this, i, nodeId]( const SGProcessing::TaskResult & )
            {
                m_IsTaskFinalized[i]->store( true );
                Color::PrintInfo( "Task finalized by ", nodeId );
            },
            []( const std::string & ) {} ) );
        queueAccessor->CreateResultsChannel( "test" );
    }
}

/**
 * @given Empty queue list
 * @when A queue channel received
 * @then A processing node is created
 */
TEST_F( ProcessingServiceTest, DISABLED_ProcessingSlotsAreAvailable )
{
    auto pubs1 = m_pubsub_nodes[0];
    auto pubs2 = m_pubsub_nodes[1];

    auto processingCore = std::make_shared<ProcessingCoreImpl>();
    auto taskQueue      = std::make_shared<ProcessingTaskQueueImpl>();
    auto enqueuer       = std::make_shared<SubTaskEnqueuerImpl>( taskQueue );

    auto processingService = std::make_shared<ProcessingServiceImpl>( pubs1,
                                                                      1,
                                                                      enqueuer,
                                                                      std::make_shared<SubTaskResultStorageMock>(),
                                                                      processingCore );

    m_processing_services.push_back( processingService );

    GossipPubSubTopic gridChannel1( pubs1, "GRID_CHANNEL_ID" );
    GossipPubSubTopic gridChannel2( pubs2, "GRID_CHANNEL_ID" );
    gridChannel1.Subscribe( []( boost::optional<const GossipPubSub::Message &> message ) {} );
    gridChannel2.Subscribe( []( boost::optional<const GossipPubSub::Message &> message ) {} );
    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

    processingService->StartProcessing( "GRID_CHANNEL_ID" );
    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

    SGProcessing::GridChannelMessage gridMessage;
    auto                             channelResponse = gridMessage.mutable_processing_channel_response();
    channelResponse->set_channel_id( "PROCESSING_QUEUE_ID" );
    gridChannel2.Publish( gridMessage.SerializeAsString() );

    EXPECT_WAIT_FOR_CONDITION(
        [&processingService]() { return processingService->GetProcessingNodesCount() == 1; },
        std::chrono::milliseconds( 3000 ),
        "Processing node was not created",
        nullptr );
}

/**
 * @given Empty queue list
 * @when No queue channel received
 * @then No new processing node is created
 */
// The test disabled due to processing room handling removed
// No room capacity is checked
TEST_F( ProcessingServiceTest, NoProcessingSlotsAvailable )
{
    auto pubs1 = m_pubsub_nodes[0];
    auto pubs2 = m_pubsub_nodes[1];

    auto processingCore = std::make_shared<ProcessingCoreImpl>();
    auto taskQueue      = std::make_shared<ProcessingTaskQueueImpl>();
    auto enqueuer       = std::make_shared<SubTaskEnqueuerImpl>( taskQueue );

    auto processingService = std::make_shared<ProcessingServiceImpl>( pubs1,
                                                                      1,
                                                                      enqueuer,
                                                                      std::make_shared<SubTaskResultStorageMock>(),
                                                                      processingCore );

    // Track the ProcessingServiceImpl for proper cleanup
    m_processing_services.push_back( processingService );

    GossipPubSubTopic gridChannel1( pubs1, "GRID_CHANNEL_ID" );
    GossipPubSubTopic gridChannel2( pubs2, "GRID_CHANNEL_ID" );
    gridChannel1.Subscribe( []( boost::optional<const GossipPubSub::Message &> message ) {} );
    gridChannel2.Subscribe( []( boost::optional<const GossipPubSub::Message &> message ) {} );
    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

    processingService->StartProcessing( "GRID_CHANNEL_ID" );
    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

    // No queue channel message sent

    EXPECT_WAIT_FOR_CONDITION(
        [&processingService]() { return processingService->GetProcessingNodesCount() == 0; },
        std::chrono::milliseconds( 3000 ),
        "No processing node should have been created",
        nullptr );
}
