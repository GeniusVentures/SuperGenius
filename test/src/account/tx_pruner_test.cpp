/**
 * @file       tx_pruner_test.cpp
 * @brief      SC-1 (first real erase), cycle idempotence, the sweep-as-cycle
 *             re-prune (D-02b), the stranding-edge budget behavior, the SC-4
 *             tick-gate refusals, and the RECL-03/RECL-04 observability +
 *             status contracts — including the D-01 compose-lock
 *             non-interleaving proof, all against the TxPruner SERVICE on
 *             real stores (GlobalDB + pubsub + temp dir, no GeniusNode).
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
