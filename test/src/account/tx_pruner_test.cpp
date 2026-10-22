/**
 * @file       tx_pruner_test.cpp
 * @brief      SC-1 (first real erase), cycle idempotence, the sweep-as-cycle
 *             re-prune (D-02b), the stranding-edge budget behavior, the SC-4
 *             tick-gate refusals, the RECL-03/RECL-04 observability +
 *             status contracts (including the D-01 compose-lock
 *             non-interleaving proof), and the SC-2 crash matrix — killing
 *             the cycle at EVERY interrupt point of BOTH seams and reopening
 *             the database on the same path converges to the same fixed
 *             point — all against the TxPruner SERVICE on real stores
 *             (GlobalDB + pubsub + temp dir, no GeniusNode).
 * @details    Every suite targets the service because GeniusNode is not
 *             unit-testable (the established testability split). Seeding
 *             always goes through the REAL publish path on the fixture's
 *             CRDT topic (never raw local puts — the Phase-16 lesson), and
 *             every erase assertion is three-part (CRDT value absent, raw
 *             tx->CID map entry absent, /prune/tx/<hash> marker PRUNED) with
 *             negative controls and byte-identity snapshots of everything
 *             the cycle must NOT touch (the /utxo/ prefix, non-tx records,
 *             the DAG heads, and the seal markers — GATE-03). Fixture
 *             discipline: BOTH interrupt seams (the cycle seam and
 *             GlobalDB's erase seam) are cleared FIRST in the fixture
 *             destructor so a hook armed by a failing case never leaks, and
 *             fresh DB paths back every case (double-run safe).
 *
 *             The RECL-03 bytes-reclaimed statement ("not measured until
 *             Phase 20") lives in the TxPruner INFO line; these suites pin
 *             its structural half — PruneStatus/PruneCycleStats carry NO
 *             bytes field at all, so no fabricated number can exist.
 * @date       2026-10-21
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <libp2p/basic/scheduler.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>

#include "account/GeniusAccount.hpp"
#include "account/TransferTransaction.hpp"
#include "account/UTXOManager.hpp"
#include "blockchain/Blockchain.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/storage/base_crdt_test.hpp"
#include "testutil/wait_condition.hpp"
#include "transaction/PruneEligibility.hpp"
#include "transaction/TransactionManager.hpp"
#include "transaction/TxPruner.hpp"

using namespace sgns;

namespace sgns
{
    /**
     * Reuse of the existing test seam declared in TransactionManager.hpp
     * (friend class TransactionManagerPendingLifecycleTestAccess): the
     * TxPruner tick gate requires State::READY, which no public API reaches
     * deterministically (StartCore's async nonce path is network-timed).
     * The pending-lifecycle test owns the same class name in its own binary;
     * this per-binary definition carries only the ChangeState member.
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

    /// The fixture's pre-seeded CRDT topic: puts on it create real blocks,
    /// heads, and seals (an empty-topics Put creates no destinations).
    constexpr char kSeedTopic[] = "CRDT.Datastore.TEST.Channel";

    /// Finality window N for every suite (the 17-04 recipes' value).
    constexpr uint64_t kWindow = 2;

    /// The account-generation tag the tests stamp onto cycles (D-C4).
    constexpr uint64_t kTestGeneration = 7;

    /// Settle window used wherever a cycle must run soon (waited out through
    /// the settled-cycle helper, never slept past blindly).
    constexpr uint64_t kSmallSettleSeconds = 1;

    /// Raw-surface snapshot: full key -> value bytes under one prefix.
    using RawSnapshot = std::map<std::string, std::string>;

    /// Binary-safe std::string copy of a buffer (Buffer::toString is a view).
    std::string BytesOf( const base::Buffer &buffer )
    {
        return std::string( buffer.toString() );
    }

    /// Simplified head map: topic -> set of CID strings.
    using HeadSnapshot = std::map<std::string, std::set<std::string>>;
} // namespace

/**
 * @brief Real-store fixture: GlobalDB + Blockchain + TransactionManager
 *        (NodeType::Light, no GeniusNode, no network sync) — the
 *        PruneEligibilityTest stack with its load-bearing teardown order,
 *        plus the TxPruner seeding/snapshot helpers.
 */
class TxPrunerTest : public ::test::CRDTFixture
{
public:
    TxPrunerTest() : ::test::CRDTFixture( "tx_pruner_test" )
    {
    }

    void SetUp() override
    {
        GeniusAccount::SetSecureStorageFactory( []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage>
                                                { return std::make_shared<MemorySecureStorage>( identifier ); } );

        account_ = GeniusAccount::New( kTestTokenId, base_path / "account" );
        assert( account_ != nullptr );

        // Load the UTXOManager's DB so the manager can store/restore UTXOs.
        auto load_result = account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() );
        assert( load_result.has_value() );

        blockchain_ = Blockchain::New( db_, account_, pubs_, []( outcome::result<void> ) {} );
        assert( blockchain_ != nullptr );

        constexpr auto kTimestampTolerance = std::chrono::milliseconds( 300000 );
        constexpr auto kMutabilityWindow   = std::chrono::milliseconds( 600000 );

        tm_ = TransactionManager::New( db_,
                                       io_,
                                       account_,
                                       blockchain_,
                                       NodeType::Light,
                                       0,
                                       kTimestampTolerance,
                                       kMutabilityWindow );
        assert( tm_ != nullptr );
    }

    ~TxPrunerTest() override
    {
        // Seam discipline (the globaldb_erase_test destructor order): a hook
        // armed by a failing case must never leak further — clear BOTH
        // interrupt seams before any teardown touches the stores.
        sgns::TxPruner::SetCycleInterruptHookForTest( nullptr );
        crdt::GlobalDB::SetEraseInterruptHookForTest( nullptr );

        /*
         * Teardown invariant (asio), copied verbatim from
         * PruneEligibilityTest/CertificateFallbackTest: stop the blockchain
         * FIRST so the ConsensusManager round-timer thread joins before any
         * db-backed handle is released; the base ~CRDTFixture stays the FINAL
         * host release (Linux CI segfault otherwise).
         */
        if ( blockchain_ )
        {
            (void) blockchain_->Stop();
        }
        tm_.reset();
        blockchain_.reset();
        account_.reset();
    }

    std::shared_ptr<GeniusAccount>      account_;
    std::shared_ptr<Blockchain>         blockchain_;
    std::shared_ptr<TransactionManager> tm_;

    //
    // Seeding helpers — every transaction goes through the REAL topic Put so
    // its CRDT value, delta CID, head, and per-key priority all exist, then
    // gets a raw tx->CID map entry in the exact StoreTransactionCID shapes
    // (the PruneEligibilityTest recipes verbatim).
    //

    /// Transfer with @p output_count outputs owned by this account (empty for spenders).
    std::shared_ptr<TransferTransaction> MakeTransfer( uint64_t nonce, size_t output_count )
    {
        SGTransaction::DAGStruct dag;
        dag.set_type( "transfer" );
        dag.set_source_addr( account_->GetAddress() );
        dag.set_nonce( nonce );
        dag.set_timestamp( static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() ) );

        std::vector<OutputDestInfo> destinations;
        destinations.reserve( output_count );
        for ( size_t i = 0; i < output_count; ++i )
        {
            destinations.push_back( { 100 + i, account_->GetAddress(), kTestTokenId } );
        }
        return std::make_shared<TransferTransaction>(
            TransferTransaction::New( {}, std::move( destinations ), std::move( dag ) ) );
    }

    /// Real-topic value Put + raw tx->CID map entry (StoreTransactionCID shapes).
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

    /// Commits several transactions' values in ONE delta block (a batched
    /// AtomicTransaction on the seed topic — the shared-block recipe) and
    /// records each tx->CID map entry against that block's CID.
    void SeedTransactionsInOneBlock( const std::vector<std::shared_ptr<GeniusTransaction>> &transactions )
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
                batch->Put( { TransactionManager::GetTransactionPath( *transaction ) }, serialized ).has_value() );
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
            key_buffer.put( TransactionManager::GetTransactionPath( *transaction ) );
            crdt::GlobalDB::Buffer value_buffer;
            value_buffer.put( block_cid );
            ASSERT_TRUE( db_->GetDataStore()->put( key_buffer, value_buffer ).has_value() );
        }
    }

    /// Plain topic Put of a raw string at @p key (driver heights, non-tx
    /// data, tombstone targets) — real publish path, never a local-only put.
    void PutSeedValue( const std::string &key, const std::string &value )
    {
        crdt::GlobalDB::Buffer buffer;
        buffer.put( value );
        ASSERT_TRUE( db_->Put( { key }, buffer, { kSeedTopic } ).has_value() );
    }

    /// PutSeedValue that also reports the delta CID string (seal snapshots).
    std::string PutSeedValueReturningCid( const std::string &key, const std::string &value )
    {
        crdt::GlobalDB::Buffer buffer;
        buffer.put( value );
        auto put = db_->Put( { key }, buffer, { kSeedTopic } );
        EXPECT_TRUE( put.has_value() );
        return put.has_value() ? put.value().toString().value() : "";
    }

    /// Records the tx->CID map entry (StoreTransactionCID shapes) for @p hash
    /// against @p cid — the resurrection re-wiring after a re-publish.
    void SeedTxCidEntry( const std::string &tx_hash, const std::string &cid )
    {
        crdt::GlobalDB::Buffer key_buffer;
        key_buffer.put( TransactionManager::GetTransactionPath( tx_hash ) );
        crdt::GlobalDB::Buffer value_buffer;
        value_buffer.put( cid );
        ASSERT_TRUE( db_->GetDataStore()->put( key_buffer, value_buffer ).has_value() );
    }

    /// Tracks a candidate's produced outputs in the UTXO manager (PutProducedUTXOs shape).
    void TrackOutputs( const GeniusTransaction &transaction )
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
    bool ConsumeOutput( const GeniusTransaction &transaction, uint32_t output_idx, const GeniusTransaction &spender )
    {
        auto txid = base::Hash256::fromReadableString( transaction.GetHash() );
        EXPECT_FALSE( txid.has_error() );
        auto consumed = account_->GetUTXOManager().ConsumeUTXOs( { { txid.value(), output_idx, {} } },
                                                                 account_->GetAddress(),
                                                                 UTXOManager::UTXOType::UTXO_NORMAL,
                                                                 SpenderHash( spender ) );
        EXPECT_TRUE( consumed.has_value() );
        return consumed.has_value() && consumed.value();
    }

    static base::Hash256 SpenderHash( const GeniusTransaction &transaction )
    {
        return base::Hash256::fromReadableString( transaction.GetHash() ).value();
    }

    /// Extracts the tx hash from a /bc-<net>/tx/<hash> candidate path.
    static std::string HashOfPath( const std::string &tx_path )
    {
        const std::string prefix = TransactionManager::GetBlockChainBase() + "tx/";
        return tx_path.substr( prefix.size() );
    }

    //
    // Pruner helpers.
    //

    /// Constructs a Light TxPruner with the suite's window and dry_run=false.
    std::shared_ptr<sgns::TxPruner> MakePruner( uint64_t k_budget, uint64_t settle_seconds )
    {
        PruningConfig config;
        config.enabled              = true;
        config.dry_run              = false;
        config.finality_window      = kWindow;
        config.max_erases_per_cycle = k_budget;
        config.settle_seconds       = settle_seconds;
        auto pruner                 = sgns::TxPruner::New( NodeType::Light, config );
        EXPECT_TRUE( pruner.has_value() );
        return pruner.has_value() ? pruner.value() : nullptr;
    }

    /// Drives the manager into the tick gate's READY state (the friend seam
    /// noted above; the gate is the only state consumer on this path).
    void ForceManagerReady()
    {
        TransactionManagerPendingLifecycleTestAccess::ChangeState( *tm_, TransactionManager::State::READY );
        ASSERT_EQ( tm_->GetState(), TransactionManager::State::READY );
    }

    /**
     * Runs cycles until one is NOT gate-skipped, delivering its stats.
     * FRESH-INSTANCE CONTRACT: the discriminator is the watermark turning
     * set (skipped ticks leave the status untouched), so @p pruner must not
     * have completed a cycle yet. The bounded wait polls RunCycle — a skip
     * is cheap and side-effect free.
     */
    void RunSettledCycle( sgns::TxPruner &pruner, PruneCycleStats &out_stats )
    {
        ForceManagerReady();
        std::chrono::milliseconds waited( 0 );
        bool                      cycle_failed = false;
        bool                      ran          = false;
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
            &waited );
        EXPECT_FALSE( cycle_failed );
        EXPECT_TRUE( ran );
    }

    /// One plain cycle (the gate is already settled on @p pruner).
    outcome::result<PruneCycleStats> RunCycle( sgns::TxPruner &pruner, uint64_t generation = kTestGeneration )
    {
        return pruner.RunCycle( *db_, *tm_, account_->GetUTXOManager(), generation );
    }

    //
    // Crash-matrix helpers (SC-2): the simulated kill tears the whole stack
    // down in the fixture's own destruction order and rebuilds it on the SAME
    // database path — the Phase-16 restart discipline (restartNodePending
    // Start), so every fixed-point assertion reads what survived on disk.
    //

    /**
     * Runs cycles until one RUNS (examined > 0) with an interruption seam
     * already armed — the RunSettledCycle discriminator (watermark turning
     * set) cannot be used here because seam-interrupted cycles deliberately
     * leave the status untouched. A gate-skipped tick is side-effect free,
     * so polling RunCycle is safe with either seam armed.
     */
    PruneCycleStats RunUntilCycleRuns( sgns::TxPruner &pruner )
    {
        ForceManagerReady();
        PruneCycleStats           stats;
        bool                      cycle_failed = false;
        bool                      ran          = false;
        std::chrono::milliseconds waited( 0 );
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                auto result = pruner.RunCycle( *db_, *tm_, account_->GetUTXOManager(), kTestGeneration );
                if ( result.has_error() )
                {
                    cycle_failed = true;
                    return true; // exit the bounded wait; EXPECT below fails loudly
                }
                stats = result.value();
                ran   = stats.examined > 0;
                return ran;
            },
            std::chrono::milliseconds( kSmallSettleSeconds * 1000 + 8000 ),
            "tick gate settles and the interrupted prune cycle runs",
            &waited );
        EXPECT_FALSE( cycle_failed );
        EXPECT_TRUE( ran );
        return stats;
    }

    /**
     * Tears the whole stack down (the fixture destructor's own order: both
     * seams already cleared, blockchain first, db before pubsub) and
     * reconstructs the GlobalDB + account + blockchain + manager stack on the
     * SAME database path — the crash/restart of the SC-2 matrix. The pubsub
     * host and the io pool survive (the Phase-16 makeNode wiring, rebuilt
     * graphsync network and scheduler over the live host); the manager is
     * re-driven to READY for the post-restart cycles.
     */
    void ReopenStackOnSamePath()
    {
        if ( blockchain_ )
        {
            (void) blockchain_->Stop();
        }
        tm_.reset();
        blockchain_.reset();
        account_.reset();
        if ( db_ )
        {
            db_->ShutdownNow();
        }
        db_.reset();

        auto scheduler = std::make_shared<libp2p::basic::SchedulerImpl>(
            std::make_shared<libp2p::basic::AsioSchedulerBackend>( pubs_->GetAsioContext() ),
            libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
        auto graphsync_network = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubs_->GetHost(),
                                                                                              scheduler );
        auto generator         = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();
        auto reopened          = crdt::GlobalDB::New( io_,
                                                      db_path_,
                                                      pubs_,
                                                      crdt::CrdtOptions::DefaultOptions(),
                                                      graphsync_network,
                                                      scheduler,
                                                      generator );
        ASSERT_TRUE( reopened.has_value() );
        db_ = reopened.value();
        db_->AddListenTopic( kSeedTopic );
        db_->AddBroadcastTopic( kSeedTopic );
        db_->Start();

        account_ = GeniusAccount::New( kTestTokenId, base_path / "account" );
        ASSERT_NE( account_, nullptr );
        ASSERT_TRUE( account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() ).has_value() );

        blockchain_ = Blockchain::New( db_, account_, pubs_, []( outcome::result<void> ) {} );
        ASSERT_NE( blockchain_, nullptr );
        constexpr auto kTimestampTolerance = std::chrono::milliseconds( 300000 );
        constexpr auto kMutabilityWindow   = std::chrono::milliseconds( 600000 );
        tm_                                = TransactionManager::New( db_,
                                                                      io_,
                                                                      account_,
                                                                      blockchain_,
                                                                      NodeType::Light,
                                                                      0,
                                                                      kTimestampTolerance,
                                                                      kMutabilityWindow );
        ASSERT_NE( tm_, nullptr );
        ForceManagerReady();
    }

    //
    // Snapshot helpers — byte-identity proofs of everything a cycle must
    // NOT touch (the unspent UTXO set, non-tx records, heads, seals).
    //

    /// Raw key -> value bytes of every entry under @p prefix.
    RawSnapshot SnapshotPrefix( const std::string &prefix )
    {
        RawSnapshot snapshot;
        auto        entries = db_->QueryRaw( crdt::GlobalDB::Buffer{}.put( prefix ) );
        if ( entries.has_error() )
        {
            return snapshot;
        }
        for ( const auto &[key, value] : entries.value() )
        {
            snapshot[BytesOf( key )] = BytesOf( value );
        }
        return snapshot;
    }

    /// Raw /crdt/-namespace entries (the durable element-key families) whose
    /// key contains @p needle — the CRDT-layer footprint of one element. The
    /// transient /crdt/work/ broadcaster-queue entries are EXCLUDED: they are
    /// pending-publish plumbing drained asynchronously by the pubsub
    /// broadcaster, not record bytes — their presence depends on drain
    /// timing, not on any prune action.
    RawSnapshot SnapshotCrdtEntriesContaining( const std::string &needle )
    {
        RawSnapshot snapshot;
        auto        entries = db_->QueryRaw( crdt::GlobalDB::Buffer{}.put( std::string( "/crdt/" ) ) );
        if ( entries.has_error() )
        {
            return snapshot;
        }
        static const std::string kTransientWorkFamily = "/crdt/work/";
        for ( const auto &[key, value] : entries.value() )
        {
            const std::string key_str = BytesOf( key );
            if ( key_str.rfind( kTransientWorkFamily, 0 ) == 0 )
            {
                continue;
            }
            if ( key_str.find( needle ) != std::string::npos )
            {
                snapshot[key_str] = BytesOf( value );
            }
        }
        return snapshot;
    }

    /// The whole topic -> heads map (guard-1's scan surface).
    HeadSnapshot SnapshotHeads( uint64_t &max_height )
    {
        HeadSnapshot snapshot;
        auto         heads = db_->GetCRDTHeadList();
        EXPECT_TRUE( heads.has_value() );
        if ( heads.has_error() )
        {
            return snapshot;
        }
        for ( const auto &[topic, topic_heads] : heads.value().first )
        {
            for ( const auto &cid : topic_heads )
            {
                snapshot[topic].insert( cid.toString().value() );
            }
        }
        max_height = heads.value().second;
        return snapshot;
    }

    /// Seal-marker bytes per CID (the raw <cid>sealed key; "absent" when
    /// gone) — GATE-03's never-erased surface.
    RawSnapshot SnapshotSeals( const std::vector<std::string> &cids )
    {
        RawSnapshot snapshot;
        for ( const auto &cid : cids )
        {
            auto seal     = db_->GetRaw( crdt::GlobalDB::Buffer{}.put( cid + "sealed" ) );
            snapshot[cid] = seal.has_value() ? BytesOf( seal.value() ) : std::string( "absent" );
        }
        return snapshot;
    }

    /// Raw block-bytes hex per CID ("absent" when gone) — the block level.
    RawSnapshot SnapshotBlocks( const std::vector<std::string> &cids )
    {
        RawSnapshot snapshot;
        for ( const auto &cid : cids )
        {
            auto parsed = CID::fromString( cid );
            EXPECT_TRUE( parsed.has_value() );
            if ( parsed.has_error() )
            {
                snapshot[cid] = "unparseable";
                continue;
            }
            auto bytes = parsed.value().toBytes();
            EXPECT_TRUE( bytes.has_value() );
            auto block = db_->GetRaw(
                crdt::GlobalDB::Buffer( bytes.has_value() ? std::move( bytes.value() ) : std::vector<uint8_t>{} ) );
            snapshot[cid] = block.has_value() ? BytesOf( block.value() ) : std::string( "absent" );
        }
        return snapshot;
    }

    /// The raw tx->CID map entry bytes at @p tx_path ("absent" when gone).
    std::string SnapshotMapEntry( const std::string &tx_path )
    {
        auto entry = db_->GetRaw( crdt::GlobalDB::Buffer{}.put( tx_path ) );
        return entry.has_value() ? BytesOf( entry.value() ) : std::string( "absent" );
    }
};

// ---------------------------------------------------------------------------
// RealPrune: SC-1 — the first real erase on the service, everything else
// byte-identical
// ---------------------------------------------------------------------------

/**
 * The phase's core service proof. Two spent-beyond-window transactions erase
 * through ONE cycle; per erased tx the three-part set holds (CRDT value
 * absent, raw map entry absent, marker PRUNED) plus the block bytes absent;
 * the /utxo/ prefix (the unspent set AND the consumed spend evidence), the
 * cert/seed non-tx records, the DAG heads, and every seal marker stay
 * byte-identical; the unspent sibling is the untouched negative control.
 */
TEST_F( TxPrunerTest, RealPrune_ErasesEligibleAndLeavesEverythingElseByteIdentical )
{
    // Ancient spender first, then a driver, so its arrival height sits far
    // below the final head (the 17-04 seeding recipe).
    auto spender    = MakeTransfer( 1, 0 );
    auto eligible_a = MakeTransfer( 2, 1 );
    auto eligible_b = MakeTransfer( 3, 1 );
    auto unspent    = MakeTransfer( 4, 2 ); // the untouched sibling

    SeedTransaction( spender );
    const std::string driver_cid_1 = PutSeedValueReturningCid( "/seed/txpruner/real-driver-1", "d1" );
    SeedTransaction( eligible_a );
    SeedTransaction( eligible_b );
    SeedTransaction( unspent );

    TrackOutputs( *eligible_a );
    TrackOutputs( *eligible_b );
    TrackOutputs( *unspent ); // deliberately never consumed
    ASSERT_TRUE( ConsumeOutput( *eligible_a, 0, *spender ) );
    ASSERT_TRUE( ConsumeOutput( *eligible_b, 0, *spender ) );

    // Non-tx data: a cert-shaped record and plain seed keys.
    const std::string cert_key    = "/cert/" + account_->GetAddress() + ":41";
    const std::string cert_cid    = PutSeedValueReturningCid( cert_key, "certificate-record" );
    const std::string seed_cid    = PutSeedValueReturningCid( "/seed/txpruner/real-data", "untouched" );
    const std::string driver_cid2 = PutSeedValueReturningCid( "/seed/txpruner/real-driver-2", "d2" );

    // Block CIDs from the map entries (captured before any erase removes them).
    const std::string cid_a = SnapshotMapEntry( TransactionManager::GetTransactionPath( *eligible_a ) );
    const std::string cid_b = SnapshotMapEntry( TransactionManager::GetTransactionPath( *eligible_b ) );
    const std::string cid_u = SnapshotMapEntry( TransactionManager::GetTransactionPath( *unspent ) );
    const std::string cid_s = SnapshotMapEntry( TransactionManager::GetTransactionPath( *spender ) );
    ASSERT_NE( cid_a, "absent" );
    ASSERT_NE( cid_b, "absent" );

    // Snapshots BEFORE: everything the cycle must not touch.
    const RawSnapshot  utxo_before  = SnapshotPrefix( "/utxo/" );
    const RawSnapshot  cert_before  = SnapshotCrdtEntriesContaining( cert_key );
    const RawSnapshot  seed_before  = SnapshotCrdtEntriesContaining( "/seed/txpruner/real-data" );
    uint64_t           max_height   = 0;
    const HeadSnapshot heads_before = SnapshotHeads( max_height );
    const RawSnapshot  seals_before = SnapshotSeals(
        { cid_a, cid_b, cid_u, cid_s, cert_cid, seed_cid, driver_cid_1, driver_cid2 } );
    const RawSnapshot blocks_before = SnapshotBlocks( { cid_u, cid_s } );
    const std::string sibling_value = BytesOf(
        db_->Get( { TransactionManager::GetTransactionPath( *unspent ) } ).value() );
    const RawSnapshot sibling_crdt = SnapshotCrdtEntriesContaining(
        TransactionManager::GetTransactionPath( *unspent ) );
    ASSERT_FALSE( utxo_before.empty() ); // tracked outputs (spent + unspent) are on disk

    // Pre-state: both eligibles present, no markers yet.
    for ( const auto &tx : { eligible_a.get(), eligible_b.get() } )
    {
        ASSERT_TRUE( db_->Get( { TransactionManager::GetTransactionPath( *tx ) } ).has_value() );
        auto pre = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
        ASSERT_TRUE( pre.has_value() );
        ASSERT_EQ( pre.value().first, PrunedLookup::NOT_FOUND );
    }

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );

    PruneCycleStats stats;
    RunSettledCycle( *pruner, stats );

    // Counts match the fixture: 4 candidates (spender, A, B, U), 2 eligible,
    // 2 erased, nothing held/skipped/errored.
    EXPECT_EQ( stats.examined, 4u );
    EXPECT_EQ( stats.eligible, 2u );
    EXPECT_EQ( stats.erased, 2u );
    EXPECT_EQ( stats.held_blocks, 0u );
    EXPECT_EQ( stats.skipped, 0u );
    EXPECT_EQ( stats.errors, 0u );
    EXPECT_EQ( stats.generation, kTestGeneration );

    for ( const auto &tx : { eligible_a.get(), eligible_b.get() } )
    {
        const std::string tx_path = TransactionManager::GetTransactionPath( *tx );

        // Three-part assertion: CRDT value gone (all four families), raw map
        // entry gone, marker PRUNED naming this tx's path.
        EXPECT_TRUE( db_->Get( { tx_path } ).has_error() );
        EXPECT_TRUE( SnapshotCrdtEntriesContaining( tx_path ).empty() );
        EXPECT_EQ( SnapshotMapEntry( tx_path ), "absent" );
        auto lookup = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        EXPECT_EQ( lookup.value().first, PrunedLookup::PRUNED );
        ASSERT_TRUE( lookup.value().second.has_value() );
        EXPECT_EQ( lookup.value().second->tx_path(), tx_path );
    }

    // The block bytes of both erased deltas are gone.
    const RawSnapshot erased_blocks = SnapshotBlocks( { cid_a, cid_b } );
    EXPECT_EQ( erased_blocks.at( cid_a ), "absent" );
    EXPECT_EQ( erased_blocks.at( cid_b ), "absent" );

    // Byte-identity of every untouched surface.
    EXPECT_EQ( SnapshotPrefix( "/utxo/" ), utxo_before ) << "the unspent UTXO set changed";
    EXPECT_EQ( SnapshotCrdtEntriesContaining( cert_key ), cert_before ) << "cert record changed";
    EXPECT_EQ( SnapshotCrdtEntriesContaining( "/seed/txpruner/real-data" ), seed_before ) << "non-tx data changed";
    uint64_t           max_height_after = 0;
    const HeadSnapshot heads_after      = SnapshotHeads( max_height_after );
    EXPECT_EQ( heads_after, heads_before ) << "the DAG heads changed";
    EXPECT_EQ( max_height_after, max_height );
    EXPECT_EQ( SnapshotSeals( { cid_a, cid_b, cid_u, cid_s, cert_cid, seed_cid, driver_cid_1, driver_cid2 } ),
               seals_before )
        << "a seal marker changed (erased deltas' seals must survive — GATE-03)";

    // Negative control: the unspent sibling is byte-identical on every level.
    EXPECT_EQ( BytesOf( db_->Get( { TransactionManager::GetTransactionPath( *unspent ) } ).value() ), sibling_value );
    EXPECT_EQ( SnapshotMapEntry( TransactionManager::GetTransactionPath( *unspent ) ), cid_u );
    EXPECT_EQ( SnapshotBlocks( { cid_u, cid_s } ), blocks_before );
    EXPECT_EQ( SnapshotCrdtEntriesContaining( TransactionManager::GetTransactionPath( *unspent ) ), sibling_crdt );
    auto sibling_lookup = PruneEligibility::LookupPrunedEntry( *db_, unspent->GetHash() );
    ASSERT_TRUE( sibling_lookup.has_value() );
    EXPECT_EQ( sibling_lookup.value().first, PrunedLookup::NOT_FOUND );

    // The watermark is the maxHeight the completed cycle observed.
    const auto status = pruner->GetPruneStatus();
    ASSERT_TRUE( status.watermark.has_value() );
    EXPECT_EQ( status.watermark.value(), max_height );
}

// ---------------------------------------------------------------------------
// Idempotent: a second cycle after a full drain is a no-op
// ---------------------------------------------------------------------------

/**
 * After the backlog drains (cycles until erased==0), one more cycle examines
 * the surviving candidates, erases nothing, and mutates nothing: markers,
 * /utxo/, heads, and the still-absent erased entries are all unchanged.
 */
TEST_F( TxPrunerTest, Idempotent_CycleAfterFullDrainErasesAndMutatesNothing )
{
    auto spender    = MakeTransfer( 1, 0 );
    auto eligible_a = MakeTransfer( 2, 1 );
    auto eligible_b = MakeTransfer( 3, 1 );
    auto unspent    = MakeTransfer( 4, 1 );

    SeedTransaction( spender );
    PutSeedValue( "/seed/txpruner/idem-driver-1", "d1" );
    SeedTransaction( eligible_a );
    SeedTransaction( eligible_b );
    SeedTransaction( unspent );

    TrackOutputs( *eligible_a );
    TrackOutputs( *eligible_b );
    TrackOutputs( *unspent );
    ASSERT_TRUE( ConsumeOutput( *eligible_a, 0, *spender ) );
    ASSERT_TRUE( ConsumeOutput( *eligible_b, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/idem-driver-2", "d2" );

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );

    // Drain fully: the settled first cycle erases both; repeat until quiet.
    PruneCycleStats stats;
    RunSettledCycle( *pruner, stats );
    ASSERT_EQ( stats.erased, 2u );
    for ( int i = 0; i < 3; ++i )
    {
        auto again = RunCycle( *pruner );
        ASSERT_TRUE( again.has_value() );
        EXPECT_EQ( again.value().erased, 0u );
    }

    // Snapshot the drained state.
    const RawSnapshot  markers_before = SnapshotPrefix( "/prune/tx/" );
    const RawSnapshot  utxo_before    = SnapshotPrefix( "/utxo/" );
    uint64_t           max_height     = 0;
    const HeadSnapshot heads_before   = SnapshotHeads( max_height );
    const std::string  map_a          = SnapshotMapEntry( TransactionManager::GetTransactionPath( *eligible_a ) );
    const std::string  map_b          = SnapshotMapEntry( TransactionManager::GetTransactionPath( *eligible_b ) );
    const std::string  sibling_value  = BytesOf(
        db_->Get( { TransactionManager::GetTransactionPath( *unspent ) } ).value() );
    ASSERT_EQ( markers_before.size(), 2u ); // exactly one marker per erased tx
    ASSERT_EQ( map_a, "absent" );
    ASSERT_EQ( map_b, "absent" );

    // The post-drain no-op cycle: examined>0, erased==0, nothing changes.
    auto result = RunCycle( *pruner );
    ASSERT_TRUE( result.has_value() );
    EXPECT_GT( result.value().examined, 0u ); // spender + unspent sibling remain
    EXPECT_EQ( result.value().eligible, 0u );
    EXPECT_EQ( result.value().erased, 0u );
    EXPECT_EQ( result.value().held_blocks, 0u );
    EXPECT_EQ( result.value().skipped, 0u );
    EXPECT_EQ( result.value().errors, 0u );

    EXPECT_EQ( SnapshotPrefix( "/prune/tx/" ), markers_before );
    EXPECT_EQ( SnapshotPrefix( "/utxo/" ), utxo_before );
    uint64_t           max_height_after = 0;
    const HeadSnapshot heads_after      = SnapshotHeads( max_height_after );
    EXPECT_EQ( heads_after, heads_before );
    EXPECT_EQ( max_height_after, max_height );
    EXPECT_EQ( SnapshotMapEntry( TransactionManager::GetTransactionPath( *eligible_a ) ), "absent" );
    EXPECT_EQ( SnapshotMapEntry( TransactionManager::GetTransactionPath( *eligible_b ) ), "absent" );
    EXPECT_EQ( BytesOf( db_->Get( { TransactionManager::GetTransactionPath( *unspent ) } ).value() ), sibling_value );

    // The markers still answer PRUNED (value absent + marker present).
    for ( const auto &tx : { eligible_a.get(), eligible_b.get() } )
    {
        auto lookup = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        EXPECT_EQ( lookup.value().first, PrunedLookup::PRUNED );
    }
}

// ---------------------------------------------------------------------------
// SweepReprunes: the cycle IS the sweep (D-02b, PRUNE-05's second half)
// ---------------------------------------------------------------------------

/**
 * A resurrected entry (an add-wins re-merge simulation: the same element
 * re-published through the REAL topic path after a full erase) is visible as
 * the marker-plus-value resurrected state, and the NEXT cycle re-erases it —
 * the evaluation is fresh every cycle (no cached eligible sets), and the
 * marker is idempotent (exactly one marker entry, no duplicate harm).
 */
TEST_F( TxPrunerTest, SweepReprunes_ResurrectedEntryIsReErasedNextCycle )
{
    auto spender  = MakeTransfer( 1, 0 );
    auto eligible = MakeTransfer( 2, 1 );
    auto unspent  = MakeTransfer( 3, 1 );

    SeedTransaction( spender );
    PutSeedValue( "/seed/txpruner/sweep-driver-1", "d1" );
    SeedTransaction( eligible );
    SeedTransaction( unspent );

    TrackOutputs( *eligible );
    TrackOutputs( *unspent );
    ASSERT_TRUE( ConsumeOutput( *eligible, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/sweep-driver-2", "d2" );

    const std::string tx_path = TransactionManager::GetTransactionPath( *eligible );
    const std::string cid_1   = SnapshotMapEntry( tx_path );

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );

    // First cycle: the full erase.
    PruneCycleStats stats;
    RunSettledCycle( *pruner, stats );
    ASSERT_EQ( stats.erased, 1u );
    ASSERT_TRUE( db_->Get( { tx_path } ).has_error() );
    {
        auto lookup = PruneEligibility::LookupPrunedEntry( *db_, eligible->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        ASSERT_EQ( lookup.value().first, PrunedLookup::PRUNED );
    }

    // The add-wins re-merge simulation: the same element again through the
    // REAL publish path (a delayed concurrent merge landing after the
    // erase), plus the map entry re-wired to the new delta.
    crdt::GlobalDB::Buffer serialized;
    serialized.put( eligible->SerializeByteVector() );
    auto republish = db_->Put( { tx_path }, serialized, { kSeedTopic } );
    ASSERT_TRUE( republish.has_value() );
    const std::string cid_2 = republish.value().toString().value();
    SeedTxCidEntry( eligible->GetHash(), cid_2 );
    PutSeedValue( "/seed/txpruner/sweep-driver-3", "d3" ); // retire cid_2 from headship

    // The resurrected state is VISIBLE: value present again while the old
    // marker survived — the marker-plus-value shape, never a silent pass.
    ASSERT_TRUE( db_->Get( { tx_path } ).has_value() );
    {
        auto lookup = PruneEligibility::LookupPrunedEntry( *db_, eligible->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        EXPECT_EQ( lookup.value().first, PrunedLookup::VALUE_PRESENT_INVARIANT_VIOLATION );
    }

    // The sweep: ONE later cycle re-erases the resurrected entry (the
    // evaluation enumerated it again — no cached eligible set could have).
    auto sweep = RunCycle( *pruner );
    ASSERT_TRUE( sweep.has_value() );
    EXPECT_EQ( sweep.value().examined, 3u ); // spender + resurrected + unspent
    EXPECT_EQ( sweep.value().eligible, 1u );
    EXPECT_EQ( sweep.value().erased, 1u );
    EXPECT_EQ( sweep.value().held_blocks, 0u );
    EXPECT_EQ( sweep.value().skipped, 0u );
    EXPECT_EQ( sweep.value().errors, 0u );

    EXPECT_TRUE( db_->Get( { tx_path } ).has_error() );
    EXPECT_EQ( SnapshotMapEntry( tx_path ), "absent" );
    EXPECT_EQ( SnapshotBlocks( { cid_1, cid_2 } ).at( cid_2 ), "absent" );
    {
        auto lookup = PruneEligibility::LookupPrunedEntry( *db_, eligible->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        EXPECT_EQ( lookup.value().first, PrunedLookup::PRUNED );
    }

    // No duplicate-marker harm: exactly ONE marker entry for this hash.
    EXPECT_EQ( SnapshotPrefix( "/prune/tx/" ).size(), 1u );
}

// ---------------------------------------------------------------------------
// SharedBlockBudget: the stranding edge (D-04 group-or-defer)
// ---------------------------------------------------------------------------

/**
 * Two eligible transactions sharing ONE delta block: with K=1 the group
 * DEFERS WHOLE (erased==0, held_blocks==1, both txs intact — a partial take
 * would strand a sibling behind the exhausted budget); with K=2 (a second
 * pruner instance — K is captured at construction) both erase together: the
 * shared block is gone once and both markers are present.
 */
TEST_F( TxPrunerTest, SharedBlockBudget_K1DefersWholeThenK2TakesWhole )
{
    auto spender = MakeTransfer( 1, 0 );
    auto tx_a    = MakeTransfer( 2, 1 );
    auto tx_b    = MakeTransfer( 3, 1 );

    SeedTransaction( spender );
    PutSeedValue( "/seed/txpruner/shared-driver-1", "d1" );

    TrackOutputs( *tx_a );
    TrackOutputs( *tx_b );
    SeedTransactionsInOneBlock( { tx_a, tx_b } );
    ASSERT_TRUE( ConsumeOutput( *tx_a, 0, *spender ) );
    ASSERT_TRUE( ConsumeOutput( *tx_b, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/shared-driver-2", "d2" );

    const std::string path_a     = TransactionManager::GetTransactionPath( *tx_a );
    const std::string path_b     = TransactionManager::GetTransactionPath( *tx_b );
    const std::string shared_cid = SnapshotMapEntry( path_a );
    ASSERT_EQ( shared_cid, SnapshotMapEntry( path_b ) ); // ONE shared delta

    // K = 1: the 2-tx group does not fit — deferred WHOLE.
    auto pruner_k1 = MakePruner( 1, kSmallSettleSeconds );
    ASSERT_NE( pruner_k1, nullptr );
    PruneCycleStats stats;
    RunSettledCycle( *pruner_k1, stats );
    EXPECT_EQ( stats.examined, 3u );
    EXPECT_EQ( stats.eligible, 2u );
    EXPECT_EQ( stats.erased, 0u );
    EXPECT_EQ( stats.held_blocks, 1u );
    EXPECT_TRUE( db_->Get( { path_a } ).has_value() );
    EXPECT_TRUE( db_->Get( { path_b } ).has_value() );
    EXPECT_NE( SnapshotBlocks( { shared_cid } ).at( shared_cid ), "absent" );
    {
        auto lookup_a = PruneEligibility::LookupPrunedEntry( *db_, tx_a->GetHash() );
        ASSERT_TRUE( lookup_a.has_value() );
        EXPECT_EQ( lookup_a.value().first, PrunedLookup::NOT_FOUND );
        auto lookup_b = PruneEligibility::LookupPrunedEntry( *db_, tx_b->GetHash() );
        ASSERT_TRUE( lookup_b.has_value() );
        EXPECT_EQ( lookup_b.value().first, PrunedLookup::NOT_FOUND );
    }

    // K = 2 (fresh instance): the group fits — both erase TOGETHER.
    auto pruner_k2 = MakePruner( 2, kSmallSettleSeconds );
    ASSERT_NE( pruner_k2, nullptr );
    PruneCycleStats stats_k2;
    RunSettledCycle( *pruner_k2, stats_k2 );
    EXPECT_EQ( stats_k2.eligible, 2u );
    EXPECT_EQ( stats_k2.erased, 2u );
    EXPECT_EQ( stats_k2.held_blocks, 0u );
    EXPECT_EQ( stats_k2.errors, 0u );

    EXPECT_TRUE( db_->Get( { path_a } ).has_error() );
    EXPECT_TRUE( db_->Get( { path_b } ).has_error() );
    EXPECT_EQ( SnapshotMapEntry( path_a ), "absent" );
    EXPECT_EQ( SnapshotMapEntry( path_b ), "absent" );
    EXPECT_EQ( SnapshotBlocks( { shared_cid } ).at( shared_cid ), "absent" ); // gone ONCE
    for ( const auto &tx : { tx_a.get(), tx_b.get() } )
    {
        auto lookup = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        EXPECT_EQ( lookup.value().first, PrunedLookup::PRUNED );
    }
}

/**
 * The missing-block completion arm: sibling A is erased directly through the
 * sanctioned Phase-16 driver (deleting the SHARED block), leaving B
 * stranded; a FRESH evaluation reports the BLOCK_ALREADY_ERASED evidence on
 * the absent block's report, and the next cycle completes B through that
 * evidence — nothing strands.
 */
TEST_F( TxPrunerTest, SharedBlockBudget_MissingBlockSiblingCompletesViaEvidence )
{
    auto spender = MakeTransfer( 1, 0 );
    auto tx_a    = MakeTransfer( 2, 1 );
    auto tx_b    = MakeTransfer( 3, 1 );

    SeedTransaction( spender );
    PutSeedValue( "/seed/txpruner/missing-driver-1", "d1" );

    TrackOutputs( *tx_a );
    TrackOutputs( *tx_b );
    SeedTransactionsInOneBlock( { tx_a, tx_b } );
    ASSERT_TRUE( ConsumeOutput( *tx_a, 0, *spender ) );
    ASSERT_TRUE( ConsumeOutput( *tx_b, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/missing-driver-2", "d2" );

    const std::string path_b     = TransactionManager::GetTransactionPath( *tx_b );
    const std::string shared_cid = SnapshotMapEntry( TransactionManager::GetTransactionPath( *tx_a ) );

    // The budget-exhaustion shape: A's sanctioned erase deletes the SHARED
    // block and strands B (the 18-03 G1 recipe, driven on this fixture).
    ASSERT_TRUE( tm_->EraseTransactionEntry( tx_a->GetHash() ).has_value() );
    ASSERT_EQ( SnapshotBlocks( { shared_cid } ).at( shared_cid ), "absent" );
    ASSERT_TRUE( db_->Get( { path_b } ).has_value() ); // B stranded, value intact

    // The explicit evidence in a FRESH evaluation's block_reports (the
    // checker-adjusted assertion: the evidence lives in the evaluator result).
    {
        PruningConfig config;
        config.finality_window = kWindow;
        auto evaluator = PruneEligibility::New( *db_, *tm_, account_->GetUTXOManager(), NodeType::Light, config );
        ASSERT_TRUE( evaluator.has_value() );
        auto evaluation = evaluator.value()->Evaluate();
        ASSERT_TRUE( evaluation.has_value() );
        const BlockGateResult *report = nullptr;
        for ( const auto &candidate_report : evaluation.value().block_reports )
        {
            if ( candidate_report.cid == shared_cid )
            {
                report = &candidate_report;
            }
        }
        ASSERT_NE( report, nullptr );
        EXPECT_TRUE( report->erasable );
        const auto evidence_found = std::find( report->reasons.begin(),
                                               report->reasons.end(),
                                               PruneReason::BLOCK_ALREADY_ERASED );
        EXPECT_NE( evidence_found, report->reasons.end() );
    }

    // The cycle completes B through the evidence: value + map entry + marker.
    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );
    PruneCycleStats stats;
    RunSettledCycle( *pruner, stats );
    EXPECT_EQ( stats.erased, 1u );
    EXPECT_EQ( stats.held_blocks, 0u );
    EXPECT_EQ( stats.errors, 0u );

    EXPECT_TRUE( db_->Get( { path_b } ).has_error() );
    EXPECT_EQ( SnapshotMapEntry( path_b ), "absent" );
    auto lookup_b = PruneEligibility::LookupPrunedEntry( *db_, tx_b->GetHash() );
    ASSERT_TRUE( lookup_b.has_value() );
    EXPECT_EQ( lookup_b.value().first, PrunedLookup::PRUNED );

    // The plain driver wrote NO marker for A — the pruned marker is the
    // cycle's alone (the two drivers stay distinguishable).
    auto lookup_a = PruneEligibility::LookupPrunedEntry( *db_, tx_a->GetHash() );
    ASSERT_TRUE( lookup_a.has_value() );
    EXPECT_EQ( lookup_a.value().first, PrunedLookup::NOT_FOUND );
}

// ---------------------------------------------------------------------------
// TickGate: SC-4 service-half refusals
// ---------------------------------------------------------------------------

/** Non-Light roles are refused at construction — the GATE-01 mirror. */
TEST_F( TxPrunerTest, TickGate_NonLightRolesRefusedAtConstruction )
{
    PruningConfig config;
    config.enabled         = true;
    config.finality_window = kWindow;

    auto full = sgns::TxPruner::New( NodeType::Full, config );
    ASSERT_TRUE( full.has_error() );
    EXPECT_EQ( full.error(), std::errc::operation_not_permitted );
    EXPECT_TRUE( sgns::TxPruner::IsPermanentStop( full.error() ) );

    auto archive = sgns::TxPruner::New( NodeType::Archive, config );
    ASSERT_TRUE( archive.has_error() );
    EXPECT_EQ( archive.error(), std::errc::operation_not_permitted );

    auto light = sgns::TxPruner::New( NodeType::Light, config );
    EXPECT_TRUE( light.has_value() );
}

/**
 * A TransactionManager outside State::READY gate-skips the cycle: zeroed
 * stats, status untouched, nothing erased — and the same pruner DOES run
 * once the manager reaches READY (the skip is the gate, not a vacuous pass).
 */
TEST_F( TxPrunerTest, TickGate_NotReadySkipsThenRunsWhenReady )
{
    auto spender  = MakeTransfer( 1, 0 );
    auto eligible = MakeTransfer( 2, 1 );

    SeedTransaction( spender );
    PutSeedValue( "/seed/txpruner/gate-driver-1", "d1" );
    SeedTransaction( eligible );
    TrackOutputs( *eligible );
    ASSERT_TRUE( ConsumeOutput( *eligible, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/gate-driver-2", "d2" );

    ASSERT_NE( tm_->GetState(), TransactionManager::State::READY ); // still CREATING

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );

    auto skipped = RunCycle( *pruner );
    ASSERT_TRUE( skipped.has_value() );
    EXPECT_EQ( skipped.value().examined, 0u );
    EXPECT_EQ( skipped.value().eligible, 0u );
    EXPECT_EQ( skipped.value().erased, 0u );
    EXPECT_EQ( skipped.value().held_blocks, 0u );
    EXPECT_EQ( skipped.value().skipped, 0u );
    EXPECT_EQ( skipped.value().errors, 0u );
    EXPECT_FALSE( pruner->GetPruneStatus().watermark.has_value() ); // status untouched
    EXPECT_TRUE( db_->Get( { TransactionManager::GetTransactionPath( *eligible ) } ).has_value() );

    // Non-vacuous proof: READY reached, the settled cycle erases.
    PruneCycleStats stats;
    RunSettledCycle( *pruner, stats );
    EXPECT_EQ( stats.erased, 1u );
    EXPECT_TRUE( pruner->GetPruneStatus().watermark.has_value() );
}

/**
 * The settle window: with a large settle the gate holds READY yet skips
 * twice (clock start, then not elapsed) without touching anything; a second
 * pruner with the small settle then proves the skip was the settle gate —
 * its cycle runs and erases.
 */
TEST_F( TxPrunerTest, TickGate_SettleWindowNotElapsedSkipsThenAllowsAfterSettle )
{
    auto spender  = MakeTransfer( 1, 0 );
    auto eligible = MakeTransfer( 2, 1 );

    SeedTransaction( spender );
    PutSeedValue( "/seed/txpruner/settle-driver-1", "d1" );
    SeedTransaction( eligible );
    TrackOutputs( *eligible );
    ASSERT_TRUE( ConsumeOutput( *eligible, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/settle-driver-2", "d2" );

    auto pruner_large_settle = MakePruner( 10, 3600 ); // settle far beyond the test
    ASSERT_NE( pruner_large_settle, nullptr );
    ForceManagerReady();

    for ( int i = 0; i < 2; ++i )
    {
        auto skipped = RunCycle( *pruner_large_settle );
        ASSERT_TRUE( skipped.has_value() );
        EXPECT_EQ( skipped.value().examined, 0u );
        EXPECT_EQ( skipped.value().erased, 0u );
    }
    EXPECT_FALSE( pruner_large_settle->GetPruneStatus().watermark.has_value() );
    EXPECT_TRUE( db_->Get( { TransactionManager::GetTransactionPath( *eligible ) } ).has_value() );

    // The same fixture under a small settle runs: the earlier skips were the
    // settle window, nothing else.
    auto pruner_small_settle = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner_small_settle, nullptr );
    PruneCycleStats stats;
    RunSettledCycle( *pruner_small_settle, stats );
    EXPECT_EQ( stats.erased, 1u );
}

/**
 * The missing-set conjunct at the accessor level: an empty fixture reports
 * HasMissingTxHashes()==false, so the gate's third input holds in every
 * cycle above. Driving a NON-empty missing set has no public seam (it fills
 * only through the sync scan inside StartCore) — not faked here.
 */
TEST_F( TxPrunerTest, TickGate_EmptyMissingSetAtAccessorLevel )
{
    const TransactionManager &const_tm = *tm_;
    EXPECT_FALSE( const_tm.HasMissingTxHashes() );
}

// ---------------------------------------------------------------------------
// Observability: RECL-03 — per-cycle fields match the fixture exactly
// ---------------------------------------------------------------------------

/**
 * The per-cycle observability contract: every PruneCycleStats counter equals
 * the fixture's known actions, the generation tag travels onto the stats and
 * the queryable status, the watermark equals the maxHeight the completed
 * cycle observed, and status.last_cycle carries the same counters. There is
 * deliberately NO bytes-reclaimed field anywhere (the RECL-03 INFO line
 * states "not measured until Phase 20" — the status can never carry a
 * fabricated number).
 */
TEST_F( TxPrunerTest, Observability_StatsAndStatusMatchFixtureActions )
{
    auto spender    = MakeTransfer( 1, 0 );
    auto eligible_a = MakeTransfer( 2, 1 );
    auto eligible_b = MakeTransfer( 3, 1 );
    auto unspent    = MakeTransfer( 4, 1 );

    SeedTransaction( spender );
    PutSeedValue( "/seed/txpruner/obs-driver-1", "d1" );
    SeedTransaction( eligible_a );
    SeedTransaction( eligible_b );
    SeedTransaction( unspent );

    TrackOutputs( *eligible_a );
    TrackOutputs( *eligible_b );
    TrackOutputs( *unspent );
    ASSERT_TRUE( ConsumeOutput( *eligible_a, 0, *spender ) );
    ASSERT_TRUE( ConsumeOutput( *eligible_b, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/obs-driver-2", "d2" );

    uint64_t max_height = 0;
    SnapshotHeads( max_height );

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );

    PruneCycleStats stats;
    RunSettledCycle( *pruner, stats );

    // Counters == the fixture's actions, nothing approximate.
    EXPECT_EQ( stats.examined, 4u );
    EXPECT_EQ( stats.eligible, 2u );
    EXPECT_EQ( stats.erased, 2u );
    EXPECT_EQ( stats.held_blocks, 0u );
    EXPECT_EQ( stats.skipped, 0u );
    EXPECT_EQ( stats.errors, 0u );
    EXPECT_EQ( stats.generation, kTestGeneration );
    EXPECT_FALSE( stats.generation_superseded ); // caller-owned: ships false (D-C4)

    const auto status = pruner->GetPruneStatus();
    ASSERT_TRUE( status.watermark.has_value() );
    EXPECT_EQ( status.watermark.value(), max_height ); // the observed maxHeight
    EXPECT_EQ( status.last_cycle.examined, stats.examined );
    EXPECT_EQ( status.last_cycle.eligible, stats.eligible );
    EXPECT_EQ( status.last_cycle.erased, stats.erased );
    EXPECT_EQ( status.last_cycle.held_blocks, stats.held_blocks );
    EXPECT_EQ( status.last_cycle.skipped, stats.skipped );
    EXPECT_EQ( status.last_cycle.errors, stats.errors );
    EXPECT_EQ( status.last_cycle.generation, stats.generation );
    EXPECT_EQ( status.last_cycle.duration_ms, stats.duration_ms );

    // The generation tag travels on every invocation's stats (a
    // generation-superseded caller would see ITS tag here).
    auto noop = RunCycle( *pruner, kTestGeneration + 2 );
    ASSERT_TRUE( noop.has_value() );
    EXPECT_EQ( noop.value().generation, kTestGeneration + 2 );
    EXPECT_EQ( pruner->GetPruneStatus().last_cycle.generation, kTestGeneration + 2 );
}

// ---------------------------------------------------------------------------
// PruneStatus: RECL-04 — the watermark contract
// ---------------------------------------------------------------------------

/**
 * The watermark is the maxHeight of the last cycle that ran to completion
 * without hard error: an INTERRUPTED cycle (seam-held before its first
 * erase) leaves both the watermark and last_cycle untouched, and the next
 * completed cycle advances the watermark monotonically to the current head.
 */
TEST_F( TxPrunerTest, PruneStatus_InterruptedCycleLeavesWatermarkUntouched )
{
    auto spender    = MakeTransfer( 1, 0 );
    auto eligible_a = MakeTransfer( 2, 1 );

    SeedTransaction( spender );
    PutSeedValue( "/seed/txpruner/wm-driver-1", "d1" );
    SeedTransaction( eligible_a );
    TrackOutputs( *eligible_a );
    ASSERT_TRUE( ConsumeOutput( *eligible_a, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/wm-driver-2", "d2" );

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );

    PruneCycleStats first;
    RunSettledCycle( *pruner, first );
    ASSERT_EQ( first.erased, 1u );
    const auto status_first = pruner->GetPruneStatus();
    ASSERT_TRUE( status_first.watermark.has_value() );
    const uint64_t watermark_first = status_first.watermark.value();

    // New heights and NEW eligible work past the first watermark.
    PutSeedValue( "/seed/txpruner/wm-driver-3", "d3" );
    auto eligible_b = MakeTransfer( 3, 1 );
    SeedTransaction( eligible_b );
    TrackOutputs( *eligible_b );
    ASSERT_TRUE( ConsumeOutput( *eligible_b, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/wm-driver-4", "d4" );
    uint64_t max_height_now = 0;
    SnapshotHeads( max_height_now );
    ASSERT_GT( max_height_now, watermark_first );

    // Interrupt the next cycle BEFORE its first erase: status untouched.
    sgns::TxPruner::SetCycleInterruptHookForTest( []( int step )
                                                  { return step == sgns::TxPruner::kInterruptBeforeErase; } );
    auto interrupted = RunCycle( *pruner );
    sgns::TxPruner::SetCycleInterruptHookForTest( nullptr );
    ASSERT_TRUE( interrupted.has_value() );
    EXPECT_GT( interrupted.value().examined, 0u ); // it evaluated — and still did not advance
    EXPECT_EQ( interrupted.value().erased, 0u );

    const auto status_interrupted = pruner->GetPruneStatus();
    ASSERT_TRUE( status_interrupted.watermark.has_value() );
    EXPECT_EQ( status_interrupted.watermark.value(), watermark_first ); // NOT advanced
    EXPECT_EQ( status_interrupted.last_cycle.erased, first.erased );    // last_cycle still cycle 1's

    // The next completed cycle advances monotonically to the current head.
    auto completed = RunCycle( *pruner );
    ASSERT_TRUE( completed.has_value() );
    EXPECT_EQ( completed.value().erased, 1u );
    const auto status_done = pruner->GetPruneStatus();
    ASSERT_TRUE( status_done.watermark.has_value() );
    EXPECT_EQ( status_done.watermark.value(), max_height_now );
    EXPECT_GE( status_done.watermark.value(), watermark_first );
}

/**
 * A CONCURRENT GetPruneStatus() during a running cycle returns coherent
 * snapshots without deadlock: a reader thread spins on the status while the
 * cycle is seam-held mid-flight, releases it only after many successful
 * reads, and both sides complete.
 */
TEST_F( TxPrunerTest, PruneStatus_ConcurrentReadDuringCycleIsCoherent )
{
    auto spender = MakeTransfer( 1, 0 );
    auto unspent = MakeTransfer( 2, 1 );

    SeedTransaction( spender );
    SeedTransaction( unspent );
    PutSeedValue( "/seed/txpruner/conc-driver-1", "d1" );

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );

    // Prime: a completed watermark-carrying cycle with no eligible work.
    PruneCycleStats prime;
    RunSettledCycle( *pruner, prime );
    ASSERT_EQ( prime.erased, 0u );
    ASSERT_TRUE( pruner->GetPruneStatus().watermark.has_value() );

    // New eligible work so the raced cycle has an erase to perform.
    auto eligible = MakeTransfer( 3, 1 );
    SeedTransaction( eligible );
    TrackOutputs( *eligible );
    ASSERT_TRUE( ConsumeOutput( *eligible, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/conc-driver-2", "d2" );

    std::atomic<bool>   hook_entered{ false };
    std::atomic<bool>   release_cycle{ false };
    std::atomic<bool>   stop_reader{ false };
    std::atomic<size_t> reads{ 0 };
    std::atomic<bool>   watermark_seen{ false };

    // Reader: spin on GetPruneStatus the whole time; after enough concurrent
    // reads INSIDE the held window, release the cycle thread.
    std::thread reader(
        [&]
        {
            while ( !stop_reader.load() )
            {
                const auto snapshot = pruner->GetPruneStatus();
                ++reads;
                if ( snapshot.watermark.has_value() )
                {
                    watermark_seen.store( true );
                }
                if ( hook_entered.load() && reads.load() > 200 )
                {
                    release_cycle.store( true );
                }
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            }
            release_cycle.store( true ); // never leave the cycle held on exit
        } );

    sgns::TxPruner::SetCycleInterruptHookForTest(
        [&]( int step )
        {
            if ( step == sgns::TxPruner::kInterruptAfterEvaluation )
            {
                hook_entered.store( true );
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
                while ( !release_cycle.load() && std::chrono::steady_clock::now() < deadline )
                {
                    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
                }
                return false; // continue the cycle (the hold was the point)
            }
            return false;
        } );

    auto raced = RunCycle( *pruner ); // returns only once the reader released it
    sgns::TxPruner::SetCycleInterruptHookForTest( nullptr );
    stop_reader.store( true );
    reader.join();

    ASSERT_TRUE( raced.has_value() );
    EXPECT_EQ( raced.value().erased, 1u );
    EXPECT_GT( reads.load(), 200u );      // the concurrent reads really happened
    EXPECT_TRUE( watermark_seen.load() ); // every snapshot was coherent (a full copy)
}

// ---------------------------------------------------------------------------
// ComposeLock: the D-01 non-interleaving proof
// ---------------------------------------------------------------------------

/**
 * The critical-section capstone. The cycle is held INSIDE the erase compose
 * (GlobalDB's own seam step 1: guards passed, CRDT staged, commit pending —
 * the compose lock HELD) while a concurrent PutElems re-add races on the
 * SAME key: the merge provably cannot interleave (it blocks on the
    * owner-vended lock until the erase commits), and the final state is a
 * legal serialization — the marker exists (the erase committed atomically)
 * and the re-added value is the add-wins resurrected shape. The next cycle
 * re-erases it (the sweep closes the race). NEVER a torn mix: value absent
 * without a marker is unreachable.
 */
TEST_F( TxPrunerTest, ComposeLock_ConcurrentMergeSerializesNeverTorn )
{
    auto spender = MakeTransfer( 1, 0 );
    auto unspent = MakeTransfer( 2, 1 );

    SeedTransaction( spender );
    SeedTransaction( unspent );
    PutSeedValue( "/seed/txpruner/cl-driver-1", "d1" );

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );

    // Prime the settle clock with a completed no-work cycle (fresh-instance
    // contract of RunSettledCycle), so the raced cycle runs immediately.
    PruneCycleStats prime;
    RunSettledCycle( *pruner, prime );
    ASSERT_EQ( prime.erased, 0u );

    // The erase target: eligible, sealed, retired from headship.
    auto eligible = MakeTransfer( 3, 1 );
    SeedTransaction( eligible );
    TrackOutputs( *eligible );
    ASSERT_TRUE( ConsumeOutput( *eligible, 0, *spender ) );
    PutSeedValue( "/seed/txpruner/cl-driver-2", "d2" );

    const std::string tx_path = TransactionManager::GetTransactionPath( *eligible );
    const std::string cid_1   = SnapshotMapEntry( tx_path );

    // Hold the cycle thread inside the compose (guards passed, staged, NOT
    // committed) via GlobalDB's erase seam; release only from this thread.
    std::promise<void> entered_promise;
    std::future<void>  entered_future = entered_promise.get_future();
    std::promise<void> release_promise;
    std::future<void>  release_future = release_promise.get_future();
    crdt::GlobalDB::SetEraseInterruptHookForTest(
        [&]( int step )
        {
            if ( step == 1 )
            {
                entered_promise.set_value();
                release_future.wait_for( std::chrono::seconds( 5 ) );
            }
            return false; // continue: the HOLD is the deterministic window
        } );

    std::optional<outcome::result<PruneCycleStats>> cycle_result;
    std::thread                                     cycle_thread( [&] { cycle_result = RunCycle( *pruner ); } );

    ASSERT_TRUE( entered_future.wait_for( std::chrono::seconds( 5 ) ) == std::future_status::ready )
        << "the cycle never reached the erase compose";

    // The racer: a concurrent merge re-adding the SAME element through the
    // real publish path (PutElems build+commit under the same CrdtSet mutex).
    std::atomic<bool>          racer_done{ false };
    std::optional<std::string> racer_cid;
    crdt::GlobalDB::Buffer     serialized;
    serialized.put( eligible->SerializeByteVector() );
    std::thread racer(
        [&]
        {
            auto put = db_->Put( { tx_path }, serialized, { kSeedTopic } );
            if ( put.has_value() )
            {
                racer_cid = put.value().toString().value();
            }
            racer_done.store( true );
        } );

    // The non-interleaving proof: while the compose lock is held, the merge
    // cannot complete (bounded observation — it is blocked on the lock).
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( 300 );
        while ( std::chrono::steady_clock::now() < deadline && !racer_done.load() )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        }
        EXPECT_FALSE( racer_done.load() ) << "the merge interleaved with the held erase compose";
    }

    // Release: the erase commits first, the merge serializes after it.
    release_promise.set_value();
    {
        std::chrono::milliseconds waited( 0 );
        ASSERT_WAIT_FOR_CONDITION( [&] { return racer_done.load(); },
                                   std::chrono::milliseconds( 5000 ),
                                   "the merge completes after the compose lock frees",
                                   &waited );
    }
    racer.join();
    cycle_thread.join();
    crdt::GlobalDB::SetEraseInterruptHookForTest( nullptr );

    ASSERT_TRUE( cycle_result.has_value() && cycle_result.value().has_value() );
    EXPECT_EQ( cycle_result.value().value().erased, 1u );
    ASSERT_TRUE( racer_cid.has_value() );

    // The legal serialization (this ordering: erase committed, THEN the
    // merge's add-wins re-add): the marker EXISTS (the erase never commits
    // partially) and the value is present again — never a torn mix.
    auto lookup = PruneEligibility::LookupPrunedEntry( *db_, eligible->GetHash() );
    ASSERT_TRUE( lookup.has_value() );
    EXPECT_NE( lookup.value().first, PrunedLookup::NOT_FOUND );     // the marker committed
    EXPECT_TRUE( db_->Get( { tx_path } ).has_value() );             // the merge won the value
    EXPECT_EQ( SnapshotBlocks( { cid_1 } ).at( cid_1 ), "absent" ); // the erased block stays gone

    // The sweep closes the race: re-wire the map entry to the merged delta,
    // retire it from headship, and the next cycle re-erases.
    SeedTxCidEntry( eligible->GetHash(), racer_cid.value() );
    PutSeedValue( "/seed/txpruner/cl-driver-3", "d3" );
    auto sweep = RunCycle( *pruner );
    ASSERT_TRUE( sweep.has_value() );
    EXPECT_EQ( sweep.value().erased, 1u );
    EXPECT_TRUE( db_->Get( { tx_path } ).has_error() );
    auto final_lookup = PruneEligibility::LookupPrunedEntry( *db_, eligible->GetHash() );
    ASSERT_TRUE( final_lookup.has_value() );
    EXPECT_EQ( final_lookup.value().first, PrunedLookup::PRUNED );
}

/**
 * The guards are evaluated INSIDE the critical section at erase time: an
 * otherwise-eligible candidate whose delta block is the LIVE topic head is
 * refused by the compose's head guard — the evaluation and the block gate
 * accepted it, the erase refused — leaving value AND marker both in the
 * pre-erase state (never torn), counted as a per-tx error on a cycle that
 * still completes. Once the headship retires, the next cycle erases cleanly.
 */
TEST_F( TxPrunerTest, ComposeLock_HeadGuardRefusesAtEraseTimeNeverTorn )
{
    auto spender  = MakeTransfer( 1, 0 );
    auto eligible = MakeTransfer( 2, 1 );

    SeedTransaction( spender );
    PutSeedValue( "/seed/txpruner/hg-driver-1", "d1" );
    SeedTransaction( eligible ); // its delta IS the topic's live head
    TrackOutputs( *eligible );
    ASSERT_TRUE( ConsumeOutput( *eligible, 0, *spender ) );
    // NO later put: the eligible tx's block stays the head.

    const std::string tx_path = TransactionManager::GetTransactionPath( *eligible );

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );

    PruneCycleStats stats;
    RunSettledCycle( *pruner, stats );

    // The evaluation accepted it (eligible), the compose refused it (guard):
    // a per-tx error, nothing erased, nothing written.
    EXPECT_EQ( stats.examined, 2u );
    EXPECT_EQ( stats.eligible, 1u );
    EXPECT_EQ( stats.erased, 0u );
    EXPECT_EQ( stats.errors, 1u );
    EXPECT_EQ( stats.held_blocks, 0u );
    EXPECT_EQ( stats.skipped, 0u );

    // Never torn: the value is untouched AND no marker exists.
    EXPECT_TRUE( db_->Get( { tx_path } ).has_value() );
    EXPECT_NE( SnapshotMapEntry( tx_path ), "absent" );
    auto lookup = PruneEligibility::LookupPrunedEntry( *db_, eligible->GetHash() );
    ASSERT_TRUE( lookup.has_value() );
    EXPECT_EQ( lookup.value().first, PrunedLookup::NOT_FOUND );

    // The cycle still ran to completion (per-tx errors never block): the
    // watermark advanced despite the refusal.
    const auto status = pruner->GetPruneStatus();
    ASSERT_TRUE( status.watermark.has_value() );

    // Retire the headship: the next cycle erases cleanly — the refusal was
    // the live guard, not a permanent fault.
    PutSeedValue( "/seed/txpruner/hg-driver-2", "d2" );
    auto retry = RunCycle( *pruner );
    ASSERT_TRUE( retry.has_value() );
    EXPECT_EQ( retry.value().erased, 1u );
    EXPECT_EQ( retry.value().errors, 0u );
    EXPECT_TRUE( db_->Get( { tx_path } ).has_error() );
    auto pruned = PruneEligibility::LookupPrunedEntry( *db_, eligible->GetHash() );
    ASSERT_TRUE( pruned.has_value() );
    EXPECT_EQ( pruned.value().first, PrunedLookup::PRUNED );
}

// ---------------------------------------------------------------------------
// CrashFixedPoint: SC-2 — kill the cycle at ANY interrupt point of BOTH
// seams, reopen the database, converge to the SAME fixed point
// ---------------------------------------------------------------------------

namespace
{
    /// One kill point of the crash matrix: which seam, which step, and which
    /// visit of that step aborts (the GlobalDB seam fires three step visits
    /// per erase compose; the cycle seam's BeforeErase visit repeats per tx).
    struct CrashKillPoint
    {
        const char *name;
        /// true: the TxPruner cycle seam; false: GlobalDB's compose seam.
        bool use_cycle_seam;
        /// The seam's step constant (GlobalDB steps are literal 1/2/3).
        int step;
        /// 1-based visit of @p step that aborts.
        int invocation;
        /// Expected: did the killed run still complete its cycle (per-tx
        /// erase failures and AfterStatus interruptions do)?
        bool cycle_completes;
    };

    /**
     * The full matrix: every cycle-level interruption position and every
     * GlobalDB compose step. The GlobalDB cases kill the SECOND erase's
     * compose (the first erase's three step visits pass) — true partial
     * progress: one tx fully committed, the next tx's batch killed at the
     * requested step, the cycle itself continuing past the per-tx error
     * (the sanctioned log-and-continue scan contract).
     */
    const std::vector<CrashKillPoint> kCrashKillPoints = {
        { "CycleAfterEvaluation", true, sgns::TxPruner::kInterruptAfterEvaluation, 1, false },
        { "CycleBeforeFirstErase", true, sgns::TxPruner::kInterruptBeforeErase, 1, false },
        { "CycleBetweenErases", true, sgns::TxPruner::kInterruptBeforeErase, 2, false },
        { "CycleBeforeStatus", true, sgns::TxPruner::kInterruptBeforeStatus, 1, false },
        { "CycleAfterStatus", true, sgns::TxPruner::kInterruptAfterStatus, 1, true },
        { "ComposeStep1MidProgress", false, 1, 2, true },
        { "ComposeStep2MidProgress", false, 2, 2, true },
        { "ComposeStep3MidProgress", false, 3, 2, true },
    };

    std::string CrashKillName( const ::testing::TestParamInfo<CrashKillPoint> &info )
    {
        return info.param.name;
    }
} // namespace

/**
 * SC-2, the crash-consistency matrix. For every interrupt point of BOTH
 * seams: seed a mixed fixture (two one-tx blocks and one shared two-tx
 * block), kill the first cycle at that point, REOPEN the whole stack on the
 * same database path, and assert (a) the immediate fixed point — every
 * transaction is in the COMPLETE pre-erase state (value + map entry, no
 * marker) or the COMPLETE post-erase state (all gone, marker PRUNED), never
 * a torn mix: never value-without-map-entry, never an orphan map entry,
 * never marker-plus-value; a same-block sibling MAY legally strand (value
 * present, its shared block already gone) — the BLOCK_ALREADY_ERASED
 * evidence completes it; (b) cycles re-run on the reopened node converge to
 * exactly the uninterrupted run's end state (all eligibles PRUNED, controls
 * untouched, heads/seals/UTXO byte-identical); and (c) one more cycle on the
 * fully-drained node is a no-op.
 *
 * TRUE-PROCESS-KILL NOTE (research open-question 2 default, plan-sanctioned
 * stretch): the seam-simulated matrix above IS the SC-2 gate. A fork-based
 * true-kill case (child opens the fixture DB, _exit(3)s mid-compose inside
 * the seam, parent reopens) is deliberately ABSENT: the fixture's pubsub
 * host and asio pool carry live threads whose locks do not survive fork()
 * (research assumption A3 — the child would deadlock on inherited mutexes
 * before reaching the compose), and GlobalDB has no pubsub-less
 * construction path. Documented here so the absence is never silent.
 */
class TxPrunerCrashFixedPointTest : public TxPrunerTest, public ::testing::WithParamInterface<CrashKillPoint>
{
};

TEST_P( TxPrunerCrashFixedPointTest, KilledCycleReopensToFixedPointAndConverges )
{
    const CrashKillPoint kill = GetParam();

    //
    // Seed the mixed fixture: an ancient spender, TWO eligible one-tx-block
    // transactions, TWO eligibles sharing ONE multi-tx block, and an unspent
    // control sibling — every erase-order permutation the cycle can take.
    //
    auto spender    = MakeTransfer( 1, 0 );
    auto eligible_a = MakeTransfer( 2, 1 );
    auto eligible_b = MakeTransfer( 3, 1 );
    auto eligible_c = MakeTransfer( 4, 1 );
    auto unspent    = MakeTransfer( 5, 2 );

    SeedTransaction( spender );
    PutSeedValue( "/seed/txpruner/crash-driver-1", "d1" );
    SeedTransaction( eligible_a );
    TrackOutputs( *eligible_a );
    ASSERT_TRUE( ConsumeOutput( *eligible_a, 0, *spender ) );
    SeedTransactionsInOneBlock( { eligible_b, eligible_c } );
    TrackOutputs( *eligible_b );
    TrackOutputs( *eligible_c );
    ASSERT_TRUE( ConsumeOutput( *eligible_b, 0, *spender ) );
    ASSERT_TRUE( ConsumeOutput( *eligible_c, 0, *spender ) );
    SeedTransaction( unspent );
    TrackOutputs( *unspent ); // deliberately never consumed
    PutSeedValue( "/seed/txpruner/crash-driver-2", "d2" );

    const std::string path_a = TransactionManager::GetTransactionPath( *eligible_a );
    const std::string path_b = TransactionManager::GetTransactionPath( *eligible_b );
    const std::string path_c = TransactionManager::GetTransactionPath( *eligible_c );

    // Block CIDs and the pre-kill byte-identity snapshots.
    const std::string cid_a      = SnapshotMapEntry( path_a );
    const std::string shared_cid = SnapshotMapEntry( path_b );
    const std::string cid_u      = SnapshotMapEntry( TransactionManager::GetTransactionPath( *unspent ) );
    const std::string cid_s      = SnapshotMapEntry( TransactionManager::GetTransactionPath( *spender ) );
    ASSERT_NE( cid_a, "absent" );
    ASSERT_EQ( shared_cid, SnapshotMapEntry( path_c ) ); // ONE shared delta
    ASSERT_EQ( SnapshotPrefix( "/prune/tx/" ).size(), 0u );

    const RawSnapshot  utxo_before   = SnapshotPrefix( "/utxo/" );
    uint64_t           max_height    = 0;
    const HeadSnapshot heads_before  = SnapshotHeads( max_height );
    const RawSnapshot  seals_before  = SnapshotSeals( { cid_a, shared_cid, cid_u, cid_s } );
    const RawSnapshot  blocks_before = SnapshotBlocks( { cid_u, cid_s } );
    const std::string  unspent_value = BytesOf(
        db_->Get( { TransactionManager::GetTransactionPath( *unspent ) } ).value() );
    const std::string spender_value = BytesOf(
        db_->Get( { TransactionManager::GetTransactionPath( *spender ) } ).value() );

    //
    // The kill: arm the requested seam, run cycles until one runs, disarm.
    // Every captured local lives at FUNCTION scope — the hooks fire inside
    // RunUntilCycleRuns below, after this arming block's branches close, so
    // block-scope captures would dangle.
    //
    std::atomic<bool> killed{ false };
    const int         step          = kill.step;
    const int         invocation    = kill.invocation;
    const int         fire_at_visit = 3 + kill.step; // GlobalDB seam only
    int               visits        = 0;
    if ( kill.use_cycle_seam )
    {
        sgns::TxPruner::SetCycleInterruptHookForTest(
            [&]( int visited )
            {
                if ( visited != step )
                {
                    return false;
                }
                if ( ++visits < invocation )
                {
                    return false; // let earlier visits pass (partial progress)
                }
                killed.store( true );
                return true;
            } );
    }
    else
    {
        // GlobalDB's seam visits steps 1/2/3 per erase compose: visits 1-3
        // are the FIRST erase (allowed through — true partial progress),
        // visit 3+step is the SECOND erase's @p step (the kill).
        crdt::GlobalDB::SetEraseInterruptHookForTest(
            [&]( int visited_step )
            {
                ++visits;
                if ( visits == fire_at_visit && visited_step == step )
                {
                    killed.store( true );
                    return true;
                }
                return false;
            } );
    }

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );
    const PruneCycleStats kill_stats = RunUntilCycleRuns( *pruner );

    sgns::TxPruner::SetCycleInterruptHookForTest( nullptr );
    crdt::GlobalDB::SetEraseInterruptHookForTest( nullptr );

    // The kill really landed at the requested point (never vacuous).
    ASSERT_TRUE( killed.load() ) << "the interrupt point was never reached: " << kill.name;
    // The status contract held across the kill: a completed run recorded the
    // watermark, an interrupted run left the status untouched.
    EXPECT_EQ( pruner->GetPruneStatus().watermark.has_value(), kill.cycle_completes );

    //
    // The REOPEN: tear the stack down and rebuild it on the same DB path —
    // every assertion below reads what actually survived on disk.
    //
    ASSERT_NO_FATAL_FAILURE( ReopenStackOnSamePath() );

    // (a) The immediate fixed point on the REOPENED node: for every
    // eligible tx either the complete pre-erase state or the complete
    // post-erase state — never a torn mix.
    size_t erased_count = 0;
    for ( const auto *tx : { eligible_a.get(), eligible_b.get(), eligible_c.get() } )
    {
        const std::string tx_path       = TransactionManager::GetTransactionPath( *tx );
        const bool        value_present = db_->Get( { tx_path } ).has_value();
        const bool        map_present   = SnapshotMapEntry( tx_path ) != "absent";
        auto              lookup        = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
        ASSERT_TRUE( lookup.has_value() );

        // Value and map entry live and die together (one batch).
        EXPECT_EQ( value_present, map_present ) << kill.name << ": torn value/map mix at " << tx_path;
        // Never marker-plus-value (the resurrection shape is a loudly
        // reported invariant violation, not a legal crash state).
        EXPECT_NE( lookup.value().first, PrunedLookup::VALUE_PRESENT_INVARIANT_VIOLATION )
            << kill.name << ": marker AND value both survived at " << tx_path;
        if ( value_present )
        {
            EXPECT_EQ( lookup.value().first, PrunedLookup::NOT_FOUND ) << kill.name << ": marker without erase";
        }
        else
        {
            EXPECT_EQ( lookup.value().first, PrunedLookup::PRUNED ) << kill.name << ": erase without marker";
            ++erased_count;
        }
    }
    // The one-tx-block tx: its block lives and dies with its own value.
    EXPECT_EQ( SnapshotBlocks( { cid_a } ).at( cid_a ) == "absent", db_->Get( { path_a } ).has_error() )
        << kill.name << ": own-block bytes/value diverged";
    // The shared block: a crash BETWEEN two same-block erases may legally
    // leave the surviving sibling's value present with the block already
    // gone (the BLOCK_ALREADY_ERASED-completable state); but BOTH siblings
    // erased MUST mean the block is gone.
    if ( db_->Get( { path_b } ).has_error() && db_->Get( { path_c } ).has_error() )
    {
        EXPECT_EQ( SnapshotBlocks( { shared_cid } ).at( shared_cid ), "absent" )
            << kill.name << ": both shared siblings erased but the block survived";
    }
    // The controls are untouched by any crash position.
    EXPECT_EQ( BytesOf( db_->Get( { TransactionManager::GetTransactionPath( *unspent ) } ).value() ), unspent_value );
    EXPECT_EQ( BytesOf( db_->Get( { TransactionManager::GetTransactionPath( *spender ) } ).value() ), spender_value );
    EXPECT_EQ( SnapshotBlocks( { cid_u, cid_s } ), blocks_before );
    EXPECT_EQ( SnapshotPrefix( "/utxo/" ), utxo_before );
    EXPECT_EQ( SnapshotPrefix( "/prune/tx/" ).size(), erased_count );

    //
    // (b) Convergence: fresh pruner on the reopened node, cycles until the
    // drain — the end state equals the UNINTERRUPTED run's end state
    // (18-06's RealPrune pinned that state: every eligible fully erased +
    // marked, controls byte-identical, heads/seals/UTXO untouched).
    //
    auto converger = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( converger, nullptr );
    PruneCycleStats conv_stats;
    RunSettledCycle( *converger, conv_stats );
    for ( int i = 0; i < 3 && conv_stats.erased > 0; ++i )
    {
        auto more = RunCycle( *converger );
        ASSERT_TRUE( more.has_value() );
        conv_stats = more.value();
    }

    for ( const auto *tx : { eligible_a.get(), eligible_b.get(), eligible_c.get() } )
    {
        const std::string tx_path = TransactionManager::GetTransactionPath( *tx );
        EXPECT_TRUE( db_->Get( { tx_path } ).has_error() ) << kill.name << ": value survived convergence";
        EXPECT_TRUE( SnapshotCrdtEntriesContaining( tx_path ).empty() ) << kill.name << ": CRDT footprint survived";
        EXPECT_EQ( SnapshotMapEntry( tx_path ), "absent" ) << kill.name << ": map entry survived convergence";
        auto lookup = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        EXPECT_EQ( lookup.value().first, PrunedLookup::PRUNED );
    }
    const RawSnapshot converged_blocks = SnapshotBlocks( { cid_a, shared_cid, cid_u, cid_s } );
    EXPECT_EQ( converged_blocks.at( cid_a ), "absent" );
    EXPECT_EQ( converged_blocks.at( shared_cid ), "absent" ); // gone ONCE
    EXPECT_EQ( converged_blocks.at( cid_u ), blocks_before.at( cid_u ) );
    EXPECT_EQ( converged_blocks.at( cid_s ), blocks_before.at( cid_s ) );
    EXPECT_EQ( SnapshotPrefix( "/prune/tx/" ).size(), 3u ); // exactly one marker per eligible
    EXPECT_EQ( BytesOf( db_->Get( { TransactionManager::GetTransactionPath( *unspent ) } ).value() ), unspent_value );
    EXPECT_EQ( SnapshotMapEntry( TransactionManager::GetTransactionPath( *unspent ) ), cid_u );
    EXPECT_EQ( BytesOf( db_->Get( { TransactionManager::GetTransactionPath( *spender ) } ).value() ), spender_value );
    EXPECT_EQ( SnapshotPrefix( "/utxo/" ), utxo_before );
    uint64_t           max_height_after = 0;
    const HeadSnapshot heads_after      = SnapshotHeads( max_height_after );
    EXPECT_EQ( heads_after, heads_before );
    EXPECT_EQ( max_height_after, max_height );
    EXPECT_EQ( SnapshotSeals( { cid_a, shared_cid, cid_u, cid_s } ), seals_before )
        << "a seal marker changed (GATE-03)";

    //
    // (c) The clean-node no-op: one more cycle on the drained reopened node
    // examines the survivors and erases nothing.
    //
    const RawSnapshot markers_converged = SnapshotPrefix( "/prune/tx/" );
    auto              noop              = RunCycle( *converger );
    ASSERT_TRUE( noop.has_value() );
    EXPECT_GT( noop.value().examined, 0u ); // spender + unspent sibling remain
    EXPECT_EQ( noop.value().eligible, 0u );
    EXPECT_EQ( noop.value().erased, 0u );
    EXPECT_EQ( noop.value().held_blocks, 0u );
    EXPECT_EQ( noop.value().errors, 0u );
    EXPECT_EQ( SnapshotPrefix( "/prune/tx/" ), markers_converged );
    EXPECT_EQ( BytesOf( db_->Get( { TransactionManager::GetTransactionPath( *unspent ) } ).value() ), unspent_value );
}

INSTANTIATE_TEST_SUITE_P( CrashFixedPointMatrix,
                          TxPrunerCrashFixedPointTest,
                          ::testing::ValuesIn( kCrashKillPoints ),
                          CrashKillName );
