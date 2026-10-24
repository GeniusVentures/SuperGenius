/**
 * @file       cold_storage_spill_test.cpp
 * @brief      Hermetic Tier-1 journal-contract tests for the
 *             BlockStoreBackedIpfsDatastore decorator (SPILL-01, v1.1
 *             Phase 8 plan 08-01).
 *
 * Registered ONLY under SGNS_COLD_STORAGE (links
 * sgns-blockstore::sgns-blockstore - the OFF-build Tier-1 lane stays
 * non-empty via the unconditional config-gate test). The decorator is
 * constructed DIRECTLY (D-10) - no GlobalDB scaffolding: a real
 * LocalBlockStore journal on a fresh temp dir, the InMemoryS3Client fake
 * behind an S3BlockStore, the shared mutex, and a test-local in-memory
 * inner IpfsDatastore (decorator-level tests need no real RocksDB inner).
 *
 * Include discipline: installed angle-bracket sgns-blockstore headers plus
 * the fake via the relative include-dir workaround in CMakeLists.txt
 * (08-RESEARCH Pitfall 6 - the fake's header is deliberately not
 * installed). Every library result is guarded with has_value() BEFORE
 * .value()/.error() (policy::terminate alias); static_cast<void> honors
 * [[nodiscard]] inside EXPECT_THROW (cold_storage_link_proof_test
 * discipline).
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
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
#include <sgns-blockstore/S3BlockStore.hpp>
#include <sgns-blockstore/UploadDrainer.hpp>

// Internal fake - relative-include-dir privilege (test-surface only).
#include "InMemoryS3Client.hpp"

#include "base/logger.hpp"
#include "crdt/globaldb/BlockStoreBackedIpfsDatastore.hpp"
#include "crdt/globaldb/ColdStorageManager.hpp"

namespace
{
    using sgns::blockstore::Cid;
    using sgns::blockstore::CidOf;
    using sgns::blockstore::DrainPendingUploads;
    using sgns::blockstore::InMemoryS3Client;
    using sgns::blockstore::LocalBlockStore;
    using sgns::blockstore::LocalBlockStoreOptions;
    using sgns::blockstore::S3BlockStore;
    using sgns::blockstore::UploadState;
    using libp2p::multi::MulticodecType;

    // IPFS resolves only inside namespace sgns - alias it for this TU.
    namespace ipfs_outcome = sgns::IPFS::outcome;

    using sgns::crdt::BlockStoreBackedIpfsDatastore;
    using sgns::crdt::ColdStorageManager;
    using sgns::crdt::ColdStorageWatermark;
    using IpfsDatastore = sgns::ipfs_lite::ipfs::IpfsDatastore;
    using IpfsDatastoreError = sgns::ipfs_lite::ipfs::IpfsDatastoreError;
    using CID = sgns::CID;

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

    /// Short-interval drainer options for the behavior suite (watermark
    /// thresholds and cadence per-case; the pair rule always holds).
    ColdStorageManager::BackpressureOptions FastOptions( uint32_t drain_interval_ms = 100,
                                                         uint32_t warn_pending    = 10,
                                                         uint32_t critical_pending = 20 )
    {
        ColdStorageManager::BackpressureOptions options;
        options.drain_interval_ms = drain_interval_ms;
        options.warn_pending      = warn_pending;
        options.critical_pending  = critical_pending;
        options.backoff_cap_ms    = 1000;
        return options;
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
        return "test-data/cold-storage-spill/case-" + std::to_string( next.fetch_add( 1 ) );
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

    /**
     * @brief Test-local in-memory inner IpfsDatastore (decorator-level
     *        tests need no real RocksDB inner store).
     */
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

        size_t size() const
        {
            return blocks_.size();
        }

    private:
        std::map<std::string, IpfsDatastore::Value> blocks_;
        std::set<std::string>                       sealed_;
    };

    class ColdStorageSpillTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            auto opened_local = LocalBlockStore::Open( LocalBlockStoreOptions{ FreshDir() } );
            ASSERT_TRUE( opened_local.has_value() );
            local_  = std::move( opened_local.value() );
            fake_   = std::make_shared<InMemoryS3Client>();
            auto opened_remote = S3BlockStore::Open( fake_ );
            ASSERT_TRUE( opened_remote.has_value() );
            remote_ = std::move( opened_remote.value() );

            cold_store_mutex_ = std::make_shared<std::mutex>();
            inner_            = std::make_shared<FakeInnerDatastore>();
            shared_local_     = std::shared_ptr<LocalBlockStore>( std::move( local_ ) );
            decorator_        = std::make_shared<BlockStoreBackedIpfsDatastore>(
                inner_, shared_local_, cold_store_mutex_, sgns::base::createLogger( "ColdStorageSpill" ) );
        }

        /// Construct a manager directly over the fixture stores (D-10).
        std::shared_ptr<ColdStorageManager> MakeManager(
            std::shared_ptr<sgns::blockstore::S3BlockStore>    remote,
            const ColdStorageManager::BackpressureOptions      &options )
        {
            return std::make_shared<ColdStorageManager>( shared_local_, std::move( remote ), cold_store_mutex_, options );
        }

        std::shared_ptr<ColdStorageManager> MakeManager( const ColdStorageManager::BackpressureOptions &options )
        {
            return MakeManager( remote_, options );
        }

        // local_ is moved into shared_local_ for the decorator; keep the
        // shared handle for direct journal assertions.
        std::unique_ptr<LocalBlockStore>          local_;
        std::shared_ptr<LocalBlockStore>          shared_local_;
        std::shared_ptr<InMemoryS3Client>         fake_;
        std::shared_ptr<sgns::blockstore::S3BlockStore> remote_;
        std::shared_ptr<std::mutex>               cold_store_mutex_;
        std::shared_ptr<FakeInnerDatastore>       inner_;
        std::shared_ptr<BlockStoreBackedIpfsDatastore> decorator_;
    };
} // namespace

/**
 * @given a decorated in-memory inner store and an empty cold journal
 * @when 10 blocks pass through the decorator's set()
 * @then  every set succeeds, the inner store holds all 10 blocks, the
 *        journal returns byte-identical bytes per CID with all 10 CIDs in
 *        PendingUpload state, and the S3 fake holds ZERO objects (no
 *        remote I/O ever runs on the write path).
 */
TEST_F( ColdStorageSpillTest, journal_write_spills_every_put )
{
    std::vector<std::string> payloads;
    std::vector<Cid>         cids;
    for ( int i = 0; i < 10; ++i )
    {
        payloads.push_back( "cold storage spill payload " + std::to_string( i ) );
        cids.push_back( MakeCid( payloads.back() ) );
    }

    for ( size_t i = 0; i < payloads.size(); ++i )
    {
        CID cid( cids[i] );
        auto res = decorator_->set( cid, IpfsDatastore::Value( SpanOf( payloads[i] ) ) );
        ASSERT_TRUE( res.has_value() ) << "set() " << i << " must succeed";
    }
    EXPECT_EQ( inner_->size(), 10u );

    for ( size_t i = 0; i < payloads.size(); ++i )
    {
        const auto expected = std::vector<uint8_t>( payloads[i].begin(), payloads[i].end() );
        auto got = shared_local_->Get( cids[i] );
        ASSERT_TRUE( got.has_value() );
        EXPECT_EQ( got.value().bytes, expected ) << "journal bytes must be identical for block " << i;

        auto state = shared_local_->GetUploadState( cids[i] );
        ASSERT_TRUE( state.has_value() );
        EXPECT_EQ( state.value(), UploadState::PendingUpload );
    }

    auto pending = shared_local_->GetPendingUploads();
    ASSERT_TRUE( pending.has_value() );
    EXPECT_EQ( pending.value().size(), 10u );

    // Zero remote I/O on the write path (journal-only spill).
    EXPECT_TRUE( fake_->ObjectKeys().empty() );
}

/**
 * @given a decorated store with one block set twice
 * @when the same CID is set again, drained directly, then re-put
 * @then the pending set holds the CID exactly once, the drain advances it
 *        to UploadedAccepted, and the re-put of the drained CID does NOT
 *        regress it to PendingUpload (adjacency edge, Pitfall 3).
 */
TEST_F( ColdStorageSpillTest, re_put_is_idempotent )
{
    const std::string payload = "re-put idempotency payload";
    const auto        cid     = MakeCid( payload );
    CID               cid_key( cid );

    auto first = decorator_->set( cid_key, IpfsDatastore::Value( SpanOf( payload ) ) );
    ASSERT_TRUE( first.has_value() );
    auto second = decorator_->set( cid_key, IpfsDatastore::Value( SpanOf( payload ) ) );
    ASSERT_TRUE( second.has_value() );

    auto pending = shared_local_->GetPendingUploads();
    ASSERT_TRUE( pending.has_value() );
    EXPECT_EQ( std::count( pending.value().begin(), pending.value().end(), cid ), 1 );

    auto drained = DrainPendingUploads( *shared_local_, *remote_ );
    ASSERT_TRUE( drained.has_value() );
    EXPECT_EQ( drained.value().size(), 1u );
    auto state = shared_local_->GetUploadState( cid );
    ASSERT_TRUE( state.has_value() );
    EXPECT_EQ( state.value(), UploadState::UploadedAccepted );

    // Re-put of the drained block: still succeeds, journal NOT regressed.
    auto re_put = decorator_->set( cid_key, IpfsDatastore::Value( SpanOf( payload ) ) );
    ASSERT_TRUE( re_put.has_value() );
    auto state_after = shared_local_->GetUploadState( cid );
    ASSERT_TRUE( state_after.has_value() );
    EXPECT_EQ( state_after.value(), UploadState::UploadedAccepted );
    auto pending_after = shared_local_->GetPendingUploads();
    ASSERT_TRUE( pending_after.has_value() );
    EXPECT_TRUE( pending_after.value().empty() );
}

/**
 * @given a decorated store with one journaled block
 * @when remove() runs at the seam
 * @then the inner (hot) store no longer contains the block while the cold
 *        store still returns its bytes and the journal record is intact
 *        (eviction is not CRDT deletion).
 */
TEST_F( ColdStorageSpillTest, remove_is_hot_only )
{
    const std::string payload = "remove hot-only payload";
    const auto        cid     = MakeCid( payload );
    CID               cid_key( cid );

    auto put = decorator_->set( cid_key, IpfsDatastore::Value( SpanOf( payload ) ) );
    ASSERT_TRUE( put.has_value() );

    auto removed = decorator_->remove( cid_key );
    ASSERT_TRUE( removed.has_value() );

    auto hot = inner_->contains( cid_key );
    ASSERT_TRUE( hot.has_value() );
    EXPECT_FALSE( hot.value() );

    auto cold = shared_local_->Get( cid );
    ASSERT_TRUE( cold.has_value() );
    EXPECT_EQ( cold.value().bytes, std::vector<uint8_t>( payload.begin(), payload.end() ) );

    auto state = shared_local_->GetUploadState( cid );
    ASSERT_TRUE( state.has_value() );
    EXPECT_EQ( state.value(), UploadState::PendingUpload );
}

/**
 * @given a CID whose multihash type (blake2s-256) is outside the library's
 *        accepted set {sha-256, blake2b-256}
 * @when its set() passes the decorator seam
 * @then set() returns a loud failure and the inner fake does NOT contain
 *        the block - a hostile CID never terminates the node (D-04 +
 *        Pitfall 4 defensive catch).
 */
TEST_F( ColdStorageSpillTest, journal_failure_fails_put_loudly )
{
    // Out-of-set multihash: blake2s-256 with fabricated digest bytes (the
    // vendored HashType set carries no sha3 member - blake2s256 is the
    // in-tree out-of-set choice).
    std::array<uint8_t, 32> digest{};
    digest.fill( 0xAB );
    auto multihash = libp2p::multi::Multihash::create( libp2p::multi::HashType::blake2s256,
                                                       gsl::span<const uint8_t>( digest.data(), digest.size() ) );
    ASSERT_TRUE( multihash.has_value() );
    CID hostile_cid( libp2p::multi::ContentIdentifier::Version::V1,
                     CID::Multicodec::RAW,
                     multihash.value() );

    const std::string payload = "hostile cid payload";
    auto res = decorator_->set( hostile_cid, IpfsDatastore::Value( SpanOf( payload ) ) );
    EXPECT_TRUE( res.has_error() );

    auto hot = inner_->contains( hostile_cid );
    ASSERT_TRUE( hot.has_value() );
    EXPECT_FALSE( hot.value() );
}

/**
 * @given a zero-length block and its correct content CID
 * @when its set() passes the decorator seam
 * @then the journal accepts it without error and it appears in the pending
 *        set (empty edge; its drain half is proven in 08-03).
 */
TEST_F( ColdStorageSpillTest, zero_length_block_journals )
{
    const auto cid = CidOf( gsl::span<const uint8_t>(), MulticodecType::Code::RAW );
    ASSERT_TRUE( cid.has_value() );
    CID cid_key( cid.value() );

    auto res = decorator_->set( cid_key, IpfsDatastore::Value() );
    ASSERT_TRUE( res.has_value() );

    auto cold = shared_local_->Get( cid.value() );
    ASSERT_TRUE( cold.has_value() );
    EXPECT_TRUE( cold.value().bytes.empty() );

    auto state = shared_local_->GetUploadState( cid.value() );
    ASSERT_TRUE( state.has_value() );
    EXPECT_EQ( state.value(), UploadState::PendingUpload );

    auto pending = shared_local_->GetPendingUploads();
    ASSERT_TRUE( pending.has_value() );
    EXPECT_EQ( std::count( pending.value().begin(), pending.value().end(), cid.value() ), 1 );
}


// ===========================================================================
// Drainer behavior suite (SPILL-02 evidence, v1.1 Phase 8 plan 08-03):
// the manager is constructed DIRECTLY over the fixture's real
// LocalBlockStore journal + InMemoryS3Client fake (D-10) - the same
// assembly the factory builds. All waits are bounded poll loops.
// ===========================================================================

/**
 * @given 10 blocks journaled through the decorator
 * @when the manager starts and drains
 * @then pending reaches 0, the fake S3 returns all blocks byte-identical
 *        via the S3BlockStore Get path, every journal record is
 *        UploadedAccepted, and NO record reaches UploadedDurable
 *        (Phase-10 boundary).
 */
TEST_F( ColdStorageSpillTest, drain_moves_journal_to_s3 )
{
    std::vector<std::string> payloads;
    std::vector<Cid>         cids;
    for ( int i = 0; i < 10; ++i )
    {
        payloads.push_back( "drain behavior payload " + std::to_string( i ) );
        cids.push_back( MakeCid( payloads.back() ) );
        CID cid_key( cids.back() );
        auto res = decorator_->set( cid_key, IpfsDatastore::Value( SpanOf( payloads.back() ) ) );
        ASSERT_TRUE( res.has_value() );
    }

    auto manager = MakeManager( FastOptions() );
    manager->Start();
    // drainer_healthy is false until the FIRST completed cycle - requiring
    // it stops the poll from passing on the initial atomic value.
    ASSERT_TRUE( PollUntil( [&] {
        const auto status = manager->GetStatus();
        return status.pending_upload_count == 0 && status.drainer_healthy;
    },
                            std::chrono::milliseconds( 10000 ) ) )
        << "pending must drain to 0 within the deadline";
    manager->Stop();

    for ( size_t i = 0; i < payloads.size(); ++i )
    {
        auto got = remote_->Get( cids[i] );
        ASSERT_TRUE( got.has_value() ) << "remote must hold block " << i;
        EXPECT_EQ( got.value().bytes, std::vector<uint8_t>( payloads[i].begin(), payloads[i].end() ) );
        auto state = shared_local_->GetUploadState( cids[i] );
        ASSERT_TRUE( state.has_value() );
        EXPECT_EQ( state.value(), UploadState::UploadedAccepted );
        EXPECT_NE( state.value(), UploadState::UploadedDurable ); // Phase-10 boundary
    }
    EXPECT_EQ( fake_->ObjectKeys().size(), 10u );
}

/**
 * @given blocks journaled with NO manager ever started (crash shape)
 * @when a SECOND store + manager open over the SAME cold-store directory
 *        with a fresh fake pre-loaded with one of the blocks
 * @then everything drains to exactly N distinct remote objects - nothing
 *        lost, nothing duplicated (D-12/SC3 createOnly convergence).
 */
TEST_F( ColdStorageSpillTest, crash_restart_reconciles )
{
    constexpr size_t kBlocks = 5;
    const std::string dir = FreshDir();
    auto               first_open = LocalBlockStore::Open( LocalBlockStoreOptions{ dir } );
    ASSERT_TRUE( first_open.has_value() );
    std::vector<std::string> payloads;
    std::vector<Cid>         cids;
    for ( size_t i = 0; i < kBlocks; ++i )
    {
        payloads.push_back( "crash resume payload " + std::to_string( i ) );
        cids.push_back( MakeCid( payloads.back() ) );
        auto put = first_open.value()->Put( cids.back(), SpanOf( payloads.back() ) );
        ASSERT_TRUE( put.has_value() );
    }
    // Simulated death: release the store object in a scope - the destructor
    // closes the DB and frees the directory LOCK (the library tests' own
    // crash shape; the WAL tail replays on reopen either way).
    {
        auto crashed = std::move( first_open.value() );
    }

    // Reopen the SAME directory; fresh fake PRE-LOADED with one block.
    auto reopened = LocalBlockStore::Open( LocalBlockStoreOptions{ dir } );
    ASSERT_TRUE( reopened.has_value() );
    auto shared_reopened = std::shared_ptr<LocalBlockStore>( std::move( reopened.value() ) );
    auto fake2           = std::make_shared<InMemoryS3Client>();
    auto opened_remote2  = S3BlockStore::Open( fake2 );
    ASSERT_TRUE( opened_remote2.has_value() );
    auto remote2 = std::shared_ptr<sgns::blockstore::S3BlockStore>( std::move( opened_remote2.value() ) );
    auto preloaded = remote2->Put( cids[0], SpanOf( payloads[0] ) );
    ASSERT_TRUE( preloaded.has_value() );

    ColdStorageManager::BackpressureOptions options = FastOptions();
    auto manager = std::make_shared<ColdStorageManager>( shared_reopened, remote2, cold_store_mutex_, options );
    manager->Start();
    ASSERT_TRUE( PollUntil( [&] {
        const auto status = manager->GetStatus();
        return status.pending_upload_count == 0 && status.drainer_healthy;
    },
                            std::chrono::milliseconds( 10000 ) ) );
    manager->Stop();

    // Exactly N distinct remote objects - nothing lost, nothing duplicated.
    EXPECT_EQ( fake2->ObjectKeys().size(), kBlocks );
    for ( size_t i = 0; i < kBlocks; ++i )
    {
        auto state = shared_reopened->GetUploadState( cids[i] );
        ASSERT_TRUE( state.has_value() );
        EXPECT_EQ( state.value(), UploadState::UploadedAccepted );
    }
}

/**
 * @given a fault-injected fake (S3 Put unavailable) and a manager with
 *        warn_pending=5, critical_pending=10
 * @when 12 decorator sets run during the outage, the fake then heals
 * @then EVERY set succeeds during the outage (D-01/SC4 - the write path
 *        never fails due to REMOTE cold-store state), the watermark
 *        crosses Warn then Critical while the journal grows, and after
 *        healing everything drains with the watermark recovering to None.
 */
TEST_F( ColdStorageSpillTest, s3d_down_window_keeps_publishing )
{
    fake_->SetFault( sgns::blockstore::S3Op::Put, sgns::blockstore::S3FaultKind::Unavailable );
    auto manager = MakeManager( FastOptions( 100, 5, 10 ) );
    manager->Start();

    // First six sets: crosses Warn (>=5, <10) observably.
    for ( int i = 0; i < 6; ++i )
    {
        const std::string payload = "outage window payload " + std::to_string( i );
        CID               cid_key( MakeCid( payload ) );
        auto res = decorator_->set( cid_key, IpfsDatastore::Value( SpanOf( payload ) ) );
        ASSERT_TRUE( res.has_value() ) << "set() must succeed during the outage";
    }
    ASSERT_TRUE( PollUntil( [&] { return manager->GetStatus().watermark == ColdStorageWatermark::Warn; },
                            std::chrono::milliseconds( 10000 ) ) )
        << "watermark must reach Warn";

    // Six more: past critical.
    for ( int i = 6; i < 12; ++i )
    {
        const std::string payload = "outage window payload " + std::to_string( i );
        CID               cid_key( MakeCid( payload ) );
        auto res = decorator_->set( cid_key, IpfsDatastore::Value( SpanOf( payload ) ) );
        ASSERT_TRUE( res.has_value() ) << "set() must keep succeeding deep into the outage";
    }
    ASSERT_TRUE( PollUntil( [&] { return manager->GetStatus().watermark == ColdStorageWatermark::Critical; },
                            std::chrono::milliseconds( 10000 ) ) )
        << "watermark must reach Critical (unbounded journal, alarm only - D-01)";
    EXPECT_FALSE( manager->GetStatus().drainer_healthy );

    // Heal: everything drains, watermark recovers to None (down-crossing).
    fake_->ClearFaults();
    ASSERT_TRUE( PollUntil( [&] {
        const auto status = manager->GetStatus();
        return status.pending_upload_count == 0 && status.watermark == ColdStorageWatermark::None;
    },
                            std::chrono::milliseconds( 10000 ) ) )
        << "post-heal catch-up must drain fully and recover the watermark";
    manager->Stop();
}

/**
 * @given a manager at drain_interval_ms=60000 (effectively paused) and one
 *        journaled block
 * @when RequestDrain() runs
 * @then it returns immediately (well under 1 s) and the pending set
 *        reaches 0 within 10 s - the drain body never ran on the test
 *        thread (SC2 enqueue-never-inline).
 */
TEST_F( ColdStorageSpillTest, ops_trigger_enqueues_never_inline )
{
    const std::string payload = "ops trigger payload";
    CID               cid_key( MakeCid( payload ) );
    auto              put = decorator_->set( cid_key, IpfsDatastore::Value( SpanOf( payload ) ) );
    ASSERT_TRUE( put.has_value() );

    auto manager = MakeManager( FastOptions( 60000 ) );
    manager->Start();
    const auto t0      = std::chrono::steady_clock::now();
    manager->RequestDrain();
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    EXPECT_LT( elapsed, std::chrono::seconds( 1 ) ) << "RequestDrain must be enqueue-only";

    ASSERT_TRUE( PollUntil( [&] {
        const auto status = manager->GetStatus();
        return status.pending_upload_count == 0 && status.drainer_healthy;
    },
                            std::chrono::milliseconds( 10000 ) ) )
        << "the enqueued drain must wake the drainer thread";
    manager->Stop();
}

/**
 * @given a manager with in-flight pending work over a healthy fake
 * @when Stop() runs, then a FRESH manager opens over the same directory
 * @then Stop returns within the grace window plus margin (15 s) and any
 *        journal leftovers resume and drain fully under the fresh manager
 *        - one resume path serves crash and clean stop (D-11/SC3).
 */
TEST_F( ColdStorageSpillTest, shutdown_grace_joins )
{
    std::vector<Cid> cids;
    for ( int i = 0; i < 3; ++i )
    {
        const std::string payload = "shutdown grace payload " + std::to_string( i );
        cids.push_back( MakeCid( payload ) );
        CID cid_key( cids.back() );
        auto res = decorator_->set( cid_key, IpfsDatastore::Value( SpanOf( payload ) ) );
        ASSERT_TRUE( res.has_value() );
    }

    auto manager = MakeManager( FastOptions( 60000 ) ); // long wait, work stays pending
    manager->Start();
    const auto t0      = std::chrono::steady_clock::now();
    manager->Stop();
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    EXPECT_LT( elapsed, std::chrono::seconds( 15 ) ) << "grace (5 s) + margin";

    // Whatever remains resumes under a fresh manager (same store+remote).
    auto fresh = MakeManager( FastOptions() );
    fresh->Start();
    ASSERT_TRUE( PollUntil( [&] {
        const auto status = fresh->GetStatus();
        return status.pending_upload_count == 0 && status.drainer_healthy;
    },
                            std::chrono::milliseconds( 10000 ) ) );
    fresh->Stop();
    for ( const auto &cid : cids )
    {
        auto state = shared_local_->GetUploadState( cid );
        ASSERT_TRUE( state.has_value() );
        EXPECT_EQ( state.value(), UploadState::UploadedAccepted );
    }
}

/**
 * @given a single-block journal plus the zero-length block from the 08-01
 *        empty edge
 * @when the manager drains
 * @then both reach the remote and the pending set empties without error
 *        (degenerate-input drain half).
 */
TEST_F( ColdStorageSpillTest, single_block_journal_drains )
{
    const std::string payload = "single degenerate payload";
    const auto        cid     = MakeCid( payload );
    CID               cid_key( cid );
    ASSERT_TRUE( decorator_->set( cid_key, IpfsDatastore::Value( SpanOf( payload ) ) ).has_value() );
    const auto empty_cid = CidOf( gsl::span<const uint8_t>(), MulticodecType::Code::RAW );
    ASSERT_TRUE( empty_cid.has_value() );
    CID empty_key( empty_cid.value() );
    ASSERT_TRUE( decorator_->set( empty_key, IpfsDatastore::Value() ).has_value() );

    auto manager = MakeManager( FastOptions() );
    manager->Start();
    ASSERT_TRUE( PollUntil( [&] {
        const auto status = manager->GetStatus();
        return status.pending_upload_count == 0 && status.drainer_healthy;
    },
                            std::chrono::milliseconds( 10000 ) ) );
    manager->Stop();

    auto pending = shared_local_->GetPendingUploads();
    ASSERT_TRUE( pending.has_value() );
    EXPECT_TRUE( pending.value().empty() );
    EXPECT_TRUE( remote_->Get( cid ).has_value() );
    EXPECT_TRUE( remote_->Get( empty_cid.value() ).has_value() );
}
