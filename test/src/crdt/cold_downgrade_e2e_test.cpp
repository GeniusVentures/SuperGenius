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
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <sstream>
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

#include <sgns-blockstore/S3Client.hpp>

// Internal fakes - relative-include-dir privilege (test-surface only).
#include <sgns-blockstore/InMemoryS3AdminClient.hpp>
#include <sgns-blockstore/InMemoryS3Client.hpp>

#include "account/GeniusAccount.hpp"
#include "account/NodeType.hpp"
#include "account/TransferTransaction.hpp"
#include "account/UTXOManager.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "blockchain/Blockchain.hpp"
#include "crdt/crdt_datastore.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/globaldb/ColdStorageStack.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/wait_condition.hpp"
#include "transaction/PruneEligibility.hpp"
#include "transaction/TransactionManager.hpp"
#include "transaction/TxPruner.hpp"

using namespace sgns;

namespace sgns
{
    /**
     * Per-binary friend-seam definition (the tx_pruner_test shape): the
     * TxPruner tick gate requires State::READY, which no public API reaches
     * deterministically. This definition carries only ChangeState - the one
     * member the timeline legs use (the settled-cycle drive).
     */
    class TransactionManagerPendingLifecycleTestAccess
    {
    public:
        static void ChangeState( TransactionManager &manager, TransactionManager::State state )
        {
            manager.ChangeState( state );
        }
    };
} // namespace sgns

namespace
{
    const sgns::TokenID kTestTokenId = sgns::TokenID::FromBytes( { 0x00 } );

    /// The fixture's pre-seeded CRDT topic (the tx_pruner_test contract).
    constexpr char kSeedTopic[] = "CRDT.Datastore.TEST.Channel";

    /// Finality window N (the 17-04 recipes' value - two driver puts put
    /// the seeded spend beyond the window).
    constexpr uint64_t kWindow = 2;

    /// The account-generation tag the timeline stamps onto cycles (D-C4).
    constexpr uint64_t kTestGeneration = 7;

    /// Settle window: waited out through the settled-cycle helper.
    constexpr uint64_t kSmallSettleSeconds = 1;

    /// Fixed tx timestamp base (re-open determinism - wall-clock
    /// timestamps would fork CIDs; the matrix recipe's shape).
    constexpr int64_t kTimelineTxTsBaseMs = 1760000000000;

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

        //
        // The D-09 timeline machinery (the cold_evidence_matrix recipe
        // with the D-10 persistent remote tier): the fakes are FIXTURE
        // MEMBERS that survive teardown/rebuild and are re-injected
        // around every GlobalDB::New - modeling the durable remote tier
        // across the in-process restart.
        //

        /// Builds the timeline stack on the SHARED timeline dir with the
        /// cold tier ON and @p role at the TransactionManager::New role
        /// param (the t1 flip point). @p fault_put/@p fault_admin apply
        /// the held-drainer faults to the PERSISTENT fakes (Put faulted
        /// from t0 onward = no re-upload is even possible - the unchurned
        /// structural claim; the admin fault is cleared only for t3's
        /// GC-bearing posture).
        void BuildTimelineStack( NodeType    role,
                                 uint32_t   drain_interval_ms,
                                 uint64_t   retention_age_ms,
                                 uint32_t   gc_interval_ms,
                                 uint32_t   grace_ms,
                                 bool       fault_put,
                                 bool       fault_admin )
        {
            keypair_path_ = FreshDir( "keypair" );

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

            // The persistent remote tier: created once, re-injected before
            // every New, cleared after (the SetColdStorageStackClientsForTesting
            // inject-before/clear-after hygiene).
            if ( !fake_s3_ )
            {
                fake_s3_   = std::make_shared<sgns::blockstore::InMemoryS3Client>();
                fake_admin_ = std::make_shared<sgns::blockstore::InMemoryS3AdminClient>();
            }
            fake_s3_->ClearFaults();
            fake_admin_->SetAdminFault( sgns::blockstore::S3AdminFaultKind::None );
            if ( fault_put )
            {
                fake_s3_->SetFault( sgns::blockstore::S3Op::Put,
                                    sgns::blockstore::S3FaultKind::Unavailable );
            }
            if ( fault_admin )
            {
                fake_admin_->SetAdminFault( sgns::blockstore::S3AdminFaultKind::Unreachable );
            }

            crdt::GlobalDB::ColdStorageOptions cold;
            cold.enabled   = true;
            cold.endpoint  = "hermetic.invalid"; // overridden - never contacted
            cold.bucket    = "downgrade-e2e";
            cold.accessKey = "test";
            cold.secretKey = "test";
            cold.backpressure.drain_interval_ms = drain_interval_ms;
            cold.backpressure.warn_pending      = 100000;
            cold.backpressure.critical_pending  = 200000;
            cold.backpressure.backoff_cap_ms    = 1000;
            cold.durability.flush_min_interval_ms = 50;
            cold.durability.flush_max_interval_ms = 300;
            cold.retention.retention_age_ms     = retention_age_ms;
            cold.retention.gc_interval_ms       = gc_interval_ms;
            cold.retention.grace_ms             = grace_ms;
            sgns::crdt::SetColdStorageStackClientsForTesting(
                std::static_pointer_cast<sgns::blockstore::S3Client>( fake_s3_ ),
                std::static_pointer_cast<sgns::blockstore::S3AdminClient>( fake_admin_ ) );

            auto globaldb_ret = crdt::GlobalDB::New( io_,
                                                     timeline_hot_dir_,
                                                     pubs_,
                                                     crdt::CrdtOptions::DefaultOptions(),
                                                     network_,
                                                     scheduler_,
                                                     generator_,
                                                     nullptr,
                                                     crdt::GlobalDB::BackupOptions{ false, 15, 12, true },
                                                     cold );
            ASSERT_TRUE( globaldb_ret.has_value() ) << "GlobalDB failed to construct";
            db_ = std::move( globaldb_ret.value() );

            // Hygiene: the process-global override has served its purpose.
            sgns::crdt::SetColdStorageStackClientsForTesting( nullptr, nullptr );

            db_->AddListenTopic( kSeedTopic );
            db_->AddBroadcastTopic( kSeedTopic );
            (void)db_->Start();
            ASSERT_TRUE( db_->HasColdStorage() ) << "the timeline runs the cold-enabled stack";

            sgns::GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage> {
                    return std::make_shared<MemorySecureStorage>( identifier );
                } );

            account_ = sgns::GeniusAccount::New( kTestTokenId,
                                                 boost::filesystem::path( timeline_hot_dir_ ) / "account" );
            ASSERT_NE( account_, nullptr );

            auto load_result = account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() );
            ASSERT_TRUE( load_result.has_value() );

            blockchain_ = sgns::Blockchain::New( db_, account_, pubs_, []( outcome::result<void> ) {} );
            ASSERT_NE( blockchain_, nullptr );

            constexpr auto kTimestampTolerance = std::chrono::milliseconds( 300000 );
            constexpr auto kMutabilityWindow   = std::chrono::milliseconds( 600000 );

            // D-09's t1 flip point: the SAME dirs + cold config, only the
            // role param changes across re-opens.
            tm_ = sgns::TransactionManager::New( db_,
                                                 io_,
                                                 account_,
                                                 blockchain_,
                                                 role,
                                                 0,
                                                 kTimestampTolerance,
                                                 kMutabilityWindow );
            ASSERT_NE( tm_, nullptr );
        }

        //
        // The tx-seeding + pruner-drive kit (the cold_compose_matrix
        // recipes verbatim - named copy).
        //

        /// Transfer with @p output_count outputs owned by this account,
        /// FIXED timestamp (re-open determinism).
        std::shared_ptr<sgns::TransferTransaction> MakeTimelineTransfer( uint64_t nonce, size_t output_count )
        {
            SGTransaction::DAGStruct dag;
            dag.set_type( "transfer" );
            dag.set_source_addr( account_->GetAddress() );
            dag.set_nonce( nonce );
            dag.set_timestamp( kTimelineTxTsBaseMs + static_cast<int64_t>( nonce ) );

            std::vector<OutputDestInfo> destinations;
            destinations.reserve( output_count );
            for ( size_t i = 0; i < output_count; ++i )
            {
                destinations.push_back( { 100 + i, account_->GetAddress(), kTestTokenId } );
            }
            return std::make_shared<sgns::TransferTransaction>(
                sgns::TransferTransaction::New( {}, std::move( destinations ), std::move( dag ) ) );
        }

        /// Real-topic value Put + raw tx->CID map entry.
        void SeedTransaction( const std::shared_ptr<GeniusTransaction> &transaction )
        {
            const std::string tx_path = TransactionManager::GetTransactionPath( *transaction );

            crdt::GlobalDB::Buffer serialized;
            serialized.put( transaction->SerializeByteVector() );
            auto put_result = db_->Put( { tx_path }, serialized, { kSeedTopic } );
            ASSERT_TRUE( put_result.has_value() );

            crdt::GlobalDB::Buffer key_buffer;
            key_buffer.put( tx_path );
            crdt::GlobalDB::Buffer value_buffer;
            value_buffer.put( put_result.value().toString().value() );
            ASSERT_TRUE( db_->GetDataStore()->put( key_buffer, value_buffer ).has_value() );
        }

        /// Plain topic Put of a raw string.
        void PutSeedValue( const std::string &key, const std::string &value )
        {
            crdt::GlobalDB::Buffer buffer;
            buffer.put( value );
            ASSERT_TRUE( db_->Put( { key }, buffer, { kSeedTopic } ).has_value() );
        }

        /// Tracks a candidate's produced outputs in the UTXO manager.
        void TrackOutputs( const sgns::GeniusTransaction &transaction )
        {
            auto outputs = transaction.GetProducedUTXOs();
            ASSERT_TRUE( outputs.has_value() );
            for ( const auto &output : *outputs )
            {
                auto put = account_->GetUTXOManager().PutUTXO( output, output.GetOwnerAddress() );
                ASSERT_TRUE( put.has_value() );
                ASSERT_TRUE( put.value() );
            }
        }

        /// Spends one output by @p spender.
        bool ConsumeOutput( const sgns::GeniusTransaction &transaction,
                            uint32_t                       output_idx,
                            const sgns::GeniusTransaction &spender )
        {
            auto txid = sgns::base::Hash256::fromReadableString( transaction.GetHash() );
            EXPECT_FALSE( txid.has_error() );
            auto consumed = account_->GetUTXOManager().ConsumeUTXOs( { { txid.value(), output_idx, {} } },
                                                                     account_->GetAddress(),
                                                                     sgns::UTXOManager::UTXOType::UTXO_NORMAL,
                                                                     SpenderHash( spender ) );
            EXPECT_TRUE( consumed.has_value() );
            return consumed.has_value() && consumed.value();
        }

        static sgns::base::Hash256 SpenderHash( const sgns::GeniusTransaction &transaction )
        {
            return sgns::base::Hash256::fromReadableString( transaction.GetHash() ).value();
        }

        /// Polls the REAL journal to Durable for @p cid.
        bool WaitDurable( const sgns::CID &cid )
        {
            return PollUntil( [&] { return db_->GetColdUploadState( cid ) == crdt::ColdUploadState::Durable; },
                              std::chrono::milliseconds( 20000 ) );
        }

        /// The concrete DAG syncer behind the broadcaster.
        static std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer> syncerOf( sgns::crdt::GlobalDB &db )
        {
            return std::static_pointer_cast<sgns::crdt::GraphsyncDAGSyncer>( db.GetBroadcaster()->GetDagSyncer() );
        }

        /// Constructs a Light TxPruner with the suite's window.
        std::shared_ptr<sgns::TxPruner> MakePruner( uint64_t budget, uint64_t settle_seconds )
        {
            sgns::PruningConfig config;
            config.enabled              = true;
            config.dry_run              = false;
            config.finality_window      = kWindow;
            config.max_erases_per_cycle = budget;
            config.settle_seconds       = settle_seconds;
            auto pruner                 = sgns::TxPruner::New( sgns::NodeType::Light, config );
            EXPECT_TRUE( pruner.has_value() );
            return pruner.has_value() ? pruner.value() : nullptr;
        }

        /// Drives the manager into the tick gate's READY state.
        void ForceManagerReady()
        {
            TransactionManagerPendingLifecycleTestAccess::ChangeState( *tm_,
                                                                       sgns::TransactionManager::State::READY );
            ASSERT_EQ( tm_->GetState(), sgns::TransactionManager::State::READY );
        }

        /// Runs cycles until one is NOT gate-skipped, delivering its stats.
        void RunSettledCycle( sgns::TxPruner &pruner, sgns::PruneCycleStats &out_stats )
        {
            ForceManagerReady();
            bool cycle_failed = false;
            bool ran          = false;
            ASSERT_WAIT_FOR_CONDITION(
                [&]
                {
                    auto result = pruner.RunCycle( *db_, *tm_, account_->GetUTXOManager(), kTestGeneration );
                    if ( result.has_error() )
                    {
                        cycle_failed = true;
                        return true; // exit the bounded wait; EXPECT below fails loudly
                    }
                    out_stats = result.value();
                    ran       = pruner.GetPruneStatus().watermark.has_value();
                    return ran;
                },
                std::chrono::milliseconds( kSmallSettleSeconds * 1000 + 8000 ),
                "tick gate settles and one prune cycle runs to completion",
                nullptr );
            EXPECT_FALSE( cycle_failed );
            EXPECT_TRUE( ran );
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
        /// D-10's persistent remote tier: fixture members surviving
        /// teardown/rebuild (re-injected around every GlobalDB::New).
        std::shared_ptr<sgns::blockstore::InMemoryS3Client>       fake_s3_;
        std::shared_ptr<sgns::blockstore::InMemoryS3AdminClient>  fake_admin_;
        /// The D-09 timeline's shared hot dir (same DBs + cold config
        /// across re-opens; only the role flips).
        std::string                                                     timeline_hot_dir_;
    };

    /**
     * LEG FP-UnackedPersists (the 18-REVIEW F-1 regression pin): the
     * interlock holds across ANY number of unacknowledged restarts. The
     * unacknowledged arm PRESERVES the archive-era record (no rewrite),
     * so a second unacknowledged check fires DowngradeUnacknowledged
     * AGAIN - a supervisor crash-loop is not consent - and the record
     * still parses as Archive until the ack key is set.
     */
    TEST_F( ColdDowngradeE2ETest, UnacknowledgedDowngradePersistsAcrossRestarts )
    {
        BuildStack();

        auto seed = sgns::RoleFingerprint::CheckAndRewrite( *db_, PruningOn(), NodeType::Archive, /*cold=*/true );
        ASSERT_TRUE( seed.has_value() );

        auto first = sgns::RoleFingerprint::CheckAndRewrite( *db_, PruningOn(), NodeType::Light, /*cold=*/true );
        ASSERT_TRUE( first.has_value() );
        EXPECT_EQ( first.value(), sgns::RoleFingerprint::Verdict::DowngradeUnacknowledged );

        // The restart-without-ack shape: the verdict must fire AGAIN.
        auto second = sgns::RoleFingerprint::CheckAndRewrite( *db_, PruningOn(), NodeType::Light, /*cold=*/true );
        ASSERT_TRUE( second.has_value() );
        EXPECT_EQ( second.value(), sgns::RoleFingerprint::Verdict::DowngradeUnacknowledged )
            << "an unacknowledged downgrade must NOT arm on the second boot "
               "(the prior record was rewritten - the F-1 laundering hazard)";

        // And the record still carries the archive prior.
        auto recorded = db_->GetRaw( FingerprintKey() );
        ASSERT_TRUE( recorded.has_value() );
        SGTransaction::RoleFingerprintRecord record;
        ASSERT_TRUE( record.ParseFromArray( recorded.value().data(),
                                            static_cast<int>( recorded.value().size() ) ) );
        EXPECT_EQ( static_cast<sgns::NodeType>( record.node_type() ), NodeType::Archive );
    }

    //
    // The GeniusNode wiring source gate (18-REVIEW F-3): the interlock's
    // gate ordering and WARN texts are production behavior - deleting the
    // block must fail a test, not just a Select-String. The
    // EraseSurfaceGateTest source-scan precedent.
    //

    /// Reads the whole source file (the crdt_erase_test readWholeFile shape).
    std::string ReadWholeSourceFile( const std::filesystem::path &path )
    {
        std::ifstream      in( path, std::ios::binary );
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    /**
     * LEG WIRE-GATE (18-REVIEW F-3): pins StartPruneTimerIfConfigured's
     * interlock wiring in GeniusNode.cpp - the CheckAndRewrite call ABOVE
     * the pruning-enabled early return, the two disarm WARNs between the
     * Light check and the TxPruner::New arm (naming the ack key + the
     * notes/18 checklist path), and the tiered-light posture INFO line.
     */
    TEST( ColdDowngradeWiringGate, InterlockGateWiringPinnedInGeniusNode )
    {
#ifdef SUPERGNUS_DOWNGRADE_GATE_SRC
        const std::filesystem::path node_src = std::filesystem::path( SUPERGNUS_DOWNGRADE_GATE_SRC )
                                                   / "account" / "GeniusNode.cpp";
        ASSERT_TRUE( std::filesystem::exists( node_src ) ) << "GeniusNode.cpp missing at " << node_src;
        const std::string text = ReadWholeSourceFile( node_src );
        ASSERT_FALSE( text.empty() );

        const auto consult_pos      = text.find( "RoleFingerprint::CheckAndRewrite" );
        const auto enabled_gate_pos = text.find( "!pruning_config_.enabled" );
        const auto light_check_pos  = text.find( "is not Light" );
        const auto downgrade_warn   = text.find( "ROLE DOWNGRADE DETECTED" );
        const auto unreadable_warn  = text.find( "role fingerprint unreadable" );
        const auto txpruner_arm     = text.find( "TxPruner::New" );
        const auto posture_line     = text.find( "tiered-light posture" );

        ASSERT_NE( consult_pos, std::string::npos ) << "the fingerprint consult call is missing";
        ASSERT_NE( enabled_gate_pos, std::string::npos );
        ASSERT_NE( light_check_pos, std::string::npos );
        ASSERT_NE( downgrade_warn, std::string::npos ) << "the downgrade WARN literal is missing";
        ASSERT_NE( unreadable_warn, std::string::npos ) << "the unreadable WARN literal is missing";
        ASSERT_NE( txpruner_arm, std::string::npos );
        ASSERT_NE( posture_line, std::string::npos ) << "the tiered-light posture INFO line is missing";

        // Ordering: the consult runs ABOVE the pruning-enabled early
        // return (an archive-era node runs pruning-disabled; a write gated
        // on pruning-enabled would leave every later downgrade blind).
        EXPECT_LT( consult_pos, enabled_gate_pos )
            << "the fingerprint check must run before the pruning-enabled early return";
        // The disarm WARNs sit between the Light check and the arm.
        EXPECT_LT( light_check_pos, downgrade_warn );
        EXPECT_LT( downgrade_warn, txpruner_arm );
        // The downgrade WARN carries the ack-key instruction and the
        // checklist path citation.
        EXPECT_NE( text.find( "downgrade_acknowledged" ), std::string::npos );
        EXPECT_NE( text.find( "notes/18-collectibility-policy-downgrade-interlock.md" ), std::string::npos );
#else
        ADD_FAILURE() << "SUPERGNUS_DOWNGRADE_GATE_SRC is not defined - the wiring gate cannot run";
#endif
    }

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

    /**
     * The D-09/D-12 single-node in-process re-open timeline, t0 through
     * t2 (SC2's downgraded half, SC3's no-re-drain the strong way, SC4's
     * markers + refusal + interlock). Cross-node semantics ride
     * compositionally on landed Phase 15 SERVE-04 + develop SURV-02/19-07
     * (D-10 - recorded so verification does not re-litigate it).
     *
     * t0: archive-open over the persistent fake S3 (fast drain), the
     *     linked spend topology seeded through the REAL CRDT Put path,
     *     all-Durable, archive-posture fingerprint written (FirstRun),
     *     then the fakes are Put/admin-faulted for the WHOLE rest of the
     *     timeline (held drainer + structurally-unchurned remote).
     * t1: light re-open on the SAME dirs (the role flip) - the interlock
     *     fires DowngradeUnacknowledged, zero markers exist while
     *     disarmed, the t0 remote objects are intact, and the ack leg
     *     returns DowngradeAcknowledged (the FP-AckArms re-seed shape -
     *     the unack check just rewrote the record to the light posture).
     * t2: light+ack rebuild with the drainer structurally held (faulted
     *     fakes - no drain pass or flush cycle can advance anything):
     *     TxPruner::RunCycle erases archive-era groups (erased > 0 -
     *     SC2's prunes-under-light-rules), the erased groups' journal
     *     records stay Durable across the cycle (SC3: existing Durable
     *     evidence admitted, no re-drain), markers flip NOT_FOUND->PRUNED
     *     (SC4), GetNodeWithoutRequest on the pruned CID refuses with
     *     HISTORY_PRUNED_LOCALLY (SC4 + the D-10 failover signal), and
     *     every t0 fake-S3 object is still present (remote unchurned).
     */
    TEST_F( ColdDowngradeE2ETest, TimelineT0ThroughT3 )
    {
        timeline_hot_dir_ = FreshDir( "timeline" );

        // The linked spend topology's CIDs (captured t0, asserted t1/t2/t3).
        std::string           victim_cid_string;
        std::string           spender_cid_string;
        std::vector<std::string> t0_object_keys;

        //
        // T0 - ARCHIVE ERA.
        //
        BuildTimelineStack( NodeType::Archive,
                            /*drain_interval_ms=*/100,
                            /*retention_age_ms=*/0, // GC never runs in t0/t1/t2
                            /*gc_interval_ms=*/0,
                            /*grace_ms=*/0,
                            /*fault_put=*/false,
                            /*fault_admin=*/false );

        auto spender = MakeTimelineTransfer( 101, 1 );
        auto victim  = MakeTimelineTransfer( 102, 1 );
        SeedTransaction( spender );
        SeedTransaction( victim );
        TrackOutputs( *victim );
        ASSERT_TRUE( ConsumeOutput( *victim, 0, *spender ) );
        // Driver puts advance the topic height past the finality window
        // (the crossfire recipe: two puts put the spend beyond kWindow).
        PutSeedValue( "/timeline/driver-1", "d1" );
        PutSeedValue( "/timeline/driver-2", "d2" );

        // The victim's constituent CID (the raw tx->CID map entry).
        {
            const std::string victim_path = TransactionManager::GetTransactionPath( *victim );
            auto              entry       = db_->GetRaw( crdt::GlobalDB::Buffer{}.put( victim_path ) );
            ASSERT_TRUE( entry.has_value() );
            victim_cid_string = std::string( entry.value().toString() );
            ASSERT_FALSE( victim_cid_string.empty() );
        }
        // The spender's CID (the non-pruned below-link block - t3's
        // eviction + remote-backed-serving target).
        {
            const std::string spender_path = TransactionManager::GetTransactionPath( *spender );
            auto              entry        = db_->GetRaw( crdt::GlobalDB::Buffer{}.put( spender_path ) );
            ASSERT_TRUE( entry.has_value() );
            spender_cid_string = std::string( entry.value().toString() );
            ASSERT_FALSE( spender_cid_string.empty() );
        }

        // All-Durable: journal records Durable + the remote objects exist.
        {
            auto victim_cid = sgns::CID::fromString( victim_cid_string );
            ASSERT_FALSE( victim_cid.has_error() );
            ASSERT_TRUE( WaitDurable( victim_cid.value() ) ) << "victim block never reached Durable";
        }
        // [19-05 dispatch-7 tart find] The victim wait alone does NOT
        // imply the seed set finished uploading: four blocks exist at
        // this point (spender, victim, driver-1, driver-2) and on a
        // slow Debug runner the LAST seed's drain can lag the victim's
        // by a drain interval - the keys capture then took a 3-key
        // baseline and EVERY later unchurned comparison failed against
        // it (run 45489840538: current=4 keys vs t0=3). Gate the
        // capture on the full deterministic fact: nothing pending, all
        // four records Durable.
        ASSERT_TRUE( PollUntil(
            [&] {
                auto status = db_->GetColdStorageStatus();
                return status.pending_upload_count == 0 && status.durable_upload_count >= 4;
            },
            std::chrono::milliseconds( 20000 ) ) )
            << "t0: the seeded blocks never all reached Durable before the keys capture";
        t0_object_keys = fake_s3_->ObjectKeys();
        ASSERT_FALSE( t0_object_keys.empty() ) << "the fake S3 must hold the seeded objects";

        // t0 leg 0: the archive-posture fingerprint (FirstRun on the
        // fresh dir; the record now parses as Archive).
        {
            auto verdict = sgns::RoleFingerprint::CheckAndRewrite( *db_, PruningOn(), NodeType::Archive, true );
            ASSERT_TRUE( verdict.has_value() );
            EXPECT_EQ( verdict.value(), sgns::RoleFingerprint::Verdict::FirstRun );
            auto recorded = db_->GetRaw( FingerprintKey() );
            ASSERT_TRUE( recorded.has_value() );
            SGTransaction::RoleFingerprintRecord record;
            ASSERT_TRUE( record.ParseFromArray( recorded.value().data(),
                                                static_cast<int>( recorded.value().size() ) ) );
            EXPECT_EQ( static_cast<NodeType>( record.node_type() ), NodeType::Archive );
        }

        // The held posture begins: no re-upload is even possible from
        // here to the end of the timeline (unchurned is structural).
        fake_s3_->SetFault( sgns::blockstore::S3Op::Put, sgns::blockstore::S3FaultKind::Unavailable );
        fake_admin_->SetAdminFault( sgns::blockstore::S3AdminFaultKind::Unreachable );

        TeardownStack();

        //
        // T1 - LIGHT RE-OPEN (the D-09 flip, the D-06 integration).
        //
        BuildTimelineStack( NodeType::Light,
                            /*drain_interval_ms=*/60000, // held
                            /*retention_age_ms=*/0,
                            /*gc_interval_ms=*/0,
                            /*grace_ms=*/0,
                            /*fault_put=*/true,
                            /*fault_admin=*/true );

        {
            auto verdict = sgns::RoleFingerprint::CheckAndRewrite( *db_, PruningOn(), NodeType::Light, true );
            ASSERT_TRUE( verdict.has_value() );
            EXPECT_EQ( verdict.value(), sgns::RoleFingerprint::Verdict::DowngradeUnacknowledged )
                << "the archive-era record must fire the interlock on the light re-open";
        }
        {
            // Zero markers while disarmed: nothing pruned at t1.
            auto marker = sgns::PruneEligibility::LookupPrunedBlockEntry( *db_, victim_cid_string );
            ASSERT_TRUE( marker.has_value() );
            EXPECT_EQ( marker.value().first, sgns::PrunedBlockLookup::NOT_FOUND );
        }
        EXPECT_EQ( fake_s3_->ObjectKeys(), t0_object_keys ) << "the remote tier is intact across the re-open";

        // The ack leg: the unacknowledged check above PRESERVED the
        // archive-era record (no rewrite on the unack'd arm), so the ack
        // config reads the same archive prior and returns
        // DowngradeAcknowledged directly.
        {
            auto acked
                = sgns::RoleFingerprint::CheckAndRewrite( *db_, PruningOn( /*ack=*/true ), NodeType::Light, true );
            ASSERT_TRUE( acked.has_value() );
            EXPECT_EQ( acked.value(), sgns::RoleFingerprint::Verdict::DowngradeAcknowledged );
        }

        TeardownStack();

        //
        // T2 - PRUNE CYCLE UNDER LIGHT RULES (SC3 held-drainer + SC4
        // markers/refusal). The drainer is structurally dead: the
        // persistent fakes carry the faults, so no drain pass or flush
        // cycle can advance ANY journal record.
        //
        BuildTimelineStack( NodeType::Light,
                            /*drain_interval_ms=*/60000, // held
                            /*retention_age_ms=*/0,
                            /*gc_interval_ms=*/0,
                            /*grace_ms=*/0,
                            /*fault_put=*/true,
                            /*fault_admin=*/true );

        {
            // Post-ack armed posture (prev = the light posture the ack
            // check rewrote; the pruner arms through this verdict).
            auto verdict = sgns::RoleFingerprint::CheckAndRewrite( *db_, PruningOn( /*ack=*/true ), NodeType::Light, true );
            ASSERT_TRUE( verdict.has_value() );
            EXPECT_EQ( verdict.value(), sgns::RoleFingerprint::Verdict::NoDowngrade );
        }

        sgns::PruneCycleStats stats;
        {
            auto pruner = MakePruner( 10, kSmallSettleSeconds );
            ASSERT_NE( pruner, nullptr );
            RunSettledCycle( *pruner, stats );
        }
        EXPECT_GT( stats.erased, 0u ) << "SC2: light rules erase archive-era groups";

        // SC3 the strong way: the erased group's journal record STILL
        // reads Durable after the cycle - the drainer provably cannot
        // re-advance anything (faulted fakes), so existing Durable
        // evidence satisfied the Phase 14 prune gate with no re-drain.
        {
            auto victim_cid = sgns::CID::fromString( victim_cid_string );
            ASSERT_FALSE( victim_cid.has_error() );
            EXPECT_EQ( db_->GetColdUploadState( victim_cid.value() ), crdt::ColdUploadState::Durable )
                << "SC3: the journal record stays Durable across the erase";
        }

        // SC4: the marker flipped NOT_FOUND -> PRUNED (produced by the
        // REAL erase path - never hand-built).
        {
            auto marker = sgns::PruneEligibility::LookupPrunedBlockEntry( *db_, victim_cid_string );
            ASSERT_TRUE( marker.has_value() );
            EXPECT_EQ( marker.value().first, sgns::PrunedBlockLookup::PRUNED )
                << "SC4: markers accumulate post-downgrade";
        }

        // SC4 + D-10's compositional signal: the distinct local refusal.
        {
            auto victim_cid = sgns::CID::fromString( victim_cid_string );
            ASSERT_FALSE( victim_cid.has_error() );
            auto syncer  = syncerOf( *db_ );
            auto resolved = syncer->GetNodeWithoutRequest( victim_cid.value() );
            ASSERT_TRUE( resolved.has_error() );
            EXPECT_EQ( resolved.error(), sgns::crdt::GraphsyncDAGSyncer::Error::HISTORY_PRUNED_LOCALLY )
                << "SC4: refusal activates on the pruned CID - the distinct signal "
                   "requesters fail over on (D-10)";
        }

        // SC2: the remote tier is unchurned - every t0 object still
        // present (structurally: no successful Put since t0).
        EXPECT_EQ( fake_s3_->ObjectKeys(), t0_object_keys );

        TeardownStack();

        //
        // T3 - STEADY STATE (D-12). The GC runs on this stage's stack
        // (retention 1ms - every t0 spill timestamp is provably old; a
        // 50 ms cadence + 1 ms grace), so the admin fault CLEARS (the
        // staging gate needs a healthy admin) while the Put fault STAYS:
        // unchurned remains structural, and with every journal record
        // already Durable no drain Put can even be attempted.
        //
        BuildTimelineStack( NodeType::Light,
                            /*drain_interval_ms=*/60000,
                            /*retention_age_ms=*/1,
                            /*gc_interval_ms=*/50,
                            /*grace_ms=*/1,
                            /*fault_put=*/true,
                            /*fault_admin=*/false );

        const auto victim_cid  = sgns::CID::fromString( victim_cid_string );
        const auto spender_cid = sgns::CID::fromString( spender_cid_string );
        ASSERT_FALSE( victim_cid.has_error() );
        ASSERT_FALSE( spender_cid.has_error() );

        // The 18-01 exemption carrying a REAL post-downgrade graph
        // in-band: the pruned victim link is an evidenced gap; the walk
        // completes and the exempted observable is live.
        ASSERT_TRUE( PollUntil(
            [&] { return db_->GetColdStorageStatus().gc_last_pass_exempted > 0; },
            std::chrono::milliseconds( 20000 ) ) )
            << "the GC pass never survived the evidenced gap (exemption not carrying the walk)";

        // The below-link spender block became walk-unreachable the
        // moment the victim turned into an exempted leaf - retention
        // evicts its LOCAL cold copy through the real GC path.
        ASSERT_TRUE( PollUntil( [&] { return db_->IsColdDisposed( spender_cid.value() ); },
                                std::chrono::milliseconds( 20000 ) ) )
            << "the below-link non-pruned block was never retention-evicted";

        // Remote-backed serving for NON-pruned cold data: the spender's
        // hot + local copies are gone (the eviction's two halves); the
        // persistent fake (Get unfaulted) serves the object and the read
        // rehydrates - the steady-state serving posture.
        {
            auto syncer = syncerOf( *db_ );
            ASSERT_TRUE( PollUntil(
                [&] { return syncer->GetNodeWithoutRequest( spender_cid.value() ).has_value(); },
                std::chrono::milliseconds( 20000 ) ) )
                << "remote-backed serving of the evicted non-pruned block never recovered";
        }

        // The refusal persists into the steady state.
        {
            auto syncer   = syncerOf( *db_ );
            auto resolved = syncer->GetNodeWithoutRequest( victim_cid.value() );
            ASSERT_TRUE( resolved.has_error() );
            EXPECT_EQ( resolved.error(), sgns::crdt::GraphsyncDAGSyncer::Error::HISTORY_PRUNED_LOCALLY );
        }

        // Seals untouched by the pass (governing invariant 3).
        {
            auto syncer = syncerOf( *db_ );
            EXPECT_TRUE( syncer->isResolved( victim_cid.value() ).value() );
            EXPECT_TRUE( syncer->isResolved( spender_cid.value() ).value() );
        }

        // Cold is permanent this milestone: every t0 remote object
        // remains (the pruned victim's bytes included - nothing deleted).
        EXPECT_EQ( fake_s3_->ObjectKeys(), t0_object_keys );

        TeardownStack();
    }
} // namespace
