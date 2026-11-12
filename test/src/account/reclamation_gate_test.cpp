/**
 * @file       reclamation_gate_test.cpp
 * @brief      RECL-02's dedicated measured-bytes CI gate (Phase 20, D-03):
 *             deletes are tombstones until compaction, and this target fails
 *             the suite unless erased bytes measurably LEAVE the disk across
 *             ONE production-surface compaction — asserted in bytes (never
 *             key counts), with bounded-range assertions immune to RocksDB
 *             writer/versioning variance.
 * @details    The fixture is the tx_pruner_test family's stack (GlobalDB +
 *             pubsub + temp dir, GeniusAccount + Blockchain +
 *             TransactionManager::Light + TxPruner, no GeniusNode); this file
 *             owns its own copy of the family helpers and includes no sibling
 *             test. Seeding always goes through the REAL publish path on the
 *             fixture's CRDT topic (never raw local puts — the Phase-16
 *             lesson), and values are sized through destination-address
 *             padding (the 20-02 canary recipe: one low-entropy output per
 *             transaction, repeated bytes for stable compression — research
 *             A2). The ONLY compaction/size path exercised is the production
 *             surface GlobalDB::CompactStorage / GetApproximateDiskUsageBytes
 *             — never a test-side raw CompactRange, which would prove a code
 *             path production never runs (Pitfall 7). Anti-vacuous by
 *             construction: the prune is proven first (erased counts + PRUNED
 *             markers), the auto-trigger is proven silent (bytes_reclaimed
 *             nullopt under the default 16MB threshold — the ONE compaction
 *             is the test's direct call), and the before-measurement is
 *             pinned against memtable/empty-range blindness (>= 80% of the
 *             erased logical bytes — Pitfalls 1/2 fail loudly here). A du
 *             belt over the fixture db_path_ (WAL/OPTIONS noise absorbed)
 *             sanity-checks the directory level but is never the >= 60%
 *             carrier (Pitfall 6).
 * @date       2026-10-24
 */

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <boost/filesystem.hpp>

#include "account/GeniusAccount.hpp"
#include "account/TransferTransaction.hpp"
#include "account/UTXOManager.hpp"
#include "account/proto/SGTransaction.pb.h"
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
     * deterministically. The pending-lifecycle binary owns the same class
     * name in its own binary; this per-binary definition carries only the
     * member this binary uses (ChangeState — the tick gate).
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

    /// The D-03 sizing: every seeded value carries this many low-entropy pad
    /// bytes in its destination address (~10KB payloads — repeated bytes for
    /// stable compression, research A2; the 20-02 canary's sizing recipe).
    constexpr size_t kGatePadBytes = 10u * 1024u;

    /// Binary-safe std::string copy of a buffer (Buffer::toString is a view).
    std::string BytesOf( const base::Buffer &buffer )
    {
        return std::string( buffer.toString() );
    }

    /// du belt (Pitfall 6): the sum of regular-file sizes under the fixture's
    /// database directory — WAL/OPTIONS/MANIFEST noise included, so it is a
    /// loose sanity bound only, never the >= 60% carrier.
    uint64_t SumRegularFileBytes( const std::string &directory )
    {
        uint64_t                       total = 0;
        const boost::filesystem::path  root( directory );
        boost::system::error_code      ec;
        for ( const auto &entry : boost::filesystem::recursive_directory_iterator( root, ec ) )
        {
            if ( ec )
            {
                break;
            }
            if ( boost::filesystem::is_regular_file( entry ) )
            {
                total += static_cast<uint64_t>( boost::filesystem::file_size( entry ) );
            }
        }
        return total;
    }
} // namespace

/**
 * @brief Real-store fixture for the reclamation gate: GlobalDB + Blockchain +
 *        TransactionManager (NodeType::Light, no GeniusNode, no network sync)
 *        — the tx_pruner_test stack with only the members these suites drive.
 */
class ReclamationGateTest : public ::test::CRDTFixture
{
public:
    ReclamationGateTest() : ::test::CRDTFixture( "reclamation_gate_test" )
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

    ~ReclamationGateTest() override
    {
        // Seam discipline (the family destructor order): no case here arms a
        // hook, but the family clears BOTH interrupt seams unconditionally so
        // a future armed case can never leak past teardown.
        sgns::TxPruner::SetCycleInterruptHookForTest( nullptr );
        crdt::GlobalDB::SetEraseInterruptHookForTest( nullptr );

        /*
         * Teardown invariant (asio), copied verbatim from
         * PruneEligibilityTest/TxPrunerTest: stop the blockchain FIRST so the
         * ConsensusManager round-timer thread joins before any db-backed
         * handle is released; the base ~CRDTFixture stays the FINAL host
         * release (Linux CI segfault otherwise).
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
    // Seeding helpers — local copies of the tx_pruner_test recipes (this file
    // includes no sibling test). Every transaction goes through the REAL
    // topic Put so its CRDT value, delta CID, head, and per-key priority all
    // exist, then gets a raw tx->CID map entry in the exact
    // StoreTransactionCID shapes.
    //

    /// Transfer with @p output_count outputs owned by this account (empty for spenders).
    /// @p dest_pad_bytes appends that many low-entropy bytes to every
    /// destination address — sizes the serialized VALUE (~10KB gate payloads)
    /// without extra outputs (the 20-02 canary's destination-address padding).
    std::shared_ptr<TransferTransaction> MakeTransfer( uint64_t nonce, size_t output_count, size_t dest_pad_bytes = 0 )
    {
        SGTransaction::DAGStruct dag;
        dag.set_type( "transfer" );
        dag.set_source_addr( account_->GetAddress() );
        dag.set_nonce( nonce );
        dag.set_timestamp( static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::system_clock::now().time_since_epoch() )
                .count() ) );

        const std::string destination = dest_pad_bytes > 0 ? account_->GetAddress() + std::string( dest_pad_bytes, 'x' )
                                                           : account_->GetAddress();
        std::vector<OutputDestInfo> destinations;
        destinations.reserve( output_count );
        for ( size_t i = 0; i < output_count; ++i )
        {
            destinations.push_back( { 100 + i, destination, kTestTokenId } );
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

    /// Plain topic Put of a raw string at @p key (head-advancing driver puts).
    void PutSeedValue( const std::string &key, const std::string &value )
    {
        crdt::GlobalDB::Buffer buffer;
        buffer.put( value );
        ASSERT_TRUE( db_->Put( { key }, buffer, { kSeedTopic } ).has_value() );
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
    /// @p owner_address overrides the consuming owner (a padded destination
    /// — ConsumeUTXOs matches the STORED owner, so a padded output must be
    /// consumed under its own address).
    bool ConsumeOutput( const GeniusTransaction &transaction,
                        uint32_t                 output_idx,
                        const GeniusTransaction &spender,
                        const std::string       &owner_address = "" )
    {
        auto txid = base::Hash256::fromReadableString( transaction.GetHash() );
        EXPECT_FALSE( txid.has_error() );
        auto consumed = account_->GetUTXOManager().ConsumeUTXOs(
            { { txid.value(), output_idx, {} } },
            owner_address.empty() ? account_->GetAddress() : owner_address,
            UTXOManager::UTXOType::UTXO_NORMAL,
            SpenderHash( spender ) );
        EXPECT_TRUE( consumed.has_value() );
        return consumed.has_value() && consumed.value();
    }

    static base::Hash256 SpenderHash( const GeniusTransaction &transaction )
    {
        return base::Hash256::fromReadableString( transaction.GetHash() ).value();
    }

    //
    // Pruner helpers.
    //

    /// Constructs a Light TxPruner with the suite's window and dry_run=false.
    /// The compact threshold KEEPS ITS DEFAULT (16MB): the gate fixture's
    /// ~2MB of erasures must never cross it, so the post-cycle auto-trigger
    /// stays silent and the test's direct CompactStorage call is the ONE
    /// compaction the measurement spans.
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
};

// ---------------------------------------------------------------------------
// Smoke: the mechanics at small scale (seed -> prune -> compact -> drop)
// ---------------------------------------------------------------------------

/**
 * The small-scale mechanics proof: five spent-beyond-window ~10KB values
 * erase through ONE settled cycle (markers PRUNED — the prune really
 * happened), the default-threshold auto-trigger stays silent (no
 * bytes_reclaimed), the production size surface reads non-zero BEFORE the
 * compaction (Pitfall 2's empty-range loudness), ONE CompactStorage drops
 * the measured size, and the unspent sibling is byte-identical across the
 * whole prune+compaction (D-C5 in miniature).
 */
TEST_F( ReclamationGateTest, Smoke_SeedPruneCompactMeasuresDropAndLeavesSiblingIdentical )
{
    constexpr uint64_t kSmokeTxCount = 5;

    // ConsumeUTXOs matches the STORED owner, so the padded outputs are
    // consumed under their own (padded) address.
    const std::string padded_owner = account_->GetAddress() + std::string( kGatePadBytes, 'x' );

    auto spender = MakeTransfer( 1, 0 );
    std::vector<std::shared_ptr<TransferTransaction>> spent;
    spent.reserve( kSmokeTxCount );
    for ( uint64_t i = 0; i < kSmokeTxCount; ++i )
    {
        spent.push_back( MakeTransfer( 2 + i, 1, kGatePadBytes ) );
    }
    auto unspent = MakeTransfer( 2 + kSmokeTxCount, 1, kGatePadBytes ); // the untouched sibling

    // Ancient spender first, then a driver, so its arrival height sits far
    // below the final head; the trailing driver advances the head past the
    // last seeded delta (GATE-03 refuses head erases) and past the window.
    SeedTransaction( spender );
    PutSeedValue( "/seed/reclamation/smoke-driver-1", "d1" );
    for ( const auto &tx : spent )
    {
        SeedTransaction( tx );
    }
    SeedTransaction( unspent );
    PutSeedValue( "/seed/reclamation/smoke-driver-2", "d2" );

    for ( const auto &tx : spent )
    {
        TrackOutputs( *tx );
        ASSERT_TRUE( ConsumeOutput( *tx, 0, *spender, padded_owner ) );
    }
    TrackOutputs( *unspent ); // deliberately never consumed

    const std::string sibling_path         = TransactionManager::GetTransactionPath( *unspent );
    const std::string sibling_value_before = BytesOf( db_->Get( { sibling_path } ).value() );
    ASSERT_FALSE( sibling_value_before.empty() );

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );

    PruneCycleStats stats;
    RunSettledCycle( *pruner, stats );
    ASSERT_EQ( stats.erased, kSmokeTxCount );
    EXPECT_EQ( stats.errors, 0u );

    // The prune actually happened: every spent tx's value is gone and its
    // marker reads PRUNED (otherwise the measurement below is vacuous).
    for ( const auto &tx : spent )
    {
        const std::string tx_path = TransactionManager::GetTransactionPath( *tx );
        EXPECT_TRUE( db_->Get( { tx_path } ).has_error() );
        auto lookup = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        ASSERT_EQ( lookup.value().first, PrunedLookup::PRUNED );
    }

    // Auto-trigger silence: ~50KB of erasures never crossed the default 16MB
    // threshold, so the ONE compaction below is the test's direct call.
    EXPECT_FALSE( pruner->GetPruneStatus().bytes_reclaimed.has_value() );

    auto before = db_->GetApproximateDiskUsageBytes();
    ASSERT_TRUE( before.has_value() );
    EXPECT_GT( before.value(), 0u ) << "empty-range blindness reads 0 (Pitfall 2)";

    ASSERT_TRUE( db_->CompactStorage( nullptr ).has_value() );

    auto after = db_->GetApproximateDiskUsageBytes();
    ASSERT_TRUE( after.has_value() );
    const uint64_t reclaimed = before.value() > after.value() ? before.value() - after.value() : 0;
    EXPECT_GT( reclaimed, 0u ) << "before=" << before.value() << " after=" << after.value();

    // Negative control: the unspent sibling is byte-identical across the
    // prune AND the compaction rewrite, and never gained a marker.
    const auto sibling_after = db_->Get( { sibling_path } );
    ASSERT_TRUE( sibling_after.has_value() );
    EXPECT_EQ( BytesOf( sibling_after.value() ), sibling_value_before );
    auto sibling_lookup = PruneEligibility::LookupPrunedEntry( *db_, unspent->GetHash() );
    ASSERT_TRUE( sibling_lookup.has_value() );
    EXPECT_EQ( sibling_lookup.value().first, PrunedLookup::NOT_FOUND );
}

// ---------------------------------------------------------------------------
// The D-03 gate: 200 x ~10KB -> prune -> ONE compaction -> bounded assertion
// ---------------------------------------------------------------------------

/**
 * The milestone's headline claim, machine-checked in measured bytes: 200
 * spent-beyond-window transactions of known ~10KB size erase through ONE
 * settled cycle (every marker PRUNED — the prune provably preceded the
 * measurement), the default-threshold auto-trigger provably stayed silent
 * (bytes_reclaimed nullopt — the ONE compaction is the direct call below),
 * and across that single production-surface compaction the measured byte
 * drop is at least 60% of the erased LOGICAL bytes (payload-only denominator
 * — the conservative bound; block/protobuf overhead only adds reclaimable
 * bytes), under a checker-calibrated sanity ceiling that catches
 * double-counting without brittleness against writer/versioning variance
 * (exact-delta rejected per D-03/Pitfall 8), with the du belt over the
 * fixture db_path_ as the loose directory-level sanity (never the carrier).
 */
TEST_F( ReclamationGateTest, MeasuredBytesGate_ReclaimsAtLeastSixtyPercentOfErasedLogicalBytes )
{
    constexpr uint64_t kGateTxCount = 200;
    constexpr uint64_t kFloorPct    = 60;

    // ConsumeUTXOs matches the STORED owner, so the padded outputs are
    // consumed under their own (padded) address.
    const std::string padded_owner = account_->GetAddress() + std::string( kGatePadBytes, 'x' );

    auto                                              spender = MakeTransfer( 1, 0 );
    std::vector<std::shared_ptr<TransferTransaction>> spent;
    spent.reserve( kGateTxCount );
    uint64_t erased_logical_bytes = 0;
    for ( uint64_t i = 0; i < kGateTxCount; ++i )
    {
        auto tx = MakeTransfer( 2 + i, 1, kGatePadBytes );
        // The exact payload bytes the value Put stores — the known-size
        // denominator every bound below is expressed against.
        erased_logical_bytes += tx->SerializeByteVector().size();
        spent.push_back( std::move( tx ) );
    }
    ASSERT_GT( erased_logical_bytes, 0u );

    // Ancient spender first (the spend-height window anchor), then a driver;
    // each spent tx seeds in its OWN block; the trailing driver advances the
    // head past the last delta (GATE-03 refuses head erases).
    SeedTransaction( spender );
    PutSeedValue( "/seed/reclamation/gate-driver-1", "d1" );
    for ( const auto &tx : spent )
    {
        SeedTransaction( tx );
    }
    PutSeedValue( "/seed/reclamation/gate-driver-2", "d2" );

    for ( const auto &tx : spent )
    {
        TrackOutputs( *tx );
        ASSERT_TRUE( ConsumeOutput( *tx, 0, *spender, padded_owner ) );
    }

    // K budget >= 200 so ONE cycle erases them all; the compact threshold
    // keeps its 16MB DEFAULT — ~2MB of erasures must never cross it.
    auto pruner = MakePruner( kGateTxCount, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );

    PruneCycleStats stats;
    RunSettledCycle( *pruner, stats );
    ASSERT_EQ( stats.erased, kGateTxCount ) << "the prune must drain every seeded tx in one cycle";
    EXPECT_EQ( stats.errors, 0u );
    EXPECT_EQ( stats.skipped, 0u );

    // The prune actually happened: every value is gone and every marker is
    // PRUNED (otherwise the byte measurement below is vacuous — T-20-0301).
    for ( const auto &tx : spent )
    {
        const std::string tx_path = TransactionManager::GetTransactionPath( *tx );
        EXPECT_TRUE( db_->Get( { tx_path } ).has_error() );
        auto lookup = PruneEligibility::LookupPrunedEntry( *db_, tx->GetHash() );
        ASSERT_TRUE( lookup.has_value() );
        ASSERT_EQ( lookup.value().first, PrunedLookup::PRUNED );
    }

    // Auto-trigger silence (T-20-0303): the default 16MB threshold vs this
    // ~2MB fixture means the post-cycle gate never fired — the ONE
    // compaction the delta spans is the direct call that follows.
    EXPECT_FALSE( pruner->GetPruneStatus().bytes_reclaimed.has_value() )
        << "the default-threshold auto-trigger must stay silent on a ~2MB fixture";

    // BEFORE: whole-DB size after the prune, before the compaction. The
    // anti-vacuous pin makes a memtable-blind or empty-range-blind snapshot
    // FAIL LOUDLY here instead of passing silently (Pitfalls 1/2).
    auto before_result = db_->GetApproximateDiskUsageBytes();
    ASSERT_TRUE( before_result.has_value() );
    const uint64_t before = before_result.value();
    ASSERT_GE( before, ( erased_logical_bytes * 8 ) / 10 )
        << "memtable/empty-range-blind measurement: before=" << before
        << " erased_logical_bytes=" << erased_logical_bytes;

    // du baseline (belt only — WAL/OPTIONS/MANIFEST noise included).
    const uint64_t du_before = SumRegularFileBytes( db_path_ );

    // The ONE compaction — the production surface only (Pitfall 7).
    ASSERT_TRUE( db_->CompactStorage( nullptr ).has_value() );

    auto after_result = db_->GetApproximateDiskUsageBytes();
    ASSERT_TRUE( after_result.has_value() );
    const uint64_t after     = after_result.value();
    const uint64_t reclaimed = before > after ? before - after : 0;

    // The observed calibration line (research A1/A2): expected comfortably
    // above the floor with default compression on low-entropy payloads.
    std::cout << "[reclamation-gate] erased_logical_bytes=" << erased_logical_bytes << " before=" << before
              << " after=" << after << " reclaimed=" << reclaimed
              << " fraction_of_erased_logical=" << ( reclaimed * 100 / erased_logical_bytes ) << "%" << std::endl;

    // The D-03 floor: at least 60% of the erased logical bytes measurably
    // left the disk across the one compaction.
    EXPECT_GE( reclaimed, ( erased_logical_bytes * kFloorPct ) / 100 )
        << "before=" << before << " after=" << after << " erased_logical_bytes=" << erased_logical_bytes
        << " reclaimed=" << reclaimed;

    // Sanity ceiling (measured-calibrated, Pitfall 8): catches double-counted
    // bytes without brittleness against compression/writer variance. The
    // CRDT stack multiplies every payload byte well beyond the plan's ~2x
    // guess: each value is stored under FOUR CRDT key families plus its
    // delta block plus the UTXO output/consume records carrying the padded
    // owner address, with memtable arena overhead on top — measured
    // before=32.1MB for erased_logical=2.13MB (~15x; reclaimed fraction
    // 1492%). 20x keeps ~34% headroom over the measured envelope while
    // still bounding the delta against a range/flag-mismatched measurement
    // (before is structurally capped near the 32MB write buffer for this
    // fixture, so a reclaimed above ~20x erased + 2MB can only be a
    // measurement bug, never legitimate storage).
    EXPECT_LE( reclaimed, erased_logical_bytes * 20 + ( 2u << 20 ) ) << "before=" << before << " after=" << after;

    // The du belt: the directory drops or stays flat across the compaction
    // (loose bound — WAL/OPTIONS churn absorbed; never the >= 60% carrier).
    const uint64_t du_after = SumRegularFileBytes( db_path_ );
    EXPECT_LE( du_after, du_before + ( 1u << 20 ) ) << "du_before=" << du_before << " du_after=" << du_after;
}
