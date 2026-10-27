/**
 * @file       cold_storage_durability_test.cpp
 * @brief      Hermetic Tier-1 flush-gated durability advancement tests
 *             (DUR-01 SC1/SC2, v1.1 Phase 10 plan 10-01).
 *
 * Registered ONLY under SGNS_COLD_STORAGE (links
 * sgns-blockstore::sgns-blockstore). The manager is constructed DIRECTLY
 * (D-10) over a real LocalBlockStore journal on a fresh temp dir, the
 * InMemoryS3Client fake behind an S3BlockStore, and the
 * InMemoryS3AdminClient fake admin (both fake headers are NOT installed -
 * reached via the relative include-dir workaround, 08-RESEARCH Pitfall 6).
 *
 * Every advancement assertion keys off the SC1 evidence chain: a mark of
 * UploadedDurable is legal ONLY after POST /objects/flush succeeded AND a
 * subsequent /stats/uploads observation reported pendingObjects == 0.
 * The fake admin's flush_count()/SetStats()/SetAdminFault() surface is
 * exactly the scripting probe for that chain.
 *
 * Include discipline: every library result is guarded with has_value()
 * BEFORE .value()/.error() (policy::terminate alias). CID landmine:
 * sgns::CID has NO implicit ctor from the blockstore Cid - wrap
 * explicitly where a sgns::CID would be needed; assertions here compare
 * library Cids directly.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <libp2p/multi/content_identifier_codec.hpp>

#include <sgns-blockstore/Cid.hpp>
#include <sgns-blockstore/LocalBlockStore.hpp>
#include <sgns-blockstore/S3AdminClient.hpp>
#include <sgns-blockstore/S3BlockStore.hpp>

// Internal fakes - relative-include-dir privilege (test-surface only).
#include "InMemoryS3AdminClient.hpp"
#include "InMemoryS3Client.hpp"

#include "crdt/globaldb/ColdStorageManager.hpp"

namespace
{
    using sgns::blockstore::Cid;
    using sgns::blockstore::CidOf;
    using sgns::blockstore::InMemoryS3AdminClient;
    using sgns::blockstore::InMemoryS3Client;
    using sgns::blockstore::LocalBlockStore;
    using sgns::blockstore::LocalBlockStoreOptions;
    using sgns::blockstore::S3AdminClient;
    using sgns::blockstore::S3AdminFaultKind;
    using sgns::blockstore::S3BlockStore;
    using sgns::blockstore::S3FaultKind;
    using sgns::blockstore::S3Op;
    using sgns::blockstore::UploadState;
    using sgns::blockstore::UploadStats;
    using libp2p::multi::MulticodecType;

    using sgns::crdt::ColdStorageManager;

    /// Bounded poll loop with deadline - never a fixed sleep, so the suite
    /// stays stable on loaded Windows CI-equivalent machines.
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

    /// Short-interval drainer options (the spill-suite FastOptions shape).
    ColdStorageManager::BackpressureOptions FastOptions( uint32_t drain_interval_ms = 100,
                                                         uint32_t warn_pending      = 10,
                                                         uint32_t critical_pending  = 20 )
    {
        ColdStorageManager::BackpressureOptions options;
        options.drain_interval_ms = drain_interval_ms;
        options.warn_pending      = warn_pending;
        options.critical_pending  = critical_pending;
        options.backoff_cap_ms    = 1000;
        return options;
    }

    /// Fast flush cadence for the behavior suite (ms-scale so cadence
    /// tests run in milliseconds; 0 would take the 30000/300000 defaults).
    ColdStorageManager::DurabilityOptions FastDurability( uint32_t flush_min_interval_ms = 50,
                                                          uint32_t flush_max_interval_ms = 300 )
    {
        ColdStorageManager::DurabilityOptions durability;
        durability.flush_min_interval_ms = flush_min_interval_ms;
        durability.flush_max_interval_ms = flush_max_interval_ms;
        return durability;
    }

    gsl::span<const uint8_t> SpanOf( const std::string &s )
    {
        return gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( s.data() ), s.size() );
    }

    /// Verified CID for a payload (library constructor, RAW codec).
    Cid MakeCid( const std::string &payload )
    {
        auto cid = CidOf( SpanOf( payload ), MulticodecType::Code::RAW );
        return cid.value(); // guarded by construction - payload is valid
    }

    // SHORT relative path under the build tree (Windows MAX_PATH; the
    // upload_drainer_test FreshDir/NextUniqueDir discipline).
    std::string NextUniqueDir()
    {
        static std::atomic<uint64_t> next{ 0 };
        return "test-data/cold-storage-durability/case-" + std::to_string( next.fetch_add( 1 ) );
    }

    // A truly FRESH store per case: the monotonic counter resets every
    // process run, so a leftover dir from a previous run would reopen its
    // stale journal - wipe before Open.
    std::string FreshDir()
    {
        auto dir = NextUniqueDir();
        std::error_code ec;
        std::filesystem::remove_all( dir, ec );
        return dir;
    }

    /// The BACK-07 object key for a RAW fixture CID (S3BlockStore's
    /// private DeriveKey replicated test-side for per-key fault scripting:
    /// <keyPrefix>/<codec>/<digest-fanout-byte>/<cid-string>, default
    /// prefix "v1/blocks", RAW codec token "raw").
    std::string DeriveTestKey( const Cid &cid )
    {
        auto cid_string = libp2p::multi::ContentIdentifierCodec::toString( cid );
        static const char *kHex = "0123456789abcdef";
        const auto         digest = cid.content_address.getHash();
        const uint8_t      first  = digest.empty() ? 0 : digest[0];
        std::string        fanout{ kHex[first >> 4], kHex[first & 0x0F] };
        return std::string( "v1/blocks" ) + "/raw/" + fanout + "/" + cid_string.value();
    }

    class ColdStorageDurabilityTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            auto opened_local = LocalBlockStore::Open( LocalBlockStoreOptions{ FreshDir() } );
            ASSERT_TRUE( opened_local.has_value() );
            shared_local_ = std::shared_ptr<LocalBlockStore>( std::move( opened_local.value() ) );

            fake_ = std::make_shared<InMemoryS3Client>();
            auto opened_remote = S3BlockStore::Open( fake_ );
            ASSERT_TRUE( opened_remote.has_value() );
            remote_ = std::shared_ptr<S3BlockStore>( std::move( opened_remote.value() ) );

            fake_admin_ = std::make_shared<InMemoryS3AdminClient>();
            cold_store_mutex_ = std::make_shared<std::mutex>();
        }

        void TearDown() override
        {
            if ( manager_ )
            {
                manager_->Stop();
            }
        }

        /// Construct a manager directly over the fixture stores with the
        /// fake admin (D-10). The durability suite always carries a
        /// remote-store mutex fixture-locally (no cold-read chain here).
        std::shared_ptr<ColdStorageManager> MakeManager(
            const ColdStorageManager::BackpressureOptions &backpressure,
            const ColdStorageManager::DurabilityOptions   &durability )
        {
            auto remote_store_mutex = std::make_shared<std::mutex>();
            manager_                = std::make_shared<ColdStorageManager>(
                shared_local_,
                remote_,
                cold_store_mutex_,
                remote_store_mutex,
                backpressure,
                durability,
                fake_admin_ );
            return manager_;
        }

        /// Journal n blocks directly into the local store (Put-born
        /// PendingUpload records - the decorator is not needed for
        /// advancement-cycle tests).
        std::vector<Cid> JournalBlocks( size_t n )
        {
            std::vector<Cid> cids;
            cids.reserve( n );
            for ( size_t i = 0; i < n; ++i )
            {
                auto cid = MakeCid( "durability payload " + std::to_string( i ) );
                auto put = shared_local_->Put( cid, SpanOf( "durability payload " + std::to_string( i ) ) );
                EXPECT_TRUE( put.has_value() );
                cids.push_back( std::move( cid ) );
            }
            return cids;
        }

        /// True when every CID's journal record reads the requested state
        /// through the manager's D-09 pass-through.
        bool AllInState( const std::vector<Cid> &cids, UploadState state ) const
        {
            for ( const auto &cid : cids )
            {
                auto observed = manager_->GetUploadState( cid );
                if ( !observed.has_value() || observed.value() != state )
                {
                    return false;
                }
            }
            return true;
        }

        std::shared_ptr<LocalBlockStore>          shared_local_;
        std::shared_ptr<InMemoryS3Client>         fake_;
        std::shared_ptr<S3BlockStore>             remote_;
        std::shared_ptr<InMemoryS3AdminClient>    fake_admin_;
        std::shared_ptr<std::mutex>               cold_store_mutex_;
        std::shared_ptr<ColdStorageManager>       manager_;
    };
} // namespace

/**
 * @given a journal of 5 PendingUpload blocks and a healthy fake admin
 *        (stats report pendingObjects == 0)
 * @when  the drainer runs with a fast flush cadence
 * @then  flush fires at least once, observes the zero-pending evidence,
 *        and EXACTLY the drained Accepted snapshot advances to
 *        UploadedDurable - visible per-CID through the manager
 *        pass-through and in the GetStatus evidence fields (SC1 tracer).
 */
TEST_F( ColdStorageDurabilityTest, e2e_flush_gated_advancement )
{
    constexpr size_t kBlocks = 5;
    const auto       cids    = JournalBlocks( kBlocks );
    auto             manager = MakeManager( FastOptions(), FastDurability() );
    manager->Start();

    ASSERT_TRUE( PollUntil( [&] {
        return fake_admin_->flush_count() >= 1 && AllInState( cids, UploadState::UploadedDurable );
    },
                            std::chrono::milliseconds( 10000 ) ) )
        << "flush-gated advancement did not complete within deadline";

    const auto status = manager->GetStatus();
    EXPECT_EQ( status.durable_upload_count, kBlocks );
    EXPECT_EQ( status.accepted_upload_count, 0u );
    EXPECT_TRUE( status.last_flush_valid );
    EXPECT_TRUE( status.last_flush_ok );
    EXPECT_EQ( status.last_flush_pending_objects, 0u );
    EXPECT_GT( status.last_flush_unix_ms, 0u );

    manager->Stop();

    // Journal truth after Stop: every record durable, none accepted.
    for ( const auto &cid : cids )
    {
        auto state = shared_local_->GetUploadState( cid );
        ASSERT_TRUE( state.has_value() );
        EXPECT_EQ( state.value(), UploadState::UploadedDurable );
    }
}

/**
 * @given journaled blocks drained to UploadedAccepted and stats scripted
 *        with pendingObjects == 1 (evidence NOT met)
 * @when  flush cycles run
 * @then  flush is still called but ZERO records advance (they stay
 *        UploadedAccepted, valid-but-not-ok evidence recorded); after the
 *        stats report pendingObjects == 0 the next cycle advances them.
 */
TEST_F( ColdStorageDurabilityTest, evidence_gate_pending_objects_blocks_advancement )
{
    constexpr size_t kBlocks = 3;
    const auto       cids    = JournalBlocks( kBlocks );

    UploadStats pending_one;
    pending_one.pendingObjects = 1;
    fake_admin_->SetStats( pending_one );

    auto manager = MakeManager( FastOptions(), FastDurability() );
    manager->Start();

    // Flush happens (call probe), multiple cycles elapse, nothing advances.
    ASSERT_TRUE( PollUntil( [&] { return fake_admin_->flush_count() >= 3; },
                            std::chrono::milliseconds( 10000 ) ) );
    EXPECT_EQ( manager->GetStatus().durable_upload_count, 0u );
    ASSERT_TRUE( AllInState( cids, UploadState::UploadedAccepted ) );
    {
        const auto status = manager->GetStatus();
        EXPECT_TRUE( status.last_flush_valid );  // observation succeeded
        EXPECT_FALSE( status.last_flush_ok );    // evidence NOT met
        EXPECT_EQ( status.last_flush_pending_objects, 1u );
    }

    // Evidence arrives: the next cycle advances the full Accepted set.
    fake_admin_->SetStats( UploadStats{} );
    ASSERT_TRUE( PollUntil( [&] { return AllInState( cids, UploadState::UploadedDurable ); },
                            std::chrono::milliseconds( 10000 ) ) );
    const auto status = manager->GetStatus();
    EXPECT_EQ( status.durable_upload_count, kBlocks );
    EXPECT_EQ( status.accepted_upload_count, 0u );
    EXPECT_TRUE( status.last_flush_ok );
    EXPECT_EQ( status.last_flush_pending_objects, 0u );
}

/**
 * @given journaled blocks and the admin scripted FlushFails
 * @when  flush cycles run, the fault then clears
 * @then  zero marks happen while the fault holds (records stay Accepted,
 *        flush backoff engages - attempts space out - and the evidence
 *        reads not-valid), and after the fault clears a later cycle
 *        advances everything and resets the flush backoff.
 */
TEST_F( ColdStorageDurabilityTest, flush_failure_keeps_accepted_then_recovers )
{
    constexpr size_t kBlocks = 3;
    const auto       cids    = JournalBlocks( kBlocks );

    fake_admin_->SetAdminFault( S3AdminFaultKind::FlushFails );
    auto manager = MakeManager( FastOptions(), FastDurability() );
    manager->Start();

    // Attempts happen and back off (several attempts over a window far
    // longer than one cadence floor - the doubling delay is visible).
    const uint64_t attempts_after_fault = fake_admin_->flush_count();
    ASSERT_TRUE( PollUntil( [&] { return fake_admin_->flush_count() >= attempts_after_fault + 3; },
                            std::chrono::milliseconds( 10000 ) ) );
    EXPECT_EQ( manager->GetStatus().durable_upload_count, 0u );
    ASSERT_TRUE( AllInState( cids, UploadState::UploadedAccepted ) );
    {
        const auto status = manager->GetStatus();
        EXPECT_FALSE( status.last_flush_valid );
        EXPECT_FALSE( status.last_flush_ok );
    }

    // Fault clears: recovery advances and the evidence flips to ok.
    fake_admin_->SetAdminFault( S3AdminFaultKind::None );
    ASSERT_TRUE( PollUntil( [&] { return AllInState( cids, UploadState::UploadedDurable ); },
                            std::chrono::milliseconds( 10000 ) ) );
    const auto status = manager->GetStatus();
    EXPECT_EQ( status.durable_upload_count, kBlocks );
    EXPECT_TRUE( status.last_flush_valid );
    EXPECT_TRUE( status.last_flush_ok );
}

/**
 * @given journaled blocks drained to Accepted and flush_min_interval_ms
 *        set far beyond the test window (10 s)
 * @when  quiescent drain cycles run for well past the drain cadence
 * @then  NO flush is issued (flush_count stays 0) - the cadence floor
 *        gates even the first attempt.
 */
TEST_F( ColdStorageDurabilityTest, cadence_floor_delays_first_flush )
{
    constexpr size_t kBlocks = 2;
    JournalBlocks( kBlocks );

    auto manager = MakeManager( FastOptions(), FastDurability( 10000, 20000 ) );
    manager->Start();

    // Drain happens fast (accepted count rises), but the flush floor
    // holds: no flush attempt within the window.
    ASSERT_TRUE( PollUntil( [&] { return manager->GetStatus().accepted_upload_count == kBlocks; },
                            std::chrono::milliseconds( 10000 ) ) );
    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
    EXPECT_EQ( fake_admin_->flush_count(), 0u );
    EXPECT_EQ( manager->GetStatus().durable_upload_count, 0u );
}

/**
 * @given one CID's remote Put persistently failing (every drain cycle is
 *        non-quiescent) while the other blocks drain to Accepted, with a
 *        fast floor and a 300 ms backstop interval
 * @when  cycles run under the sustained non-quiescent activity
 * @then  quiescence is NEVER the trigger - only the D-01 max-interval
 *        backstop can pass the cadence gate - and the healthy records
 *        still advance to UploadedDurable within the backstop budget
 *        while the failing CID stays PendingUpload (never drained, never
 *        marked).
 */
TEST_F( ColdStorageDurabilityTest, backstop_flushes_under_sustained_activity )
{
    constexpr size_t kBlocks = 4;
    const auto       cids    = JournalBlocks( kBlocks );

    // Fail exactly one CID's remote Put forever: every drain cycle ends
    // non-quiescent (any_failure), so only the D-01 backstop can flush.
    fake_->SetFaultForKey( DeriveTestKey( cids[0] ), S3Op::Put, S3FaultKind::Unavailable );

    auto manager = MakeManager( FastOptions(), FastDurability( 50, 300 ) );
    manager->Start();

    ASSERT_TRUE( PollUntil( [&] { return manager->GetStatus().durable_upload_count == kBlocks - 1; },
                            std::chrono::milliseconds( 10000 ) ) )
        << "backstop did not flush within its interval under non-quiescent load";
    EXPECT_GE( fake_admin_->flush_count(), 1u );

    auto stuck = manager->GetUploadState( cids[0] );
    ASSERT_TRUE( stuck.has_value() );
    EXPECT_EQ( stuck.value(), UploadState::PendingUpload );
}

/**
 * @given journaled blocks drained to UploadedAccepted with the flush
 *        floor set far beyond the test window (the cadence never fires)
 * @when  Stop() runs
 * @then  the D-04 bounded final flush makes exactly one attempt after
 *        the join (flush_count 0 -> 1), observes the zero-pending
 *        evidence, and every record is UploadedDurable when Stop
 *        returns (D-04/SC3 shutdown leg).
 */
TEST_F( ColdStorageDurabilityTest, shutdown_final_flush_advances )
{
    constexpr size_t kBlocks = 3;
    const auto       cids    = JournalBlocks( kBlocks );
    auto             manager = MakeManager( FastOptions(), FastDurability( 10000, 20000 ) );
    manager->Start();

    // Drain reaches Accepted, but the cadence floor holds: no flush.
    ASSERT_TRUE( PollUntil( [&] { return manager->GetStatus().accepted_upload_count == kBlocks; },
                            std::chrono::milliseconds( 10000 ) ) );
    EXPECT_EQ( fake_admin_->flush_count(), 0u );

    manager->Stop();

    // The final flush is the ONLY attempt, and it advanced everything.
    EXPECT_EQ( fake_admin_->flush_count(), 1u );
    EXPECT_EQ( manager->GetStatus().durable_upload_count, kBlocks );
    for ( const auto &cid : cids )
    {
        auto state = shared_local_->GetUploadState( cid );
        ASSERT_TRUE( state.has_value() );
        EXPECT_EQ( state.value(), UploadState::UploadedDurable );
    }
}

/**
 * @given journaled blocks drained to Accepted, the admin scripted
 *        FlushFails, and the flush floor beyond the test window
 * @when  Stop() runs, the fault then clears and a FRESH manager opens
 *        over the same store with a fast cadence
 * @then  the failed final flush leaves every record UploadedAccepted
 *        (the resume property - leftovers are next startup's work), and
 *        the fresh manager's first flush cycle advances them (D-04).
 */
TEST_F( ColdStorageDurabilityTest, shutdown_final_flush_failure_leaves_accepted )
{
    constexpr size_t kBlocks = 3;
    const auto       cids    = JournalBlocks( kBlocks );
    fake_admin_->SetAdminFault( S3AdminFaultKind::FlushFails );
    auto manager = MakeManager( FastOptions(), FastDurability( 10000, 20000 ) );
    manager->Start();

    ASSERT_TRUE( PollUntil( [&] { return manager->GetStatus().accepted_upload_count == kBlocks; },
                            std::chrono::milliseconds( 10000 ) ) );
    manager->Stop();

    // Failed final flush: records stay Accepted, nothing advanced.
    EXPECT_EQ( manager->GetStatus().durable_upload_count, 0u );
    for ( const auto &cid : cids )
    {
        auto state = shared_local_->GetUploadState( cid );
        ASSERT_TRUE( state.has_value() );
        EXPECT_EQ( state.value(), UploadState::UploadedAccepted );
    }

    // Resume: a fresh manager over the SAME store advances them on its
    // first evidence-backed cycle once the fault clears.
    fake_admin_->SetAdminFault( S3AdminFaultKind::None );
    auto resumed = MakeManager( FastOptions(), FastDurability() );
    resumed->Start();
    ASSERT_TRUE( PollUntil( [&] {
        auto state = resumed->GetUploadState( cids[0] );
        return state.has_value() && state.value() == UploadState::UploadedDurable;
    },
                            std::chrono::milliseconds( 10000 ) ) );
    for ( const auto &cid : cids )
    {
        auto state = shared_local_->GetUploadState( cid );
        ASSERT_TRUE( state.has_value() );
        EXPECT_EQ( state.value(), UploadState::UploadedDurable );
    }
}

/**
 * @given the compile-time eviction durability gate and a journaled block
 * @when  the gate constant and the D-09 per-CID pass-through are read
 * @then  the gate is exactly UploadedDurable (D-10 - no config knob can
 *        lower it), the cadence/final-flush budget constants carry their
 *        documented values, and a Put-born record reads PendingUpload
 *        through the manager before any cycle runs (SC2).
 */
TEST_F( ColdStorageDurabilityTest, eviction_gate_constant_and_per_cid_state )
{
    static_assert( static_cast<int>( ColdStorageManager::kEvictionDurabilityGate )
                       == static_cast<int>( UploadState::UploadedDurable ),
                   "eviction durability gate must be UploadedDurable (D-10)" );
    EXPECT_EQ( ColdStorageManager::kEvictionDurabilityGate, UploadState::UploadedDurable );
    EXPECT_EQ( ColdStorageManager::kDefaultFlushMinIntervalMs, std::chrono::milliseconds( 30000 ) );
    EXPECT_EQ( ColdStorageManager::kDefaultFlushMaxIntervalMs, std::chrono::milliseconds( 300000 ) );
    EXPECT_EQ( ColdStorageManager::kCadenceFlushReadTimeoutMs, std::chrono::milliseconds( 30000 ) );
    EXPECT_EQ( ColdStorageManager::kFinalFlushBudgetMs, std::chrono::milliseconds( 2000 ) );

    const auto cids    = JournalBlocks( 1 );
    auto       manager = MakeManager( FastOptions(), FastDurability() );
    auto       state   = manager->GetUploadState( cids[0] );
    ASSERT_TRUE( state.has_value() );
    EXPECT_EQ( state.value(), UploadState::PendingUpload );
}
