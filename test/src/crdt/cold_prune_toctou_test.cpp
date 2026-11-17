/**
 * @file       cold_prune_toctou_test.cpp
 * @brief      Red-first TOCTOU counter-test for the in-compose
 *             cold-durability re-check (v1.2 Phase 14 plan 14-02 /
 *             PRUNE-02, D-06/D-07): a durability-state regression
 *             (Durable->Accepted or Durable->NoRecord) induced BETWEEN
 *             gate admission and the erase call must be REFUSED — the hot
 *             value survives, no /prune/ marker exists, the deferral
 *             lands in cold_deferred_groups, and stats.errors does NOT
 *             grow (Pitfall 1: a refusal is a counter, never a failure
 *             reason).
 *
 *             INDUCED REGRESSION (why injection, not a driven flip): the
 *             real journal is monotone in-process (research Q1 — no
 *             Durable->Accepted path exists; the only Durable-record
 *             removals are GC disposal paths yielding NoRecord, which the
 *             gate already defers), and the coldstore RocksDB is locked
 *             by the live GlobalDB. The regression is therefore INJECTED
 *             at the exact read seam the admission gate and the in-compose
 *             guard share: GlobalDB::SetColdUploadStateHookForTest
 *             (TEST-ONLY, PRODUCTION MUST NEVER ARM IT) — a flip there IS
 *             "the state read changed between admission and compose".
 *
 *             POSITION DISCIPLINE (Pitfall 2 — do not relax): the flip is
 *             armed ONLY at the TxPruner-side step-5 seam
 *             (kInterruptAfterColdGate — after cold admission, before the
 *             per-tx erase loop). This test NEVER arms GlobalDB's
 *             erase-hook steps 1/2/3: those positions are mid-compose,
 *             AFTER the in-compose re-check, and are unprovable by any
 *             read-then-commit implementation — the post-re-check window
 *             is closed by journal monotonicity (research Q1) and is
 *             Phase 16's Interaction-C ruling surface, not this test's.
 *
 *             Red-first per D-07: authored against the 14-01 gate
 *             (admission but NO in-compose re-check), observed failing
 *             with the destroying-erase signature (the flipped state is
 *             read never, the erase proceeds, erased==1 and the value is
 *             gone) — then guard 5 lands in EraseEntry's compose critical
 *             section and this suite goes green. Every assertion's
 *             failure message carries the literal token TOCTOU so the
 *             red evidence is identifiable in the run output.
 *
 *             Fixture: the cold_prune_gate_test fixture family (the
 *             hermetic cold GlobalDB recipe + the tx_pruner_test
 *             construction pattern), single-group seeding (one eligible
 *             spent-beyond-window transaction driven to REAL Durable via
 *             the injected fake S3 + fake-admin flush — LEG 1's drive).
 * @date       2026-11-16
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <libp2p/basic/scheduler.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <libp2p/host/host.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <soralog/impl/configurator_from_yaml.hpp>

#include <sgns-blockstore/S3AdminClient.hpp>
#include <sgns-blockstore/S3Client.hpp>

// Internal fakes - relative-include-dir privilege (test-surface only).
#include <sgns-blockstore/InMemoryS3AdminClient.hpp>
#include <sgns-blockstore/InMemoryS3Client.hpp>

#include "account/GeniusAccount.hpp"
#include "account/NodeType.hpp"
#include "account/TransferTransaction.hpp"
#include "account/UTXOManager.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "base/logger.hpp"
#include "blockchain/Blockchain.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/globaldb/ColdStorageStack.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "crdt/hierarchical_key.hpp"
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
     * deterministically. This definition carries only ChangeState.
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

    /// Finality window N (the 17-04 recipes' value).
    constexpr uint64_t kWindow = 2;

    /// The account-generation tag the tests stamp onto cycles (D-C4).
    constexpr uint64_t kTestGeneration = 7;

    /// Settle window: waited out through the settled-cycle helper.
    constexpr uint64_t kSmallSettleSeconds = 1;

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

    /// SHORT relative path under the build tree (Windows MAX_PATH; the
    /// upload_drainer_test FreshDir/NextUniqueDir discipline). Own lane
    /// prefix so parallel ctest binaries never collide on dirs.
    std::string NextUniqueDir( const char *lane )
    {
        static std::atomic<uint64_t> next{ 0 };
        return std::string( "test-data/cold-prune-toctou/" ) + lane + "-"
             + std::to_string( next.fetch_add( 1, std::memory_order_relaxed ) );
    }

    std::string FreshDir( const char *lane )
    {
        auto           dir = NextUniqueDir( lane );
        std::error_code ec;
        std::filesystem::remove_all( dir, ec );
        return dir;
    }

    std::string LoggingConfig()
    {
        return R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: cold_prune_toctou_test
    sink: console
    level: error
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )";
    }

    /**
     * @brief  The TOCTOU counter-test fixture: the cold_prune_gate_test
     *         fixture family (hermetic cold GlobalDB + real
     *         TransactionManager + TxPruner) parameterized over the
     *         regression vector {Accepted, NoRecord} — the two
     *         non-Durable states a flipped Durable record can read.
     */
    class ColdPruneToctouTest : public ::testing::Test,
                                public ::testing::WithParamInterface<sgns::crdt::ColdUploadState>
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

        ~ColdPruneToctouTest() override
        {
            // Fixture discipline (documented on both seams): hooks armed
            // by a failing case must never leak further — clear ALL THREE
            // test seams (the cycle interrupt hook, the cold-state
            // override, and GlobalDB's erase interrupt hook) before any
            // teardown touches the stores.
            sgns::TxPruner::SetCycleInterruptHookForTest( nullptr );
            sgns::crdt::GlobalDB::SetColdUploadStateHookForTest( nullptr );
            sgns::crdt::GlobalDB::SetEraseInterruptHookForTest( nullptr );

            // Teardown invariant (asio, the tx_pruner_test order).
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

            // LEG 1's posture: fast drain + fast flush cadence so the
            // seeded block reaches REAL Durable before the probe.
            fake_s3_    = std::make_shared<sgns::blockstore::InMemoryS3Client>();
            fake_admin_ = std::make_shared<sgns::blockstore::InMemoryS3AdminClient>();
            crdt::GlobalDB::ColdStorageOptions cold;
            cold.enabled   = true;
            cold.endpoint  = "hermetic.invalid"; // overridden below - never contacted
            cold.bucket    = "prune-toctou";
            cold.accessKey = "test";
            cold.secretKey = "test";
            cold.backpressure.drain_interval_ms = 50;
            cold.backpressure.warn_pending      = 100000;
            cold.backpressure.critical_pending  = 200000;
            cold.backpressure.backoff_cap_ms    = 1000;
            cold.durability.flush_min_interval_ms = 50;
            cold.durability.flush_max_interval_ms = 300;
            sgns::crdt::SetColdStorageStackClientsForTesting(
                std::static_pointer_cast<sgns::blockstore::S3Client>( fake_s3_ ),
                std::static_pointer_cast<sgns::blockstore::S3AdminClient>( fake_admin_ ) );

            auto globaldb_ret = crdt::GlobalDB::New( io_,
                                                     hot_dir,
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
            db_->Start();
            ASSERT_TRUE( db_->HasColdStorage() ) << "tier-presence signal mismatch (SC4/Pitfall 7)";

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

        std::shared_ptr<sgns::TransferTransaction> MakeTransfer( uint64_t nonce, size_t output_count )
        {
            SGTransaction::DAGStruct dag;
            dag.set_type( "transfer" );
            dag.set_source_addr( account_->GetAddress() );
            dag.set_nonce( nonce );
            dag.set_timestamp( static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch() )
                    .count() ) );

            std::vector<OutputDestInfo> destinations;
            destinations.reserve( output_count );
            for ( size_t i = 0; i < output_count; ++i )
            {
                destinations.push_back( { 100 + i, account_->GetAddress(), kTestTokenId } );
            }
            return std::make_shared<sgns::TransferTransaction>(
                sgns::TransferTransaction::New( {}, std::move( destinations ), std::move( dag ) ) );
        }

        void SeedTransaction( const std::shared_ptr<sgns::GeniusTransaction> &transaction )
        {
            const std::string tx_path = sgns::TransactionManager::GetTransactionPath( *transaction );

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

        void PutSeedValue( const std::string &key, const std::string &value )
        {
            crdt::GlobalDB::Buffer buffer;
            buffer.put( value );
            ASSERT_TRUE( db_->Put( { key }, buffer, { kSeedTopic } ).has_value() );
        }

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

        bool ConsumeOutput( const sgns::GeniusTransaction &transaction,
                            uint32_t                        output_idx,
                            const sgns::GeniusTransaction  &spender )
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

        std::string SnapshotMapEntry( const std::string &tx_path )
        {
            auto entry = db_->GetRaw( crdt::GlobalDB::Buffer{}.put( tx_path ) );
            return entry.has_value() ? std::string( entry.value().toString() ) : std::string( "absent" );
        }

        std::shared_ptr<sgns::TxPruner> MakePruner( uint64_t k_budget, uint64_t settle_seconds )
        {
            sgns::PruningConfig config;
            config.enabled              = true;
            config.dry_run              = false;
            config.finality_window      = kWindow;
            config.max_erases_per_cycle = k_budget;
            config.settle_seconds       = settle_seconds;
            auto pruner                 = sgns::TxPruner::New( sgns::NodeType::Light, config );
            EXPECT_TRUE( pruner.has_value() );
            return pruner.has_value() ? pruner.value() : nullptr;
        }

        void ForceManagerReady()
        {
            TransactionManagerPendingLifecycleTestAccess::ChangeState( *tm_,
                                                                       sgns::TransactionManager::State::READY );
            ASSERT_EQ( tm_->GetState(), sgns::TransactionManager::State::READY );
        }

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
                        return true;
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

        sgns::crdt::ColdUploadState JournalStateOf( const std::string &cid_string )
        {
            auto cid = sgns::CID::fromString( cid_string );
            EXPECT_FALSE( cid.has_error() );
            return db_->GetColdUploadState( cid.value() );
        }

        std::shared_ptr<boost::asio::io_context>                io_;
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>        pubs_;
        std::shared_ptr<libp2p::basic::SchedulerImpl>           scheduler_;
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network> network_;
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator_;
        std::shared_ptr<sgns::crdt::GlobalDB>                   db_;
        std::shared_ptr<sgns::blockstore::InMemoryS3Client>     fake_s3_;
        std::shared_ptr<sgns::blockstore::InMemoryS3AdminClient> fake_admin_;
        std::string                                             keypair_path_;

        std::shared_ptr<sgns::GeniusAccount>      account_;
        std::shared_ptr<sgns::Blockchain>         blockchain_;
        std::shared_ptr<sgns::TransactionManager> tm_;
    };

    /**
     * The induced-regression scenario, parameterized over the regression
     * vector (the globaldb_erase_test INSTANTIATE shape):
     *
     *  1. Seed ONE eligible spent-beyond-window transaction and drive its
     *     block to REAL Durable (LEG 1's fake-S3 + fake-admin flush drive).
     *  2. Arm the TxPruner cycle seam at kInterruptAfterColdGate: on the
     *     first step-5 visit — after the conjunct ADMITTED the group on
     *     the real Durable evidence — install the cold-state override
     *     returning the REGRESSED state for that group's CID, and return
     *     false (observe, not abort).
     *  3. RunCycle completes; assert the refusal contract: erased == 0,
     *     value STILL PRESENT, marker ABSENT, cold_deferred_groups >= 1,
     *     errors == 0.
     *
     * RED (guard-less 14-01): the flipped state is read never, the erase
     * proceeds — erased==1 and the value is GONE (the destroying-erase
     * signature this test exists to catch).
     */
    TEST_P( ColdPruneToctouTest, InducedRegressionBetweenAdmissionAndComposeIsRefused )
    {
        BuildStack();
        ASSERT_NE( db_, nullptr );
        const auto regressed = GetParam();

        // Single-group seeding: one eligible candidate behind one CID.
        auto spender  = MakeTransfer( 1, 0 );
        auto eligible = MakeTransfer( 2, 1 );
        auto unspent  = MakeTransfer( 3, 1 );

        SeedTransaction( spender );
        PutSeedValue( "/seed/toctou/driver-1", "d1" );
        SeedTransaction( eligible );
        SeedTransaction( unspent );

        TrackOutputs( *eligible );
        TrackOutputs( *unspent );
        ASSERT_TRUE( ConsumeOutput( *eligible, 0, *spender ) );
        PutSeedValue( "/seed/toctou/driver-2", "d2" );

        const std::string tx_path = sgns::TransactionManager::GetTransactionPath( *eligible );
        const std::string cid_str = SnapshotMapEntry( tx_path );
        ASSERT_NE( cid_str, "absent" );
        auto target_cid = sgns::CID::fromString( cid_str );
        ASSERT_FALSE( target_cid.has_error() );

        // REAL Durable evidence first (LEG 1's drive) — the admission
        // conjunct must ADMIT the group before the regression is induced.
        ASSERT_TRUE( PollUntil( [&] { return JournalStateOf( cid_str ) == crdt::ColdUploadState::Durable; },
                                std::chrono::milliseconds( 20000 ) ) )
            << "TOCTOU precondition: eligible block never reached Durable (cid " << cid_str << ")";

        // The probe (Pitfall 2 discipline: step 5 ONLY — never GlobalDB's
        // erase-hook steps): on the admission->erase gap, flip the shared
        // read seam for this group's CID to the regressed state.
        sgns::TxPruner::SetCycleInterruptHookForTest(
            [target_cid = target_cid.value(), regressed]( int step ) -> bool {
                if ( step == sgns::TxPruner::kInterruptAfterColdGate )
                {
                    sgns::crdt::GlobalDB::SetColdUploadStateHookForTest(
                        [target_cid, regressed]( const sgns::CID &c )
                            -> std::optional<sgns::crdt::ColdUploadState> {
                            return c == target_cid ? std::optional<sgns::crdt::ColdUploadState>( regressed )
                                                   : std::nullopt;
                        } );
                }
                return false; // observe, never abort (the seam contract)
            } );

        auto pruner = MakePruner( 10, kSmallSettleSeconds );
        ASSERT_NE( pruner, nullptr );

        sgns::PruneCycleStats stats;
        RunSettledCycle( *pruner, stats );

        // The refusal contract (every message TOCTOU-marked for the red
        // run's identifiability).
        EXPECT_EQ( stats.erased, 0u )
            << "TOCTOU destroying erase: the regressed (non-Durable) journal state let the erase proceed";
        EXPECT_EQ( stats.errors, 0u ) << "TOCTOU misclassification: the refusal surfaced as an error (Pitfall 1)";
        EXPECT_GE( stats.cold_deferred_groups, 1u )
            << "TOCTOU counter missing: the refusal must land in cold_deferred_groups, never errors";
        EXPECT_TRUE( db_->Get( { tx_path } ).has_value() )
            << "TOCTOU value destroyed: the hot value survived neither the regression nor the refusal";
        auto lookup = sgns::PruneEligibility::LookupPrunedEntry( *db_, eligible->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        EXPECT_EQ( lookup.value().first, sgns::PrunedLookup::NOT_FOUND )
            << "TOCTOU marker staged: no /prune/ marker may exist when the erase was refused";

        // Negative control: the unspent sibling survives untouched.
        EXPECT_TRUE( db_->Get( { sgns::TransactionManager::GetTransactionPath( *unspent ) } ).has_value() );
    }

    INSTANTIATE_TEST_SUITE_P(
        RegressionVector,
        ColdPruneToctouTest,
        ::testing::Values( sgns::crdt::ColdUploadState::Accepted, sgns::crdt::ColdUploadState::NoRecord ),
        []( const ::testing::TestParamInfo<sgns::crdt::ColdUploadState> &info ) {
            switch ( info.param )
            {
                case sgns::crdt::ColdUploadState::Accepted:
                    return "DurableToAccepted";
                case sgns::crdt::ColdUploadState::NoRecord:
                    return "DurableToNoRecord";
                default:
                    return "Unexpected";
            }
        } );
} // namespace
