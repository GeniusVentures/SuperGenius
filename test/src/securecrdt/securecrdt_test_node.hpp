/**
 * @file       securecrdt_test_node.hpp
 * @brief      Minimal single-node GlobalDB test fixture helper, extracted
 *             inline (not shared via a header outside test/src/securecrdt)
 *             from test/src/crdt/globaldb_integration.cpp's
 *             TestNodeCollection::addNode pattern. These tests never call
 *             connectNodes() -- SecureCrdt is exercised against ONE node's
 *             local Put/Get/QueryKeyValues only, per RESEARCH.md Pitfall 3(a).
 */
#ifndef SGNS_TEST_SECURECRDT_TEST_NODE_HPP
#define SGNS_TEST_SECURECRDT_TEST_NODE_HPP

#include <boost/asio/io_context.hpp>
#include <boost/dll.hpp>
#include <boost/filesystem.hpp>
#include <gtest/gtest.h>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <memory>
#include <mutex>
#include <soralog/impl/configurator_from_yaml.hpp>
#include <string>
#include <thread>

#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_pubsub/gossip_pubsub.hpp>

#include "crdt/crdt_options.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"

namespace sgns::test::securecrdt
{
    /// @brief Configures libp2p's soralog logging system exactly once per
    ///        process. Constructing a GossipPubSub/libp2p Host before this
    ///        runs segfaults inside libp2p's Noise security adaptor, which
    ///        unconditionally calls createLogger() during DI injector
    ///        construction (see test/src/crdt/globaldb_integration.cpp's
    ///        SetUpTestSuite for the reference sequence this mirrors).
    inline void EnsureLoggingSystemConfigured()
    {
        static std::once_flag once;
        std::call_once( once,
                        []()
                        {
                            static const std::string kConfig = R"(
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: securecrdt_test
    sink: console
    level: error
    children:
      - name: libp2p
      - name: gossip
      - name: debug
)";
                            auto loggerConfigurator = std::make_shared<libp2p::log::Configurator>();
                            auto configFromYAML =
                                std::make_shared<soralog::ConfiguratorFromYAML>( loggerConfigurator, kConfig );
                            auto loggingSystem = std::make_shared<soralog::LoggingSystem>( configFromYAML );
                            const auto confResult = loggingSystem->configure();
                            if ( confResult.has_error )
                            {
                                throw std::runtime_error( "Could not configure logger for securecrdt tests" );
                            }
                            libp2p::log::setLoggingSystem( loggingSystem );
                        } );
    }
    /// @brief One unconnected single-node GlobalDB instance for SecureCrdt tests.
    struct SecureCrdtTestNode
    {
        std::string                                      basePath;
        std::shared_ptr<boost::asio::io_context>         io;
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub;
        std::shared_ptr<sgns::crdt::GlobalDB>            db;
        std::thread                                      ioThread;

        SecureCrdtTestNode()                                          = default;
        SecureCrdtTestNode( const SecureCrdtTestNode & )               = delete;
        SecureCrdtTestNode &operator=( const SecureCrdtTestNode & )    = delete;
        SecureCrdtTestNode( SecureCrdtTestNode && ) noexcept           = default;
        SecureCrdtTestNode &operator=( SecureCrdtTestNode && ) noexcept = default;

        ~SecureCrdtTestNode()
        {
            if ( io )
            {
                io->stop();
            }
            if ( ioThread.joinable() )
            {
                ioThread.join();
            }
            if ( pubsub )
            {
                pubsub->Stop();
            }
            db.reset();
            io.reset();
        }
    };

    /// @brief Builds one unconnected GlobalDB test node named `dbName` for the
    ///        currently-running gtest test case.
    inline std::unique_ptr<SecureCrdtTestNode> MakeSecureCrdtTestNode( const std::string &dbName )
    {
        EnsureLoggingSystemConfigured();
        const std::string testName   = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        const std::string binaryPath = boost::dll::program_location().parent_path().string();
        const std::string basePath   = binaryPath + "/" + dbName + "_" + testName;
        boost::filesystem::remove_all( basePath );
        boost::filesystem::create_directories( basePath );

        sgns::crdt::KeyPairFileStorage keyStore( basePath + "/key" );
        auto                           keyPair  = keyStore.GetKeyPair().value();
        auto                           pubsub   = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( keyPair );
        const std::string              listenIp = "0.0.0.0";
        const auto startError = pubsub->Start( 0, {}, listenIp, {} ).get();
        if ( startError )
        {
            return nullptr;
        }

        auto io        = std::make_shared<boost::asio::io_context>();
        auto scheduler = std::make_shared<libp2p::basic::SchedulerImpl>(
            std::make_shared<libp2p::basic::AsioSchedulerBackend>( io ),
            libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
        auto graphsyncnetwork =
            std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubsub->GetHost(), scheduler );
        auto generator = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();

        auto globaldb_ret = sgns::crdt::GlobalDB::New( io, basePath + "/CommonKey", pubsub,
                                                       sgns::crdt::CrdtOptions::DefaultOptions(), graphsyncnetwork,
                                                       scheduler, generator );
        if ( globaldb_ret.has_error() )
        {
            return nullptr;
        }

        auto node    = std::make_unique<SecureCrdtTestNode>();
        node->basePath = basePath;
        node->io        = io;
        node->pubsub    = pubsub;
        node->db        = std::move( globaldb_ret.value() );
        node->db->Start();
        node->ioThread = std::thread( [io]() { io->run(); } );
        return node;
    }
} // namespace sgns::test::securecrdt

#endif // SGNS_TEST_SECURECRDT_TEST_NODE_HPP
