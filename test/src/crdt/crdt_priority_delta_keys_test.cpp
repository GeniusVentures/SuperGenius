/**
 * @file       crdt_priority_delta_keys_test.cpp
 * @brief      Read-exposure passthrough tests on a real single-node store:
 *             GlobalDB::GetKeyPriority and GlobalDB::GetLocalDeltaKeys.
 *
 * Proves the two Wave-0 CRDT read chains the eligibility evaluator needs:
 * the per-key priority read (17-04 spend-recency window) and the
 * elements-AND-tombstones delta-key enumeration (17-05 multi-element block
 * gate). Both exposures are read-only passthroughs; nothing here writes
 * CRDT state except the seeding Puts/Remove on a real topic (an empty-topics
 * Put creates no destinations -- Phase-16 lesson).
 */

#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <boost/dll.hpp>
#include <boost/asio/io_context.hpp>
#include <chrono>
#include <algorithm>
#include <libp2p/basic/scheduler.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <thread>

#include "crdt/globaldb/globaldb.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/hierarchical_key.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "base/buffer.hpp"
#include "base/logger.hpp"
#include "testutil/outcome.hpp"
#include "testutil/remove_all.hpp"
#include "testutil/globaldb_node_shutdown.hpp"

#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/local_requests.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/multi/multihash.hpp>

namespace
{
    std::string GetLoggingSystem( const std::string & )
    {
        return R"(
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
    }

    /// Guaranteed-unparseable CID string: 'n' is not a multibase prefix, so
    /// CID::fromString fails with UNSUPPORTED_BASE before any block access.
    constexpr char kGarbageCid[] = "not-a-cid";
} // namespace

class CrdtPriorityDeltaKeysTest : public ::testing::Test
{
public:
    /// Single real-store node (globaldb_erase_test.cpp EraseNodeCollection
    /// wiring): fresh per-test temp dir, full GlobalDB::New stack with the
    /// scheduler on the pubsub context, Start(), ShutdownNow teardown.
    class SingleNodeCollection
    {
    public:
        struct Node
        {
            std::string                                      basePath;
            std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub;
            std::shared_ptr<sgns::crdt::GlobalDB>            db;

            Node()                               = default;
            Node( const Node & )                 = delete;
            Node &operator=( const Node & )      = delete;
            Node( Node && ) noexcept             = default;
            Node &operator=( Node && ) noexcept  = default;
        };

        void addNode( const std::string &dbName )
        {
            const std::string testName   = ::testing::UnitTest::GetInstance()->current_test_info()->name();
            const std::string binaryPath = boost::dll::program_location().parent_path().string();
            const std::string basePath   = binaryPath + "/" + dbName + "_" + testName;
            sgns::test::removeAllWithRetry( basePath );
            boost::filesystem::create_directories( basePath );

            sgns::crdt::KeyPairFileStorage keyStore( basePath + "/key" );
            auto                           keyPair = keyStore.GetKeyPair().value();
            auto                           pubsub  = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( keyPair );
            const std::string              listenIp = "0.0.0.0";
            const auto                     startError = pubsub->Start( 0, {}, listenIp, {} ).get();
            ASSERT_FALSE( startError ) << "Could not start GlobalDB test node: " << startError.message();

            // GraphSync writes to libp2p streams from its scheduler thread, and libp2p
            // is single-threaded per host, so the scheduler has to run on the host's
            // io_context. A private one here races yamux's WriteQueue.
            auto io        = pubsub->GetAsioContext();
            auto scheduler = std::make_shared<libp2p::basic::SchedulerImpl>(
                std::make_shared<libp2p::basic::AsioSchedulerBackend>( pubsub->GetAsioContext() ),
                libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
            auto graphsyncnetwork = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubsub->GetHost(),
                                                                                                 scheduler );
            auto generator        = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();

            auto globaldb_ret = sgns::crdt::GlobalDB::New( io,
                                                           basePath + "/CommonKey",
                                                           pubsub,
                                                           sgns::crdt::CrdtOptions::DefaultOptions(),
                                                           graphsyncnetwork,
                                                           scheduler,
                                                           generator );
            if ( globaldb_ret.has_error() )
            {
                return;
            }
            auto db = std::move( globaldb_ret.value() );

            db->Start();
            nodes_.push_back( Node{ basePath, pubsub, db } );
        }

        ~SingleNodeCollection()
        {
            for ( auto &node : nodes_ )
            {
                sgns::test::stopGlobalDBNode( node.db, node.pubsub );
            }

            nodes_.clear();
        }

        std::vector<Node> &getNodes()
        {
            return nodes_;
        }

    private:
        std::vector<Node> nodes_;
    };

    static void SetUpTestSuite()
    {
        const std::string binaryPath         = boost::dll::program_location().parent_path().string();
        const std::string loggingYAML        = GetLoggingSystem( binaryPath + "/globaldbtest" );
        auto              loggerConfigurator = std::make_shared<libp2p::log::Configurator>();
        auto       configFromYAML = std::make_shared<soralog::ConfiguratorFromYAML>( loggerConfigurator, loggingYAML );
        auto       loggingSystem  = std::make_shared<soralog::LoggingSystem>( configFromYAML );
        const auto confResult     = loggingSystem->configure();
        if ( confResult.has_error )
        {
            throw std::runtime_error( "Could not configure logger" );
        }
        libp2p::log::setLoggingSystem( loggingSystem );
        auto nodeLogger = sgns::base::createLogger( "SuperGeniusDemo", binaryPath + "/sgnslog2.log" );
        nodeLogger->set_level( spdlog::level::err );
        const auto loggerGlobalDB    = sgns::base::createLogger( "GlobalDB" );
        const auto loggerBroadcaster = sgns::base::createLogger( "PubSubBroadcasterExt" );
        const auto loggerDataStore   = sgns::base::createLogger( "CrdtDatastore" );
        loggerGlobalDB->set_level( spdlog::level::err );
        loggerBroadcaster->set_level( spdlog::level::err );
        loggerDataStore->set_level( spdlog::level::off );
    }

    /// Brings up one real-store node listening on the shared topic and
    /// returns it; every test starts from this identical posture. The
    /// collection is non-copyable (real pubsub/db handles), so it is returned
    /// via unique_ptr exactly like the erase-test analog.
    std::unique_ptr<SingleNodeCollection> MakeStartedNode( const std::string &dbName )
    {
        auto nodes = std::make_unique<SingleNodeCollection>();
        nodes->addNode( dbName );
        EXPECT_EQ( nodes->getNodes().size(), 1 );
        if ( nodes->getNodes().empty() )
        {
            return nodes;
        }
        auto &db = *nodes->getNodes().front().db;
        EXPECT_FALSE( db.AddBroadcastTopic( kTopic ).has_error() );
        db.AddListenTopic( kTopic );
        return nodes;
    }

    static constexpr char kTopic[] = "priority_delta_keys_topic";
};

TEST_F( CrdtPriorityDeltaKeysTest, PriorityPassthroughReturnsPutPriority )
{
    auto nodes = MakeStartedNode( "crdt_priority_passthrough_put" );
    ASSERT_EQ( nodes->getNodes().size(), 1 );
    auto &db = *nodes->getNodes().front().db;

    const std::string key_path = "/priority/passthrough/keyA";
    sgns::base::Buffer value;
    value.put( "valueA" );
    auto put_cid = db.Put( sgns::crdt::HierarchicalKey( key_path ), value, { kTopic } );
    ASSERT_TRUE( put_cid.has_value() );

    // The Put path records the delta's height as its head's priority, so the
    // head height IS the priority the Put recorded for the key.
    auto head_height = db.GetCRDTHeadHeight( put_cid.value(), kTopic );
    ASSERT_TRUE( head_height.has_value() );

    auto priority = db.GetKeyPriority( key_path );
    ASSERT_TRUE( priority.has_value() );
    EXPECT_EQ( priority.value(), head_height.value() );

    // Stability: a second read reports the same value.
    auto second_read = db.GetKeyPriority( key_path );
    ASSERT_TRUE( second_read.has_value() );
    EXPECT_EQ( second_read.value(), priority.value() );
}

TEST_F( CrdtPriorityDeltaKeysTest, PriorityPassthroughToleratesMissingKey )
{
    auto nodes = MakeStartedNode( "crdt_priority_passthrough_missing" );
    ASSERT_EQ( nodes->getNodes().size(), 1 );
    auto &db = *nodes->getNodes().front().db;

    // Documented crdt_set semantics the caveat-carrying callers must know:
    // a never-put key reads as SUCCESS with value 0, never as failure --
    // 0 does not imply existence.
    auto priority = db.GetKeyPriority( "/priority/passthrough/never-put" );
    ASSERT_TRUE( priority.has_value() );
    EXPECT_EQ( priority.value(), 0u );
}

TEST_F( CrdtPriorityDeltaKeysTest, DeltaKeysCoverElementsAndTombstones )
{
    auto nodes = MakeStartedNode( "crdt_delta_keys_elements_tombstones" );
    ASSERT_EQ( nodes->getNodes().size(), 1 );
    auto &db = *nodes->getNodes().front().db;

    const std::string key_a = "/priority/delta/elementA";
    const std::string key_b = "/priority/delta/elementB";
    sgns::base::Buffer value_a;
    value_a.put( "valueA" );
    sgns::base::Buffer value_b;
    value_b.put( "valueB" );

    // One delta block carrying TWO elements (single atomic batch Put).
    auto elements_cid = db.Put( std::vector<sgns::crdt::GlobalDB::DataPair>{
                                     { sgns::crdt::HierarchicalKey( key_a ), value_a },
                                     { sgns::crdt::HierarchicalKey( key_b ), value_b } },
                                 { kTopic } );
    ASSERT_TRUE( elements_cid.has_value() );

    // The CRDT-layer Remove creates the tombstone delta for key_a.
    auto tombstone_cid = db.Remove( sgns::crdt::HierarchicalKey( key_a ), { kTopic } );
    ASSERT_TRUE( tombstone_cid.has_value() );

    // Element block: exactly the two element keys, keys-only.
    auto element_keys = db.GetLocalDeltaKeys( elements_cid.value().toString().value() );
    ASSERT_TRUE( element_keys.has_value() );
    ASSERT_EQ( element_keys.value().size(), 2u );
    auto sorted_keys = element_keys.value();
    std::sort( sorted_keys.begin(), sorted_keys.end() );
    EXPECT_EQ( sorted_keys, ( std::vector<std::string>{ key_a, key_b } ) );

    // Tombstone block: exactly the tombstoned key. An elements-only reader
    // (the GetLocalDeltaKeyValues gap) returns NOTHING here -- this is the
    // PRUNE-03 exposure being closed.
    auto tombstone_keys = db.GetLocalDeltaKeys( tombstone_cid.value().toString().value() );
    ASSERT_TRUE( tombstone_keys.has_value() );
    ASSERT_EQ( tombstone_keys.value().size(), 1u );
    EXPECT_EQ( tombstone_keys.value().front(), key_a );
}

TEST_F( CrdtPriorityDeltaKeysTest, DeltaKeysFailClosedOnUnknownCid )
{
    auto nodes = MakeStartedNode( "crdt_delta_keys_unknown_cid" );
    ASSERT_EQ( nodes->getNodes().size(), 1 );
    auto &db = *nodes->getNodes().front().db;

    // A garbage CID string must fail closed, never return empty-success.
    auto keys = db.GetLocalDeltaKeys( kGarbageCid );
    EXPECT_TRUE( keys.has_error() );
}
