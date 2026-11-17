/**
 * @file       cold_prune_gate_test.cpp
 * @brief      Cold-durability prune admission gate (v1.2 Phase 14 plan
 *             14-01 Task 2 / PRUNE-01, PRUNE-03): TxPruner::RunCycle's
 *             block-group work-list filter admits a group for erasure
 *             ONLY when the group's single delta-block CID (the D-01
 *             removal set) reads EXACTLY ColdUploadState::Durable through
 *             GlobalDB::GetColdUploadState — mirroring the GC
 *             kEvictionDurabilityGate precedent (exact equality, dedicated
 *             counter, continue-never-abort). Everything else defers the
 *             WHOLE group as a counter (cold_deferred_groups), never an
 *             error; an Unreadable journal read defers loudly
 *             (cold_unreadable_groups + one WARN, D-03). The
 *             no-cold-tier node is the SC4 vacuous branch (D-04):
 *             byte-for-byte pre-phase behavior, both cold counters zero.
 *
 *             Fixture posture (D-08a hermetic-first): the
 *             cold_storage_resurrection_test recipe — a REAL cold-enabled
 *             GlobalDB (real RocksDB hot store + real coldstore journal)
 *             over SetColdStorageStackClientsForTesting-injected
 *             InMemoryS3Client/InMemoryS3AdminClient fakes with endpoint
 *             "hermetic.invalid" (never contacted), hygiene-cleared
 *             immediately after construction — combined with the
 *             tx_pruner_test construction pattern (real GeniusAccount +
 *             Blockchain + TransactionManager + TxPruner, the friend-seam
 *             tick-gate drive, the eligible-candidate seeding recipes).
 *             Zero network.
 *
 *             LEG 1 (Durable admits — the only permit): fast drain (50 ms)
 *             + fast flush cadence (50/300 ms) drive REAL journal
 *             transitions Pending -> Accepted -> Durable on the injected
 *             fakes; the cycle then erases with every cold counter zero.
 *             LEG 2 (Pending defers the whole group, Pitfall 4): a HIGH
 *             drain interval (60 s) holds the journal at Pending
 *             deterministically; TWO eligible transactions share ONE
 *             delta-block CID (SeedTransactionsInOneBlock) and BOTH stay
 *             present, the deferral is a counter (never an error), and a
 *             second cycle re-evaluates the same group
 *             (deferred-not-forgotten). LEG 3 (SC4 vacuous branch): a
 *             cold-disabled GlobalDB erases exactly as before.
 *
 *             The five-state journal matrix's NoRecord/Accepted direct-CF
 *             legs, the real-fault Unreadable legs, and the SC3
 *             composition proof land in plan 14-03 — this suite carries
 *             the three headline behaviors (D-09: Tier-1 now).
 * @date       2026-11-16
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
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
     * deterministically. This definition carries only ChangeState — the one
     * member this binary uses.
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

    /// The fixture's pre-seeded CRDT topic (the tx_pruner_test contract):
    /// puts on it create real blocks, heads, and seals.
    constexpr char kSeedTopic[] = "CRDT.Datastore.TEST.Channel";

    /// Finality window N (the 17-04 recipes' value).
    constexpr uint64_t kWindow = 2;

    /// The account-generation tag the tests stamp onto cycles (D-C4).
    constexpr uint64_t kTestGeneration = 7;

    /// Settle window: waited out through the settled-cycle helper.
    constexpr uint64_t kSmallSettleSeconds = 1;

    /// Bounded poll loop with deadline - never a fixed sleep (Tier-1 audit;
    /// the durability/resurrection suite discipline).
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
    /// upload_drainer_test FreshDir/NextUniqueDir discipline).
    std::string NextUniqueDir( const char *lane )
    {
        static std::atomic<uint64_t> next{ 0 };
        return std::string( "test-data/cold-prune-gate/" ) + lane + "-"
             + std::to_string( next.fetch_add( 1, std::memory_order_relaxed ) );
    }

    /// A truly FRESH store per case: the monotonic counter resets every
    /// process run, so a leftover dir from a previous run would reopen its
    /// stale state - wipe before use.
    std::string FreshDir( const char *lane )
    {
        auto           dir = NextUniqueDir( lane );
        std::error_code ec;
        std::filesystem::remove_all( dir, ec );
        return dir;
    }

    /// The suite's logging config (the resurrection-test shape: a
    /// std::string-returning function avoids the ConfiguratorFromYAML
    /// ctor's const-char* overload ambiguity).
    std::string LoggingConfig()
    {
        return R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: cold_prune_gate_test
    sink: console
    level: error
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )";
    }

    /**
     * @brief  Hermetic cold-enabled (or cold-disabled) GlobalDB +
     *         TransactionManager + TxPruner stack — the resurrection-test
     *         cold recipe combined with the tx_pruner_test construction
     *         pattern. One stack per test; BuildStack() chooses the
     *         posture (LEG 1 fast drain, LEG 2 held-Pending drain, LEG 3
     *         cold off).
     */
    class ColdPruneGateTest : public ::testing::Test
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

        ~ColdPruneGateTest() override
        {
            // Fixture discipline: a hook armed by a failing case must never
            // leak further — clear BOTH interrupt seams before any teardown
            // touches the stores (the tx_pruner_test destructor order).
            sgns::TxPruner::SetCycleInterruptHookForTest( nullptr );
            crdt::GlobalDB::SetEraseInterruptHookForTest( nullptr );

            // Teardown invariant (asio, the tx_pruner_test order): stop the
            // blockchain FIRST so the ConsensusManager round-timer thread
            // joins before any db-backed handle is released.
            if ( blockchain_ )
            {
                (void)blockchain_->Stop();
            }
            tm_.reset();
            blockchain_.reset();
            account_.reset();

            // db_ must be released BEFORE pubs_->Stop() (the CRDTFixture
            // invariant: ~BasicHost deregisters leftover TcpConnections
            // from the host's reactor). ShutdownNow stops the cold drainer
            // and GC threads first.
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
        //
        // Stack construction.
        //

        /// Builds the single-node stack. @p cold_enabled installs the
        /// hermetic cold tier (fake S3 + fake admin, "hermetic.invalid");
        /// @p drain_interval_ms shapes the journal's determinism: 50 ms =
        /// real transitions fire fast (LEG 1), 60000 ms = birth-Pending
        /// records persist (LEG 2).
        void BuildStack( bool cold_enabled, uint32_t drain_interval_ms )
        {
            const std::string hot_dir = FreshDir( "hot" );
            keypair_path_            = FreshDir( "keypair" );

            // Application-work pool (the CRDTFixture contract): tests drive
            // it by hand; it stays separate from the host's context.
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

            // The scheduler rides the host's io_context (the
            // single-threaded-per-host discipline).
            scheduler_ = std::make_shared<libp2p::basic::SchedulerImpl>(
                std::make_shared<libp2p::basic::AsioSchedulerBackend>( pubs_->GetAsioContext() ),
                libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
            network_   = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubs_->GetHost(), scheduler_ );
            generator_ = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();

            // --- The hermetic cold posture (the resurrection recipe):
            // cold ON through the TEST-ONLY client override with the
            // never-contacted endpoint; cold OFF passes default options
            // (cold_manager_ null - the SC4 vacuous branch).
            crdt::GlobalDB::ColdStorageOptions cold;
            if ( cold_enabled )
            {
                fake_s3_    = std::make_shared<sgns::blockstore::InMemoryS3Client>();
                fake_admin_ = std::make_shared<sgns::blockstore::InMemoryS3AdminClient>();
                cold.enabled   = true;
                cold.endpoint  = "hermetic.invalid"; // overridden below - never contacted
                cold.bucket    = "prune-gate";
                cold.accessKey = "test";
                cold.secretKey = "test";
                cold.backpressure.drain_interval_ms = drain_interval_ms;
                cold.backpressure.warn_pending      = 100000;
                cold.backpressure.critical_pending  = 200000;
                cold.backpressure.backoff_cap_ms    = 1000;
                // FastDurability (50/300 ms): the fast flush cadence that
                // makes Durable waits deterministic (LEG 1). Retention
                // stays 0 = the GC never runs (the D-02 absent=off
                // posture) - nothing but the pruner may erase.
                cold.durability.flush_min_interval_ms = 50;
                cold.durability.flush_max_interval_ms = 300;
                sgns::crdt::SetColdStorageStackClientsForTesting(
                    std::static_pointer_cast<sgns::blockstore::S3Client>( fake_s3_ ),
                    std::static_pointer_cast<sgns::blockstore::S3AdminClient>( fake_admin_ ) );
            }

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
            ASSERT_EQ( db_->HasColdStorage(), cold_enabled ) << "tier-presence signal mismatch (SC4/Pitfall 7)";

            // The tx_pruner_test SetUp recipe over the cold-capable db.
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

        //
        // Seeding helpers — the tx_pruner_test recipes verbatim (every
        // transaction goes through the REAL topic Put so its CRDT value,
        // delta CID, head, and per-key priority all exist, then gets a raw
        // tx->CID map entry in the exact StoreTransactionCID shapes).
        //

        /// Transfer with @p output_count outputs owned by this account (empty for spenders).
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

        /// Real-topic value Put + raw tx->CID map entry (StoreTransactionCID shapes).
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

        /// Commits several transactions' values in ONE delta block (the
        /// shared-block recipe) and records each tx->CID map entry against
        /// that block's CID.
        void SeedTransactionsInOneBlock( const std::vector<std::shared_ptr<sgns::GeniusTransaction>> &transactions )
        {
            auto batch = db_->BeginTransaction();
            ASSERT_NE( batch, nullptr );
            if ( batch == nullptr )
            {
                return;
            }
            for ( const auto &transaction : transactions )
            {
                crdt::GlobalDB::Buffer serialized;
                serialized.put( transaction->SerializeByteVector() );
                ASSERT_TRUE(
                    batch->Put( { sgns::TransactionManager::GetTransactionPath( *transaction ) }, serialized )
                        .has_value() );
            }
            auto commit = batch->Commit( { kSeedTopic } );
            ASSERT_TRUE( commit.has_value() );
            if ( !commit.has_value() )
            {
                return;
            }
            const std::string block_cid = commit.value().toString().value();

            for ( const auto &transaction : transactions )
            {
                crdt::GlobalDB::Buffer key_buffer;
                key_buffer.put( sgns::TransactionManager::GetTransactionPath( *transaction ) );
                crdt::GlobalDB::Buffer value_buffer;
                value_buffer.put( block_cid );
                ASSERT_TRUE( db_->GetDataStore()->put( key_buffer, value_buffer ).has_value() );
            }
        }

        /// Plain topic Put of a raw string at @p key (driver heights).
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

        /// Spends one output by @p spender; returns ConsumeUTXOs's verdict.
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

        /// The raw tx->CID map entry bytes at @p tx_path ("absent" when gone).
        std::string SnapshotMapEntry( const std::string &tx_path )
        {
            auto entry = db_->GetRaw( crdt::GlobalDB::Buffer{}.put( tx_path ) );
            return entry.has_value() ? std::string( entry.value().toString() ) : std::string( "absent" );
        }

        //
        // Pruner helpers (the tx_pruner_test recipes).
        //

        /// Constructs a Light TxPruner with the suite's window and dry_run=false.
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

        /// Drives the manager into the tick gate's READY state (the friend seam).
        void ForceManagerReady()
        {
            TransactionManagerPendingLifecycleTestAccess::ChangeState( *tm_,
                                                                       sgns::TransactionManager::State::READY );
            ASSERT_EQ( tm_->GetState(), sgns::TransactionManager::State::READY );
        }

        /// Runs cycles until one is NOT gate-skipped, delivering its stats
        /// (the fresh-instance watermark discriminator).
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

        /// One plain cycle (the gate is already settled on @p pruner).
        outcome::result<sgns::PruneCycleStats> RunCycle( sgns::TxPruner &pruner )
        {
            return pruner.RunCycle( *db_, *tm_, account_->GetUTXOManager(), kTestGeneration );
        }

        /// Reads the journal state for a CID string through the UNGATED
        /// enum bridge (the gate's own read path).
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

    //
    // LEG 1 — Durable admits and erases (the only permit).
    //

    /**
     * The cold-enabled Light node with every journal record evidence-
     * Durable: the cycle erases the eligible spent transactions exactly as
     * the pre-phase behavior (erased == eligible), every cold counter
     * stays zero, no error is recorded, and each erased tx shows the
     * value-XOR-marker contract (value absent, /prune/ marker PRUNED).
     */
    TEST_F( ColdPruneGateTest, Gate_DurableAdmitsAndErases )
    {
        BuildStack( /*cold_enabled=*/true, /*drain_interval_ms=*/50 );
        ASSERT_NE( db_, nullptr );

        // The RealPrune seeding shape: ancient spender first, then the two
        // eligibles, then the unspent sibling; driver puts raise the head
        // past the spend-recency window.
        auto spender    = MakeTransfer( 1, 0 );
        auto eligible_a = MakeTransfer( 2, 1 );
        auto eligible_b = MakeTransfer( 3, 1 );
        auto unspent    = MakeTransfer( 4, 2 );

        SeedTransaction( spender );
        PutSeedValue( "/seed/gate/durable-driver-1", "d1" );
        SeedTransaction( eligible_a );
        SeedTransaction( eligible_b );
        SeedTransaction( unspent );

        TrackOutputs( *eligible_a );
        TrackOutputs( *eligible_b );
        TrackOutputs( *unspent ); // deliberately never consumed
        ASSERT_TRUE( ConsumeOutput( *eligible_a, 0, *spender ) );
        ASSERT_TRUE( ConsumeOutput( *eligible_b, 0, *spender ) );
        PutSeedValue( "/seed/gate/durable-driver-2", "d2" );

        const std::string cid_a = SnapshotMapEntry( sgns::TransactionManager::GetTransactionPath( *eligible_a ) );
        const std::string cid_b = SnapshotMapEntry( sgns::TransactionManager::GetTransactionPath( *eligible_b ) );
        ASSERT_NE( cid_a, "absent" );
        ASSERT_NE( cid_b, "absent" );

        // REAL transitions to Durable (LEG 1's deterministic drive): the
        // injected fake S3 client's PutObject succeeds on the drainer's
        // pass, and the fake-admin flush evidence + the fast durability
        // cadence advance the journal - PollUntil the Durable evidence for
        // BOTH groups' CIDs (the WaitAllDurable shape, loud on deadline).
        ASSERT_TRUE( PollUntil( [&] { return JournalStateOf( cid_a ) == crdt::ColdUploadState::Durable; },
                                std::chrono::milliseconds( 20000 ) ) )
            << "eligible_a's block never reached Durable (cid " << cid_a << ")";
        ASSERT_TRUE( PollUntil( [&] { return JournalStateOf( cid_b ) == crdt::ColdUploadState::Durable; },
                                std::chrono::milliseconds( 20000 ) ) )
            << "eligible_b's block never reached Durable (cid " << cid_b << ")";

        auto pruner = MakePruner( 10, kSmallSettleSeconds );
        ASSERT_NE( pruner, nullptr );

        sgns::PruneCycleStats stats;
        RunSettledCycle( *pruner, stats );

        // Durable admits: identical counts to the pre-phase RealPrune
        // contract, both cold counters zero, zero errors.
        EXPECT_EQ( stats.examined, 4u );
        EXPECT_EQ( stats.eligible, 2u );
        EXPECT_EQ( stats.erased, 2u );
        EXPECT_EQ( stats.held_blocks, 0u );
        EXPECT_EQ( stats.skipped, 0u );
        EXPECT_EQ( stats.errors, 0u );
        EXPECT_EQ( stats.cold_deferred_groups, 0u );
        EXPECT_EQ( stats.cold_unreadable_groups, 0u );

        // Value XOR marker for each erased tx (the LookupPrunedEntry query
        // helper): value absent AND marker PRUNED.
        for ( const auto *tx : { eligible_a.get(), eligible_b.get() } )
        {
            const std::string tx_path = sgns::TransactionManager::GetTransactionPath( *tx );
            EXPECT_TRUE( db_->Get( { tx_path } ).has_error() ) << "erased value still present: " << tx_path;
            auto lookup = sgns::PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
            ASSERT_TRUE( lookup.has_value() );
            EXPECT_EQ( lookup.value().first, sgns::PrunedLookup::PRUNED );
        }

        // Negative control: the unspent sibling survives untouched.
        EXPECT_TRUE(
            db_->Get( { sgns::TransactionManager::GetTransactionPath( *unspent ) } ).has_value() );
        auto unspent_lookup = sgns::PruneEligibility::LookupPrunedEntry( *db_, unspent->GetHash() );
        ASSERT_TRUE( unspent_lookup.has_value() );
        EXPECT_EQ( unspent_lookup.value().first, sgns::PrunedLookup::NOT_FOUND );
    }

    //
    // LEG 2 — Pending defers the WHOLE group, loudly-as-a-counter.
    //

    /**
     * The cold-enabled node with the journal held at Pending (the 60 s
     * drain interval keeps birth records Pending deterministically): TWO
     * eligible transactions sharing ONE delta-block CID both stay present,
     * NOTHING erases, the deferral lands in cold_deferred_groups (never
     * errors - Pitfall 1), no marker exists, and a second cycle
     * re-evaluates the same group (deferred-not-forgotten).
     */
    TEST_F( ColdPruneGateTest, Gate_PendingDefersWholeGroupAsCounter )
    {
        BuildStack( /*cold_enabled=*/true, /*drain_interval_ms=*/60000 );
        ASSERT_NE( db_, nullptr );

        // The SharedBlockBudget seeding shape: two eligibles behind ONE
        // delta-block CID (the whole-group deferral surface, Pitfall 4).
        auto spender = MakeTransfer( 1, 0 );
        auto tx_a    = MakeTransfer( 2, 1 );
        auto tx_b    = MakeTransfer( 3, 1 );

        SeedTransaction( spender );
        PutSeedValue( "/seed/gate/pending-driver-1", "d1" );

        TrackOutputs( *tx_a );
        TrackOutputs( *tx_b );
        SeedTransactionsInOneBlock( { tx_a, tx_b } );
        ASSERT_TRUE( ConsumeOutput( *tx_a, 0, *spender ) );
        ASSERT_TRUE( ConsumeOutput( *tx_b, 0, *spender ) );
        PutSeedValue( "/seed/gate/pending-driver-2", "d2" );

        const std::string path_a     = sgns::TransactionManager::GetTransactionPath( *tx_a );
        const std::string path_b     = sgns::TransactionManager::GetTransactionPath( *tx_b );
        const std::string shared_cid = SnapshotMapEntry( path_a );
        ASSERT_EQ( shared_cid, SnapshotMapEntry( path_b ) ); // ONE shared delta

        // Deterministic precondition: the shared block's journal record is
        // birth-Pending (spill-on-write) and STAYS Pending (no drainer
        // pass inside the test window).
        ASSERT_EQ( JournalStateOf( shared_cid ), crdt::ColdUploadState::Pending )
            << "journal not held at Pending - drain interval leaked a pass";

        auto pruner = MakePruner( 10, kSmallSettleSeconds );
        ASSERT_NE( pruner, nullptr );

        sgns::PruneCycleStats stats;
        RunSettledCycle( *pruner, stats );

        // Pending defers the whole group: the group is enumerated and
        // eligible, but NOTHING erases and the deferral is a counter —
        // never an error (Pitfall 1), never a WARN "Prune erase failed".
        EXPECT_EQ( stats.examined, 3u );
        EXPECT_EQ( stats.eligible, 2u );
        EXPECT_EQ( stats.erased, 0u );
        EXPECT_EQ( stats.held_blocks, 0u );
        EXPECT_EQ( stats.skipped, 0u );
        EXPECT_EQ( stats.errors, 0u );
        EXPECT_EQ( stats.cold_deferred_groups, 1u ); // ONE group (two txs, one CID)
        EXPECT_EQ( stats.cold_unreadable_groups, 0u );

        // Whole-group deferral: BOTH same-block siblings stay present, no
        // /prune/ marker exists (value XOR marker - nothing erased).
        EXPECT_TRUE( db_->Get( { path_a } ).has_value() );
        EXPECT_TRUE( db_->Get( { path_b } ).has_value() );
        for ( const auto *tx : { tx_a.get(), tx_b.get() } )
        {
            auto lookup = sgns::PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
            ASSERT_TRUE( lookup.has_value() );
            EXPECT_EQ( lookup.value().first, sgns::PrunedLookup::NOT_FOUND );
        }

        // Deferred-not-forgotten: a second cycle re-evaluates the same
        // group — still enumerated, still eligible, still deferred (the
        // counter increments again; liveness per T-14-03).
        auto second = RunCycle( *pruner );
        ASSERT_TRUE( second.has_value() );
        EXPECT_EQ( second.value().examined, 3u );
        EXPECT_EQ( second.value().eligible, 2u );
        EXPECT_EQ( second.value().erased, 0u );
        EXPECT_EQ( second.value().errors, 0u );
        EXPECT_EQ( second.value().cold_deferred_groups, 1u );
        EXPECT_TRUE( db_->Get( { path_a } ).has_value() );
        EXPECT_TRUE( db_->Get( { path_b } ).has_value() );
    }

    //
    // LEG 3 — SC4 vacuous branch: the no-cold-tier node is byte-for-byte
    // pre-phase behavior.
    //

    /**
     * A GlobalDB with cold storage disabled (cold_manager_ null,
     * HasColdStorage() false): the conjunct admits every erasable group
     * unchanged — same candidates, same erases, both cold counters zero.
     * This leg IS the regression proof that the gate is vacuous without a
     * cold tier (D-04: automatic activation — Light AND cold-enabled —
     * no config knob).
     */
    TEST_F( ColdPruneGateTest, Gate_ColdDisabledNodeVacuousBranch )
    {
        BuildStack( /*cold_enabled=*/false, /*drain_interval_ms=*/0 );
        ASSERT_NE( db_, nullptr );
        EXPECT_FALSE( db_->HasColdStorage() ); // the SC4 tier-presence signal

        // The RealPrune seeding shape (identical data to LEG 1).
        auto spender    = MakeTransfer( 1, 0 );
        auto eligible_a = MakeTransfer( 2, 1 );
        auto eligible_b = MakeTransfer( 3, 1 );
        auto unspent    = MakeTransfer( 4, 2 );

        SeedTransaction( spender );
        PutSeedValue( "/seed/gate/vacuous-driver-1", "d1" );
        SeedTransaction( eligible_a );
        SeedTransaction( eligible_b );
        SeedTransaction( unspent );

        TrackOutputs( *eligible_a );
        TrackOutputs( *eligible_b );
        TrackOutputs( *unspent );
        ASSERT_TRUE( ConsumeOutput( *eligible_a, 0, *spender ) );
        ASSERT_TRUE( ConsumeOutput( *eligible_b, 0, *spender ) );
        PutSeedValue( "/seed/gate/vacuous-driver-2", "d2" );

        auto pruner = MakePruner( 10, kSmallSettleSeconds );
        ASSERT_NE( pruner, nullptr );

        sgns::PruneCycleStats stats;
        RunSettledCycle( *pruner, stats );

        // Behavior identical to pre-phase: same candidates, same erases,
        // cold counters zero.
        EXPECT_EQ( stats.examined, 4u );
        EXPECT_EQ( stats.eligible, 2u );
        EXPECT_EQ( stats.erased, 2u );
        EXPECT_EQ( stats.held_blocks, 0u );
        EXPECT_EQ( stats.skipped, 0u );
        EXPECT_EQ( stats.errors, 0u );
        EXPECT_EQ( stats.cold_deferred_groups, 0u );
        EXPECT_EQ( stats.cold_unreadable_groups, 0u );

        for ( const auto *tx : { eligible_a.get(), eligible_b.get() } )
        {
            const std::string tx_path = sgns::TransactionManager::GetTransactionPath( *tx );
            EXPECT_TRUE( db_->Get( { tx_path } ).has_error() );
            auto lookup = sgns::PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
            ASSERT_TRUE( lookup.has_value() );
            EXPECT_EQ( lookup.value().first, sgns::PrunedLookup::PRUNED );
        }
        EXPECT_TRUE(
            db_->Get( { sgns::TransactionManager::GetTransactionPath( *unspent ) } ).has_value() );
    }
} // namespace
