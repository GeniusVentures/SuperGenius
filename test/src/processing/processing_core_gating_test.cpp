/**
 * @file       processing_core_gating_test.cpp
 * @brief      Unit tests for the gated per-subtask processing host composition
 *             (D-11): Noise-only security, deny-list connection gater, pnet
 *             binding for configured keys, eager failure on invalid key
 *             material, and unchanged public-mode construction via defaulted
 *             arguments.
 *
 *             DESCOPED by owner order (2026-09-02, deferred-items.md 3): the
 *             membership allow-list admission/rejection case is NOT tested -
 *             membership enforcement moves to the SuperGenius application
 *             layer, not the libp2p gater.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <libp2p/log/configurator.hpp>
#include <libp2p/host/host.hpp>
#include <ipfs_pubsub/deny_list_connection_gater.hpp>

#include "processing/impl/processing_core_impl.hpp"
#include "processing/processing_task_queue.hpp"

namespace
{
    /// libp2p components constructed by the injector create loggers eagerly, so
    /// the logging system must exist before the first host composition (same
    /// setup as pubsub_counts).
    const std::string logger_config( R"(
    # ----------------
    sinks:
      - name: console
        type: console
        color: true
    groups:
      - name: processing_core_gating_test
        sink: console
        level: info
        children:
          - name: libp2p
    # ----------------
      )" );

    /// Well-formed PSK (same literal as pubsub_counts) - the positive pnet case.
    constexpr std::string_view SWARM_KEY_PNET = "/key/swarm/psk/1.0.0/\n/base16/"
                                                "000102030405060708090a0b0c0d0e0f101112131415161718"
                                                "191a1b1c1d1e1f\n";

    /// Malformed PSK: swarm-key framing with a non-hex payload. The injector's
    /// usePrivateNetwork must reject it eagerly (PskValidationError) before any
    /// host can be assembled - the exact contract ProcessingCoreImpl catches.
    const std::string SWARM_KEY_INVALID = "/key/swarm/psk/1.0.0/\n/base16/\nZZZZ\n";

    /// Kademlia configuration identical to the one ProcessSubTask uses.
    libp2p::protocol::kademlia::Config MakeKademliaConfig()
    {
        libp2p::protocol::kademlia::Config config;
        config.randomWalk.enabled  = true;
        config.randomWalk.interval = std::chrono::seconds( 300 );
        config.requestConcurency   = 20;
        return config;
    }

    std::shared_ptr<sgns::ipfs_pubsub::DenyListConnectionGater> MakeGater()
    {
        return std::make_shared<sgns::ipfs_pubsub::DenyListConnectionGater>();
    }

    /// Minimal task-queue stand-in so ProcessingCoreImpl::New can be exercised
    /// without a GlobalDB-backed queue. No method is ever called by these tests.
    class FakeTaskQueue : public sgns::processing::ProcessingTaskQueue
    {
    public:
        outcome::result<void> EnqueueTask( const SGProcessing::Task &,
                                           const std::list<SGProcessing::SubTask> &,
                                           std::shared_ptr<sgns::crdt::AtomicTransaction> ) override
        {
            return outcome::success();
        }

        outcome::result<SGProcessing::Task> GetTask( const std::string & ) override
        {
            return outcome::failure( std::error_code() );
        }

        bool GetSubTasks( const std::string &, std::list<SGProcessing::SubTask> &subTasks ) override
        {
            subTasks.clear();
            return false;
        }

        outcome::result<std::pair<std::string, SGProcessing::Task>> GrabTask() override
        {
            return outcome::failure( std::error_code() );
        }

        outcome::result<std::shared_ptr<sgns::crdt::AtomicTransaction>> CompleteTask(
            const std::string &,
            const SGProcessing::TaskResult & ) override
        {
            return outcome::failure( std::error_code() );
        }

        bool IsTaskCompleted( const std::string & ) override
        {
            return false;
        }

        void MarkTaskBad( const std::string & ) override {}

        std::vector<std::string> ListTaskKeys() override
        {
            return {};
        }

        outcome::result<SGProcessing::TaskResult> GetTaskResult( const std::string & ) override
        {
            return outcome::failure( std::error_code() );
        }
    };
} // namespace

/// Test fixture ensuring the libp2p logging system exists before any injector
/// composition materializes libp2p components.
class ProcessingCoreGatingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        auto logging_system = std::make_shared<soralog::LoggingSystem>(
            std::make_shared<soralog::ConfiguratorFromYAML>(
                // Original LibP2P logging config
                std::make_shared<libp2p::log::Configurator>(),
                // Additional logging config for application
                logger_config ) );
        logging_system->configure();

        libp2p::log::setLoggingSystem( logging_system );
        libp2p::log::setLevelOfGroup( "processing_core_gating_test", soralog::Level::OFF );
    }
};

/// Empty network key: the composition stays public (no pnet binding) and still
/// builds a working host with the gated binding set (Noise-only + gater).
TEST_F( ProcessingCoreGatingTest, EmptyKeyBuildsPublicHost )
{
    auto context = sgns::processing::ProcessingCoreImpl::MakeGatedHostInjector( "", MakeGater(), MakeKademliaConfig() );

    ASSERT_NE( context.io_context, nullptr );
    ASSERT_TRUE( context.make_host );

    auto host = context.make_host();
    ASSERT_NE( host, nullptr );
    EXPECT_FALSE( host->getId().toBase58().empty() );

    // The shared-config composition materializes one host instance per injector.
    EXPECT_EQ( context.make_host(), host );
}

/// A well-formed network key: the pnet binding composes and the host builds.
TEST_F( ProcessingCoreGatingTest, ValidKeyBuildsPnetHost )
{
    auto context = sgns::processing::ProcessingCoreImpl::MakeGatedHostInjector( std::string( SWARM_KEY_PNET ),
                                                                               MakeGater(),
                                                                               MakeKademliaConfig() );

    ASSERT_NE( context.io_context, nullptr );
    ASSERT_TRUE( context.make_host );

    auto host = context.make_host();
    ASSERT_NE( host, nullptr );
    EXPECT_FALSE( host->getId().toBase58().empty() );
}

/// Invalid key material must fail EAGERLY as a std::exception (PskValidationError)
/// - the contract ProcessSubTask catches and maps to PNET_INITIALIZATION_ERROR.
TEST_F( ProcessingCoreGatingTest, InvalidKeyFailsEagerly )
{
    EXPECT_THROW( {
        auto context = sgns::processing::ProcessingCoreImpl::MakeGatedHostInjector( SWARM_KEY_INVALID,
                                                                                   MakeGater(),
                                                                                   MakeKademliaConfig() );
        (void)context;
    },
                  std::exception );
}

/// Defaulted construction arguments keep the public path: New without a key and
/// New with an explicitly empty key both construct a usable core (regression
/// pin for public nodes after D-11).
TEST_F( ProcessingCoreGatingTest, DefaultArgumentsKeepPublicConstruction )
{
    auto queue = std::make_shared<FakeTaskQueue>();
    ASSERT_NE( queue, nullptr );

    auto core_defaulted = sgns::processing::ProcessingCoreImpl::New( queue, 1, sgns::TokenID{} );
    ASSERT_NE( core_defaulted, nullptr );
    EXPECT_FLOAT_EQ( core_defaulted->GetProgress(), 0.0f );

    auto core_public = sgns::processing::ProcessingCoreImpl::New( queue, 1, sgns::TokenID{}, "" );
    ASSERT_NE( core_public, nullptr );
    EXPECT_FLOAT_EQ( core_public->GetProgress(), 0.0f );
}
