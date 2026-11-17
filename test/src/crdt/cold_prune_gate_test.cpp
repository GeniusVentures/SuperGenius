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
 *             legs, the real-fault Unreadable legs, the adjacency/empty/
 *             ordering edges, and the SC3 composition proof land in plan
 *             14-03 — this suite carries the three headline behaviors
 *             (D-09: Tier-1 now) and the matrix completion (14-03
 *             Tasks 1-2): NoRecord/Accepted defer via direct journal-CF
 *             seeding (D-02: NoRecord defers forever, re-evaluated next
 *             cycle, never permanent-stop); garbage journal values decode
 *             conservatively to Pending and DEFER (never Durable, never
 *             Unreadable); Unreadable defers loudly at bridge level (a
 *             REAL FaultController fault through the free bridge — no
 *             overrides) and at gate level (dedicated
 *             cold_unreadable_groups counter + one WARN + the cycle
 *             completes — D-03/MI-02 never-healthy-by-omission); a
 *             deferred leader never blocks a Durable follower
 *             (first-appearance order, continue-never-abort); an empty
 *             cycle moves no counter; and the SC3 crossing legs prove the
 *             cold conjunct composes WITH PruneEligibility::Evaluate as
 *             a pure AND — the gate never widens admission.
 *
 *             EVALUATOR PURITY CONTRACT (D-C6, PRUNE-03/D-05 — the
 *             composition target this suite never edits): "PURE evaluator
 *             (D-C6): it runs on committed state only — driven from a
 *             timer cycle by the caller, NEVER from CRDT element
 *             callbacks (they fire pre-batch-commit). It performs ZERO
 *             writes: no Put, no Erase, no staging, no hooks. The
 *             Phase-18 TxPruner is the reuse seam — it calls Evaluate()
 *             verbatim and owns every mutation." (PruneEligibility.hpp,
 *             verbatim). The mechanical twin of every composition leg is
 *             the phase-level purity gate: git diff --exit-code
 *             2af184fa1 on PruneEligibility.{hpp,cpp} must exit 0.
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

#include <rocksdb/db.h>
#include <rocksdb/options.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <libp2p/basic/scheduler.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <libp2p/host/host.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>
#include <soralog/impl/configurator_from_yaml.hpp>

#include <sgns-blockstore/Cid.hpp>
#include <sgns-blockstore/LocalBlockStore.hpp>
#include <sgns-blockstore/S3AdminClient.hpp>
#include <sgns-blockstore/S3BlockStore.hpp>
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
#include "crdt/globaldb/ColdStorageManager.hpp"
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
            TeardownStack();
        }

        /// The stack teardown sequence (also callable MID-TEST by the
        /// journal-seeding legs between populate and reopen): clear seams,
        /// stop the blockchain, release the manager stack, quiesce the
        /// GlobalDB (stops the cold drainer + GC threads and releases the
        /// coldstore RocksDB dir lock the seeding helper needs), stop the
        /// pubsub host LAST (the CRDTFixture invariant).
        void TeardownStack()
        {
            // Fixture discipline: a hook armed by a failing case must never
            // leak further — clear BOTH interrupt seams before any teardown
            // touches the stores (the tx_pruner_test destructor order).
            sgns::TxPruner::SetCycleInterruptHookForTest( nullptr );
            crdt::GlobalDB::SetColdUploadStateHookForTest( nullptr );
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

        /// Builds the single-node stack on a FRESH dir. @p cold_enabled
        /// installs the hermetic cold tier (fake S3 + fake admin,
        /// "hermetic.invalid"); @p drain_interval_ms shapes the journal's
        /// determinism: 50 ms = real transitions fire fast (LEG 1), 60000 ms
        /// = birth-Pending records persist (LEG 2 and the seeded legs).
        void BuildStack( bool cold_enabled, uint32_t drain_interval_ms, bool inject_hermetic_clients = true )
        {
            BuildStackOnDir( cold_enabled, drain_interval_ms, FreshDir( "hot" ), inject_hermetic_clients );
        }

        /// Same as BuildStack but on an EXISTING hot dir (the journal-seed
        /// legs' PHASE C reopen: the CRDT state, tx->CID map, UTXO records,
        /// and the SEEDED coldstore journal all survive the round trip).
        ///
        /// DRAIN-LOOP REALITY (determinism contract): the drainer's FIRST
        /// poll runs IMMEDIATELY on Start (D-12 crash-restart
        /// reconciliation) — with HEALTHY hermetic fakes, every Pending
        /// record uploads successfully and advances to Accepted within
        /// milliseconds. Legs that need REAL transitions (LEG 1, the
        /// TOCTOU precondition, the AND-composition) inject the healthy
        /// fakes (@p inject_hermetic_clients = true); legs that need the
        /// journal HELD at its birth/seeded state (LEG 2, the seeded
        /// matrix legs, the Unreadable gate leg, the empty leg) pass
        /// false: the SAME hermetic fakes are injected but FAULTED — the
        /// S3 fake's Put fails Unavailable (drain advances nothing) and
        /// the admin fake is Unreachable (flush advances nothing) — so
        /// the immediate reconciliation pass can move no record and the
        /// seeded bytes hold deterministically. (Constructing the
        /// PRODUCTION client against the never-contacted endpoint is not
        /// an option: the real minio-cpp client's construction is not
        /// safe in-process against an unresolvable host.)
        void BuildStackOnDir( bool            cold_enabled,
                              uint32_t        drain_interval_ms,
                              const std::string &hot_dir,
                              bool             inject_hermetic_clients = true )
        {
            last_hot_dir_ = hot_dir;
            keypair_path_ = FreshDir( "keypair" );

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
                if ( inject_hermetic_clients )
                {
                    fake_s3_    = std::make_shared<sgns::blockstore::InMemoryS3Client>();
                    fake_admin_ = std::make_shared<sgns::blockstore::InMemoryS3AdminClient>();
                }
                else
                {
                    // The HELD-journal posture: the same hermetic fakes,
                    // faulted — uploads fail Unavailable and the admin is
                    // Unreachable, so no drain pass or flush cycle can
                    // advance any journal record (see the determinism
                    // contract above).
                    fake_s3_    = std::make_shared<sgns::blockstore::InMemoryS3Client>();
                    fake_admin_ = std::make_shared<sgns::blockstore::InMemoryS3AdminClient>();
                    fake_s3_->SetFault( sgns::blockstore::S3Op::Put,
                                        sgns::blockstore::S3FaultKind::Unavailable );
                    fake_admin_->SetAdminFault( sgns::blockstore::S3AdminFaultKind::Unreachable );
                }
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
        /// The hot dir of the CURRENT (or last torn-down) stack — the
        /// journal-seeding legs derive its coldstore path from this.
        std::string                                             last_hot_dir_;

        std::shared_ptr<sgns::GeniusAccount>      account_;
        std::shared_ptr<sgns::Blockchain>         blockchain_;
        std::shared_ptr<sgns::TransactionManager> tm_;
    };

    //
    // Journal-CF seeding machinery (v1.2 Phase 14 plan 14-03 Task 1,
    // D-08a): deterministic arbitrary-state injection against the
    // coldstore's PUBLISHED upload_journal column family. LOCK
    // DISCIPLINE: the helper opens the coldstore RocksDB directly and is
    // therefore legal ONLY while the fixture's GlobalDB is CLOSED (a
    // second Open against a live store hits the RocksDB dir lock) — the
    // three-phase same-dir flow (populate -> TeardownStack -> seed ->
    // BuildStackOnDir reopen) enforces it structurally.
    //

    /// The journal record codec (verbatim from LocalBlockStore.cpp:159-166):
    /// [version-byte 0x01][state-byte] with 0x00=PendingUpload,
    /// 0x01=UploadedAccepted, 0x02=UploadedDurable; the adjacency variants
    /// carry an unknown state byte / version byte.
    const std::string kJournalPending       = std::string{ '\x01', '\x00' };
    const std::string kJournalAccepted      = std::string{ '\x01', '\x01' };
    const std::string kJournalDurable       = std::string{ '\x01', '\x02' };
    const std::string kJournalGarbageState  = std::string{ '\x01', '\x07' }; // unknown state byte
    const std::string kJournalGarbageVersion = std::string{ '\x02', '\x02' }; // unknown version byte

    /// The journal key for a CID string: byte-identical to
    /// LocalBlockStore's MakeKey (libp2p ContentIdentifierCodec::encode).
    std::string JournalKeyOf( const std::string &cid_string )
    {
        auto cid     = sgns::CID::fromString( cid_string );
        EXPECT_FALSE( cid.has_error() );
        auto encoded = libp2p::multi::ContentIdentifierCodec::encode( cid.value() );
        EXPECT_FALSE( encoded.has_error() );
        return std::string( encoded.value().begin(), encoded.value().end() );
    }

    /**
     * @brief  Rewrites (or deletes, when @p value_bytes is nullopt) one
     *         CID's record in the coldstore's upload_journal CF. Asserts
     *         the record EXISTS first (fixture hygiene: the test knows
     *         exactly which records the populate phase birthed) and that
     *         the journal is non-empty. Requires the GlobalDB over @p
     *         coldstore_dir's hot dir to be CLOSED (RocksDB dir lock).
     * @return true on success (false + ADD_FAILURE on any RocksDB error).
     */
    bool SeedJournalRecord( const std::string &coldstore_dir,
                            const std::string &cid_string,
                            std::optional<std::string> value_bytes )
    {
        const std::string key = JournalKeyOf( cid_string );

        rocksdb::Options                list_options;
        std::vector<std::string>        cf_names;
        rocksdb::Status                 status = rocksdb::DB::ListColumnFamilies( list_options, coldstore_dir, &cf_names );
        if ( !status.ok() )
        {
            ADD_FAILURE() << "SeedJournalRecord: ListColumnFamilies failed at " << coldstore_dir << ": "
                          << status.ToString();
            return false;
        }
        std::vector<rocksdb::ColumnFamilyDescriptor> descriptors;
        for ( const auto &name : cf_names )
        {
            descriptors.emplace_back( name, rocksdb::ColumnFamilyOptions() );
        }
        rocksdb::DB                                *raw_db = nullptr;
        std::vector<rocksdb::ColumnFamilyHandle *> handles;
        status = rocksdb::DB::Open( rocksdb::DBOptions(), coldstore_dir, descriptors, &handles, &raw_db );
        if ( !status.ok() )
        {
            ADD_FAILURE() << "SeedJournalRecord: Open failed at " << coldstore_dir << ": " << status.ToString();
            return false;
        }
        std::unique_ptr<rocksdb::DB> db( raw_db );
        rocksdb::ColumnFamilyHandle *journal = nullptr;
        for ( auto *handle : handles )
        {
            if ( handle->GetName() == sgns::blockstore::kUploadJournalColumnFamilyName )
            {
                journal = handle;
            }
        }
        if ( journal == nullptr )
        {
            ADD_FAILURE() << "SeedJournalRecord: upload_journal CF not found at " << coldstore_dir;
            for ( auto *handle : handles )
            {
                delete handle;
            }
            return false;
        }

        // Fixture hygiene: the target record exists and the journal is
        // non-empty (the populate phase birthed exactly these records).
        std::string current;
        status = db->Get( rocksdb::ReadOptions(), journal, key, &current );
        if ( !status.ok() )
        {
            ADD_FAILURE() << "SeedJournalRecord: target record absent before seeding (populate bug): "
                          << status.ToString();
        }
        size_t record_count = 0;
        {
            std::unique_ptr<rocksdb::Iterator> it( db->NewIterator( rocksdb::ReadOptions(), journal ) );
            for ( it->SeekToFirst(); it->Valid(); it->Next() )
            {
                ++record_count;
            }
        }
        EXPECT_GE( record_count, 1u ) << "SeedJournalRecord: empty journal (populate bug)";

        if ( value_bytes.has_value() )
        {
            status = db->Put( rocksdb::WriteOptions(), journal, key, value_bytes.value() );
        }
        else
        {
            status = db->Delete( rocksdb::WriteOptions(), journal, key );
        }
        const bool ok = status.ok();
        if ( !ok )
        {
            ADD_FAILURE() << "SeedJournalRecord: seed write failed: " << status.ToString();
        }
        for ( auto *handle : handles )
        {
            delete handle;
        }
        return ok;
    }

    /// A tiny spdlog sink counting WARN-level lines matching a substring —
    /// the gate-level Unreadable leg's "one WARN per group" observability
    /// (attached to the process-wide "TxPruner" logger the service
    /// registered via base::createLogger).
    class CountingWarnSink final : public spdlog::sinks::base_sink<std::mutex>
    {
    public:
        explicit CountingWarnSink( std::string needle )
            : needle_( std::move( needle ) )
        {
        }

        std::atomic<uint64_t> matches{ 0 };

    protected:
        void sink_it_( const spdlog::details::log_msg &msg ) override
        {
            if ( msg.level == spdlog::level::warn )
            {
                std::string formatted = std::string( msg.payload.data(), msg.payload.size() );
                if ( formatted.find( needle_ ) != std::string::npos )
                {
                    ++matches;
                }
            }
        }

        void flush_() override
        {
        }

    private:
        std::string needle_;
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
        // No hermetic clients: the unreachable endpoint holds the journal
        // at birth-Pending deterministically (the drainer's immediate
        // first pass uploads nothing).
        BuildStack( /*cold_enabled=*/true, /*drain_interval_ms=*/60000, /*inject_hermetic_clients=*/false );
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

    //
    // MATRIX (14-03 Task 1) — the journal-CF seeded legs: NoRecord /
    // Accepted / adjacency, parameterized over the seed variant. Every leg
    // runs the three-phase same-dir flow: populate (drain held at 60 s, so
    // every block births Pending) -> TeardownStack -> SeedJournalRecord ->
    // BuildStackOnDir reopen -> RunCycle -> deferral contract.
    //

    /// The seed variants under test. NoRecord deletes the record (D-02:
    /// defer forever, re-evaluated next cycle, never permanent-stop);
    /// Accepted overwrites to UploadedAccepted; the two garbage variants
    /// assert conservative decode — an unknown state or version byte
    /// decodes to PendingUpload and the group DEFERS (never a Durable
    /// permit, never the Unreadable counter — Pitfall 5a).
    class ColdPruneJournalSeedTest : public ColdPruneGateTest,
                                     public ::testing::WithParamInterface<std::string>
    {
    };

    TEST_P( ColdPruneJournalSeedTest, SeededStateDefersWholeGroupAsCounter )
    {
        const std::string &variant = GetParam();
        std::optional<std::string> seed;
        if ( variant == "Accepted" )
        {
            seed = kJournalAccepted;
        }
        else if ( variant == "GarbageStateByte" )
        {
            seed = kJournalGarbageState;
        }
        else if ( variant == "GarbageVersionByte" )
        {
            seed = kJournalGarbageVersion;
        }
        // "NoRecord" -> nullopt (delete the record).

        // PHASE A — populate: the RealPrune shape with the drainer held
        // off (no hermetic clients: the unreachable endpoint keeps every
        // birth record Pending deterministically — the drainer's immediate
        // reconciliation pass uploads nothing).
        BuildStack( /*cold_enabled=*/true, /*drain_interval_ms=*/60000, /*inject_hermetic_clients=*/false );
        ASSERT_NE( db_, nullptr );

        auto spender  = MakeTransfer( 1, 0 );
        auto eligible = MakeTransfer( 2, 1 );
        auto unspent  = MakeTransfer( 3, 1 );

        SeedTransaction( spender );
        PutSeedValue( "/seed/gate/seed-driver-1", "d1" );
        SeedTransaction( eligible );
        SeedTransaction( unspent );

        TrackOutputs( *eligible );
        TrackOutputs( *unspent );
        ASSERT_TRUE( ConsumeOutput( *eligible, 0, *spender ) );
        PutSeedValue( "/seed/gate/seed-driver-2", "d2" );

        const std::string tx_path = sgns::TransactionManager::GetTransactionPath( *eligible );
        const std::string cid_str = SnapshotMapEntry( tx_path );
        ASSERT_NE( cid_str, "absent" );
        ASSERT_EQ( JournalStateOf( cid_str ), crdt::ColdUploadState::Pending ) << "populate must birth Pending";

        const std::string hot_dir = last_hot_dir_;

        // PHASE B — seed (GlobalDB closed: the RocksDB dir lock is free).
        TeardownStack();
        ASSERT_TRUE( SeedJournalRecord( hot_dir + "/coldstore", cid_str, seed ) );

        // PHASE C — reopen and drive: nothing re-Puts and the drainer's
        // immediate reconciliation pass cannot succeed (no clients
        // injected), so the SEEDED bytes survive; the cycle reads the
        // seeded state at admission.
        BuildStackOnDir( /*cold_enabled=*/true,
                         /*drain_interval_ms=*/60000,
                         hot_dir,
                         /*inject_hermetic_clients=*/false );
        ASSERT_NE( db_, nullptr );
        const crdt::ColdUploadState expected_reopen_state =
            variant == "NoRecord"    ? crdt::ColdUploadState::NoRecord
            : variant == "Accepted" ? crdt::ColdUploadState::Accepted
                                     : crdt::ColdUploadState::Pending; // garbage -> conservative decode
        ASSERT_EQ( JournalStateOf( cid_str ), expected_reopen_state )
            << "seeded state did not survive the reopen (variant " << variant << ")";

        auto pruner = MakePruner( 10, kSmallSettleSeconds );
        ASSERT_NE( pruner, nullptr );

        sgns::PruneCycleStats stats;
        RunSettledCycle( *pruner, stats );

        // The deferral contract — identical for every non-Durable seed
        // (value states AND the conservative-decode garbage variants).
        EXPECT_EQ( stats.examined, 3u );
        EXPECT_EQ( stats.eligible, 1u );
        EXPECT_EQ( stats.erased, 0u );
        EXPECT_EQ( stats.errors, 0u );
        EXPECT_EQ( stats.cold_deferred_groups, 1u ) << "variant " << variant << " must defer as Pending-class";
        EXPECT_EQ( stats.cold_unreadable_groups, 0u )
            << "variant " << variant << " must NEVER surface as Unreadable (conservative decode, Pitfall 5a)";
        EXPECT_TRUE( db_->Get( { tx_path } ).has_value() ) << "variant " << variant << " destroyed the value";
        {
            auto lookup = sgns::PruneEligibility::LookupPrunedEntry( *db_, eligible->GetHash() );
            ASSERT_TRUE( lookup.has_value() );
            EXPECT_EQ( lookup.value().first, sgns::PrunedLookup::NOT_FOUND );
        }

        // D-02: defer-forever accepted — a second cycle re-evaluates and
        // defers again (deferred-not-forgotten), and the CYCLE COMPLETES
        // (a deferral is never a permanent-stop class: the result is a
        // success carrying counters, not a failure the caller stops on).
        auto second = RunCycle( *pruner );
        ASSERT_TRUE( second.has_value() ) << "variant " << variant << ": deferral classified as cycle failure";
        EXPECT_FALSE( sgns::TxPruner::IsPermanentStop( std::error_code{} ) );
        EXPECT_EQ( second.value().erased, 0u );
        EXPECT_EQ( second.value().cold_deferred_groups, 1u );
        EXPECT_TRUE( db_->Get( { tx_path } ).has_value() );
    }

    INSTANTIATE_TEST_SUITE_P(
        SeedVariants,
        ColdPruneJournalSeedTest,
        ::testing::Values( "NoRecord", "Accepted", "GarbageStateByte", "GarbageVersionByte" ) );

    //
    // UNREADABLE — (a) bridge level: a REAL fault through the free bridge.
    //

    /**
     * The family-1 leg (the durability-test fixture shape): a
     * directly-held LocalBlockStore + ColdStorageManager over the hermetic
     * fakes. A journaled block reads Pending; arming FaultController's
     * Unavailable fault makes GetUploadState fail non-NotFound and the
     * FREE BRIDGE sgns::crdt::GetColdUploadState maps it to Unreadable —
     * the real fault->mapping chain with ZERO overrides (research OQ2's
     * sanctioned split: the full-GlobalDB fixture cannot arm the fault —
     * the manager owns the store handle and a second Open hits the
     * RocksDB lock).
     */
    TEST_F( ColdPruneGateTest, UnreadableBridge_MapsRealFault )
    {
        using sgns::blockstore::Cid;
        using sgns::blockstore::CidOf;
        using sgns::blockstore::FaultKind;
        using sgns::blockstore::InMemoryS3AdminClient;
        using sgns::blockstore::InMemoryS3Client;
        using sgns::blockstore::LocalBlockStore;
        using sgns::blockstore::LocalBlockStoreOptions;
        using sgns::blockstore::S3BlockStore;
        using sgns::blockstore::S3Client;

        const std::string dir = FreshDir( "unreadable" );
        auto opened_local     = LocalBlockStore::Open( LocalBlockStoreOptions{ dir } );
        ASSERT_TRUE( opened_local.has_value() );
        auto local = std::shared_ptr<LocalBlockStore>( std::move( opened_local.value() ) );

        auto fake_s3 = std::make_shared<InMemoryS3Client>();
        auto opened_remote = S3BlockStore::Open( std::static_pointer_cast<S3Client>( fake_s3 ) );
        ASSERT_TRUE( opened_remote.has_value() );
        auto remote = std::shared_ptr<S3BlockStore>( std::move( opened_remote.value() ) );

        auto               fake_admin         = std::make_shared<InMemoryS3AdminClient>();
        auto               cold_store_mutex   = std::make_shared<std::mutex>();
        auto               remote_store_mutex = std::make_shared<std::mutex>();
        sgns::crdt::ColdStorageManager::BackpressureOptions backpressure;
        sgns::crdt::ColdStorageManager::DurabilityOptions    durability;
        sgns::crdt::ColdStorageManager::RetentionOptions     retention; // 0 = GC never runs
        auto manager = std::make_shared<sgns::crdt::ColdStorageManager>( local,
                                                                        remote,
                                                                        cold_store_mutex,
                                                                        remote_store_mutex,
                                                                        backpressure,
                                                                        durability,
                                                                        fake_admin,
                                                                        retention );

        // Journal a block (Put births a PendingUpload record).
        const std::string payload = "unreadable-bridge-probe";
        auto cid = CidOf( gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( payload.data() ),
                                                   payload.size() ),
                          libp2p::multi::MulticodecType::Code::RAW );
        ASSERT_TRUE( cid.has_value() );
        auto put = local->Put( cid.value(),
                              gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( payload.data() ),
                                                       payload.size() ) );
        ASSERT_TRUE( put.has_value() );

        // CID landmine: sgns::CID has NO implicit ctor from the library
        // Cid — wrap explicitly (the resurrection-test discipline).
        const sgns::CID probe_cid( cid.value() );

        // Healthy read: Pending (the birth state), through the free bridge.
        EXPECT_EQ( sgns::crdt::GetColdUploadState( manager, probe_cid ), crdt::ColdUploadState::Pending );

        // The REAL fault: all ops fail TemporarilyUnavailable (never
        // NotFound, never false) -> the bridge maps the non-NotFound
        // failure to Unreadable (ColdStorageStack.cpp:344-354).
        local->SetFault( FaultKind::Unavailable );
        EXPECT_EQ( sgns::crdt::GetColdUploadState( manager, probe_cid ), crdt::ColdUploadState::Unreadable );

        // Clear the fault (fixture hygiene — the store outlives the test).
        local->SetFault( FaultKind::None );
        EXPECT_EQ( sgns::crdt::GetColdUploadState( manager, probe_cid ), crdt::ColdUploadState::Pending );
    }

    //
    // UNREADABLE — (b) gate level: the dedicated counter + one WARN + the
    // cycle completes (D-03), driven through the 14-02 read hook (the only
    // way a full-GlobalDB fixture can produce an Unreadable read).
    //

    /**
     * A journal read that fails (Unreadable) defers the group LOUDLY: the
     * dedicated cold_unreadable_groups counter grows (NOT
     * cold_deferred_groups for this group), exactly ONE WARN line fires
     * (observed through a counting sink on the process-wide TxPruner
     * logger), the value survives, no marker exists, the cycle COMPLETES
     * (never a permanent-stop class — a single unreadable record must not
     * brick pruning), and the next cycle re-evaluates (D-03's letter).
     */
    TEST_F( ColdPruneGateTest, Gate_UnreadableDefersLoudlyAndCycleCompletes )
    {
        BuildStack( /*cold_enabled=*/true, /*drain_interval_ms=*/60000, /*inject_hermetic_clients=*/false );
        ASSERT_NE( db_, nullptr );

        auto spender  = MakeTransfer( 1, 0 );
        auto eligible = MakeTransfer( 2, 1 );
        auto unspent  = MakeTransfer( 3, 1 );

        SeedTransaction( spender );
        PutSeedValue( "/seed/gate/unreadable-driver-1", "d1" );
        SeedTransaction( eligible );
        SeedTransaction( unspent );

        TrackOutputs( *eligible );
        TrackOutputs( *unspent );
        ASSERT_TRUE( ConsumeOutput( *eligible, 0, *spender ) );
        PutSeedValue( "/seed/gate/unreadable-driver-2", "d2" );

        const std::string tx_path = sgns::TransactionManager::GetTransactionPath( *eligible );
        const std::string cid_str = SnapshotMapEntry( tx_path );
        ASSERT_NE( cid_str, "absent" );
        auto target_cid = sgns::CID::fromString( cid_str );
        ASSERT_FALSE( target_cid.has_error() );

        // The Unreadable injector: the read seam reports a failed read for
        // the group's CID (the full-GlobalDB fixture cannot arm the real
        // FaultController — the manager owns the store handle; the hook is
        // the sanctioned gate-level driver, cleared in teardown).
        sgns::crdt::GlobalDB::SetColdUploadStateHookForTest(
            [target_cid = target_cid.value() ]( const sgns::CID &c )
                -> std::optional<sgns::crdt::ColdUploadState> {
                return c == target_cid ? std::optional<sgns::crdt::ColdUploadState>(
                                           sgns::crdt::ColdUploadState::Unreadable )
                                       : std::nullopt;
            } );

        // WARN observability: attach a counting sink to the process-wide
        // TxPruner logger (base::createLogger registers it in spdlog's
        // global registry under the tag).
        auto counting_sink = std::make_shared<CountingWarnSink>( "Unreadable" );
        auto tx_pruner_logger = spdlog::get( "TxPruner" );
        ASSERT_NE( tx_pruner_logger, nullptr ) << "TxPruner logger not registered";
        tx_pruner_logger->sinks().push_back( counting_sink );

        auto pruner = MakePruner( 10, kSmallSettleSeconds );
        ASSERT_NE( pruner, nullptr );

        sgns::PruneCycleStats stats;
        RunSettledCycle( *pruner, stats );

        // D-03: loud, dedicated, never a brick.
        EXPECT_EQ( stats.erased, 0u );
        EXPECT_EQ( stats.errors, 0u );
        EXPECT_EQ( stats.cold_unreadable_groups, 1u );
        EXPECT_EQ( stats.cold_deferred_groups, 0u ) << "the Unreadable group must use its DEDICATED counter";
        EXPECT_EQ( counting_sink->matches.load(), 1u ) << "exactly ONE WARN per group expected";
        EXPECT_TRUE( db_->Get( { tx_path } ).has_value() );
        {
            auto lookup = sgns::PruneEligibility::LookupPrunedEntry( *db_, eligible->GetHash() );
            ASSERT_TRUE( lookup.has_value() );
            EXPECT_EQ( lookup.value().first, sgns::PrunedLookup::NOT_FOUND );
        }

        // The next cycle re-evaluates (loud again — never permanent-stop).
        auto second = RunCycle( *pruner );
        ASSERT_TRUE( second.has_value() );
        EXPECT_EQ( second.value().cold_unreadable_groups, 1u );
        EXPECT_EQ( counting_sink->matches.load(), 2u );

        // Fixture hygiene: detach the sink (the logger is process-global).
        auto &sinks = tx_pruner_logger->sinks();
        sinks.erase( std::remove( sinks.begin(), sinks.end(), counting_sink ), sinks.end() );
        sgns::crdt::GlobalDB::SetColdUploadStateHookForTest( nullptr );
    }

    //
    // ORDERING edge — a deferred leader neither blocks nor reorders a
    // Durable follower (first-appearance order, continue-never-abort).
    //

    /**
     * Two eligible groups, leader Pending (deferred) and follower Durable
     * (admitted, seeded through the journal CF): ONE cycle erases the
     * follower IN THE SAME CYCLE — the deferred leader's group is skipped
     * whole and the cycle continues past it. Counters show one deferral +
     * one erase; the leader's value survives, the follower's is gone with
     * its marker present.
     */
    TEST_F( ColdPruneGateTest, Gate_OrderingDeferredLeaderDoesNotBlockSurvivor )
    {
        // PHASE A — populate with the drainer held (everything births
        // Pending): leader first (first-appearance order), follower second.
        BuildStack( /*cold_enabled=*/true, /*drain_interval_ms=*/60000, /*inject_hermetic_clients=*/false );
        ASSERT_NE( db_, nullptr );

        auto spender = MakeTransfer( 1, 0 );
        auto leader  = MakeTransfer( 2, 1 );
        auto follower = MakeTransfer( 3, 1 );

        SeedTransaction( spender );
        PutSeedValue( "/seed/gate/order-driver-1", "d1" );
        SeedTransaction( leader );
        SeedTransaction( follower );

        TrackOutputs( *leader );
        TrackOutputs( *follower );
        ASSERT_TRUE( ConsumeOutput( *leader, 0, *spender ) );
        ASSERT_TRUE( ConsumeOutput( *follower, 0, *spender ) );
        PutSeedValue( "/seed/gate/order-driver-2", "d2" );

        const std::string leader_path   = sgns::TransactionManager::GetTransactionPath( *leader );
        const std::string follower_path = sgns::TransactionManager::GetTransactionPath( *follower );
        const std::string leader_cid    = SnapshotMapEntry( leader_path );
        const std::string follower_cid  = SnapshotMapEntry( follower_path );
        ASSERT_NE( leader_cid, follower_cid ) << "the ordering leg needs two DISTINCT delta blocks";

        const std::string hot_dir = last_hot_dir_;

        // PHASE B — seed: leader stays Pending (no rewrite needed), the
        // follower's record is rewritten to Durable.
        TeardownStack();
        ASSERT_TRUE( SeedJournalRecord( hot_dir + "/coldstore", follower_cid, kJournalDurable ) );

        // PHASE C — reopen and drive (no clients: the seeded Durable and
        // the held Pending both survive the immediate reconciliation).
        BuildStackOnDir( /*cold_enabled=*/true,
                         /*drain_interval_ms=*/60000,
                         hot_dir,
                         /*inject_hermetic_clients=*/false );
        ASSERT_NE( db_, nullptr );
        ASSERT_EQ( JournalStateOf( leader_cid ), crdt::ColdUploadState::Pending );
        ASSERT_EQ( JournalStateOf( follower_cid ), crdt::ColdUploadState::Durable );

        auto pruner = MakePruner( 10, kSmallSettleSeconds );
        ASSERT_NE( pruner, nullptr );

        sgns::PruneCycleStats stats;
        RunSettledCycle( *pruner, stats );

        // One cycle: the follower erased IN THE SAME CYCLE as the leader's
        // deferral (defer is continue-never-abort; no reordering, no
        // blocking, no partial-cycle).
        EXPECT_EQ( stats.erased, 1u );
        EXPECT_EQ( stats.cold_deferred_groups, 1u );
        EXPECT_EQ( stats.cold_unreadable_groups, 0u );
        EXPECT_EQ( stats.errors, 0u );
        EXPECT_TRUE( db_->Get( { leader_path } ).has_value() ) << "the deferred leader's value must survive";
        EXPECT_TRUE( db_->Get( { follower_path } ).has_error() ) << "the Durable follower must erase same-cycle";
        {
            auto leader_lookup = sgns::PruneEligibility::LookupPrunedEntry( *db_, leader->GetHash() );
            ASSERT_TRUE( leader_lookup.has_value() );
            EXPECT_EQ( leader_lookup.value().first, sgns::PrunedLookup::NOT_FOUND );
            auto follower_lookup = sgns::PruneEligibility::LookupPrunedEntry( *db_, follower->GetHash() );
            ASSERT_TRUE( follower_lookup.has_value() );
            EXPECT_EQ( follower_lookup.value().first, sgns::PrunedLookup::PRUNED );
        }
    }

    //
    // EMPTY edge — the gate moves no counter on empty input.
    //

    /**
     * A cold-enabled cycle with NO candidates (a fresh node, no
     * transactions): the cycle completes with every counter zero — the
     * gate is silent on empty input (and a non-erasable group is never
     * admitted: base admission stays the evaluator's).
     */
    TEST_F( ColdPruneGateTest, Gate_EmptyCycleMovesNoCounters )
    {
        BuildStack( /*cold_enabled=*/true, /*drain_interval_ms=*/60000, /*inject_hermetic_clients=*/false );
        ASSERT_NE( db_, nullptr );
        ASSERT_TRUE( db_->HasColdStorage() );

        // No transactions at all — only a plain seed put (non-tx data).
        PutSeedValue( "/seed/gate/empty-driver", "d1" );

        auto pruner = MakePruner( 10, kSmallSettleSeconds );
        ASSERT_NE( pruner, nullptr );

        sgns::PruneCycleStats stats;
        RunSettledCycle( *pruner, stats );

        EXPECT_EQ( stats.examined, 0u );
        EXPECT_EQ( stats.eligible, 0u );
        EXPECT_EQ( stats.erased, 0u );
        EXPECT_EQ( stats.cold_deferred_groups, 0u );
        EXPECT_EQ( stats.cold_unreadable_groups, 0u );
        EXPECT_EQ( stats.errors, 0u );
        EXPECT_EQ( stats.held_blocks, 0u );
    }

    //
    // SC3 composition crossing legs (14-03 Task 2) — the cold conjunct
    // composes WITH PruneEligibility::Evaluate verdicts as a pure AND:
    //   erasable x Durable      -> erased   (LEG 1)
    //   erasable x Pending      -> deferred (LEG 2)
    //   NON-erasable x Durable  -> NOT erased, NO cold-counter motion (c)
    //   no-cold x erasable      -> erased   (LEG 3)
    // The gate never widens admission (Pitfall 3's quiet-widening is
    // structurally killed) — and the evaluator stays byte-identical
    // (the phase-level purity git-gate; the D-C6 contract is quoted in
    // this file's header).
    //

    /**
     * Crossing leg (c) — the no-widening proof: a candidate the EVALUATOR
     * itself rejects (its spend sits INSIDE the spend-recency window: no
     * driver puts follow the consume, so maxHeight - spend_height <
     * finality_window) while its journal record is SEEDED Durable. The
     * gate must not erase: the group never reaches the cold conjunct
     * (erasable_cids membership is the FIRST conjunct), both cold counters
     * stay zero, and the value survives. Eligibility owns base admission;
     * the cold conjunct only narrows.
     */
    TEST_F( ColdPruneGateTest, Composition_NonErasableTimesDurableNeverErases )
    {
        // PHASE A — populate: the spend is the LAST height-raising act (no
        // driver puts after it), keeping the candidate INSIDE the window.
        BuildStack( /*cold_enabled=*/true, /*drain_interval_ms=*/60000, /*inject_hermetic_clients=*/false );
        ASSERT_NE( db_, nullptr );

        auto spender   = MakeTransfer( 1, 0 );
        auto in_window = MakeTransfer( 2, 1 );

        // The window keys on the SPENDER's height (spend_height = the
        // spender's CRDT priority; maxHeight - spend_height >= window) —
        // so the in-window shape seeds the spender RECENTLY (after the
        // height-raising drivers), the exact inverse of RealPrune's
        // ancient-spender-first recipe: the spend binds at the current
        // head and no further puts follow, keeping the candidate inside
        // the spend-recency window.
        SeedTransaction( in_window );
        TrackOutputs( *in_window );
        PutSeedValue( "/seed/gate/composition-driver-1", "d1" );
        PutSeedValue( "/seed/gate/composition-driver-2", "d2" );
        SeedTransaction( spender );
        ASSERT_TRUE( ConsumeOutput( *in_window, 0, *spender ) );
        // Deliberately NO further puts: maxHeight == spend_height, inside
        // the window -> the evaluator rejects the candidate.

        const std::string tx_path = sgns::TransactionManager::GetTransactionPath( *in_window );
        const std::string cid_str = SnapshotMapEntry( tx_path );
        ASSERT_NE( cid_str, "absent" );

        const std::string hot_dir = last_hot_dir_;

        // PHASE B — seed the journal Durable (the crossing's cold leg).
        TeardownStack();
        ASSERT_TRUE( SeedJournalRecord( hot_dir + "/coldstore", cid_str, kJournalDurable ) );

        // PHASE C — reopen and drive (no clients: the seeded Durable
        // survives the drainer's immediate reconciliation).
        BuildStackOnDir( /*cold_enabled=*/true,
                         /*drain_interval_ms=*/60000,
                         hot_dir,
                         /*inject_hermetic_clients=*/false );
        ASSERT_NE( db_, nullptr );
        ASSERT_EQ( JournalStateOf( cid_str ), crdt::ColdUploadState::Durable );

        // The two INDEPENDENT verdicts, read directly (the same pure
        // Evaluate call RunCycle makes): the evaluator says NOT eligible,
        // the journal says Durable.
        {
            sgns::PruningConfig config;
            config.enabled              = true;
            config.dry_run              = false;
            config.finality_window      = kWindow;
            config.max_erases_per_cycle = 10;
            config.settle_seconds       = kSmallSettleSeconds;
            auto evaluator = sgns::PruneEligibility::New( *db_, *tm_, account_->GetUTXOManager(),
                                                          sgns::NodeType::Light, config );
            ASSERT_TRUE( evaluator.has_value() );
            auto evaluation = evaluator.value()->Evaluate();
            ASSERT_TRUE( evaluation.has_value() );
            const sgns::PruneCandidateResult *in_window_candidate = nullptr;
            for ( const auto &candidate : evaluation.value().candidates )
            {
                if ( candidate.cid.value_or( "" ) == cid_str )
                {
                    in_window_candidate = &candidate;
                }
            }
            ASSERT_NE( in_window_candidate, nullptr ) << "the in-window candidate must enumerate";
            EXPECT_FALSE( in_window_candidate->eligible ) << "the evaluator must reject the in-window spend";
            EXPECT_TRUE( evaluation.value().block_reports.empty() )
                << "no block report exists for a non-eligible candidate (base admission is the evaluator's)";
        }
        ASSERT_EQ( JournalStateOf( cid_str ), crdt::ColdUploadState::Durable );

        auto pruner = MakePruner( 10, kSmallSettleSeconds );
        ASSERT_NE( pruner, nullptr );

        sgns::PruneCycleStats stats;
        RunSettledCycle( *pruner, stats );

        // The AND composition's no-widening leg: not erased, NO cold
        // counter motion (the group never reached the conjunct).
        EXPECT_EQ( stats.eligible, 0u );
        EXPECT_EQ( stats.erased, 0u );
        EXPECT_EQ( stats.cold_deferred_groups, 0u ) << "the gate must not count a group the evaluator never admitted";
        EXPECT_EQ( stats.cold_unreadable_groups, 0u );
        EXPECT_EQ( stats.errors, 0u );
        EXPECT_TRUE( db_->Get( { tx_path } ).has_value() );
        {
            auto lookup = sgns::PruneEligibility::LookupPrunedEntry( *db_, in_window->GetHash() );
            ASSERT_TRUE( lookup.has_value() );
            EXPECT_EQ( lookup.value().first, sgns::PrunedLookup::NOT_FOUND );
        }
    }

    /**
     * The direct AND-composition assertion for an ADMITTED group: both
     * independent verdicts are read in-test — the evaluator's block report
     * says erasable (the same pure Evaluate call RunCycle makes) AND the
     * journal reads Durable — and the cycle's erase is exactly their
     * conjunction. LEG 1 (erasable x Durable -> erased) is the behavioral
     * leg; this test pins the two-verdict reading.
     */
    TEST_F( ColdPruneGateTest, Composition_AndConjunctionReadsBothVerdicts )
    {
        BuildStack( /*cold_enabled=*/true, /*drain_interval_ms=*/50 );
        ASSERT_NE( db_, nullptr );

        auto spender  = MakeTransfer( 1, 0 );
        auto eligible = MakeTransfer( 2, 1 );

        SeedTransaction( spender );
        PutSeedValue( "/seed/gate/and-driver-1", "d1" );
        SeedTransaction( eligible );
        TrackOutputs( *eligible );
        ASSERT_TRUE( ConsumeOutput( *eligible, 0, *spender ) );
        PutSeedValue( "/seed/gate/and-driver-2", "d2" );

        const std::string cid_str = SnapshotMapEntry( sgns::TransactionManager::GetTransactionPath( *eligible ) );
        ASSERT_NE( cid_str, "absent" );

        // REAL transitions to Durable first (LEG 1's drive).
        ASSERT_TRUE( PollUntil( [&] { return JournalStateOf( cid_str ) == crdt::ColdUploadState::Durable; },
                                std::chrono::milliseconds( 20000 ) ) )
            << "eligible block never reached Durable";

        // Verdict 1 — the evaluator (pure, public, the identical call
        // RunCycle makes): the candidate is eligible and its block report
        // is erasable.
        {
            sgns::PruningConfig config;
            config.enabled              = true;
            config.dry_run              = false;
            config.finality_window      = kWindow;
            config.max_erases_per_cycle = 10;
            config.settle_seconds       = kSmallSettleSeconds;
            auto evaluator = sgns::PruneEligibility::New( *db_, *tm_, account_->GetUTXOManager(),
                                                          sgns::NodeType::Light, config );
            ASSERT_TRUE( evaluator.has_value() );
            auto evaluation = evaluator.value()->Evaluate();
            ASSERT_TRUE( evaluation.has_value() );
            bool found_eligible = false;
            for ( const auto &candidate : evaluation.value().candidates )
            {
                if ( candidate.cid.value_or( "" ) == cid_str )
                {
                    EXPECT_TRUE( candidate.eligible );
                    found_eligible = true;
                }
            }
            ASSERT_TRUE( found_eligible );
            ASSERT_EQ( evaluation.value().block_reports.size(), 1u );
            EXPECT_EQ( evaluation.value().block_reports[0].cid, cid_str );
            EXPECT_TRUE( evaluation.value().block_reports[0].erasable ) << "verdict 1: the evaluator permits";
        }

        // Verdict 2 — the journal: evidence-exactly-Durable.
        ASSERT_EQ( JournalStateOf( cid_str ), crdt::ColdUploadState::Durable ) << "verdict 2: cold evidence permits";

        // The conjunction: the cycle erases exactly this group.
        auto pruner = MakePruner( 10, kSmallSettleSeconds );
        ASSERT_NE( pruner, nullptr );
        sgns::PruneCycleStats stats;
        RunSettledCycle( *pruner, stats );
        EXPECT_EQ( stats.erased, 1u );
        EXPECT_EQ( stats.cold_deferred_groups, 0u );
        EXPECT_EQ( stats.cold_unreadable_groups, 0u );
        EXPECT_EQ( stats.errors, 0u );
        auto lookup = sgns::PruneEligibility::LookupPrunedEntry( *db_, eligible->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        EXPECT_EQ( lookup.value().first, sgns::PrunedLookup::PRUNED );
    }
} // namespace
