#include "processing_service_test.hpp"
#include "processing/processing_service.hpp"

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <unordered_set>
#include "base/sgns_version.hpp"
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
    m_pubsub_keypairs.clear();
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
        // CR-G01 fixture repair: construct every gossip host from an EXPLICIT
        // keypair and retain a copy -- the single-arg ctor's internal keypair
        // is inaccessible, and gated-surface tests must seal sender-side
        // payloads / wire signing keys with the host's own key material.
        auto keypair = GenerateEd25519KeyPair();
        m_pubsub_keypairs.emplace_back( std::make_shared<const libp2p::crypto::KeyPair>( keypair ) );
        auto pubsub_node = m_pubsub_nodes.emplace_back( std::make_shared<GossipPubSub>( std::move( keypair ), config ) );

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

    std::atomic<bool> successCallbackFired{ false };
    std::atomic<bool> errorCallbackFired{ false };

    auto processingService = std::make_shared<ProcessingServiceImpl>( pubs1,
                                                                      1,
                                                                      enqueuer,
                                                                      std::make_shared<SubTaskResultStorageMock>(),
                                                                      processingCore,
                                                                      [&successCallbackFired]( const std::string &,
                                                                                               const SGProcessing::TaskResult & )
                                                                      { successCallbackFired = true; },
                                                                      [&errorCallbackFired]( const std::string & )
                                                                      { errorCallbackFired = true; },
                                                                      "TEST_NODE_ADDRESS_1" );

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

    std::atomic<bool> successCallbackFired{ false };
    std::atomic<bool> errorCallbackFired{ false };

    auto processingService = std::make_shared<ProcessingServiceImpl>( pubs1,
                                                                      1,
                                                                      enqueuer,
                                                                      std::make_shared<SubTaskResultStorageMock>(),
                                                                      processingCore,
                                                                      [&successCallbackFired]( const std::string &,
                                                                                               const SGProcessing::TaskResult & )
                                                                      { successCallbackFired = true; },
                                                                      [&errorCallbackFired]( const std::string & )
                                                                      { errorCallbackFired = true; },
                                                                      "TEST_NODE_ADDRESS_2" );

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

/**
 * @given A processing service on pubs1 with a membership filter allowing only the
 *        service host itself and pubs2; a mesh-connected third pubsub host that is
 *        NOT a member
 * @when The allowed peer publishes a grid-channel processing_channel_response, then
 *       the non-member third host publishes an otherwise-identical response with a
 *       distinct channel id
 * @then The allowed message creates a processing node (AcceptProcessingChannel path)
 *       and the non-member's message leaves the node count unchanged — its sender is
 *       denied at OnMessage entry before any grid handling.
 */
TEST_F( ProcessingServiceTest, GridMessagesFromNonMemberPeersAreIgnored )
{
    auto pubs1 = m_pubsub_nodes[0];
    auto pubs2 = m_pubsub_nodes[1];

    auto processingCore = std::make_shared<ProcessingCoreImpl>();
    auto taskQueue      = std::make_shared<ProcessingTaskQueueImpl>();
    auto enqueuer       = std::make_shared<SubTaskEnqueuerImpl>( taskQueue );

    std::atomic<bool> successCallbackFired{ false };
    std::atomic<bool> errorCallbackFired{ false };

    // maximalNodesCount = 2 so a BROKEN gate on the negative leg would observably
    // grow the node count (a second node for the distinct denied channel id) —
    // the negative window cannot pass vacuously on room capacity.
    auto processingService = std::make_shared<ProcessingServiceImpl>( pubs1,
                                                                      2,
                                                                      enqueuer,
                                                                      std::make_shared<SubTaskResultStorageMock>(),
                                                                      processingCore,
                                                                      [&successCallbackFired]( const std::string &,
                                                                                               const SGProcessing::TaskResult & )
                                                                      { successCallbackFired = true; },
                                                                      [&errorCallbackFired]( const std::string & )
                                                                      { errorCallbackFired = true; },
                                                                      "TEST_NODE_GRID_GATE" );

    m_processing_services.push_back( processingService );

    // The service's grid topic internally appends the net-and-version appendix
    // (ProcessingServiceImpl::Listen) — the raw topics must match it exactly.
    const std::string gridTopic = "GRID_CHANNEL_ID" + sgns::version::GetNetAndVersionAppendix();
    GossipPubSubTopic gridChannel1( pubs1, gridTopic );
    GossipPubSubTopic gridChannel2( pubs2, gridTopic );
    gridChannel1.Subscribe( []( boost::optional<const GossipPubSub::Message &> message ) {} );
    gridChannel2.Subscribe( []( boost::optional<const GossipPubSub::Message &> message ) {} );

    // Wait for the topic mesh: both hosts must see each other on the grid topic
    std::vector<libp2p::peer::PeerId> peers1;
    std::vector<libp2p::peer::PeerId> peers2;
    ASSERT_WAIT_FOR_CONDITION(
        ( [&gridChannel1, &gridChannel2, &peers1, &peers2]()
        {
            peers1 = gridChannel1.getAllPeers();
            peers2 = gridChannel2.getAllPeers();
            return !peers1.empty() && !peers2.empty();
        } ),
        std::chrono::milliseconds( 5000 ),
        "Grid topic mesh not established between the two hosts",
        nullptr );

    processingService->StartProcessing( "GRID_CHANNEL_ID" );

    // Allow list: the service host's own id and pubs2's id — and nothing else.
    // Snapshot BEFORE the third node joins so it cannot be in the set.
    std::unordered_set<std::string> members;
    for ( const auto &peer : peers1 )
    {
        members.insert( peer.toBase58() );
    }
    for ( const auto &peer : peers2 )
    {
        members.insert( peer.toBase58() );
    }
    processingService->SetMembershipFilter(
        [members]( const libp2p::peer::PeerId &peer ) { return members.count( peer.toBase58() ) > 0; } );
    // CR-G01: a filtered service must also be able to SEAL its own publishes.
    processingService->SetGossipSigningKey( m_pubsub_keypairs[0] );

    // --- Positive leg: the allowed peer's channel response creates a processing
    //     node. The payload is SEALED with the publisher's own host keypair --
    //     the gated service authenticates before it consults membership.
    SGProcessing::GridChannelMessage allowedMessage;
    allowedMessage.mutable_processing_channel_response()->set_channel_id( "PROCESSING_QUEUE_ID_ALLOWED" );
    const auto allowedSealed = SealPayloadForKey( *m_pubsub_keypairs[1], allowedMessage.SerializeAsString() );
    ASSERT_FALSE( allowedSealed.empty() ) << "sender-side sealing failed";
    gridChannel2.Publish( allowedSealed );

    EXPECT_WAIT_FOR_CONDITION(
        [&processingService]() { return processingService->GetProcessingNodesCount() > 0; },
        std::chrono::milliseconds( 5000 ),
        "Allowed peer's grid message did not create a processing node",
        nullptr );
    const size_t countAfterPositiveLeg = processingService->GetProcessingNodesCount();
    EXPECT_GE( countAfterPositiveLeg, 1 );

    // --- Negative leg: a mesh-connected THIRD (non-member) pubsub publishes the
    // same message shape with a distinct channel id. Its payload is SEALED with
    // its OWN host keypair (an honestly-envelope'd non-member), so the deny is
    // attributable to the MEMBERSHIP predicate, not to a missing envelope.
    libp2p::protocol::gossip::Config config;
    config.echo_forward_mode       = true;
    config.sign_messages           = true; // from IS populated — the deny is attributable to the filter
    config.seen_cache_limit        = 10;
    config.heartbeat_interval_msec = std::chrono::milliseconds{ 100 };
    auto pubs3_keypair = GenerateEd25519KeyPair();
    auto pubs3         = std::make_shared<GossipPubSub>( pubs3_keypair, config );
    auto pubs3StartFuture = pubs3->Start( 0, { pubs1->GetInterfaceAddress() } );
    if ( auto result = pubs3StartFuture.get(); result )
    {
        FAIL() << "Third pubsub node failed to start: " << result.message();
    }
    GossipPubSubTopic gridChannel3( pubs3, gridTopic );
    gridChannel3.Subscribe( []( boost::optional<const GossipPubSub::Message &> message ) {} );
    // Wait for the third node to join the mesh BEFORE publishing — an unconnected
    // third node would make the negative window prove nothing
    ASSERT_WAIT_FOR_CONDITION( [&gridChannel3]() { return gridChannel3.getPeerCount() > 0; },
                               std::chrono::milliseconds( 5000 ),
                               "Third pubsub did not join the grid topic mesh",
                               nullptr );

    SGProcessing::GridChannelMessage deniedMessage;
    deniedMessage.mutable_processing_channel_response()->set_channel_id( "PROCESSING_QUEUE_ID_DENIED" );
    const auto deniedSealed = SealPayloadForKey( pubs3_keypair, deniedMessage.SerializeAsString() );
    ASSERT_FALSE( deniedSealed.empty() ) << "non-member sender-side sealing failed";
    gridChannel3.Publish( deniedSealed );

    // Bounded negative window (grace-loop pattern): the node count must stay unchanged
    const auto denyDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( 3000 );
    while ( std::chrono::steady_clock::now() < denyDeadline )
    {
        ASSERT_EQ( countAfterPositiveLeg, processingService->GetProcessingNodesCount() )
            << "Non-member peer's grid message was processed although the filter denies it";
        std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    }
    EXPECT_EQ( countAfterPositiveLeg, processingService->GetProcessingNodesCount() );

    // Neither user callback fired during the scene (spurious-callback detectors)
    EXPECT_FALSE( successCallbackFired.load() );
    EXPECT_FALSE( errorCallbackFired.load() );

    // Stop the standalone third node (not tracked by the fixture): unsubscribe its
    // topic first, then stop the host — the same order TearDown uses for the rest.
    gridChannel3.Unsubscribe();
    pubs3->Stop();
}
