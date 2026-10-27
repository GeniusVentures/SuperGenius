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
#include <map>
#include <memory>
#include <set>
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

#include "crdt/globaldb/BlockStoreBackedIpfsDatastore.hpp"
#include "crdt/globaldb/ColdStorageManager.hpp"

#include "base/logger.hpp"

namespace
{
    // IPFS resolves only inside namespace sgns - alias it for this TU
    // (the decorator's set()/get() surface is IpfsDatastore-typed).
    namespace ipfs_outcome = sgns::IPFS::outcome;
    using IpfsDatastore      = sgns::ipfs_lite::ipfs::IpfsDatastore;
    using IpfsDatastoreError = sgns::ipfs_lite::ipfs::IpfsDatastoreError;
    using CID                = sgns::CID;
    using sgns::crdt::BlockStoreBackedIpfsDatastore;
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

    /// @brief Test-local in-memory inner IpfsDatastore (decorator-level
    ///        outage/publish tests need no real RocksDB inner store - the
    ///        spill suite's FakeInnerDatastore shape).
    class FakeInnerDatastore final : public IpfsDatastore
    {
    public:
        ipfs_outcome::result<bool> contains( const CID &key ) const override
        {
            auto key_str = key.toString();
            if ( key_str.has_error() )
            {
                return key_str.error();
            }
            return blocks_.count( key_str.value() ) > 0;
        }

        ipfs_outcome::result<void> set( const CID &key, Value value ) override
        {
            auto key_str = key.toString();
            if ( key_str.has_error() )
            {
                return key_str.error();
            }
            blocks_[key_str.value()] = std::move( value );
            return ipfs_outcome::success();
        }

        ipfs_outcome::result<void> seal( const CID &key ) override
        {
            auto key_str = key.toString();
            if ( key_str.has_error() )
            {
                return key_str.error();
            }
            sealed_.insert( key_str.value() );
            return ipfs_outcome::success();
        }

        ipfs_outcome::result<bool> is_sealed( const CID &key ) const override
        {
            auto key_str = key.toString();
            if ( key_str.has_error() )
            {
                return key_str.error();
            }
            return sealed_.count( key_str.value() ) > 0;
        }

        ipfs_outcome::result<Value> get( const CID &key ) const override
        {
            auto key_str = key.toString();
            if ( key_str.has_error() )
            {
                return key_str.error();
            }
            auto it = blocks_.find( key_str.value() );
            if ( it == blocks_.end() )
            {
                return IpfsDatastoreError::NOT_FOUND;
            }
            return it->second;
        }

        ipfs_outcome::result<void> remove( const CID &key ) override
        {
            auto key_str = key.toString();
            if ( key_str.has_error() )
            {
                return key_str.error();
            }
            blocks_.erase( key_str.value() );
            return ipfs_outcome::success();
        }

    private:
        std::map<std::string, IpfsDatastore::Value> blocks_;
        std::set<std::string>                       sealed_;
    };

    class ColdStorageDurabilityTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            shared_local_dir_ = FreshDir();
            auto opened_local = LocalBlockStore::Open( LocalBlockStoreOptions{ shared_local_dir_ } );
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
        std::string                               shared_local_dir_;
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

// ---------------------------------------------------------------------------
// SC3 fault-injection honesty suite (DUR-01, v1.1 Phase 10 plan 10-02
// Task 1): crash reconciliation, admin outage, shutdown under load,
// idempotent replay, and the mid-flush acceptance race - the proofs the
// 10-01 construction stands on, plus the discharge of both flagged
// assumptions.
// ---------------------------------------------------------------------------

/**
 * @given a directory holding MIXED leftovers after a simulated crash -
 *        3 records drained to UploadedAccepted (never advanced: the
 *        crashed manager had no admin client) and 2 still PendingUpload
 *        (their remote Puts were failing)
 * @when  a fresh manager with a fast cadence opens over the SAME
 *        directory
 * @then  the first poll re-drains the Pending records (Phase-8 D-12
 *        path) and the first eligible flush cycle advances the stranded
 *        Accepted records - eventually everything is Durable and the
 *        accepted count does not freeze non-zero across the restart
 *        (SC3 leg 1 / Pitfall 2).
 */
TEST_F( ColdStorageDurabilityTest, crash_reconciliation_mixed_leftovers )
{
    constexpr size_t kBlocks = 5;
    const std::string dir = FreshDir();
    std::shared_ptr<LocalBlockStore> first_store; // assigned below
    {
        auto opened = LocalBlockStore::Open( LocalBlockStoreOptions{ dir } );
        ASSERT_TRUE( opened.has_value() );
        first_store = std::shared_ptr<LocalBlockStore>( std::move( opened.value() ) );
    }
    ASSERT_NE( first_store, nullptr );

    std::vector<Cid> cids;
    for ( size_t i = 0; i < kBlocks; ++i )
    {
        const auto payload = "crash-mixed payload " + std::to_string( i );
        auto       cid     = MakeCid( payload );
        ASSERT_TRUE( first_store->Put( cid, SpanOf( payload ) ).has_value() );
        cids.push_back( std::move( cid ) );
    }
    // Two CIDs' remote Puts fail forever: they stay PendingUpload across
    // the simulated crash.
    fake_->SetFaultForKey( DeriveTestKey( cids[3] ), S3Op::Put, S3FaultKind::Unavailable );
    fake_->SetFaultForKey( DeriveTestKey( cids[4] ), S3Op::Put, S3FaultKind::Unavailable );

    // Manager A: NO admin client - drains to Accepted, cannot advance, and
    // its Stop() final flush is inert (the crash shape: nothing advanced).
    {
        auto remote_store_mutex = std::make_shared<std::mutex>();
        auto manager_a          = std::make_shared<ColdStorageManager>(
            first_store, remote_, cold_store_mutex_, remote_store_mutex, FastOptions() );
        manager_a->Start();
        ASSERT_TRUE( PollUntil( [&] {
            const auto status = manager_a->GetStatus();
            return status.accepted_upload_count == 3 && status.pending_upload_count == 2;
        },
                                std::chrono::milliseconds( 10000 ) ) );
        manager_a->Stop();
    }
    // Simulated death: release the store so the directory lock frees (the
    // library's own crash shape - WAL tail replays on reopen).
    first_store.reset();
    ASSERT_EQ( first_store.get(), nullptr );

    // Reopen over the SAME directory; heal the remote; fresh admin fake
    // posture (faults cleared) - the resume manager.
    fake_->ClearFaults();
    auto reopened = LocalBlockStore::Open( LocalBlockStoreOptions{ dir } );
    ASSERT_TRUE( reopened.has_value() );
    auto reopened_store = std::shared_ptr<LocalBlockStore>( std::move( reopened.value() ) );
    auto remote_store_mutex_b = std::make_shared<std::mutex>();
    manager_ = std::make_shared<ColdStorageManager>(
        reopened_store, remote_, cold_store_mutex_, remote_store_mutex_b, FastOptions(),
        FastDurability(), fake_admin_ );
    manager_->Start();

    ASSERT_TRUE( PollUntil( [&] { return AllInState( cids, UploadState::UploadedDurable ); },
                            std::chrono::milliseconds( 10000 ) ) );
    const auto status = manager_->GetStatus();
    EXPECT_EQ( status.durable_upload_count, kBlocks );
    EXPECT_EQ( status.accepted_upload_count, 0u );
}

/**
 * @given a decorated store publishing through set() while the admin API
 *        is scripted Unreachable
 * @when  publishes continue and flush cycles fail during the outage, the
 *        fault then clears
 * @then  EVERY set() succeeds on the dagMutex_-held write path (journal
 *        first - an admin outage can never block a publish), records
 *        drain to Accepted and their GetUploadState NEVER surfaces a
 *        NotFound-shaped failure, drainer_healthy stays TRUE while
 *        flush_backoff_active engages INDEPENDENTLY (Pitfall 6), and
 *        after the fault clears the next cycle advances everything and
 *        the flush backoff resets (SC3 leg 2 / D-07).
 */
TEST_F( ColdStorageDurabilityTest, admin_outage_never_not_found_and_drain_unpoisoned )
{
    auto inner     = std::make_shared<FakeInnerDatastore>();
    auto decorator = std::make_shared<BlockStoreBackedIpfsDatastore>(
        inner, shared_local_, cold_store_mutex_, sgns::base::createLogger( "ColdStorageDurability" ) );

    constexpr size_t kBlocks = 3;
    std::vector<Cid> cids;
    fake_admin_->SetAdminFault( S3AdminFaultKind::Unreachable );
    auto manager = MakeManager( FastOptions(), FastDurability() );
    manager->Start();

    // Publishes through the decorator succeed THROUGHOUT the outage.
    for ( size_t i = 0; i < kBlocks; ++i )
    {
        const auto payload = "outage payload " + std::to_string( i );
        auto       cid     = MakeCid( payload );
        CID        cid_key( cid );
        auto res = decorator->set( cid_key, IpfsDatastore::Value( SpanOf( payload ) ) );
        ASSERT_TRUE( res.has_value() ) << "publish " << i << " must succeed during the admin outage";
        cids.push_back( std::move( cid ) );
    }

    // Drain reaches Accepted under the outage; flush attempts fail; the
    // state reads Accepted (never NotFound), drain health holds, flush
    // backoff engages independently.
    ASSERT_TRUE( PollUntil( [&] {
        const auto status = manager->GetStatus();
        return status.accepted_upload_count == kBlocks && status.flush_backoff_active
               && fake_admin_->flush_count() >= 3;
    },
                            std::chrono::milliseconds( 10000 ) ) );
    for ( const auto &cid : cids )
    {
        auto state = manager->GetUploadState( cid );
        ASSERT_TRUE( state.has_value() ); // never a NotFound-shaped failure
        EXPECT_EQ( state.value(), UploadState::UploadedAccepted );
    }
    EXPECT_TRUE( manager->GetStatus().drainer_healthy );

    // Fault clears: the next cycle advances and the flush backoff resets.
    fake_admin_->SetAdminFault( S3AdminFaultKind::None );
    ASSERT_TRUE( PollUntil( [&] { return AllInState( cids, UploadState::UploadedDurable ); },
                            std::chrono::milliseconds( 10000 ) ) );
    const auto recovered = manager->GetStatus();
    EXPECT_FALSE( recovered.flush_backoff_active );
    EXPECT_EQ( recovered.durable_upload_count, kBlocks );
}

/**
 * @given drained-to-Accepted records and the admin scripted GarbageStats
 *        (a 200-body the strict parse must reject)
 * @when  flush cycles run, the fault then clears
 * @then  flush itself succeeds (attempts observed) but ZERO records
 *        advance while the observation cannot be parsed - conservative
 *        failure, never NotFound - and the recovery cycle advances them.
 */
TEST_F( ColdStorageDurabilityTest, garbage_stats_keeps_accepted_then_recovers )
{
    constexpr size_t kBlocks = 2;
    const auto       cids    = JournalBlocks( kBlocks );
    fake_admin_->SetAdminFault( S3AdminFaultKind::GarbageStats );
    auto manager = MakeManager( FastOptions(), FastDurability() );
    manager->Start();

    ASSERT_TRUE( PollUntil( [&] { return fake_admin_->flush_count() >= 2; },
                            std::chrono::milliseconds( 10000 ) ) );
    EXPECT_EQ( manager->GetStatus().durable_upload_count, 0u );
    ASSERT_TRUE( AllInState( cids, UploadState::UploadedAccepted ) );
    EXPECT_FALSE( manager->GetStatus().last_flush_valid );

    fake_admin_->SetAdminFault( S3AdminFaultKind::None );
    ASSERT_TRUE( PollUntil( [&] { return AllInState( cids, UploadState::UploadedDurable ); },
                            std::chrono::milliseconds( 10000 ) ) );
    EXPECT_TRUE( manager->GetStatus().last_flush_valid );
}

/**
 * @given a publisher thread setting blocks through the decorator while a
 *        SlowFlush (3 s) exceeds the 2 s final-flush budget
 * @when  Stop() runs under that load
 * @then  Stop() returns bounded (grace window + final-flush budget +
 *        margin), nothing crashes, records are left Accepted (the final
 *        flush could not complete inside its budget), and a fresh
 *        manager over the same directory advances them at restart
 *        (SC3 leg 3 / D-04 / Pitfall 4).
 */
TEST_F( ColdStorageDurabilityTest, shutdown_under_load_slow_flush_bounded )
{
    auto inner     = std::make_shared<FakeInnerDatastore>();
    auto decorator = std::make_shared<BlockStoreBackedIpfsDatastore>(
        inner, shared_local_, cold_store_mutex_, sgns::base::createLogger( "ColdStorageDurability" ) );

    auto manager = MakeManager( FastOptions(), FastDurability( 10000, 20000 ) );
    manager->Start();

    std::atomic<bool> publishing{ true };
    std::thread publisher( [&]
    {
        uint64_t n = 0;
        while ( publishing.load() )
        {
            const auto payload = "shutdown-load payload " + std::to_string( n );
            auto       cid     = MakeCid( payload );
            CID        cid_key( cid );
            (void)decorator->set( cid_key, IpfsDatastore::Value( SpanOf( payload ) ) );
            ++n;
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        }
    } );

    // Let some blocks drain to Accepted, then arm the slow flush and stop.
    ASSERT_TRUE( PollUntil( [&] { return manager->GetStatus().accepted_upload_count >= 2; },
                            std::chrono::milliseconds( 10000 ) ) );
    fake_admin_->SetSlowFlushDelayMs( 3000 ); // > kFinalFlushBudgetMs (2000)

    const auto   stop_start = std::chrono::steady_clock::now();
    manager->Stop();
    const auto   stop_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - stop_start )
                                       .count();
    publishing.store( false );
    publisher.join();

    const int64_t bound_ms = ColdStorageManager::kShutdownGraceWindow.count()
                           + ColdStorageManager::kFinalFlushBudgetMs.count() + 3000 /* margin */;
    std::cout << "[measurement] Stop() under SlowFlush load returned in " << stop_elapsed_ms
              << " ms (bound " << bound_ms << " ms)\n";
    EXPECT_LT( stop_elapsed_ms, bound_ms ) << "Stop() must stay bounded by grace + final budget";

    // The final flush could not complete inside its budget: Accepted
    // records remain. Reuse the STILL-OPEN fixture store with a fresh
    // manager (same process, same directory) - the resume proof.
    EXPECT_GT( manager->GetStatus().accepted_upload_count, 0u );
    fake_admin_->SetSlowFlushDelayMs( 0 );
    auto remote_store_mutex = std::make_shared<std::mutex>();
    auto manager_b          = std::make_shared<ColdStorageManager>(
        shared_local_, remote_, cold_store_mutex_, remote_store_mutex, FastOptions(),
        FastDurability(), fake_admin_ );
    manager_b->Start();
    ASSERT_TRUE( PollUntil( [&] {
        const auto status = manager_b->GetStatus();
        return status.pending_upload_count == 0 && status.accepted_upload_count == 0
               && status.durable_upload_count > 0;
    },
                            std::chrono::milliseconds( 15000 ) ) );
    manager_b->Stop();
}

/**
 * @given a journal where every record is already UploadedDurable
 * @when  additional flush cycles run (the cadence keeps observing)
 * @then  flush_count keeps advancing while ZERO records change state and
 *        durable_upload_count is unchanged - re-running the flush+advance
 *        cycle over the same records is a no-op-advancement (D-03
 *        snapshot semantics; discharges the 10-01 idempotency flag).
 */
TEST_F( ColdStorageDurabilityTest, idempotent_replay_marks_nothing )
{
    constexpr size_t kBlocks = 2;
    const auto       cids    = JournalBlocks( kBlocks );
    auto             manager = MakeManager( FastOptions(), FastDurability() );
    manager->Start();
    ASSERT_TRUE( PollUntil( [&] { return AllInState( cids, UploadState::UploadedDurable ); },
                            std::chrono::milliseconds( 10000 ) ) );

    const auto flushes_before = fake_admin_->flush_count();
    const auto durable_before = manager->GetStatus().durable_upload_count;
    ASSERT_TRUE( PollUntil( [&] { return fake_admin_->flush_count() >= flushes_before + 2; },
                            std::chrono::milliseconds( 10000 ) ) );
    EXPECT_EQ( manager->GetStatus().durable_upload_count, durable_before );
    ASSERT_TRUE( AllInState( cids, UploadState::UploadedDurable ) );
}

/**
 * @given a flush cycle in flight whose on_flush hook journals + accepts a
 *        NEW block MID-FLUSH (after the snapshot was captured)
 * @when  the cycle completes with pendingObjects == 0 and marks its
 *        snapshot
 * @then  the late-accepted CID is NOT marked on that cycle's evidence -
 *        it stays UploadedAccepted until exactly the NEXT cycle advances
 *        it (D-03; discharges the 10-01 acceptance-race flag).
 */
TEST_F( ColdStorageDurabilityTest, acceptance_race_mid_flush_excluded_from_marks )
{
    const auto payload_x = "race payload X";
    auto       cid_x     = MakeCid( payload_x );
    ASSERT_TRUE( shared_local_->Put( cid_x, SpanOf( payload_x ) ).has_value() );

    const auto  payload_y = "race payload Y (late accept)";
    auto        cid_y     = MakeCid( payload_y );
    std::atomic<bool> late_accepted{ false };

    fake_admin_->SetOnFlush( [&]
    {
        if ( late_accepted.exchange( true ) )
        {
            return; // arm once: only the FIRST flush races a late accept
        }
        // Runs on the drainer thread INSIDE the (unlocked) admin call:
        // journal + accept a brand-new block AFTER the cycle's snapshot
        // was captured under cold_store_mutex_.
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        (void)shared_local_->Put( cid_y, SpanOf( payload_y ) );
        (void)shared_local_->MarkUploadedAccepted( cid_y );
    } );

    auto manager = MakeManager( FastOptions(), FastDurability() );
    manager->Start();

    // The racing cycle completes: X advances on the zero-pending
    // evidence, Y does NOT ride that evidence.
    ASSERT_TRUE( PollUntil( [&] {
        auto state = manager->GetUploadState( cid_x );
        return state.has_value() && state.value() == UploadState::UploadedDurable;
    },
                            std::chrono::milliseconds( 10000 ) ) );
    ASSERT_TRUE( late_accepted.load() );
    {
        auto late = manager->GetUploadState( cid_y );
        ASSERT_TRUE( late.has_value() );
        EXPECT_EQ( late.value(), UploadState::UploadedAccepted )
            << "the late-accepted CID must not ride the racing cycle's evidence";
    }

    // Exactly the NEXT cycle advances it.
    ASSERT_TRUE( PollUntil( [&] {
        auto late = manager->GetUploadState( cid_y );
        return late.has_value() && late.value() == UploadState::UploadedDurable;
    },
                            std::chrono::milliseconds( 10000 ) ) );
}

// ---------------------------------------------------------------------------
// SC4 staging-health suite (DUR-01, v1.1 Phase 10 plan 10-02 Task 2):
// raw counters + the conservative D-12 predicate in the status getter,
// on-tick freshness, on-demand D-13 refresh, restart count seeding.
// ---------------------------------------------------------------------------

/// Script a stats body with the given counters (pendingObjects always 0
/// so the evidence gate never interferes with the health assertions).
UploadStats StagingStats( uint64_t buffer_used, uint64_t buffer_limit, uint64_t failed )
{
    UploadStats stats;
    stats.pendingObjects = 0;
    stats.bufferUsed     = buffer_used;
    stats.bufferLimit    = buffer_limit;
    stats.failedUploads  = failed;
    return stats;
}

/**
 * @given clean stats (flat failures, buffer far under the fraction)
 * @when  a cadence tick observes them
 * @then  staging_healthy reads true with the raw counters visible.
 */
TEST_F( ColdStorageDurabilityTest, staging_healthy_on_clean_stats )
{
    fake_admin_->SetStats( StagingStats( 100, 1000, 0 ) );
    auto manager = MakeManager( FastOptions(), FastDurability() );
    manager->Start();
    ASSERT_TRUE( PollUntil( [&] { return manager->GetStatus().staging_healthy; },
                            std::chrono::milliseconds( 10000 ) ) );
    const auto status = manager->GetStatus();
    EXPECT_EQ( status.staging_buffer_used, 100u );
    EXPECT_EQ( status.staging_buffer_limit, 1000u );
    EXPECT_EQ( status.staging_failed_uploads, 0u );
    EXPECT_EQ( status.staging_pending_objects, 0u );
}

/**
 * @given bufferUsed above kStagingBufferFraction of a non-zero limit
 * @when  a tick observes them
 * @then  staging_healthy reads false (the GC-inhibition signal Phase 11
 *        consumes) even though the flush evidence itself is fine.
 */
TEST_F( ColdStorageDurabilityTest, staging_unhealthy_when_buffer_fraction_exceeded )
{
    fake_admin_->SetStats( StagingStats( 900, 1000, 0 ) ); // > 0.8 * 1000
    auto manager = MakeManager( FastOptions(), FastDurability() );
    manager->Start();
    ASSERT_TRUE( PollUntil( [&] { return manager->GetStatus().staging_buffer_limit == 1000; },
                            std::chrono::milliseconds( 10000 ) ) );
    const auto status = manager->GetStatus();
    EXPECT_FALSE( status.staging_healthy );
    EXPECT_TRUE( status.last_flush_ok ); // evidence fine - health is separate
    // The public threshold constant is citable (Phase-9 D-12 precedent).
    EXPECT_DOUBLE_EQ( ColdStorageManager::kStagingBufferFraction, 0.8 );
}

/**
 * @given bufferLimit == 0 (openapi verbatim: unlimited) with a huge
 *        bufferUsed and flat failures
 * @when  a tick observes them
 * @then  the buffer clause is vacuously true - health decided by the
 *        other clauses alone.
 */
TEST_F( ColdStorageDurabilityTest, staging_zero_limit_is_unlimited )
{
    fake_admin_->SetStats( StagingStats( 4000000000ull, 0, 0 ) );
    auto manager = MakeManager( FastOptions(), FastDurability() );
    manager->Start();
    ASSERT_TRUE( PollUntil( [&] { return manager->GetStatus().staging_healthy; },
                            std::chrono::milliseconds( 10000 ) ) );
    EXPECT_EQ( manager->GetStatus().staging_buffer_used, 4000000000ull );
}

/**
 * @given failedUploads growing between ticks
 * @when  ticks observe 5 -> 5 -> 6
 * @then  the growth tick reads unhealthy and the flat delta tick reads
 *        healthy again (delta-since-previous-tick semantics).
 */
TEST_F( ColdStorageDurabilityTest, staging_unhealthy_on_failed_uploads_growth )
{
    fake_admin_->SetStats( StagingStats( 0, 1000, 5 ) ); // first tick: delta vs 0 -> unhealthy
    auto manager = MakeManager( FastOptions(), FastDurability() );
    manager->Start();
    ASSERT_TRUE( PollUntil( [&] { return manager->GetStatus().staging_failed_uploads == 5; },
                            std::chrono::milliseconds( 10000 ) ) );
    EXPECT_FALSE( manager->GetStatus().staging_healthy );

    // Flat delta: healthy again.
    ASSERT_TRUE( PollUntil( [&] { return manager->GetStatus().staging_healthy; },
                            std::chrono::milliseconds( 10000 ) ) );

    // Growth: unhealthy until the next flat tick.
    fake_admin_->SetStats( StagingStats( 0, 1000, 6 ) );
    ASSERT_TRUE( PollUntil( [&] { return manager->GetStatus().staging_failed_uploads == 6
                                       && !manager->GetStatus().staging_healthy; },
                            std::chrono::milliseconds( 10000 ) ) );
    ASSERT_TRUE( PollUntil( [&] { return manager->GetStatus().staging_healthy; },
                            std::chrono::milliseconds( 10000 ) ) );
}

/**
 * @given healthy staging state, then the admin scripted Unreachable
 * @when  a tick's admin call fails
 * @then  staging_healthy reads false (fail-safe on unreadable evidence)
 *        while the counters keep their last-known values.
 */
TEST_F( ColdStorageDurabilityTest, staging_fail_safe_on_admin_unreachable )
{
    fake_admin_->SetStats( StagingStats( 100, 1000, 0 ) );
    auto manager = MakeManager( FastOptions(), FastDurability() );
    manager->Start();
    ASSERT_TRUE( PollUntil( [&] { return manager->GetStatus().staging_healthy; },
                            std::chrono::milliseconds( 10000 ) ) );

    fake_admin_->SetAdminFault( S3AdminFaultKind::Unreachable );
    ASSERT_TRUE( PollUntil( [&] { return !manager->GetStatus().staging_healthy; },
                            std::chrono::milliseconds( 10000 ) ) );
    // Counters hold their last-known values (100/1000/0).
    EXPECT_EQ( manager->GetStatus().staging_buffer_used, 100u );
    EXPECT_EQ( manager->GetStatus().staging_buffer_limit, 1000u );
    fake_admin_->SetAdminFault( S3AdminFaultKind::None );
}

/**
 * @given a manager whose flush floor is far beyond the test window (no
 *        tick will fire) and newly scripted stats
 * @when  RefreshStagingStats() is called from the TEST thread while the
 *        drainer loop runs
 * @then  the cached counters update immediately without waiting a tick,
 *        serialized through the admin mutex (no deadlock, no race).
 */
TEST_F( ColdStorageDurabilityTest, staging_refresh_on_demand_updates_without_tick )
{
    fake_admin_->SetStats( StagingStats( 100, 1000, 0 ) );
    auto manager = MakeManager( FastOptions(), FastDurability( 10000, 20000 ) );
    manager->Start();
    std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
    EXPECT_FALSE( manager->GetStatus().staging_healthy ); // no tick ran yet

    fake_admin_->SetStats( StagingStats( 250, 1000, 0 ) );
    manager->RefreshStagingStats(); // on-demand (D-13), test-thread call
    const auto status = manager->GetStatus();
    EXPECT_EQ( status.staging_buffer_used, 250u );
    EXPECT_EQ( status.staging_buffer_limit, 1000u );
    EXPECT_TRUE( status.staging_healthy );
}

/**
 * @given a reopened directory already holding 2 Accepted and 1 Durable
 *        journal records
 * @when  a fresh manager starts over it with a flush floor beyond the
 *        test window (nothing advances)
 * @then  the first-cycle seeding reports accepted=2 / durable=1 in the
 *        status getter - counts survive restart without any scan in
 *        GetStatus().
 */
TEST_F( ColdStorageDurabilityTest, staging_restart_seeding_counts )
{
    const std::string dir = FreshDir();
    {
        auto opened = LocalBlockStore::Open( LocalBlockStoreOptions{ dir } );
        ASSERT_TRUE( opened.has_value() );
        auto store = std::shared_ptr<LocalBlockStore>( std::move( opened.value() ) );
        const std::string payloads[3] = { "seed A", "seed B", "seed C" };
        for ( const auto &payload : payloads )
        {
            auto cid = MakeCid( payload );
            ASSERT_TRUE( store->Put( cid, SpanOf( payload ) ).has_value() );
            ASSERT_TRUE( store->MarkUploadedAccepted( cid ).has_value() );
        }
        auto durable_cid = MakeCid( payloads[2] );
        ASSERT_TRUE( store->MarkUploadedDurable( durable_cid ).has_value() );
    } // store released - directory lock freed

    auto reopened = LocalBlockStore::Open( LocalBlockStoreOptions{ dir } );
    ASSERT_TRUE( reopened.has_value() );
    auto reopened_store = std::shared_ptr<LocalBlockStore>( std::move( reopened.value() ) );
    auto remote_store_mutex = std::make_shared<std::mutex>();
    manager_ = std::make_shared<ColdStorageManager>(
        reopened_store, remote_, cold_store_mutex_, remote_store_mutex, FastOptions(),
        FastDurability( 10000, 20000 ), fake_admin_ );
    manager_->Start();
    ASSERT_TRUE( PollUntil( [&] {
        const auto status = manager_->GetStatus();
        return status.accepted_upload_count == 2 && status.durable_upload_count == 1;
    },
                            std::chrono::milliseconds( 10000 ) ) );
}
