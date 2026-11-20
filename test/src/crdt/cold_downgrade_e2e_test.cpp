/**
 * @file       cold_downgrade_e2e_test.cpp
 * @brief      Downgrade interlock verdict matrix (v1.2 Phase 18 plan 18-02;
 *             POLICY-02 / SC4; D-05/D-06/D-07).
 *
 * THE INTERLOCK: a detected archive/full -> light+pruning downgrade can
 * never silently arm the pruner. The persisted role fingerprint
 * (PruneEligibility::PRUNE_ROLE_FINGERPRINT_KEY, "/prune/role-fingerprint")
 * is read-compared-rewritten through the node-local raw surface; without
 * the explicit ack key (PruningConfig::downgrade_acknowledged,
 * sgns_config.json's "pruning" object) the verdict is
 * DowngradeUnacknowledged and the GeniusNode gate DISARMS pruning behind a
 * loud WARN (D-06 fail-toward-under-erase: the node starts normally, the
 * pruner stays off). Full record:
 * notes/18-collectibility-policy-downgrade-interlock.md
 *
 * D-11 BOTH-TIERS DECLARATION: this file is the deterministic Tier-1
 * hermetic tier. The t0->t3 single-node re-open timeline legs (the SC2/SC3
 * E2E of plan 18-03) EXTEND this same file and fixture; the dev-gated live
 * leg on a real MinIO stack lives in cold_downgrade_live_smoke_test.cpp
 * (Phase 16 D-11 hybrid discipline).
 *
 * Fixture: the cold_evidence_matrix_test.cpp recipe (copied kit - named
 * copy, this suite's own lane dirs): real GlobalDB over a fresh hot dir +
 * pubsub host + account/blockchain/TM wiring, in-memory fakes re-injected
 * BEFORE GlobalDB::New and cleared AFTER. The FP legs need only the
 * node-local raw surface (GetRaw/PutRaw), so they build the LEAN stack
 * (cold tier disabled) - the cold-enabled variants arrive with 18-03's
 * timeline legs. Waits only via PollUntil bounded predicates; fresh dir
 * per case (Windows LOCK contention, 18-RESEARCH Pitfall 9).
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

#include <rapidjson/document.h>

#include <rocksdb/db.h>
#include <rocksdb/options.h>

#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <libp2p/basic/scheduler.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <libp2p/host/host.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <soralog/impl/configurator_from_yaml.hpp>

#include "account/GeniusAccount.hpp"
#include "account/NodeType.hpp"
#include "blockchain/Blockchain.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/globaldb/ColdStorageStack.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "transaction/PruneEligibility.hpp"
#include "transaction/TransactionManager.hpp"
#include "transaction/TxPruner.hpp"

using namespace sgns;

namespace
{
    const sgns::TokenID kTestTokenId = sgns::TokenID::FromBytes( { 0x00 } );

    /// The fixture's pre-seeded CRDT topic (the tx_pruner_test contract).
    constexpr char kSeedTopic[] = "CRDT.Datastore.TEST.Channel";

    /// Bounded poll loop with deadline - never a fixed sleep (Tier-1 audit).
    bool PollUntil( const std::function<bool()> &predicate,
                    std::chrono::milliseconds       deadline,
                    std::chrono::milliseconds       step = std::chrono::milliseconds( 10 ) )
    {
        const auto end = std::chrono::steady_clock::now() + deadline;
        while ( std::chrono::steady_clock::now() < end )
        {
            if ( predicate() )
            {
                return true;
            }
            std::this_thread::sleep_for( step );
        }
        return predicate();
    }

    /// SHORT relative path under the build tree (Windows MAX_PATH).
    std::string NextUniqueDir( const char *lane )
    {
        static std::atomic<uint64_t> next{ 0 };
        return std::string( "test-data/cold-downgrade-e2e/" ) + lane + "-"
             + std::to_string( next.fetch_add( 1, std::memory_order_relaxed ) );
    }

    /// A truly FRESH store per case: wipe before use (a leftover dir from a
    /// previous run would reopen its stale state).
    std::string FreshDir( const char *lane )
    {
        auto           dir = NextUniqueDir( lane );
        std::error_code ec;
        std::filesystem::remove_all( dir, ec );
        return dir;
    }

    /// The suite's logging config (the resurrection-test shape).
    std::string LoggingConfig()
    {
        return R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: cold_downgrade_e2e_test
    sink: console
    level: error
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )";
    }

    /// A valid ENABLED PruningConfig parsed through the production
    /// FromJSON surface (never aggregate-initialized - the parse is part
    /// of what the legs prove). @p ack sets downgrade_acknowledged.
    sgns::PruningConfig PruningOn( bool ack = false )
    {
        rapidjson::Document doc;
        if ( ack )
        {
            doc.Parse( R"({"enabled": true, "finality_window": 10, "downgrade_acknowledged": true})" );
        }
        else
        {
            doc.Parse( R"({"enabled": true, "finality_window": 10})" );
        }
        return sgns::PruningConfig::FromJSON( doc );
    }

    /**
     * @brief      The downgrade-interlock fixture (the
     *             cold_evidence_matrix_test.cpp recipe, this suite's own
     *             lane dirs).
     *
     * The FP legs need only a REAL GlobalDB's node-local raw surface;
     * they build the LEAN stack (cold tier disabled). The 18-03 timeline
     * legs extend this fixture with the cold-enabled held-drainer
     * variants on the same recipe.
     */
    class ColdDowngradeE2ETest : public ::testing::Test
    {
    public:
        static void SetUpTestSuite()
        {
            auto log_system = std::make_shared<soralog::LoggingSystem>(
                std::make_shared<soralog::ConfiguratorFromYAML>(
                    std::make_shared<libp2p::log::Configurator>(), LoggingConfig() ) );
            (void)log_system->configure();
            libp2p::log::setLoggingSystem( log_system );
        }

        ~ColdDowngradeE2ETest() override
        {
            TeardownStack();
        }

        /// The stack teardown sequence (the cold_prune_gate discipline
        /// copied wholesale): clear seams, stop the blockchain, release
        /// the manager stack, quiesce the GlobalDB (stops the cold
        /// drainer + GC threads and releases the coldstore RocksDB dir
        /// lock), stop the pubsub host LAST.
        void TeardownStack()
        {
            // Fixture discipline: a hook armed by a failing case must never
            // leak further - clear EVERY static seam before any teardown
            // touches the stores.
            sgns::TxPruner::SetCycleInterruptHookForTest( nullptr );
            crdt::GlobalDB::SetColdUploadStateHookForTest( nullptr );
            crdt::GlobalDB::SetEraseInterruptHookForTest( nullptr );

            if ( blockchain_ )
            {
                (void)blockchain_->Stop();
            }
            tm_.reset();
            blockchain_.reset();
            account_.reset();

            if ( db_ )
            {
                db_->ShutdownNow();
            }
            db_.reset();
            try
            {
                if ( pubs_ )
                {
                    pubs_->Stop();
                }
            }
            catch ( const std::exception &err )
            {
                std::cerr << "GossipPubSub::Stop() exception: " << err.what() << std::endl;
            }
            pubs_.reset();
            io_.reset();
        }

    protected:
        /// Builds the single-node LEAN stack on a FRESH dir (cold tier
        /// disabled - the FP legs need only the raw surface). The 18-03
        /// timeline legs add the cold-enabled variants of this same shape.
        void BuildStack()
        {
            const std::string hot_dir = FreshDir( "hot" );
            keypair_path_            = FreshDir( "keypair" );

            io_ = std::make_shared<boost::asio::io_context>();

            pubs_ = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
                crdt::KeyPairFileStorage( keypair_path_ ).GetKeyPair().value() );
            ASSERT_NE( pubs_, nullptr );
            auto start_future = pubs_->Start( 0, {} );
            ASSERT_TRUE( PollUntil(
                [&] { return start_future.wait_for( std::chrono::milliseconds( 0 ) ) == std::future_status::ready; },
                std::chrono::milliseconds( 20000 ) ) )
                << "pubsub host failed to start";
            ASSERT_FALSE( start_future.get() ) << "GossipPubSub::Start failed";

            scheduler_ = std::make_shared<libp2p::basic::SchedulerImpl>(
                std::make_shared<libp2p::basic::AsioSchedulerBackend>( pubs_->GetAsioContext() ),
                libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
            network_   = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubs_->GetHost(), scheduler_ );
            generator_ = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();

            auto globaldb_ret = crdt::GlobalDB::New( io_,
                                                     hot_dir,
                                                     pubs_,
                                                     crdt::CrdtOptions::DefaultOptions(),
                                                     network_,
                                                     scheduler_,
                                                     generator_,
                                                     nullptr,
                                                     crdt::GlobalDB::BackupOptions{ false, 15, 12, true } );
            ASSERT_TRUE( globaldb_ret.has_value() ) << "GlobalDB failed to construct";
            db_ = std::move( globaldb_ret.value() );

            db_->AddListenTopic( kSeedTopic );
            db_->AddBroadcastTopic( kSeedTopic );
            (void)db_->Start(); // [[nodiscard]] - failure surfaces via the asserts below
            ASSERT_FALSE( db_->HasColdStorage() ) << "the FP legs run the LEAN (cold-off) stack";

            // The tx_pruner_test SetUp recipe over the db.
            sgns::GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage> {
                    return std::make_shared<MemorySecureStorage>( identifier );
                } );

            account_ = sgns::GeniusAccount::New( kTestTokenId, boost::filesystem::path( hot_dir ) / "account" );
            ASSERT_NE( account_, nullptr );

            auto load_result = account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() );
            ASSERT_TRUE( load_result.has_value() );

            blockchain_ = sgns::Blockchain::New( db_, account_, pubs_, []( outcome::result<void> ) {} );
            ASSERT_NE( blockchain_, nullptr );

            constexpr auto kTimestampTolerance = std::chrono::milliseconds( 300000 );
            constexpr auto kMutabilityWindow   = std::chrono::milliseconds( 600000 );

            tm_ = sgns::TransactionManager::New( db_,
                                                 io_,
                                                 account_,
                                                 blockchain_,
                                                 sgns::NodeType::Light,
                                                 0,
                                                 kTimestampTolerance,
                                                 kMutabilityWindow );
            ASSERT_NE( tm_, nullptr );
        }

        /// Raw-key buffer at the fingerprint key (the ONE home - the
        /// PruneEligibility static, never a hand-built literal).
        crdt::GlobalDB::Buffer FingerprintKey()
        {
            crdt::GlobalDB::Buffer key;
            key.put( std::string( sgns::PruneEligibility::PRUNE_ROLE_FINGERPRINT_KEY ) );
            return key;
        }

        // Declaration order == reverse destruction order.
        std::shared_ptr<boost::asio::io_context>                       io_;
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>               pubs_;
        std::shared_ptr<libp2p::basic::SchedulerImpl>                  scheduler_;
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network>     network_;
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator_;
        std::shared_ptr<crdt::GlobalDB>                                db_;
        std::shared_ptr<sgns::GeniusAccount>                           account_;
        std::shared_ptr<sgns::Blockchain>                              blockchain_;
        std::shared_ptr<sgns::TransactionManager>                      tm_;
        std::string                                                     keypair_path_;
    };

    //
    // The verdict matrix (SC4's mechanical half). Each leg drives the
    // PRODUCTION helper over a REAL GlobalDB raw surface.
    //

    /**
     * LEG FP-FirstRun (the birth-tiered-light control, Pitfall 2): a
     * fresh node has NO prior fingerprint record - GetRaw NOT_FOUND - so
     * the first CheckAndRewrite returns FirstRun and NEVER a downgrade
     * verdict. A fresh light node's first prune cycle is not an archive
     * conversion.
     */
    TEST_F( ColdDowngradeE2ETest, FirstRunBirthTieredLightNeverFires )
    {
        BuildStack();

        auto verdict = sgns::RoleFingerprint::CheckAndRewrite( *db_, PruningOn(), NodeType::Light, /*cold=*/true );
        ASSERT_TRUE( verdict.has_value() );
        EXPECT_EQ( verdict.value(), sgns::RoleFingerprint::Verdict::FirstRun );

        // The current posture was recorded: the record now exists.
        auto recorded = db_->GetRaw( FingerprintKey() );
        ASSERT_TRUE( recorded.has_value() );
        SGTransaction::RoleFingerprintRecord record;
        EXPECT_TRUE( record.ParseFromArray( recorded.value().data(),
                                            static_cast<int>( recorded.value().size() ) ) );
        EXPECT_EQ( static_cast<sgns::NodeType>( record.node_type() ), NodeType::Light );
        EXPECT_TRUE( record.pruning_enabled() );
        EXPECT_TRUE( record.cold_storage_enabled() );
    }

    /**
     * LEG FP-DowngradeFire: a prior archive-era posture (seeded through
     * the production helper - the legal seed) followed by
     * Light+pruning+no-ack detects the downgrade: DowngradeUnacknowledged.
     */
    TEST_F( ColdDowngradeE2ETest, DowngradeFireOnArchiveToLight )
    {
        BuildStack();

        // Seed the archive-era posture through the production helper.
        auto seed = sgns::RoleFingerprint::CheckAndRewrite( *db_, PruningOn(), NodeType::Archive, /*cold=*/true );
        ASSERT_TRUE( seed.has_value() );

        auto verdict = sgns::RoleFingerprint::CheckAndRewrite( *db_, PruningOn(), NodeType::Light, /*cold=*/true );
        ASSERT_TRUE( verdict.has_value() );
        EXPECT_EQ( verdict.value(), sgns::RoleFingerprint::Verdict::DowngradeUnacknowledged );
    }

    /**
     * LEG FP-AckArms: the identical detected downgrade WITH the explicit
     * ack key set returns DowngradeAcknowledged - operator consent arms.
     */
    TEST_F( ColdDowngradeE2ETest, AckArmsTheDetectedDowngrade )
    {
        BuildStack();

        auto seed = sgns::RoleFingerprint::CheckAndRewrite( *db_, PruningOn(), NodeType::Archive, /*cold=*/true );
        ASSERT_TRUE( seed.has_value() );

        auto verdict
            = sgns::RoleFingerprint::CheckAndRewrite( *db_, PruningOn( /*ack=*/true ), NodeType::Light, /*cold=*/true );
        ASSERT_TRUE( verdict.has_value() );
        EXPECT_EQ( verdict.value(), sgns::RoleFingerprint::Verdict::DowngradeAcknowledged );
    }

    /**
     * LEG FP-FullFires (the A2 ruling): a prior FULL posture fires the
     * interlock identically to a prior Archive one - ANY
     * ReplicatesAllAccounts prior role is a downgrade source.
     */
    TEST_F( ColdDowngradeE2ETest, FullSourceFiresLikeArchive )
    {
        BuildStack();

        auto seed = sgns::RoleFingerprint::CheckAndRewrite( *db_, PruningOn(), NodeType::Full, /*cold=*/true );
        ASSERT_TRUE( seed.has_value() );

        auto verdict = sgns::RoleFingerprint::CheckAndRewrite( *db_, PruningOn(), NodeType::Light, /*cold=*/true );
        ASSERT_TRUE( verdict.has_value() );
        EXPECT_EQ( verdict.value(), sgns::RoleFingerprint::Verdict::DowngradeUnacknowledged );
    }

    /**
     * LEG FP-UnreadableDisarm: corrupt bytes at the fingerprint key (the
     * key from the static; only the VALUE is garbage) disarm AND the
     * corrupt record SURVIVES the call un-overwritten - no laundering an
     * unreadable prior role through one reboot (the disarm persists until
     * operator repair).
     */
    TEST_F( ColdDowngradeE2ETest, UnreadableDisarmPreservesCorruptRecord )
    {
        BuildStack();

        // Deliberately unparseable value bytes at the sanctioned key.
        crdt::GlobalDB::Buffer corrupt;
        corrupt.put( std::string( "\x80\x80\x80\x80\x80", 5 ) );
        ASSERT_TRUE( db_->PutRaw( FingerprintKey(), corrupt ).has_value() );

        auto verdict = sgns::RoleFingerprint::CheckAndRewrite( *db_, PruningOn(), NodeType::Light, /*cold=*/true );
        ASSERT_TRUE( verdict.has_value() );
        EXPECT_EQ( verdict.value(), sgns::RoleFingerprint::Verdict::UnreadableDisarm );

        // The no-laundering contract: the corrupt bytes are STILL there.
        auto after = db_->GetRaw( FingerprintKey() );
        ASSERT_TRUE( after.has_value() );
        EXPECT_EQ( after.value().size(), corrupt.size() );
        EXPECT_EQ( std::string( after.value().toString() ), std::string( "\x80\x80\x80\x80\x80", 5 ) );
    }

    /**
     * LEG FP-SamePosture: Light+pruning twice - the second check reads
     * the posture the first wrote and returns NoDowngrade (restart of an
     * unchanged node never fires; also the idempotency edge).
     */
    TEST_F( ColdDowngradeE2ETest, SamePostureRestartNeverFires )
    {
        BuildStack();

        auto first = sgns::RoleFingerprint::CheckAndRewrite( *db_, PruningOn(), NodeType::Light, /*cold=*/true );
        ASSERT_TRUE( first.has_value() );
        EXPECT_EQ( first.value(), sgns::RoleFingerprint::Verdict::FirstRun );

        auto second = sgns::RoleFingerprint::CheckAndRewrite( *db_, PruningOn(), NodeType::Light, /*cold=*/true );
        ASSERT_TRUE( second.has_value() );
        EXPECT_EQ( second.value(), sgns::RoleFingerprint::Verdict::NoDowngrade );
    }

    /**
     * LEG CFG-AckParse (pure unit, no db): the ack key parses through the
     * production PruningConfig::FromJSON surface - present+bool yields
     * true, absent keeps false, wrong type keeps false without throwing
     * (the per-key type-checked absent-keeps-default posture).
     */
    TEST( ColdDowngradeConfigParse, AckKeyPerKeyTypeCheckedParse )
    {
        {
            rapidjson::Document doc;
            doc.Parse( R"({"enabled": true, "finality_window": 10, "downgrade_acknowledged": true})" );
            const auto config = sgns::PruningConfig::FromJSON( doc );
            EXPECT_TRUE( config.enabled );
            EXPECT_TRUE( config.downgrade_acknowledged );
        }
        {
            rapidjson::Document doc;
            doc.Parse( R"({"enabled": true, "finality_window": 10})" );
            const auto config = sgns::PruningConfig::FromJSON( doc );
            EXPECT_TRUE( config.enabled );
            EXPECT_FALSE( config.downgrade_acknowledged ) << "absent keeps default false";
        }
        {
            rapidjson::Document doc;
            doc.Parse( R"({"enabled": true, "finality_window": 10, "downgrade_acknowledged": "yes"})" );
            const auto config = sgns::PruningConfig::FromJSON( doc );
            EXPECT_TRUE( config.enabled );
            EXPECT_FALSE( config.downgrade_acknowledged ) << "ill-typed keeps default false (no throw)";
        }
    }
} // namespace
