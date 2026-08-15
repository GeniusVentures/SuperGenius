/**
 * @file       processing_service_test_base.cpp
 * @brief      Base class method implementations for ProcessingServiceTest.
 * @date       2026-06-30
 *
 * Extracted from processing_service_test.cpp so test targets can link
 * against the base class without pulling in disabled test cases that
 * reference outdated constructor signatures.
 */

#include "processing_service_test.hpp"

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <gtest/gtest.h>
#include <thread>
#include "testutil/wait_condition.hpp"
#include "base/logger.hpp"

using namespace sgns::processing;
using namespace sgns::test;

const std::string _psvc_test_logger_config( R"(
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
    SetUp( "processing_service_test", _psvc_test_logger_config );
    Initialize( 2, 50 );
}

void ProcessingServiceTest::SetUp( std::string name, std::string loggerConfig )
{
    auto logSystem = std::make_shared<soralog::LoggingSystem>( std::make_shared<soralog::ConfiguratorFromYAML>(
        std::make_shared<libp2p::log::Configurator>(),
        loggerConfig ) );
    if ( auto result = logSystem->configure(); result.has_error )
    {
        throw std::domain_error( "Unable to configure soralog" );
    }

    libp2p::log::setLoggingSystem( logSystem );

    m_Logger = logSystem->getLogger( "console", name );
#ifdef SGNS_DEBUGLOGS
    libp2p::log::setLevelOfGroup( name, soralog::Level::OFF );

    auto loggerProcQM = sgns::base::createLogger( "ProcessingSubTaskQueueManager" );
    loggerProcQM->set_level( spdlog::level::trace );

    loggerProcQM = sgns::base::createLogger( "ProcessingSubTaskQueue" );
    loggerProcQM->set_level( spdlog::level::off );

    loggerProcQM = sgns::base::createLogger( "ProcessingSubTaskQueueAccessorImpl" );
    loggerProcQM->set_level( spdlog::level::trace );
    auto loggerProcEngine = sgns::base::createLogger( "ProcessingEngine" );
    loggerProcEngine->set_level( spdlog::level::off );
    auto loggerQueueChannel = sgns::base::createLogger( "ProcessingSubTaskQueueChannelPubSub" );
    loggerQueueChannel->set_level( spdlog::level::off );
    auto loggerBroadcaster = sgns::base::createLogger( "PubSubBroadcasterExt" );
    loggerBroadcaster->set_level( spdlog::level::trace );
    auto loggerPubsub = sgns::base::createLogger( "GossipPubSub" );
    loggerPubsub->set_level( spdlog::level::trace );
#else
    libp2p::log::setLevelOfGroup( name, soralog::Level::OFF );
#endif
}

void ProcessingServiceTest::TearDown()
{
    for ( auto &service : m_processing_services )
    {
        if ( service )
        {
            service->StopProcessing();
        }
    }

    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

    for ( auto &engine : m_processing_engines )
    {
        if ( engine )
        {
            engine->StopQueueProcessing();
        }
    }

    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

    for ( auto &engine : m_processing_engines )
    {
        if ( engine )
        {
            engine.reset();
        }
    }

    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    for ( auto &service : m_processing_services )
    {
        if ( service )
        {
            service.reset();
        }
    }

    for ( auto &accessor : m_processing_queues_accessors )
    {
        if ( accessor )
        {
            accessor.reset();
        }
    }

    for ( auto &mgr : m_processing_queues_managers )
    {
        if ( mgr )
        {
            mgr.reset();
        }
    }

    for ( auto &pubsub : m_processing_queues_channel_pub_subs )
    {
        if ( pubsub )
        {
            pubsub.reset();
        }
    }

    for ( auto &core : m_processing_cores )
    {
        if ( core )
        {
            core.reset();
        }
    }

    for ( auto &pubs : m_pubsub_nodes )
    {
        if ( pubs )
        {
            pubs->Stop();
        }
    }

    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

    for ( auto &pubs : m_pubsub_nodes )
    {
        if ( pubs )
        {
            pubs.reset();
        }
    }

    m_pubsub_nodes.clear();
    m_processing_queues_accessors.clear();
    m_processing_queues_managers.clear();
    m_processing_engines.clear();
    m_processing_queues_channel_pub_subs.clear();
    m_processing_cores.clear();
    m_pubsub_futures.clear();
    m_processing_services.clear();

    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
}

void ProcessingServiceTest::Initialize( uint64_t numNodes, size_t processingTime )
{
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
        for (auto node : bootstrap_nodes) {
            Color::PrintInfo("  with bootstrap node: ", node);
        }

        m_pubsub_futures.emplace_back( m_pubsub_nodes[i]->Start( 0, bootstrap_nodes ) );

        if ( auto result = m_pubsub_futures.back().get(); result )
        {
            throw std::runtime_error( "PubSub node " + std::to_string( i ) + " failed to start: " +
                                      result.message() );
        }
        Color::PrintInfo( "PubSub node ", i, " started successfully" );

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
