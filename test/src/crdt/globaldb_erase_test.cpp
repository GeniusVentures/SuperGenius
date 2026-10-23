/**
 * @file       globaldb_erase_test.cpp
 * @brief      End-to-end erase tests for GlobalDB::EraseEntry.
 *
 * Single real pubsub node per test (fresh database via removeAllWithRetry);
 * proves SC-1 (three-part assertions + negative control), SC-2 (the three
 * interruption-step crash injections), and SC-3 (head and resolved guard
 * rejections) through the facade API on the real compose path. The
 * Redelivery/RefusalFailover/PrunedMesh suites ride the N-node
 * EraseNodeCollection topology: pruned-root redelivery is terminal (18-01),
 * a pruned server's clean refusal fails over per-CID to a serving peer
 * (19-07, SURV-02), and a fresh node bootstraps a pruned mesh through the
 * full node's serving broadcast (19-07, SURV-03).
 */

#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <boost/dll.hpp>
#include <boost/asio/io_context.hpp>
#include <chrono>
#include <libp2p/basic/scheduler.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <thread>

#include "crdt/globaldb/globaldb.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/crdt_datastore.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
#include "crdt/hierarchical_key.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "base/buffer.hpp"
#include "base/logger.hpp"
#include "testutil/outcome.hpp"
#include "testutil/wait_condition.hpp"
#include "testutil/remove_all.hpp"

#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <libp2p/host/host.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/local_requests.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/multi/multihash.hpp>

#include "base/sgns_version.hpp"

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

#define WAIT_TIMEOUT ( std::chrono::milliseconds( 25000 ) )

    /// Test-fabricated CID (never published, never sealed) for the
    /// unresolved-CID guard case.
    sgns::CID makeUnseenCid( const std::vector<uint8_t> &digest )
    {
        using libp2p::multi::HashType;
        using libp2p::multi::Multihash;
        return sgns::CID( sgns::CID::Version::V1,
                          sgns::CID::Multicodec::SHA2_256,
                          Multihash::create( HashType::sha256, digest ).value() );
    }
} // namespace

class GlobalDBEraseTest : public ::testing::Test
{
public:
    /// Single-node collection (globaldb_integration.cpp addNode recipe).
    class EraseNodeCollection
    {
    public:
        struct EraseNode
        {
            std::string                                      basePath;
            std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub;
            std::shared_ptr<sgns::crdt::GlobalDB>            db;

            EraseNode()                                   = default;
            EraseNode( const EraseNode & )                = delete;
            EraseNode &operator=( const EraseNode & )     = delete;
            EraseNode( EraseNode && ) noexcept            = default;
            EraseNode &operator=( EraseNode && ) noexcept = default;
        };

        void addNode( const std::string &dbName )
        {
            const std::string testName   = ::testing::UnitTest::GetInstance()->current_test_info()->name();
            const std::string binaryPath = boost::dll::program_location().parent_path().string();
            const std::string basePath   = binaryPath + "/" + dbName + "_" + testName;
            sgns::test::removeAllWithRetry( basePath );
            boost::filesystem::create_directories( basePath );

            EraseNode node = makeNode( basePath, /*start=*/true );
            if ( node.db )
            {
                nodes_.push_back( std::move( node ) );
            }
        }

        /// Tears node @p index down (db FIRST, then pubsub -- the graphsync
        /// chain co-owns the host wired from the pubsub, so releasing the db
        /// before pubsub->Stop() is the only teardown order that never
        /// deregisters a connection against a dead reactor) and recreates it
        /// on the SAME base path WITHOUT db->Start(). The caller reconnects
        /// peers and then Start()s, so the restarted head exchange's first
        /// RebroadcastHeads publishes into a live connection (an immediate
        /// start would broadcast into an empty peer set and the next cycle
        /// only fires after the 60s default interval).
        void restartNodePendingStart( size_t index )
        {
            ASSERT_LT( index, nodes_.size() );
            EraseNode &old = nodes_[index];
            if ( old.db )
            {
                old.db->ShutdownNow();
            }
            old.db.reset();
            if ( old.pubsub )
            {
                old.pubsub->Stop();
            }
            const std::string basePath = old.basePath;
            nodes_[index]              = makeNode( basePath, /*start=*/false );
        }

        /// Dials @p to_idx from @p from_idx (ONE direction only: two dials
        /// inside the same gossip heartbeat collide on the shared source
        /// tuple, the multi_node_finality_fault_test ConnectAndWaitForPeers
        /// lesson) and waits for a connected host pair.
        void connectNodes( size_t fromIdx, size_t toIdx )
        {
            ASSERT_LT( fromIdx, nodes_.size() );
            ASSERT_LT( toIdx, nodes_.size() );
            auto &from = nodes_[fromIdx];
            auto &to   = nodes_[toIdx];
            ASSERT_TRUE( from.pubsub && to.pubsub );
            from.pubsub->AddPeers( { to.pubsub->GetInterfaceAddress() } );
            auto from_host = from.pubsub->GetHost();
            auto to_info   = to.pubsub->GetHost()->getPeerInfo();
            ASSERT_WAIT_FOR_CONDITION(
                [&] { return from_host->connectedness( to_info ) == libp2p::Host::Connectedness::CONNECTED; },
                std::chrono::seconds( 10 ),
                "redelivery nodes form one connected libp2p pair",
                nullptr );
        }

        /// Creates one GlobalDB node (key file + pubsub + graphsync + db) on
        /// the given base path. The pubsub advertises the loopback address:
        /// the broadcast message carries the sender's PeerInfo and a
        /// wildcard-only listener yields no addresses, so every Broadcast
        /// would silently drop ("No addresses found",
        /// pubsub_broadcaster_ext.cpp) and receivers could never fetch.
        /// Returns a node with a null db on creation failure.
        EraseNode makeNode( const std::string &basePath, bool start )
        {
            EraseNode node;
            node.basePath = basePath;

            sgns::crdt::KeyPairFileStorage keyStore( basePath + "/key" );
            auto                           keyPair = keyStore.GetKeyPair().value();
            node.pubsub                            = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( keyPair );
            const std::string listenIp             = "0.0.0.0";
            const auto        startError           = node.pubsub->Start( 0, {}, listenIp, { "127.0.0.1" } ).get();
            EXPECT_FALSE( startError ) << "Could not start GlobalDB test node: " << startError.message();
            if ( startError )
            {
                return node;
            }

            // GraphSync writes to libp2p streams from its scheduler thread, and libp2p
            // is single-threaded per host, so the scheduler has to run on the host's
            // io_context. A private one here races yamux's WriteQueue.
            auto io        = node.pubsub->GetAsioContext();
            auto scheduler = std::make_shared<libp2p::basic::SchedulerImpl>(
                std::make_shared<libp2p::basic::AsioSchedulerBackend>( node.pubsub->GetAsioContext() ),
                libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
            auto graphsyncnetwork = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( node.pubsub->GetHost(),
                                                                                                 scheduler );
            auto generator        = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();

            auto globaldb_ret = sgns::crdt::GlobalDB::New( io,
                                                           basePath + "/CommonKey",
                                                           node.pubsub,
                                                           sgns::crdt::CrdtOptions::DefaultOptions(),
                                                           graphsyncnetwork,
                                                           scheduler,
                                                           generator );
            if ( globaldb_ret.has_error() )
            {
                return node;
            }
            node.db = std::move( globaldb_ret.value() );

            if ( start )
            {
                node.db->Start();
            }
            return node;
        }

        ~EraseNodeCollection()
        {
            for ( auto &node : nodes_ )
            {
                // Always clear the interruption seam before teardown: a hook
                // left armed from a failed case must never leak further.
                sgns::crdt::GlobalDB::SetEraseInterruptHookForTest( nullptr );
                if ( node.db )
                {
                    node.db->ShutdownNow();
                }
                node.db.reset();
                node.pubsub->Stop();
            }

            nodes_.clear();
        }

        std::vector<EraseNode> &getNodes()
        {
            return nodes_;
        }

    private:
        std::vector<EraseNode> nodes_;
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

    /// One seeded entry: the delta CID of the put (head at its moment) and
    /// the CRDT element key, which doubles as the raw tx->CID map key.
    struct SeededEntry
    {
        sgns::CID   cid;
        std::string tx_path;
        std::string value;
    };

    /// Puts one value through the public CRDT path on the topic and returns
    /// the delta CID. Put returns only after the self-created root job
    /// completed (heads updated, seal written by UpdateCRDTHeads).
    static SeededEntry SeedEntry( sgns::crdt::GlobalDB &db, const std::string &tx_path, const std::string &value )
    {
        sgns::base::Buffer value_buffer;
        value_buffer.put( value );
        auto put_result = db.Put( sgns::crdt::HierarchicalKey( tx_path ), value_buffer, { kTopic } );
        EXPECT_TRUE( put_result.has_value() );
        return { put_result.value(), tx_path, value };
    }

    static bool headListContains( sgns::crdt::GlobalDB &db, const sgns::CID &cid )
    {
        auto heads = db.GetCRDTHeadList();
        if ( heads.has_error() )
        {
            return false;
        }
        for ( const auto &[topic, topic_heads] : heads.value().first )
        {
            if ( topic_heads.count( cid ) > 0 )
            {
                return true;
            }
        }
        return false;
    }

    /// Raw seal-key presence through the raw facade. The seal marker key is
    /// cid.toString() plus the four-char suffix built by its ONE production
    /// home, the vendored RocksdbDatastore::seal() (datastore_rocksdb.cpp);
    /// production code never constructs it -- this test builds it ONLY to
    /// assert the erase left it in place (GATE-03).
    static bool rawSealKeyPresent( sgns::crdt::GlobalDB &db, const sgns::CID &cid )
    {
        auto key = db.GetRaw( sgns::base::Buffer{}.put( cid.toString().value() + "sealed" ) );
        return key.has_value();
    }

    /// Raw block-bytes key (vendored contract: Buffer(cid.toBytes()) is the
    /// one home of the block-key format, datastore_rocksdb.cpp encode()).
    static sgns::base::Buffer blockKeyOf( const sgns::CID &cid )
    {
        return sgns::base::Buffer( cid.toBytes().value() );
    }

    /// Counts raw CRDT-namespace entries whose key contains @p needle (any
    /// of the four families of the element key). HierarchicalKey always
    /// carries the leading slash, so the raw namespace prefix is "/crdt/".
    static size_t countCrdtRawEntries( sgns::crdt::GlobalDB &db, const std::string &needle )
    {
        auto entries = db.QueryRaw( sgns::base::Buffer{}.put( std::string( "/crdt/" ) ) );
        if ( entries.has_error() )
        {
            return 0;
        }
        size_t count = 0;
        for ( const auto &[key, value] : entries.value() )
        {
            if ( key.toString().find( needle ) != std::string::npos )
            {
                ++count;
            }
        }
        return count;
    }

    static constexpr char kTopic[] = "erase_e2e_topic";
};

TEST_F( GlobalDBEraseTest, EraseEntryRemovesAllLevelsEndToEnd )
{
    auto testNodes = std::make_unique<EraseNodeCollection>();
    testNodes->addNode( "globaldb_erase_e2e" );
    ASSERT_EQ( testNodes->getNodes().size(), 1 );
    auto &db = *testNodes->getNodes().front().db;
    ASSERT_FALSE( db.AddBroadcastTopic( kTopic ).has_error() );
    db.AddListenTopic( kTopic );

    // Two deltas: A (erasable once retired) and B (retires A, stays head).
    const auto entry_a = SeedEntry( db, "/erase/entryA", "VALUE_A" );
    ASSERT_TRUE( headListContains( db, entry_a.cid ) );
    const auto entry_b = SeedEntry( db, "/erase/entryB", "VALUE_B" );
    ASSERT_TRUE( headListContains( db, entry_b.cid ) );

    // The put CID of A is no longer any topic's head (retired by B's
    // ancestry) -- the precondition that makes it erasable at all.
    ASSERT_FALSE( headListContains( db, entry_a.cid ) );

    // Seed the tx->CID map entry with the EXACT key/value shapes
    // StoreTransactionCID writes: key_buffer.put( key ) with the bare tx
    // path and value_buffer.put( cid ) with the cid STRING -- never the
    // CID's raw toBytes() (TransactionManager.cpp:4369-4392).
    ASSERT_TRUE( db.PutRaw( sgns::base::Buffer{}.put( entry_a.tx_path ),
                            sgns::base::Buffer{}.put( entry_a.cid.toString().value() ) )
                     .has_value() );

    // Present-before asserts on every level.
    EXPECT_EQ( db.Get( sgns::crdt::HierarchicalKey( entry_a.tx_path ) ).value().toString(), entry_a.value );
    EXPECT_GE( countCrdtRawEntries( db, "erase/entryA" ), 3 ); // elems + value + priority
    EXPECT_TRUE( db.GetRaw( blockKeyOf( entry_a.cid ) ).has_value() );
    EXPECT_EQ( db.GetRaw( sgns::base::Buffer{}.put( entry_a.tx_path ) ).value().toString(),
               entry_a.cid.toString().value() );
    EXPECT_TRUE( rawSealKeyPresent( db, entry_a.cid ) );
    const std::string sibling_block_hex_before = db.GetRaw( blockKeyOf( entry_b.cid ) ).value().toHex();

    // The erase itself, through the one facade API.
    EXPECT_OUTCOME_TRUE_1( db.EraseEntry( entry_a.tx_path, entry_a.cid ) );

    // Absent-after: CRDT entries, raw block bytes, map entry.
    EXPECT_TRUE( db.Get( sgns::crdt::HierarchicalKey( entry_a.tx_path ) ).has_error() );
    EXPECT_EQ( countCrdtRawEntries( db, "erase/entryA" ), 0 );
    EXPECT_TRUE( db.GetRaw( blockKeyOf( entry_a.cid ) ).has_error() );
    EXPECT_TRUE( db.GetRaw( sgns::base::Buffer{}.put( entry_a.tx_path ) ).has_error() );

    // The seal marker survives the committed erase (GATE-03).
    EXPECT_TRUE( rawSealKeyPresent( db, entry_a.cid ) );

    // Negative control: sibling entry B is byte-identical on both levels and
    // is still the head.
    EXPECT_EQ( db.Get( sgns::crdt::HierarchicalKey( entry_b.tx_path ) ).value().toString(), entry_b.value );
    EXPECT_EQ( db.GetRaw( blockKeyOf( entry_b.cid ) ).value().toHex(), sibling_block_hex_before );
    EXPECT_TRUE( rawSealKeyPresent( db, entry_b.cid ) );
    EXPECT_TRUE( headListContains( db, entry_b.cid ) );
    EXPECT_FALSE( headListContains( db, entry_a.cid ) );
}

class GlobalDBEraseInterruptTest : public GlobalDBEraseTest, public ::testing::WithParamInterface<int>
{
};

TEST_P( GlobalDBEraseInterruptTest, InterruptedEraseIsAllOrNothing )
{
    const int step_target = GetParam(); // 1 = after CRDT staging, 2 = after block staging, 3 = after map staging

    auto testNodes = std::make_unique<EraseNodeCollection>();
    testNodes->addNode( "globaldb_erase_interrupt" );
    ASSERT_EQ( testNodes->getNodes().size(), 1 );
    auto &db = *testNodes->getNodes().front().db;
    ASSERT_FALSE( db.AddBroadcastTopic( kTopic ).has_error() );
    db.AddListenTopic( kTopic );

    const auto entry_a = SeedEntry( db, "/erase/entryA", "VALUE_A" );
    const auto entry_b = SeedEntry( db, "/erase/entryB", "VALUE_B" );
    ASSERT_FALSE( headListContains( db, entry_a.cid ) );
    ASSERT_TRUE( db.PutRaw( sgns::base::Buffer{}.put( entry_a.tx_path ),
                            sgns::base::Buffer{}.put( entry_a.cid.toString().value() ) )
                     .has_value() );

    // Present-before snapshots.
    const std::string block_hex_before = db.GetRaw( blockKeyOf( entry_a.cid ) ).value().toHex();

    // Abort the compose at the requested step: the erase fails and the
    // dying uncommitted batch must leave EVERYTHING present.
    sgns::crdt::GlobalDB::SetEraseInterruptHookForTest( [step_target]( int step ) { return step == step_target; } );
    auto erased = db.EraseEntry( entry_a.tx_path, entry_a.cid );
    EXPECT_TRUE( erased.has_error() );
    sgns::crdt::GlobalDB::SetEraseInterruptHookForTest( nullptr );

    // All-present: never value-without-block, never block-without-map-entry.
    EXPECT_EQ( db.Get( sgns::crdt::HierarchicalKey( entry_a.tx_path ) ).value().toString(), entry_a.value );
    EXPECT_GE( countCrdtRawEntries( db, "erase/entryA" ), 3 );
    EXPECT_EQ( db.GetRaw( blockKeyOf( entry_a.cid ) ).value().toHex(), block_hex_before );
    EXPECT_EQ( db.GetRaw( sgns::base::Buffer{}.put( entry_a.tx_path ) ).value().toString(),
               entry_a.cid.toString().value() );
    EXPECT_TRUE( rawSealKeyPresent( db, entry_a.cid ) );
    EXPECT_TRUE( headListContains( db, entry_b.cid ) );
}

INSTANTIATE_TEST_SUITE_P( EraseInterruptSteps, GlobalDBEraseInterruptTest, ::testing::Values( 1, 2, 3 ) );

TEST_F( GlobalDBEraseTest, HeadCidIsRejected )
{
    auto testNodes = std::make_unique<EraseNodeCollection>();
    testNodes->addNode( "globaldb_erase_head_guard" );
    ASSERT_EQ( testNodes->getNodes().size(), 1 );
    auto &db = *testNodes->getNodes().front().db;
    ASSERT_FALSE( db.AddBroadcastTopic( kTopic ).has_error() );
    db.AddListenTopic( kTopic );

    const auto entry_a = SeedEntry( db, "/erase/entryA", "VALUE_A" );
    const auto entry_b = SeedEntry( db, "/erase/entryB", "VALUE_B" );
    ASSERT_TRUE( headListContains( db, entry_b.cid ) );

    const std::string block_hex_before = db.GetRaw( blockKeyOf( entry_b.cid ) ).value().toHex();

    // B's CID is the live head: rejected fail-closed, nothing erased.
    auto erased = db.EraseEntry( entry_b.tx_path, entry_b.cid );
    EXPECT_TRUE( erased.has_error() );
    EXPECT_TRUE( erased.error() == sgns::crdt::GlobalDB::Error::INVALID_PARAMETERS );

    // Value B fully intact: CRDT value, raw entries, block bytes, seal, head.
    EXPECT_EQ( db.Get( sgns::crdt::HierarchicalKey( entry_b.tx_path ) ).value().toString(), entry_b.value );
    EXPECT_GE( countCrdtRawEntries( db, "erase/entryB" ), 3 );
    EXPECT_EQ( db.GetRaw( blockKeyOf( entry_b.cid ) ).value().toHex(), block_hex_before );
    EXPECT_TRUE( rawSealKeyPresent( db, entry_b.cid ) );
    EXPECT_TRUE( headListContains( db, entry_b.cid ) );
}

TEST_F( GlobalDBEraseTest, UnresolvedCidIsRejected )
{
    auto testNodes = std::make_unique<EraseNodeCollection>();
    testNodes->addNode( "globaldb_erase_unresolved_guard" );
    ASSERT_EQ( testNodes->getNodes().size(), 1 );
    auto &db = *testNodes->getNodes().front().db;
    ASSERT_FALSE( db.AddBroadcastTopic( kTopic ).has_error() );
    db.AddListenTopic( kTopic );

    const auto entry_a = SeedEntry( db, "/erase/entryA", "VALUE_A" );
    const auto entry_b = SeedEntry( db, "/erase/entryB", "VALUE_B" );
    ASSERT_TRUE( db.PutRaw( sgns::base::Buffer{}.put( entry_a.tx_path ),
                            sgns::base::Buffer{}.put( entry_a.cid.toString().value() ) )
                     .has_value() );

    // A CID this node has never seen carries no seal marker: the resolved
    // guard rejects it before any staging.
    const auto unseen_cid = makeUnseenCid( { 0x81, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45,
                                             0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x81, 0x23, 0x45, 0x67, 0x89, 0xAB,
                                             0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF } );

    const std::string block_hex_before = db.GetRaw( blockKeyOf( entry_a.cid ) ).value().toHex();

    auto erased = db.EraseEntry( entry_a.tx_path, unseen_cid );
    EXPECT_TRUE( erased.has_error() );
    EXPECT_TRUE( erased.error() == sgns::crdt::GlobalDB::Error::INVALID_PARAMETERS );

    // Nothing changed for the real entry on any level.
    EXPECT_EQ( db.Get( sgns::crdt::HierarchicalKey( entry_a.tx_path ) ).value().toString(), entry_a.value );
    EXPECT_GE( countCrdtRawEntries( db, "erase/entryA" ), 3 );
    EXPECT_EQ( db.GetRaw( blockKeyOf( entry_a.cid ) ).value().toHex(), block_hex_before );
    EXPECT_EQ( db.GetRaw( sgns::base::Buffer{}.put( entry_a.tx_path ) ).value().toString(),
               entry_a.cid.toString().value() );
    EXPECT_TRUE( rawSealKeyPresent( db, entry_a.cid ) );
}

TEST_F( GlobalDBEraseTest, RootFetchAttemptsCounterIsAResettableZeroObservable )
{
    // SC-3's Wave-0 observable (18-01 Task 1): the fetch-attempt counter must
    // exist as static test-only accessors on CrdtDatastore, reset to zero, and
    // be readable without any datastore instance. RED gate: the build fails on
    // the missing members until EnqueueRootCID's counter lands.
    sgns::crdt::CrdtDatastore::ResetRootFetchAttemptsForTest();
    EXPECT_EQ( sgns::crdt::CrdtDatastore::GetRootFetchAttemptsForTest(), 0 );
}

/// SC-3 (PRUNE-05, D-02): a node that erased a root must treat the root's
/// re-delivery as terminal -- zero fetch attempts, zero re-created CRDT
/// elements -- through BOTH delivery modes (peer head re-broadcast and the
/// restarted head exchange), while unpruned roots still fetch.
///
/// Topology invariant: node A is DEAF (broadcast topics only, never a listen
/// topic on the prune topic), so nothing B broadcasts can advance A's heads.
/// That keeps the pruned root CID A's LIVE head forever -- exactly the
/// lagging-peer shape that re-delivers pruned roots in production.
class GlobalDBEraseRedeliveryTest : public GlobalDBEraseTest
{
protected:
    static constexpr char kTracerTopic[] = "erase_e2e_tracer_topic";

    /// The gossip-level topic name: the broadcaster appends the net/version
    /// appendix internally, and getPeerCount speaks the full name.
    static std::string fullTopicName( const std::string &topic )
    {
        return topic + sgns::version::GetNetAndVersionAppendix();
    }

    /// Seeds one value through the public CRDT path on an explicit topic
    /// (SeedEntry is fixed to the erase topic).
    static SeededEntry SeedEntryOn( sgns::crdt::GlobalDB                  &db,
                                    const std::string                     &tx_path,
                                    const std::string                     &value,
                                    const std::unordered_set<std::string> &topics )
    {
        sgns::base::Buffer value_buffer;
        value_buffer.put( value );
        auto put_result = db.Put( sgns::crdt::HierarchicalKey( tx_path ), value_buffer, topics );
        EXPECT_TRUE( put_result.has_value() );
        return { put_result.value(), tx_path, value };
    }

    /// Present-and-applied predicate for one seeded entry on a node: the
    /// CRDT value landed through the public read path.
    static bool entryApplied( sgns::crdt::GlobalDB &db, const SeededEntry &entry )
    {
        auto value = db.Get( sgns::crdt::HierarchicalKey( entry.tx_path ) );
        return value.has_value() && value.value().toString() == entry.value;
    }

    /// Seeds the shared prune shape: A (node 0) puts entryA on the prune
    /// topic; B (node 1) receives, applies, and seals it; B's own entryB put
    /// retires entryA on B ONLY (A is deaf and never hears it); B erases
    /// entryA through the one facade API. Returns the erased entry through
    /// @p entry_a. After this helper: A's live head IS the pruned root, and B
    /// holds value-less, block-less, map-less state with the seal intact.
    void seedPrunedRoot( EraseNodeCollection &nodes, const std::string &dbPrefix, SeededEntry &entry_a )
    {
        nodes.addNode( dbPrefix + "_a" );
        nodes.addNode( dbPrefix + "_b" );
        ASSERT_EQ( nodes.getNodes().size(), 2 );
        nodes.connectNodes( 0, 1 );
        auto &a_db = *nodes.getNodes()[0].db;
        auto &b_db = *nodes.getNodes()[1].db;

        ASSERT_FALSE( a_db.AddBroadcastTopic( kTopic ).has_error() );
        ASSERT_FALSE( b_db.AddBroadcastTopic( kTopic ).has_error() );
        b_db.AddListenTopic( kTopic );

        entry_a = SeedEntry( a_db, "/redel/entryA", "VALUE_A" );

        // B received, applied, and sealed the root.
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                auto value = b_db.Get( sgns::crdt::HierarchicalKey( entry_a.tx_path ) );
                return value.has_value() && value.value().toString() == entry_a.value &&
                       rawSealKeyPresent( b_db, entry_a.cid ) && headListContains( b_db, entry_a.cid );
            },
            WAIT_TIMEOUT,
            "seeding put reached the receiving node and wrote its seal",
            nullptr );

        // B's own put retires entryA on B; A (deaf) keeps it as a live head.
        const auto entry_b = SeedEntry( b_db, "/redel/entryB", "VALUE_B" );
        ASSERT_WAIT_FOR_CONDITION(
            [&] { return headListContains( b_db, entry_b.cid ) && !headListContains( b_db, entry_a.cid ); },
            WAIT_TIMEOUT,
            "retiring put advanced the receiving node's head past the pruned root",
            nullptr );
        EXPECT_TRUE( headListContains( a_db, entry_a.cid ) );
        EXPECT_FALSE( headListContains( a_db, entry_b.cid ) );

        // The tx->CID map entry in the exact StoreTransactionCID shape.
        ASSERT_TRUE( b_db.PutRaw( sgns::base::Buffer{}.put( entry_a.tx_path ),
                                  sgns::base::Buffer{}.put( entry_a.cid.toString().value() ) )
                         .has_value() );

        EXPECT_OUTCOME_TRUE_1( b_db.EraseEntry( entry_a.tx_path, entry_a.cid ) );

        // Absent-after on every level; the seal survives (GATE-03).
        EXPECT_TRUE( b_db.Get( sgns::crdt::HierarchicalKey( entry_a.tx_path ) ).has_error() );
        EXPECT_EQ( countCrdtRawEntries( b_db, "redel/entryA" ), 0 );
        EXPECT_TRUE( b_db.GetRaw( blockKeyOf( entry_a.cid ) ).has_error() );
        EXPECT_TRUE( b_db.GetRaw( sgns::base::Buffer{}.put( entry_a.tx_path ) ).has_error() );
        EXPECT_TRUE( rawSealKeyPresent( b_db, entry_a.cid ) );
    }

    /// Post-delivery invariants: the erased element was not re-created on any
    /// level, and the seal marker is still the terminal record.
    void assertNothingResurrected( sgns::crdt::GlobalDB &b_db, const SeededEntry &entry_a )
    {
        EXPECT_TRUE( b_db.Get( sgns::crdt::HierarchicalKey( entry_a.tx_path ) ).has_error() );
        EXPECT_EQ( countCrdtRawEntries( b_db, "redel/entryA" ), 0 );
        EXPECT_TRUE( b_db.GetRaw( blockKeyOf( entry_a.cid ) ).has_error() );
        EXPECT_TRUE( b_db.GetRaw( sgns::base::Buffer{}.put( entry_a.tx_path ) ).has_error() );
        EXPECT_TRUE( rawSealKeyPresent( b_db, entry_a.cid ) );
    }
};

/// Case 1 -- peer head re-broadcast of a pruned root: A's explicit head
/// broadcast re-delivers the pruned root CID to B on the topic B has been
/// subscribed to all along; the guard makes it terminal (zero fetch
/// attempts). The never-erased tracer head rides the SAME broadcast on a
/// topic B only joins afterwards -- gossip's message cache delivers it to B
/// on join, and its fetch proves B's intake is alive and still fetching
/// (non-vacuous zero), so the window's fetch-attempt delta must be exactly
/// one.
TEST_F( GlobalDBEraseRedeliveryTest, RebroadcastRedeliveryOfPrunedRootMakesZeroFetchAttempts )
{
    auto        testNodes = std::make_unique<EraseNodeCollection>();
    SeededEntry entry_a;
    seedPrunedRoot( *testNodes, "globaldb_redel_rebroadcast", entry_a );
    auto &a_db = *testNodes->getNodes()[0].db;
    auto &b_db = *testNodes->getNodes()[1].db;

    // The tracer head lives on a second topic B never joined, so B has
    // neither its block nor its seal: its delivery is a guaranteed fetch.
    ASSERT_FALSE( a_db.AddBroadcastTopic( kTracerTopic ).has_error() );
    const auto tracer = SeedEntryOn( a_db, "/redel/tracer", "TRACER_VALUE", { kTracerTopic } );

    sgns::crdt::CrdtDatastore::ResetRootFetchAttemptsForTest();
    EXPECT_OUTCOME_TRUE_1( a_db.GetCRDTDataStore()->BroadcastHeadsForTopics( { kTopic, kTracerTopic } ) );

    // Joining the tracer topic now lets the cached tracer broadcast through
    // to B -- the in-window fetch that proves the intake is live.
    b_db.AddListenTopic( kTracerTopic );
    ASSERT_WAIT_FOR_CONDITION( [&] { return sgns::crdt::CrdtDatastore::GetRootFetchAttemptsForTest() >= 1; },
                               WAIT_TIMEOUT,
                               "tracer head re-delivered through the same broadcast was fetched",
                               nullptr );

    // Exactly one fetch (the tracer). The pruned root's re-broadcast in the
    // same delivery window contributed ZERO.
    EXPECT_EQ( sgns::crdt::CrdtDatastore::GetRootFetchAttemptsForTest(), 1 );
    assertNothingResurrected( b_db, entry_a );
}

/// Case 2 -- restarted head exchange: node A (whose persisted heads still
/// name the pruned root, because A never heard the retiring delta) restarts;
/// its first RebroadcastHeads re-delivers the pruned root to B through the
/// one guarded intake. Same zero-fetch, zero-re-creation outcome.
TEST_F( GlobalDBEraseRedeliveryTest, RestartHeadExchangeRedeliveryOfPrunedRootMakesZeroFetchAttempts )
{
    auto        testNodes = std::make_unique<EraseNodeCollection>();
    SeededEntry entry_a;
    seedPrunedRoot( *testNodes, "globaldb_redel_restart", entry_a );
    auto &b_db = *testNodes->getNodes()[1].db;

    // A also holds a tracer head on a topic B never joined: after the
    // restart, the same first RebroadcastHeads delivers BOTH heads, and the
    // tracer's fetch proves the delivery reached B's live intake.
    auto &a_db = *testNodes->getNodes()[0].db;
    ASSERT_FALSE( a_db.AddBroadcastTopic( kTracerTopic ).has_error() );
    const auto tracer = SeedEntryOn( a_db, "/redel/tracer", "TRACER_VALUE", { kTracerTopic } );

    // Tear A down and recreate it on the same base path, un-started: the
    // persisted heads (still naming the pruned root) survive, and the test
    // controls when the restarted head exchange first broadcasts.
    testNodes->restartNodePendingStart( 0 );
    auto &a_restarted = testNodes->getNodes()[0];
    ASSERT_TRUE( a_restarted.db );
    ASSERT_FALSE( a_restarted.db->AddBroadcastTopic( kTopic ).has_error() );
    ASSERT_FALSE( a_restarted.db->AddBroadcastTopic( kTracerTopic ).has_error() );
    a_restarted.db->AddTopicName( kTopic );
    a_restarted.db->AddTopicName( kTracerTopic );

    // Non-vacuity: the pruned root is still the restarted node's live head.
    ASSERT_TRUE( headListContains( *a_restarted.db, entry_a.cid ) );

    // B joins the tracer topic before the reconnect so its subscriptions are
    // announced when the pair reconnects.
    b_db.AddListenTopic( kTracerTopic );
    testNodes->connectNodes( 0, 1 );
    auto a_pubsub = a_restarted.pubsub;
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return a_pubsub->getPeerCount( fullTopicName( kTopic ) ) >= 1 &&
                   a_pubsub->getPeerCount( fullTopicName( kTracerTopic ) ) >= 1;
        },
        WAIT_TIMEOUT,
        "restarted node learned the receiver's topic subscriptions",
        nullptr );

    sgns::crdt::CrdtDatastore::ResetRootFetchAttemptsForTest();
    a_restarted.db->Start();

    ASSERT_WAIT_FOR_CONDITION( [&] { return sgns::crdt::CrdtDatastore::GetRootFetchAttemptsForTest() >= 1; },
                               WAIT_TIMEOUT,
                               "tracer head re-delivered by the restarted head exchange was fetched",
                               nullptr );

    // Exactly one fetch (the tracer); the pruned root's re-delivery through
    // the restarted head exchange contributed ZERO.
    EXPECT_EQ( sgns::crdt::CrdtDatastore::GetRootFetchAttemptsForTest(), 1 );
    assertNothingResurrected( b_db, entry_a );
}

/// Case 3 -- negative control: nothing on B is pruned; a head B has never
/// seen arrives through the same intake and B DOES fetch it. The guard must
/// not suppress normal fetches.
TEST_F( GlobalDBEraseRedeliveryTest, RedeliveryOfUnprunedRootStillFetches )
{
    auto testNodes = std::make_unique<EraseNodeCollection>();
    testNodes->addNode( "globaldb_redel_control_a" );
    testNodes->addNode( "globaldb_redel_control_b" );
    ASSERT_EQ( testNodes->getNodes().size(), 2 );
    testNodes->connectNodes( 0, 1 );
    auto &a_db = *testNodes->getNodes()[0].db;
    auto &b_db = *testNodes->getNodes()[1].db;

    ASSERT_FALSE( a_db.AddBroadcastTopic( kTracerTopic ).has_error() );
    b_db.AddListenTopic( kTracerTopic );

    sgns::crdt::CrdtDatastore::ResetRootFetchAttemptsForTest();
    const auto tracer = SeedEntryOn( a_db, "/redel/tracer", "TRACER_VALUE", { kTracerTopic } );

    // The delivery drives exactly one fetch attempt, and the value lands.
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            auto value = b_db.Get( sgns::crdt::HierarchicalKey( tracer.tx_path ) );
            return value.has_value() && value.value().toString() == tracer.value &&
                   rawSealKeyPresent( b_db, tracer.cid ) &&
                   sgns::crdt::CrdtDatastore::GetRootFetchAttemptsForTest() >= 1;
        },
        WAIT_TIMEOUT,
        "unpruned root delivery still fetches and applies",
        nullptr );
    EXPECT_EQ( sgns::crdt::CrdtDatastore::GetRootFetchAttemptsForTest(), 1 );
}

/// SC-5 (RetryDueFailedRoots guard): a root on the failed-fetch retry ladder
/// whose block this node has since pruned (seal retained, block gone) is
/// TERMINAL -- the retry pass must skip it BEFORE the fetch-attempt counter
/// site (Pitfall 7), clear the ladder entry, and resurrect nothing. The
/// mid-flight ladder state (a fetch failed, the seal appeared through another
/// path) cannot be produced deterministically through the two-node wiring, so
/// the ForTest seam places the pruned root on the ladder exactly as the failed
/// fetch did; the retry pass itself is the REAL DAG-worker-loop pass.
TEST_F( GlobalDBEraseRedeliveryTest, RetryDueFailedRootsSkipsPrunedRootWithZeroFetchAttempts )
{
    auto        testNodes = std::make_unique<EraseNodeCollection>();
    SeededEntry entry_a;
    seedPrunedRoot( *testNodes, "globaldb_redel_retry_guard", entry_a );
    auto &b_db = *testNodes->getNodes()[1].db;

    auto        &b_datastore = *b_db.GetCRDTDataStore();
    const size_t baseline    = b_datastore.GetFailedRootRetryCountForTest();

    // The mid-flight state: the pruned root sits on the retry ladder, due in
    // one FAILED_ROOT_RETRY_BASE_DELAY (5s); the worker loop runs
    // RetryDueFailedRoots every ~500ms pass.
    sgns::crdt::CrdtDatastore::ScheduleFailedRootRetryForTest( b_datastore, entry_a.cid );
    EXPECT_EQ( b_datastore.GetFailedRootRetryCountForTest(), baseline + 1 );

    sgns::crdt::CrdtDatastore::ResetRootFetchAttemptsForTest();

    // One retry-pass horizon (the 5s base delay plus worker-loop slack): the
    // guard must make the entry terminal -- cleared from the ladder.
    ASSERT_WAIT_FOR_CONDITION( [&] { return b_datastore.GetFailedRootRetryCountForTest() == baseline; },
                               std::chrono::seconds( 15 ),
                               "pruned root's retry-ladder entry went terminal",
                               nullptr );

    // Zero fetch attempts across the pass: the guard fired before the counter
    // site, and no other delivery moved the counter either.
    EXPECT_EQ( sgns::crdt::CrdtDatastore::GetRootFetchAttemptsForTest(), 0 );
    assertNothingResurrected( b_db, entry_a );
}

/// Negative control: an UNRESOLVED root (no seal, no block -- never processed
/// by this node) on the same ladder still enqueues: DeleteCIDBlock +
/// EnqueueRootCID run and the fetch-attempt counter advances. The SC-5 guard
/// suppresses only sealed roots, never ordinary retries.
TEST_F( GlobalDBEraseTest, RetryDueFailedRootsStillRetriesUnresolvedRoot )
{
    auto testNodes = std::make_unique<EraseNodeCollection>();
    testNodes->addNode( "globaldb_redel_retry_control" );
    ASSERT_EQ( testNodes->getNodes().size(), 1 );
    auto &db = *testNodes->getNodes().front().db;
    ASSERT_FALSE( db.AddBroadcastTopic( kTopic ).has_error() );
    db.AddListenTopic( kTopic );

    const auto unseen_cid = makeUnseenCid( { 0x11, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45,
                                             0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x11, 0x23, 0x45, 0x67, 0x89, 0xAB,
                                             0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF } );

    auto        &datastore = *db.GetCRDTDataStore();
    const size_t baseline  = datastore.GetFailedRootRetryCountForTest();

    sgns::crdt::CrdtDatastore::ScheduleFailedRootRetryForTest( datastore, unseen_cid );
    EXPECT_EQ( datastore.GetFailedRootRetryCountForTest(), baseline + 1 );

    sgns::crdt::CrdtDatastore::ResetRootFetchAttemptsForTest();
    ASSERT_WAIT_FOR_CONDITION( [&] { return sgns::crdt::CrdtDatastore::GetRootFetchAttemptsForTest() >= 1; },
                               std::chrono::seconds( 15 ),
                               "unresolved root's retry pass still enqueues a fetch",
                               nullptr );

    // The doomed fetch reschedules the entry: only the sealed-root guard
    // clears ladder entries, so ordinary retries keep their ladder slot.
    EXPECT_GE( datastore.GetFailedRootRetryCountForTest(), baseline + 1 );
}

/// SC-5 (RetryStalledDeltas, verify-only): the stalled-delta retry path is
/// deliberately local-only (GetNodeWithoutRequest -- no fetch, the documented
/// anti-pattern is ever adding one). A stalled delta whose block was pruned
/// keeps that property under the worst input: ZERO fetch attempts across real
/// retry passes, and bounded give-up at the MAX_FAILED_ROOT_RETRIES cap. The
/// ladder state is seeded through the same locked scheduler a natural
/// dependency-stall uses; the retry pass that observes the pruned block is the
/// REAL DAG-worker-loop pass.
TEST_F( GlobalDBEraseTest, StalledDeltaPrunedBlockGivesUpLocallyWithZeroFetchAttempts )
{
    auto testNodes = std::make_unique<EraseNodeCollection>();
    testNodes->addNode( "globaldb_stalled_pruned" );
    ASSERT_EQ( testNodes->getNodes().size(), 1 );
    auto &db = *testNodes->getNodes().front().db;
    ASSERT_FALSE( db.AddBroadcastTopic( kTopic ).has_error() );
    db.AddListenTopic( kTopic );

    // The pruned shape on one node: A seeded then retired by B, map entry in
    // the StoreTransactionCID shape, erased through the one facade API so the
    // seal marker remains (block gone).
    const auto entry_a = SeedEntry( db, "/stalled/entryA", "VALUE_A" );
    const auto entry_b = SeedEntry( db, "/stalled/entryB", "VALUE_B" );
    ASSERT_FALSE( headListContains( db, entry_a.cid ) );
    ASSERT_TRUE( db.PutRaw( sgns::base::Buffer{}.put( entry_a.tx_path ),
                            sgns::base::Buffer{}.put( entry_a.cid.toString().value() ) )
                     .has_value() );
    EXPECT_OUTCOME_TRUE_1( db.EraseEntry( entry_a.tx_path, entry_a.cid ) );
    ASSERT_TRUE( db.GetRaw( blockKeyOf( entry_a.cid ) ).has_error() );
    ASSERT_TRUE( rawSealKeyPresent( db, entry_a.cid ) );

    auto        &datastore = *db.GetCRDTDataStore();
    const size_t baseline  = datastore.GetStalledDeltaRetryCountForTest();

    // The mid-flight state: the pruned delta block sits on the stalled
    // ladder, due in one base delay (5s).
    sgns::crdt::CrdtDatastore::ScheduleStalledDeltaRetryForTest( datastore, entry_a.cid );
    EXPECT_EQ( datastore.GetStalledDeltaRetryCountForTest(), baseline + 1 );

    sgns::crdt::CrdtDatastore::ResetRootFetchAttemptsForTest();

    // A quiet window spanning the due time plus worker-loop slack: at least
    // one real RetryStalledDeltas pass re-attempts the pruned block locally
    // (GetNodeWithoutRequest fails on the absent block and reschedules). No
    // predicate observable exists for "a pass ran", so the window is the
    // 5s base delay plus multi-pass slack.
    std::this_thread::sleep_for( std::chrono::seconds( 8 ) );

    // The pinned property: even with the block gone, the retry path never
    // fetched -- zero counter movement across the real pass(es).
    EXPECT_EQ( sgns::crdt::CrdtDatastore::GetRootFetchAttemptsForTest(), 0 );
    EXPECT_EQ( datastore.GetStalledDeltaRetryCountForTest(), baseline + 1 );

    // Bounded give-up: each further failed local pass increments the attempt
    // counter through the same locked scheduler, and the MAX_FAILED_ROOT_RETRIES
    // cap erases the entry. Driving the remaining attempts directly pins that
    // a stalled entry cannot retry forever.
    size_t drives = 0;
    while ( datastore.GetStalledDeltaRetryCountForTest() > baseline && drives < 10 )
    {
        sgns::crdt::CrdtDatastore::ScheduleStalledDeltaRetryForTest( datastore, entry_a.cid );
        ++drives;
    }
    EXPECT_EQ( datastore.GetStalledDeltaRetryCountForTest(), baseline );
    EXPECT_LE( drives, 9 );
    EXPECT_EQ( sgns::crdt::CrdtDatastore::GetRootFetchAttemptsForTest(), 0 );
    // Still pruned, never resurrected: the value stays erased, the block stays
    // gone, the seal marker is still the terminal record.
    EXPECT_TRUE( db.Get( sgns::crdt::HierarchicalKey( entry_a.tx_path ) ).has_error() );
    EXPECT_TRUE( db.GetRaw( blockKeyOf( entry_a.cid ) ).has_error() );
    EXPECT_TRUE( rawSealKeyPresent( db, entry_a.cid ) );
}

/// SURV-02 (peer half, 19-07): a pruned server refuses a pruned-CID
/// graphsync request cleanly -- the responder answers NOT_FOUND immediately
/// (graphsync_impl.cpp: a block the responder lacks is "nothing selected",
/// never a hung request) -- and the requester fails over to a serving peer.
/// The refusal is accounted as a per-CID failure (getNode's FAILED /
/// non-connection-status branch records RecordCIDFailure and walks to the
/// next route peer), never as a connection blacklist: the light peer keeps
/// serving data it still holds. Three real pubsub nodes on the N-node
/// EraseNodeCollection topology: a requester, a light server that prunes
/// the target entry, and a full server that keeps everything.
class GlobalDBEraseRefusalFailoverTest : public GlobalDBEraseRedeliveryTest
{
protected:
    /// The concrete DAG syncer owning the route walk and the per-peer
    /// failure accounting; the broadcaster hands it out as void.
    static std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer> syncerOf( sgns::crdt::GlobalDB &db )
    {
        return std::static_pointer_cast<sgns::crdt::GraphsyncDAGSyncer>( db.GetBroadcaster()->GetDagSyncer() );
    }
};

/// The refusal/failover scenario in three parts. Part 1 is hermetic: the
/// requester joins no topic (empty route table, no cached broadcasts),
/// the test registers the pruned light FIRST and the full node second
/// for the pruned CID through the public AddRoute (the registration
/// broadcasts drive via AddMultiCIDInfo), and one direct getNode walks
/// the production route -- consult the light, take its clean refusal
/// (the responder's storage lookup deterministically misses), record the
/// per-CID failure, fail over to the serving peer. Part 2 is the mesh
/// E2E: the requester grafts the topic, the light-attributed cached
/// publishes and head re-broadcast deliver the pruned-CID learning (all
/// refusal-bound), and the full node's fresh fork delta registers the
/// serving route that completes the fetch; the ladder drains and the
/// attempts stay bounded. Part 3 proves the light was never
/// connection-blacklisted: it still serves its fresh data.
TEST_F( GlobalDBEraseRefusalFailoverTest, PrunedPeerRefusalFailsOverToServingPeer )
{
    auto testNodes = std::make_unique<EraseNodeCollection>();
    testNodes->addNode( "globaldb_refusal_requester" );
    testNodes->addNode( "globaldb_refusal_light" );
    testNodes->addNode( "globaldb_refusal_full" );
    ASSERT_EQ( testNodes->getNodes().size(), 3 );
    testNodes->connectNodes( 1, 2 ); // seeding pair: light dials full

    auto      &r_db        = *testNodes->getNodes()[0].db;
    auto      &l_db        = *testNodes->getNodes()[1].db;
    auto      &f_db        = *testNodes->getNodes()[2].db;
    const auto light_peer  = testNodes->getNodes()[1].pubsub->GetHost()->getId();
    auto      &r_datastore = *r_db.GetCRDTDataStore();

    // The light node is DEAF (broadcast topics only): it never hears the
    // full node's deltas, so only its own retire put can move its head
    // past the prune target (the seedPrunedRoot topology). The full
    // server listens -- and, this phase, publishes NOTHING: every cached
    // or rebroadcast message the requester can see is light-attributed.
    ASSERT_FALSE( l_db.AddBroadcastTopic( kTopic ).has_error() );
    ASSERT_FALSE( f_db.AddBroadcastTopic( kTopic ).has_error() );
    f_db.AddListenTopic( kTopic );

    const auto entry_a = SeedEntryOn( l_db, "/refusal/entryA", "A", { kTopic } );
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return entryApplied( f_db, entry_a ) && rawSealKeyPresent( f_db, entry_a.cid ) &&
                   headListContains( f_db, entry_a.cid );
        },
        WAIT_TIMEOUT,
        "full server received and sealed the shared history root",
        nullptr );
    const auto entry_l2 = SeedEntryOn( l_db, "/refusal/entryL2", "L2", { kTopic } );
    ASSERT_WAIT_FOR_CONDITION(
        [&] { return !headListContains( l_db, entry_a.cid ) && headListContains( l_db, entry_l2.cid ); },
        WAIT_TIMEOUT,
        "light node's retire put moved its head past the prune target",
        nullptr );
    ASSERT_WAIT_FOR_CONDITION( [&] { return headListContains( f_db, entry_l2.cid ); },
                               WAIT_TIMEOUT,
                               "full server received the retiring delta",
                               nullptr );

    // The pruned state on the light node (seal retained -- GATE-03).
    ASSERT_TRUE( l_db.PutRaw( sgns::base::Buffer{}.put( entry_a.tx_path ),
                              sgns::base::Buffer{}.put( entry_a.cid.toString().value() ) )
                     .has_value() );
    EXPECT_OUTCOME_TRUE_1( l_db.EraseEntry( entry_a.tx_path, entry_a.cid ) );
    EXPECT_TRUE( l_db.Get( sgns::crdt::HierarchicalKey( entry_a.tx_path ) ).has_error() );
    EXPECT_TRUE( l_db.GetRaw( blockKeyOf( entry_a.cid ) ).has_error() );
    EXPECT_TRUE( rawSealKeyPresent( l_db, entry_a.cid ) );
    // The pruned server's DAG layer cannot serve the erased node: the
    // block-bytes key the responder reads is the same key the erase
    // removed, so a graphsync request for the pruned CID cannot be
    // answered with content.
    EXPECT_TRUE( syncerOf( l_db )->GetNodeFromMerkleDAG( entry_a.cid ).has_error() );
    // The serving state on the full node: value, block bytes, seal.
    EXPECT_TRUE( entryApplied( f_db, entry_a ) );
    EXPECT_TRUE( f_db.GetRaw( blockKeyOf( entry_a.cid ) ).has_value() );
    EXPECT_TRUE( rawSealKeyPresent( f_db, entry_a.cid ) );

    // Part 1 -- the peer half, hermetic. The requester joins NO topic
    // (its route table is empty and no cached broadcast can reach it);
    // the test registers the two routes itself, in order, through the
    // public AddRoute -- the same registration broadcasts drive via
    // AddMultiCIDInfo -- with the PRUNED light first and the serving
    // full node second. One direct getNode call then walks the exact
    // production route: consult the light (dialing its advertised
    // address on demand), take its clean refusal (the responder's
    // storage lookup deterministically misses -- asserted above),
    // record the per-CID failure, and fail over to the full node,
    // which serves.
    const auto full_peer = testNodes->getNodes()[2].pubsub->GetHost()->getId();
    const auto light_addr =
        libp2p::multi::Multiaddress::create( testNodes->getNodes()[1].pubsub->GetInterfaceAddress() ).value();
    const auto full_addr = libp2p::multi::Multiaddress::create( testNodes->getNodes()[2].pubsub->GetInterfaceAddress() )
                               .value();

    auto &r_syncer = *syncerOf( r_db );
    r_syncer.AddRoute( entry_a.cid, light_peer, { light_addr } ); // consulted FIRST
    r_syncer.AddRoute( entry_a.cid, full_peer, { full_addr } );   // failover target

    const size_t ladder_baseline = r_datastore.GetFailedRootRetryCountForTest();
    sgns::crdt::CrdtDatastore::ResetRootFetchAttemptsForTest();
    EXPECT_OUTCOME_TRUE( fetched_node, r_syncer.getNode( entry_a.cid ) );
    EXPECT_EQ( fetched_node->getCID(), entry_a.cid );
    // The refusal was CID-scoped, never a connection blacklist: a
    // connection-failure classification would have blacklisted the light.
    EXPECT_FALSE( r_syncer.IsOnBlackList( light_peer ) );
    // The direct walk enqueues no root job: zero fetch-attempt movement,
    // no storm.
    EXPECT_EQ( sgns::crdt::CrdtDatastore::GetRootFetchAttemptsForTest(), 0 );
    EXPECT_EQ( r_datastore.GetFailedRootRetryCountForTest(), ladder_baseline );
    // Discard the directly-fetched block (the LRU copy -- no root job
    // ran, so the dag service never saw it) so the mesh half below
    // processes the pruned CID through the real broadcast intake like
    // any fresh node would, instead of the intake's HasBlock check
    // silently skipping it.
    EXPECT_OUTCOME_TRUE_1( r_syncer.DeleteCIDBlock( entry_a.cid ) );

    // Part 2 -- the mesh half, end to end. The requester subscribes
    // BEFORE dialing so its subscription is announced at connect (the
    // Case-2 lesson); the graft relays the cached seeding publishes
    // (every one of them light-attributed, so the pruned-history
    // fetches under them take the same refusal), and the light's head
    // re-broadcast guarantees the learning. The full node's own fork
    // delta -- a put on the topic, published only now -- registers the
    // serving route whose walk pulls the missing history; the
    // pruned-CID values land and the rescued roots drain the ladder.
    r_db.AddListenTopic( kTopic );
    testNodes->connectNodes( 0, 1 );
    testNodes->connectNodes( 0, 2 );
    auto light_pubsub = testNodes->getNodes()[1].pubsub;
    auto full_pubsub  = testNodes->getNodes()[2].pubsub;
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            // The light node is deaf and never announces a subscription;
            // the light's topic count sees {full, requester}, the full
            // node's sees {requester}.
            return light_pubsub->getPeerCount( fullTopicName( kTopic ) ) >= 2 &&
                   full_pubsub->getPeerCount( fullTopicName( kTopic ) ) >= 1;
        },
        std::chrono::seconds( 10 ),
        "both servers learned the requester's topic subscription",
        nullptr );

    EXPECT_OUTCOME_TRUE_1( l_db.GetCRDTDataStore()->BroadcastHeadsForTopics( { kTopic } ) );
    const auto entry_fmid = SeedEntryOn( f_db, "/refusal/entryFmid", "F_MID", { kTopic } );
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        { return entryApplied( r_db, entry_a ) && entryApplied( r_db, entry_l2 ) && entryApplied( r_db, entry_fmid ); },
        WAIT_TIMEOUT,
        "pruned-CID fetch completed through the serving peer after the refusal",
        nullptr );
    // The rescue root becomes the requester's converged head as its
    // root job finalizes (a refused sibling root's head update is lost
    // with its failed job -- the rescue head supersedes it).
    ASSERT_WAIT_FOR_CONDITION( [&] { return headListContains( r_db, entry_fmid.cid ); },
                               WAIT_TIMEOUT,
                               "rescue root finalized as the requester's head",
                               nullptr );

    EXPECT_FALSE( r_syncer.IsOnBlackList( light_peer ) );
    // No retry-ladder ride left behind: the rescued roots complete on
    // their next retry (blocks now present) and drain the ladder.
    ASSERT_WAIT_FOR_CONDITION( [&] { return r_datastore.GetFailedRootRetryCountForTest() == ladder_baseline; },
                               WAIT_TIMEOUT,
                               "rescued roots drained the retry ladder -- no doomed rides left",
                               nullptr );
    // Bounded attempts across the whole mesh window: a few enqueues with
    // dedup; the 8-attempt doomed ladder would run far past this bound.
    EXPECT_LE( sgns::crdt::CrdtDatastore::GetRootFetchAttemptsForTest(), 8 );
    // The pruned peer's terminal state is untouched by the failover.
    EXPECT_TRUE( l_db.Get( sgns::crdt::HierarchicalKey( entry_a.tx_path ) ).has_error() );
    EXPECT_TRUE( l_db.GetRaw( blockKeyOf( entry_a.cid ) ).has_error() );
    EXPECT_TRUE( rawSealKeyPresent( l_db, entry_a.cid ) );

    // Part 3 -- functional not-blacklisted proof: a fetch the light CAN
    // still serve (its fresh put, head-broadcast automatically on
    // publish) routes to it and completes -- the connection-vs-CID
    // distinction.
    sgns::crdt::CrdtDatastore::ResetRootFetchAttemptsForTest();
    const auto entry_l3 = SeedEntryOn( l_db, "/refusal/entryL3", "L3", { kTopic } );
    ASSERT_WAIT_FOR_CONDITION( [&] { return entryApplied( r_db, entry_l3 ); },
                               WAIT_TIMEOUT,
                               "light peer still serves data it holds -- not connection-blacklisted",
                               nullptr );
    EXPECT_FALSE( r_syncer.IsOnBlackList( light_peer ) );
    EXPECT_GE( sgns::crdt::CrdtDatastore::GetRootFetchAttemptsForTest(), 1 );
    EXPECT_LE( sgns::crdt::CrdtDatastore::GetRootFetchAttemptsForTest(), 4 );
}

/// SURV-03 / D-04 (19-07): a fresh light node bootstraps through a pruned
/// mesh. Three seeders share the mesh topic -- two light peers that prune
/// the history and one full node that keeps it -- and the full node also
/// owns a serving topic carrying the same history head (a put on both
/// topics links the mesh history, so one serving-topic broadcast carries
/// the whole pruned DAG beneath it). The fresh node subscribes the
/// serving topic first: the full node is the ONLY possible route source
/// on it (Pitfall 6 -- routes register from broadcasts), so the pruned
/// history provably arrives from the full node, with no ladder rides.
/// Joining the pruned mesh afterwards -- with every block already
/// present -- fetches nothing: no re-fetch storm from the pruned lights,
/// seals intact.
class GlobalDBErasePrunedMeshTest : public GlobalDBEraseRedeliveryTest
{
protected:
    static constexpr char kMeshTopic[]  = "erase_e2e_mesh_topic";
    static constexpr char kServeTopic[] = "erase_e2e_serve_topic";
};

TEST_F( GlobalDBErasePrunedMeshTest, FreshNodeBootstrapsPrunedHistoryServedByFullNode )
{
    auto testNodes = std::make_unique<EraseNodeCollection>();
    testNodes->addNode( "globaldb_mesh_light1" );
    testNodes->addNode( "globaldb_mesh_light2" );
    testNodes->addNode( "globaldb_mesh_full" );
    testNodes->addNode( "globaldb_mesh_fresh" );
    ASSERT_EQ( testNodes->getNodes().size(), 4 );
    testNodes->connectNodes( 0, 1 ); // seeding mesh: light1 dials light2
    testNodes->connectNodes( 0, 2 ); // light1 dials full
    testNodes->connectNodes( 1, 2 ); // light2 dials full

    auto &l1_db       = *testNodes->getNodes()[0].db;
    auto &l2_db       = *testNodes->getNodes()[1].db;
    auto &f_db        = *testNodes->getNodes()[2].db;
    auto &fresh_db    = *testNodes->getNodes()[3].db;
    auto &fresh_store = *fresh_db.GetCRDTDataStore();

    // Light 1 is deaf (the retire-put topology); light 2 and the full
    // node listen on the mesh topic. The full node owns the serving topic
    // alone -- no light ever publishes or subscribes it, so every route
    // a serving-topic subscriber can hold is full-node-attributed.
    ASSERT_FALSE( l1_db.AddBroadcastTopic( kMeshTopic ).has_error() );
    l2_db.AddListenTopic( kMeshTopic );
    ASSERT_FALSE( f_db.AddBroadcastTopic( kMeshTopic ).has_error() );
    f_db.AddListenTopic( kMeshTopic );
    ASSERT_FALSE( f_db.AddBroadcastTopic( kServeTopic ).has_error() );
    f_db.AddListenTopic( kServeTopic );

    // Seed the history on all three: light 1 puts, the mesh receives.
    const auto entry_a = SeedEntryOn( l1_db, "/mesh/entryA", "A", { kMeshTopic } );
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return entryApplied( l2_db, entry_a ) && entryApplied( f_db, entry_a ) &&
                   rawSealKeyPresent( l2_db, entry_a.cid ) && rawSealKeyPresent( f_db, entry_a.cid );
        },
        WAIT_TIMEOUT,
        "mesh received and sealed the history root",
        nullptr );
    // Light 1's retire put moves every mesh head past the prune target.
    const auto entry_l1 = SeedEntryOn( l1_db, "/mesh/entryL1", "L1_RETIRE", { kMeshTopic } );
    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return !headListContains( l1_db, entry_a.cid ) && headListContains( l1_db, entry_l1.cid ) &&
                   headListContains( l2_db, entry_l1.cid ) && headListContains( f_db, entry_l1.cid );
        },
        WAIT_TIMEOUT,
        "retire put moved every mesh head past the prune target",
        nullptr );

    // Both lights prune the history (seal retained -- GATE-03).
    for ( const size_t light_idx : { size_t{ 0 }, size_t{ 1 } } )
    {
        auto &light_db = *testNodes->getNodes()[light_idx].db;
        ASSERT_TRUE( light_db
                         .PutRaw( sgns::base::Buffer{}.put( entry_a.tx_path ),
                                  sgns::base::Buffer{}.put( entry_a.cid.toString().value() ) )
                         .has_value() );
        EXPECT_OUTCOME_TRUE_1( light_db.EraseEntry( entry_a.tx_path, entry_a.cid ) );
        EXPECT_TRUE( light_db.Get( sgns::crdt::HierarchicalKey( entry_a.tx_path ) ).has_error() );
        EXPECT_TRUE( light_db.GetRaw( blockKeyOf( entry_a.cid ) ).has_error() );
        EXPECT_TRUE( rawSealKeyPresent( light_db, entry_a.cid ) );
    }

    // The full node's serving head: a put on BOTH topics links the mesh
    // history, so the serving-topic broadcast carries the pruned DAG
    // beneath it. Asserted BEFORE the fresh node's fetch window opens:
    // the serving route's content exists.
    const auto entry_f = SeedEntryOn( f_db, "/mesh/entryF", "F_SERVE", { kMeshTopic, kServeTopic } );
    ASSERT_WAIT_FOR_CONDITION( [&] { return headListContains( f_db, entry_f.cid ); },
                               WAIT_TIMEOUT,
                               "full node's serving head landed on both topics",
                               nullptr );
    EXPECT_TRUE( entryApplied( f_db, entry_a ) );
    EXPECT_TRUE( f_db.GetRaw( blockKeyOf( entry_a.cid ) ).has_value() );
    EXPECT_TRUE( rawSealKeyPresent( f_db, entry_a.cid ) );

    // Phase B -- the fresh node bootstraps through the full node. It
    // subscribes the serving topic (subscribing before dialing so the
    // subscription is announced at connect) and knows the mesh topic
    // NAME only: the datastore's link-fetch gate for mesh-named links,
    // without joining the mesh's gossip yet.
    fresh_db.AddListenTopic( kServeTopic );
    fresh_db.AddTopicName( kMeshTopic );
    EXPECT_FALSE( entryApplied( fresh_db, entry_a ) ); // non-vacuous: holds nothing
    testNodes->connectNodes( 3, 2 );
    auto full_pubsub = testNodes->getNodes()[2].pubsub;
    ASSERT_WAIT_FOR_CONDITION( [&] { return full_pubsub->getPeerCount( fullTopicName( kServeTopic ) ) >= 1; },
                               std::chrono::seconds( 10 ),
                               "full node learned the fresh node's serving-topic subscription",
                               nullptr );

    const size_t ladder_baseline = fresh_store.GetFailedRootRetryCountForTest();
    sgns::crdt::CrdtDatastore::ResetRootFetchAttemptsForTest();
    // The Pitfall-6 wiring: the serving head exists (asserted above) and
    // the delivery path is up; the broadcast registers the full node's
    // route on the fresh node BEFORE any fetch window it controls.
    EXPECT_OUTCOME_TRUE_1( f_db.GetCRDTDataStore()->BroadcastHeadsForTopics( { kServeTopic } ) );

    ASSERT_WAIT_FOR_CONDITION(
        [&]
        {
            return entryApplied( fresh_db, entry_a ) && entryApplied( fresh_db, entry_l1 ) &&
                   entryApplied( fresh_db, entry_f );
        },
        WAIT_TIMEOUT,
        "fresh node bootstrapped the pruned-history heads from the full node's broadcast",
        nullptr );
    ASSERT_WAIT_FOR_CONDITION( [&] { return headListContains( fresh_db, entry_f.cid ); },
                               WAIT_TIMEOUT,
                               "serving head finalized as the fresh node's head",
                               nullptr );
    // No ladder rides and a bounded fetch total: the serving head's root
    // enqueue (the pruned-history links ride the same walk) plus at most
    // a re-delivery or a slow mesh-side fetch of the serving delta --
    // the counter dedups pending roots, so the exact count wobbles by
    // delivery timing; a route-starved ladder ride would run to 8.
    EXPECT_EQ( fresh_store.GetFailedRootRetryCountForTest(), ladder_baseline );
    EXPECT_GE( sgns::crdt::CrdtDatastore::GetRootFetchAttemptsForTest(), 1 );
    EXPECT_LE( sgns::crdt::CrdtDatastore::GetRootFetchAttemptsForTest(), 4 );

    // Phase C -- join the pruned mesh with everything already present.
    // The lights' cached head broadcasts deliver, find every block
    // present, and fetch NOTHING: no re-fetch storm, seals untouched.
    sgns::crdt::CrdtDatastore::ResetRootFetchAttemptsForTest();
    fresh_db.AddListenTopic( kMeshTopic );
    testNodes->connectNodes( 3, 0 );
    testNodes->connectNodes( 3, 1 );
    ASSERT_WAIT_FOR_CONDITION( [&] { return full_pubsub->getPeerCount( fullTopicName( kMeshTopic ) ) >= 2; },
                               std::chrono::seconds( 10 ),
                               "fresh node's mesh-topic subscription is known before the quiet window",
                               nullptr );
    std::this_thread::sleep_for( std::chrono::seconds( 6 ) );

    EXPECT_EQ( sgns::crdt::CrdtDatastore::GetRootFetchAttemptsForTest(), 0 );
    EXPECT_EQ( fresh_store.GetFailedRootRetryCountForTest(), ladder_baseline );
    for ( const size_t light_idx : { size_t{ 0 }, size_t{ 1 } } )
    {
        auto &light_db = *testNodes->getNodes()[light_idx].db;
        EXPECT_TRUE( light_db.Get( sgns::crdt::HierarchicalKey( entry_a.tx_path ) ).has_error() );
        EXPECT_TRUE( light_db.GetRaw( blockKeyOf( entry_a.cid ) ).has_error() );
        EXPECT_TRUE( rawSealKeyPresent( light_db, entry_a.cid ) );
    }
    EXPECT_TRUE( entryApplied( fresh_db, entry_a ) );
}
