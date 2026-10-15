/**
 * @file       globaldb_erase_test.cpp
 * @brief      End-to-end erase tests for GlobalDB::EraseEntry.
 *
 * Single real pubsub node per test (fresh database via removeAllWithRetry);
 * proves SC-1 (three-part assertions + negative control), SC-2 (the three
 * interruption-step crash injections), and SC-3 (head and resolved guard
 * rejections) through the facade API on the real compose path.
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
#include "crdt/hierarchical_key.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "base/buffer.hpp"
#include "base/logger.hpp"
#include "testutil/outcome.hpp"
#include "testutil/wait_condition.hpp"
#include "testutil/remove_all.hpp"

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

            sgns::crdt::KeyPairFileStorage keyStore( basePath + "/key" );
            auto                           keyPair    = keyStore.GetKeyPair().value();
            auto                           pubsub     = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( keyPair );
            const std::string              listenIp   = "0.0.0.0";
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
            nodes_.push_back( EraseNode{ basePath, pubsub, db } );
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
