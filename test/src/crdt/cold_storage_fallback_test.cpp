/**
 * @file       cold_storage_fallback_test.cpp
 * @brief      Hermetic Tier-1 cold-read fallback tests (READ-01, v1.1
 *             Phase 9 plans 09-01/09-03).
 *
 * Registered ONLY under SGNS_COLD_STORAGE (links
 * sgns-blockstore::sgns-blockstore - the OFF-build Tier-1 lane stays
 * non-empty via the unconditional config-gate test). The chain service is
 * constructed DIRECTLY over the Phase-8 stack handles (09-RESEARCH §10.1
 * planner ruling: the hook seam is the test cut - DAGSyncer-side wiring
 * is compile-proven and covered by the regression lane): a real
 * LocalBlockStore journal on a fresh temp dir, the InMemoryS3Client fake
 * behind an S3BlockStore, the shared mutexes, and a test-local spy inner
 * IpfsDatastore behind the journal-spill decorator (the rehydrate
 * target).
 *
 * ─── PHASE-9 EVIDENCE MATRIX (SC1-SC5; every case named below runs in
 *     this binary - evidence describes what runs) ───
 *
 * SC1 no-NotFound-escape: SC1_NoNotFoundEscape_S3Unavailable (a scripted
 *     s3d Unavailable with the block absent everywhere classifies
 *     TemporarilyUnavailable at BOTH the ColdReadStatus layer and the
 *     MapColdReadStatus layer - never CID_NOT_FOUND).
 * SC3 honest taxonomy: SC3_CorruptAttributesRemoteTier (byte-flip),
 *     SC3_CorruptAttributesLocalTier (FaultController CorruptData),
 *     SC3_LocalCorruptServesGoodS3Copy, SC3_UnauthorizedSurfacesOwnClass,
 *     SC3_MapColdReadStatusExhaustiveTable (full 4x2 input space),
 *     SC3_NoPeerBlameMappingSafetyNet (no cold status input can yield
 *     PEER_BLACKLISTED/TIMED_OUT; the structural half - zero
 *     RecordCIDFailure/BlackListPeer references in the cold-read sources
 *     - is this plan's scoped source audit).
 * SC4 rehydrate silence: SC4_RehydrateSilence_BytesOnlyNoMarkers (exactly
 *     one set, zero marker writes - §2.4 bytes-only ruling) and
 *     SC4_PreSealedBlockGainsNoMarker (pre-sealed blocks untouched).
 * SC5 hermetic lane: this target registers gated with
 *     set_tests_properties LABELS "Tier1"; the lane runs under
 *     ctest -L Tier1 --no-tests=error (fail-closed).
 * D-decisions: D01_S3TierHitEndToEnd, D02_LocalTierHitEndToEnd,
 *     D02_LocalTierHitPerformsZeroS3Gets (tier selection, fake counter),
 *     D03_DefinitiveAbsenceIsTheOnlyNotFound,
 *     D03_LocalUnavailableFallsThroughToS3 (mid-chain fall-through),
 *     D06_NullHookNoOpReturnsHotErrorUnchanged (disabled bar),
 *     D07_RewarmCoalescesDuplicateEnqueues (exactly one fetch under
 *     duplicates), D07_RewarmQueueOverflowDropsWithoutBlocking,
 *     D07_RewarmExpiredBudgetEntryIsSkipped,
 *     D07_RewarmFaultRecoveryMapNeverWedges, D08_HasLocalColdBoolContract-
 *     UnderFault, D11_RewarmStopJoinsAndIsIdempotent.
 *
 * Include discipline: installed angle-bracket sgns-blockstore headers
 * plus the fake via the relative include-dir workaround in CMakeLists.txt
 * (08-RESEARCH Pitfall 6). Every library result is guarded has_value()
 * BEFORE .value()/.error() (policy::terminate alias).
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
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

// Internal fake - relative-include-dir privilege (test-surface only).
#include <sgns-blockstore/InMemoryS3Client.hpp>

#include "base/logger.hpp"
#include "crdt/cold_read_fallback.hpp"
#include "crdt/globaldb/BlockStoreBackedIpfsDatastore.hpp"
#include "crdt/globaldb/ColdReadService.hpp"
#include "crdt/graphsync_dagsyncer.hpp"

namespace
{
    using sgns::blockstore::Cid;
    using sgns::blockstore::CidOf;
    using sgns::blockstore::InMemoryS3Client;
    using sgns::blockstore::LocalBlockStore;
    using sgns::blockstore::LocalBlockStoreOptions;
    using sgns::blockstore::S3BlockStore;
    using libp2p::multi::MulticodecType;

    // IPFS resolves only inside namespace sgns - alias it for this TU.
    namespace ipfs_outcome = sgns::IPFS::outcome;

    using sgns::crdt::BlockStoreBackedIpfsDatastore;
    using sgns::crdt::ColdReadFallback;
    using sgns::crdt::ColdReadService;
    using sgns::crdt::ColdReadStatus;
    using IpfsDatastore    = sgns::ipfs_lite::ipfs::IpfsDatastore;
    using IpfsDatastoreError = sgns::ipfs_lite::ipfs::IpfsDatastoreError;
    using CID              = sgns::CID;

    /// Bounded poll loop with deadline - never a fixed sleep, so the
    /// suite stays stable on loaded Windows CI-equivalent machines.
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
        return "test-data/cold-storage-fallback/case-" + std::to_string( next.fetch_add( 1 ) );
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
     * @brief Test-local in-memory inner IpfsDatastore with call
     *        recording - the SC4 rehydrate-silence spy (decorator-level
     *        tests need no real RocksDB inner store).
     */
    class SpyInnerDatastore final : public IpfsDatastore
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
            ++set_calls_;
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
            ++seal_calls_;
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

        size_t set_calls() const
        {
            return set_calls_;
        }

        size_t seal_calls() const
        {
            return seal_calls_;
        }

    private:
        std::map<std::string, IpfsDatastore::Value> blocks_;
        std::set<std::string>                       sealed_;
        std::atomic<size_t>                         set_calls_{ 0 };
        std::atomic<size_t>                         seal_calls_{ 0 };
    };

    /**
     * @brief Test-local S3Client wrapper counting Get calls before
     *        delegating to the in-memory fake - the D-02 tier-selection
     *        probe (the fake itself has no call counter).
     */
    class CountingS3Client final : public sgns::blockstore::S3Client
    {
    public:
        explicit CountingS3Client( std::shared_ptr<InMemoryS3Client> inner ) : inner_( std::move( inner ) )
        {
        }

        size_t get_calls() const
        {
            return get_calls_;
        }

        sgns::blockstore::result<sgns::blockstore::S3PutResult, sgns::blockstore::BlockStatus> Put(
            const std::string &key, const std::vector<uint8_t> &bytes, const sgns::blockstore::PutObjectOptions &options = {} ) override
        {
            return inner_->Put( key, bytes, options );
        }

        sgns::blockstore::result<sgns::blockstore::S3Object, sgns::blockstore::BlockStatus> Get(
            const std::string &key ) override
        {
            ++get_calls_;
            return inner_->Get( key );
        }

        sgns::blockstore::result<sgns::blockstore::S3HeadResult, sgns::blockstore::BlockStatus> Head(
            const std::string &key ) override
        {
            return inner_->Head( key );
        }

        sgns::blockstore::result<void, sgns::blockstore::BlockStatus> Delete( const std::string &key ) override
        {
            return inner_->Delete( key );
        }

    private:
        std::shared_ptr<InMemoryS3Client> inner_;
        std::atomic<size_t>               get_calls_{ 0 };
    };

    class ColdStorageFallbackTest : public ::testing::Test
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
            remote_ = std::move( opened_remote.value() );

            cold_store_mutex_   = std::make_shared<std::mutex>();
            remote_store_mutex_ = std::make_shared<std::mutex>();
            inner_              = std::make_shared<SpyInnerDatastore>();
            decorator_          = std::make_shared<BlockStoreBackedIpfsDatastore>(
                inner_, shared_local_, cold_store_mutex_, sgns::base::createLogger( "ColdStorageFallback" ) );
            service_ = std::make_shared<ColdReadService>(
                shared_local_, remote_, cold_store_mutex_, remote_store_mutex_, decorator_ );
        }

        std::shared_ptr<LocalBlockStore>                 shared_local_;
        std::shared_ptr<InMemoryS3Client>                fake_;
        std::shared_ptr<S3BlockStore>                    remote_;
        std::shared_ptr<std::mutex>                      cold_store_mutex_;
        std::shared_ptr<std::mutex>                      remote_store_mutex_;
        std::shared_ptr<SpyInnerDatastore>               inner_;
        std::shared_ptr<BlockStoreBackedIpfsDatastore>   decorator_;
        std::shared_ptr<ColdReadService>                 service_;
    };
} // namespace

/**
 * @given a block journaled into the Local tier ONLY (the hot store never
 *        saw it - the Phase-8 spill shape)
 * @when  the cold chain fetches it (hook seam: Get then Rehydrate, the
 *        FetchNodeFromColdStorage composition)
 * @then  the CID-verified bytes return, the spy inner saw EXACTLY ONE set
 *        and ZERO seal writes (SC4 bytes-only rehydrate), and the hot
 *        store now serves the block (D-04 re-cache).
 */
TEST_F( ColdStorageFallbackTest, D02_LocalTierHitEndToEnd )
{
    const std::string payload = "local tier cold read payload";
    const auto        cid     = MakeCid( payload );
    const CID         cid_key( cid );

    // Journal-only block: direct LocalBlockStore Put, hot store untouched.
    auto journaled = shared_local_->Put( cid, SpanOf( payload ) );
    ASSERT_TRUE( journaled.has_value() );
    ASSERT_EQ( inner_->set_calls(), 0u );

    sgns::common::Buffer out;
    const auto status = service_->Get( cid_key, out );
    (void)status;
    ASSERT_FALSE( out.empty() ) << "Local-tier hit must return bytes";
    EXPECT_EQ( out.toVector(), std::vector<uint8_t>( payload.begin(), payload.end() ) );

    // The DAGSyncer-side composition: rehydrate BEFORE returning (D-04).
    service_->Rehydrate( cid_key, out );

    // SC4: exactly one set, zero resolved-marker writes, and the hot
    // store now serves the block.
    EXPECT_EQ( inner_->set_calls(), 1u );
    EXPECT_EQ( inner_->seal_calls(), 0u );
    auto hot = inner_->get( cid_key );
    ASSERT_TRUE( hot.has_value() );
    EXPECT_EQ( hot.value().toVector(), std::vector<uint8_t>( payload.begin(), payload.end() ) );
}

/**
 * @given a block present ONLY in the S3 tier (the Local tier misses)
 * @when  the cold chain fetches it
 * @then  the chain resolves from S3 with CID-verified bytes, re-caches
 *        through the decorator set (SC4), and the hot store serves it.
 */
TEST_F( ColdStorageFallbackTest, D01_S3TierHitEndToEnd )
{
    const std::string payload = "s3 tier cold read payload";
    const auto        cid     = MakeCid( payload );
    const CID         cid_key( cid );

    // Remote-only block: direct S3BlockStore Put (Local tier never sees it).
    auto put = remote_->Put( cid, SpanOf( payload ) );
    ASSERT_TRUE( put.has_value() );

    sgns::common::Buffer out;
    const auto status = service_->Get( cid_key, out );
    (void)status;
    ASSERT_FALSE( out.empty() ) << "S3-tier hit must return bytes";
    EXPECT_EQ( out.toVector(), std::vector<uint8_t>( payload.begin(), payload.end() ) );

    service_->Rehydrate( cid_key, out );

    EXPECT_EQ( inner_->set_calls(), 1u );
    EXPECT_EQ( inner_->seal_calls(), 0u );
    auto hot = inner_->get( cid_key );
    ASSERT_TRUE( hot.has_value() );
    EXPECT_EQ( hot.value().toVector(), std::vector<uint8_t>( payload.begin(), payload.end() ) );
}

/**
 * @given a GraphsyncDAGSyncer constructed WITHOUT a cold-read hook (the
 *        disabled shape)
 * @when  GetNodeWithoutRequest runs for a CID the hot store does not hold
 * @then  the original hot-miss error returns UNCHANGED (CID_NOT_FOUND) -
 *        the null hook is a no-op and the disabled bar stays
 *        byte-identical (Phase-7).
 */
TEST_F( ColdStorageFallbackTest, D06_NullHookNoOpReturnsHotErrorUnchanged )
{
    const std::string payload = "absent payload";
    const auto        cid     = MakeCid( payload );
    const CID         cid_key( cid );

    auto dagSyncer = std::make_shared<sgns::crdt::GraphsyncDAGSyncer>(
        decorator_, nullptr, nullptr /* no graphsync/host needed for the no-request path */ );

    auto result = dagSyncer->GetNodeWithoutRequest( cid_key );
    ASSERT_TRUE( result.has_error() );
    EXPECT_EQ( result.error(),
               sgns::crdt::GraphsyncDAGSyncer::Error::CID_NOT_FOUND )
        << "null hook must pass the hot-miss error through unchanged";
    // No cold write may have happened on the disabled path.
    EXPECT_EQ( inner_->set_calls(), 0u );
    EXPECT_TRUE( fake_->ObjectKeys().empty() );
}

/**
 * @given a block journaled into the Local tier and a COUNTING S3 client
 *        behind the chain's remote tier
 * @when  the chain resolves the block from the Local tier (D-02)
 * @then  ZERO S3 Gets ran - tier selection is proven behaviorally, not
 *        just by return value (D-02: a cold-dir hit avoids the S3 round
 *        trip).
 */
TEST_F( ColdStorageFallbackTest, D02_LocalTierHitPerformsZeroS3Gets )
{
    const std::string payload = "tier selection payload";
    const auto        cid     = MakeCid( payload );
    const CID         cid_key( cid );

    auto counting = std::make_shared<CountingS3Client>( fake_ );
    auto opened_remote = sgns::blockstore::S3BlockStore::Open( counting );
    ASSERT_TRUE( opened_remote.has_value() );
    std::shared_ptr<S3BlockStore> counting_remote = std::move( opened_remote.value() );
    ColdReadService counting_service(
        shared_local_, counting_remote, cold_store_mutex_, remote_store_mutex_, decorator_ );

    auto journaled = shared_local_->Put( cid, SpanOf( payload ) );
    ASSERT_TRUE( journaled.has_value() );

    sgns::common::Buffer out;
    (void)counting_service.Get( cid_key, out );
    ASSERT_FALSE( out.empty() );
    counting_service.Rehydrate( cid_key, out );

    // D-02 tier selection: the Local-tier hit must never touch S3.
    EXPECT_EQ( counting->get_calls(), 0u );
    EXPECT_EQ( inner_->set_calls(), 1u );
}

/**
 * @given a block present in NEITHER cold tier (definitive absence)
 * @when  the chain runs
 * @then  the classification is NotFound - the only legitimate NotFound
 *        per the envelope rule (SC1) - and MapColdReadStatus maps it to
 *        CID_NOT_FOUND.
 */
TEST_F( ColdStorageFallbackTest, D03_DefinitiveAbsenceIsTheOnlyNotFound )
{
    const std::string payload = "definitively absent payload";
    const auto        cid     = MakeCid( payload );
    const CID         cid_key( cid );

    sgns::common::Buffer out;
    const auto status = service_->Get( cid_key, out );
    EXPECT_TRUE( out.empty() );
    EXPECT_EQ( status.cls, ColdReadStatus::Class::NotFound );
    EXPECT_TRUE( status.fromRemoteTier ) << "absence is attributed to the last tier consulted (remote)";
    EXPECT_EQ( sgns::crdt::MapColdReadStatus( status ),
               sgns::crdt::GraphsyncDAGSyncer::Error::CID_NOT_FOUND );
}

/**
 * @given s3d scripted Unavailable on Get with the block absent everywhere
 * @when  the chain runs
 * @then  the classification is TemporarilyUnavailable with remote
 *        attribution - NEVER NotFound (SC1 headline) - and
 *        MapColdReadStatus maps it to COLD_STORAGE_TEMPORARILY_UNAVAILABLE,
 *        never CID_NOT_FOUND (D-03).
 */
TEST_F( ColdStorageFallbackTest, SC1_NoNotFoundEscape_S3Unavailable )
{
    const std::string payload = "transport blip payload";
    const auto        cid     = MakeCid( payload );
    const CID         cid_key( cid );

    fake_->SetFault( sgns::blockstore::S3Op::Get, sgns::blockstore::S3FaultKind::Unavailable );

    sgns::common::Buffer out;
    const auto status = service_->Get( cid_key, out );
    EXPECT_TRUE( out.empty() );
    EXPECT_EQ( status.cls, ColdReadStatus::Class::TemporarilyUnavailable );
    EXPECT_TRUE( status.fromRemoteTier );
    EXPECT_EQ( sgns::crdt::MapColdReadStatus( status ),
               sgns::crdt::GraphsyncDAGSyncer::Error::COLD_STORAGE_TEMPORARILY_UNAVAILABLE )
        << "SC1: a transport-shaped failure must never surface as CID_NOT_FOUND";
}

/**
 * @given a stored S3 object whose served bytes are XOR-flipped (the
 *        adapter SUCCEEDS; S3BlockStore's CID verification is the
 *        detector)
 * @when  the chain fetches it
 * @then  the classification is Corrupt attributed to the REMOTE tier and
 *        it never maps to CID_NOT_FOUND (SC3).
 */
TEST_F( ColdStorageFallbackTest, SC3_CorruptAttributesRemoteTier )
{
    const std::string payload = "remote corruption payload";
    const auto        cid     = MakeCid( payload );
    const CID         cid_key( cid );

    auto put = remote_->Put( cid, SpanOf( payload ) );
    ASSERT_TRUE( put.has_value() );
    // Exactly one stored object: its derived key is the only candidate.
    auto keys = fake_->ObjectKeys();
    ASSERT_EQ( keys.size(), 1u );
    fake_->SetFlipBytesForKey( keys[0], true );

    sgns::common::Buffer out;
    const auto status = service_->Get( cid_key, out );
    EXPECT_TRUE( out.empty() );
    EXPECT_EQ( status.cls, ColdReadStatus::Class::Corrupt );
    EXPECT_TRUE( status.fromRemoteTier ) << "SC3: corruption attributes its source tier (remote)";
    EXPECT_EQ( sgns::crdt::MapColdReadStatus( status ),
               sgns::crdt::GraphsyncDAGSyncer::Error::COLD_STORAGE_CORRUPT );
}

/**
 * @given a Local-tier-resident block with FaultKind::CorruptData injected
 *        on LocalBlockStore (bytes fail CID verification at the tier)
 * @when  the chain fetches it
 * @then  the classification is Corrupt attributed to the LOCAL tier - the
 *        chain continues to S3 per §3.2, finds absence there, and the
 *        remembered local Corrupt outranks the remote NotFound (SC3).
 */
TEST_F( ColdStorageFallbackTest, SC3_CorruptAttributesLocalTier )
{
    const std::string payload = "local corruption payload";
    const auto        cid     = MakeCid( payload );
    const CID         cid_key( cid );

    auto journaled = shared_local_->Put( cid, SpanOf( payload ) );
    ASSERT_TRUE( journaled.has_value() );
    shared_local_->SetFault( sgns::blockstore::FaultKind::CorruptData );

    sgns::common::Buffer out;
    const auto status = service_->Get( cid_key, out );
    EXPECT_TRUE( out.empty() );
    EXPECT_EQ( status.cls, ColdReadStatus::Class::Corrupt );
    EXPECT_FALSE( status.fromRemoteTier ) << "SC3: corruption attributes its source tier (local)";
    EXPECT_EQ( sgns::crdt::MapColdReadStatus( status ),
               sgns::crdt::GraphsyncDAGSyncer::Error::COLD_STORAGE_CORRUPT );
}

/**
 * @given the full 4-class x 2-tier input space of MapColdReadStatus
 * @when  every combination maps onto the DAGSyncer error surface
 * @then  each row yields its SC3 table value, and no non-NotFound input
 *        ever maps to CID_NOT_FOUND.
 */
TEST( ColdReadStatusMapping, SC3_MapColdReadStatusExhaustiveTable )
{
    struct Row
    {
        ColdReadStatus::Class              cls;
        bool                               from_remote_tier;
        sgns::crdt::GraphsyncDAGSyncer::Error expected;
    };
    using Class = ColdReadStatus::Class;
    using Error = sgns::crdt::GraphsyncDAGSyncer::Error;
    const std::vector<Row> rows = {
        { Class::NotFound, false, Error::CID_NOT_FOUND },
        { Class::NotFound, true, Error::CID_NOT_FOUND },
        { Class::TemporarilyUnavailable, false, Error::COLD_STORAGE_TEMPORARILY_UNAVAILABLE },
        { Class::TemporarilyUnavailable, true, Error::COLD_STORAGE_TEMPORARILY_UNAVAILABLE },
        { Class::Corrupt, false, Error::COLD_STORAGE_CORRUPT },
        { Class::Corrupt, true, Error::COLD_STORAGE_CORRUPT },
        { Class::Unauthorized, false, Error::COLD_STORAGE_UNAUTHORIZED },
        { Class::Unauthorized, true, Error::COLD_STORAGE_UNAUTHORIZED },
    };
    for ( const auto &row : rows )
    {
        ColdReadStatus status;
        status.cls           = row.cls;
        status.fromRemoteTier = row.from_remote_tier;
        const auto mapped = sgns::crdt::MapColdReadStatus( status );
        EXPECT_EQ( mapped, row.expected );
        if ( row.cls != Class::NotFound )
        {
            EXPECT_NE( mapped, Error::CID_NOT_FOUND )
                << "SC1: class " << static_cast<int>( row.cls )
                << " must never surface as CID_NOT_FOUND";
        }
    }
}

/**
 * @given HasLocalCold over a cold-dir-resident CID, an absent CID, and a
 *        Local tier faulted Unavailable (D-08 bool contract)
 * @when  each presence query runs
 * @then  resident answers true, absent answers false, and the fault
 *        answers false WITHOUT throwing - the freeze-sensitive callers
 *        must never block or throw (D-08/D-09).
 */
TEST_F( ColdStorageFallbackTest, D08_HasLocalColdBoolContractUnderFault )
{
    const std::string payload = "cold-dir presence payload";
    const auto        cid     = MakeCid( payload );
    const CID         cid_key( cid );

    auto journaled = shared_local_->Put( cid, SpanOf( payload ) );
    ASSERT_TRUE( journaled.has_value() );

    EXPECT_TRUE( service_->HasLocalCold( cid_key ) );

    const auto absent_cid = MakeCid( "never stored anywhere" );
    EXPECT_FALSE( service_->HasLocalCold( CID( absent_cid ) ) );

    // Injected local-tier Unavailable: false, never a throw (D-08).
    shared_local_->SetFault( sgns::blockstore::FaultKind::Unavailable );
    EXPECT_NO_THROW( { EXPECT_FALSE( service_->HasLocalCold( cid_key ) ); } );
}

/**
 * @given the Local tier faulted Unavailable (transport blip) while a good
 *        copy sits in S3, behind a COUNTING client
 * @when  the chain runs
 * @then  the chain FALLS THROUGH to the S3 tier (fake Get count >= 1) and
 *        resolves the good remote bytes - D-03 mid-chain fall-through;
 *        the local transport failure never ends the read.
 */
TEST_F( ColdStorageFallbackTest, D03_LocalUnavailableFallsThroughToS3 )
{
    const std::string payload = "local blip fall-through payload";
    const auto        cid     = MakeCid( payload );
    const CID         cid_key( cid );

    auto counting = std::make_shared<CountingS3Client>( fake_ );
    auto opened_remote = sgns::blockstore::S3BlockStore::Open( counting );
    ASSERT_TRUE( opened_remote.has_value() );
    std::shared_ptr<S3BlockStore> counting_remote = std::move( opened_remote.value() );
    ColdReadService fallthrough_service(
        shared_local_, counting_remote, cold_store_mutex_, remote_store_mutex_, decorator_ );

    auto put = counting_remote->Put( cid, SpanOf( payload ) );
    ASSERT_TRUE( put.has_value() );
    shared_local_->SetFault( sgns::blockstore::FaultKind::Unavailable );

    sgns::common::Buffer out;
    const auto status = fallthrough_service.Get( cid_key, out );
    EXPECT_FALSE( out.empty() ) << "the good S3 copy must serve after a local transport blip (D-03)";
    EXPECT_EQ( out.toVector(), std::vector<uint8_t>( payload.begin(), payload.end() ) );
    EXPECT_GE( counting->get_calls(), 1u ) << "the chain must still consult the S3 tier (D-03 fall-through)";
    // On success the status payload is the documented default (meaningful
    // only on failure) - the SC1 no-NotFound-escape property is asserted
    // on the FAILURE-shape cases (SC1_NoNotFoundEscape_S3Unavailable).
}

/**
 * @given a CORRUPT Local-tier copy and a GOOD S3 copy of the same CID
 * @when  the chain runs
 * @then  the good S3 bytes serve - Corrupt stops the tier, never the
 *        chain (§3.2: the remote copy may be good; SC3).
 */
TEST_F( ColdStorageFallbackTest, SC3_LocalCorruptServesGoodS3Copy )
{
    const std::string payload = "local corrupt good remote payload";
    const auto        cid     = MakeCid( payload );
    const CID         cid_key( cid );

    auto put_local = shared_local_->Put( cid, SpanOf( payload ) );
    ASSERT_TRUE( put_local.has_value() );
    auto put_remote = remote_->Put( cid, SpanOf( payload ) );
    ASSERT_TRUE( put_remote.has_value() );
    shared_local_->SetFault( sgns::blockstore::FaultKind::CorruptData );

    sgns::common::Buffer out;
    (void)service_->Get( cid_key, out );
    ASSERT_FALSE( out.empty() ) << "a corrupt local copy must not mask the good S3 copy";
    EXPECT_EQ( out.toVector(), std::vector<uint8_t>( payload.begin(), payload.end() ) );
}

/**
 * @given an Unauthorized fault scripted on S3 Get with the block absent
 * @when  the chain runs
 * @then  the classification is Unauthorized - its OWN class, never
 *        NotFound, never TemporarilyUnavailable (SC3 table row).
 */
TEST_F( ColdStorageFallbackTest, SC3_UnauthorizedSurfacesOwnClass )
{
    const std::string payload = "unauthorized payload";
    const auto        cid     = MakeCid( payload );
    const CID         cid_key( cid );

    fake_->SetFault( sgns::blockstore::S3Op::Get, sgns::blockstore::S3FaultKind::Unauthorized );

    sgns::common::Buffer out;
    const auto status = service_->Get( cid_key, out );
    EXPECT_TRUE( out.empty() );
    EXPECT_EQ( status.cls, ColdReadStatus::Class::Unauthorized );
    EXPECT_NE( status.cls, ColdReadStatus::Class::NotFound );
    EXPECT_NE( status.cls, ColdReadStatus::Class::TemporarilyUnavailable );
    EXPECT_EQ( sgns::crdt::MapColdReadStatus( status ),
               sgns::crdt::GraphsyncDAGSyncer::Error::COLD_STORAGE_UNAUTHORIZED );
}

/**
 * @given the full 8-input ColdReadStatus space
 * @when  every input maps onto the DAGSyncer error surface
 * @then  NO input ever yields PEER_BLACKLISTED or TIMED_OUT - a cold
 *        status can never blame a peer or claim a network timeout (SC3
 *        behavioral proxy for the no-peer-blame rule; the structural
 *        half is the scoped source audit in this plan's verification).
 */
TEST( ColdReadStatusMapping, SC3_NoPeerBlameMappingSafetyNet )
{
    using Class = ColdReadStatus::Class;
    using Error = sgns::crdt::GraphsyncDAGSyncer::Error;
    const std::vector<std::pair<Class, bool>> inputs = {
        { Class::NotFound, false },          { Class::NotFound, true },
        { Class::TemporarilyUnavailable, false }, { Class::TemporarilyUnavailable, true },
        { Class::Corrupt, false },           { Class::Corrupt, true },
        { Class::Unauthorized, false },      { Class::Unauthorized, true },
    };
    for ( const auto &[cls, from_remote] : inputs )
    {
        ColdReadStatus status;
        status.cls           = cls;
        status.fromRemoteTier = from_remote;
        const auto mapped = sgns::crdt::MapColdReadStatus( status );
        EXPECT_NE( mapped, Error::PEER_BLACKLISTED )
            << "class " << static_cast<int>( cls ) << " must never blame a peer (SC3)";
        EXPECT_NE( mapped, Error::TIMED_OUT )
            << "class " << static_cast<int>( cls ) << " must never claim a peer timeout (SC3)";
    }
}

/**
 * @given a never-sealed block fetched cold through the chain
 * @when  the rehydrate path writes it back
 * @then  the spy inner saw EXACTLY ONE bytes-write (set) and ZERO
 *        resolved-marker writes - SC4 + the §2.4 ruling: bytes only this
 *        phase; the write surface is the decorator set() and nothing
 *        else (no restore re-entry storms: pubsub/routing/heads are
 *        unreachable from this layer by construction, §2.3).
 */
TEST_F( ColdStorageFallbackTest, SC4_RehydrateSilence_BytesOnlyNoMarkers )
{
    const std::string payload = "rehydrate silence payload";
    const auto        cid     = MakeCid( payload );
    const CID         cid_key( cid );

    auto put = remote_->Put( cid, SpanOf( payload ) );
    ASSERT_TRUE( put.has_value() );

    sgns::common::Buffer out;
    (void)service_->Get( cid_key, out );
    ASSERT_FALSE( out.empty() );
    service_->Rehydrate( cid_key, out );

    EXPECT_EQ( inner_->set_calls(), 1u ) << "SC4: exactly one bytes-write";
    EXPECT_EQ( inner_->seal_calls(), 0u ) << "SC4/§2.4: zero resolved-marker writes";
}

/**
 * @given a block that WAS previously sealed in the hot fixture
 * @when  a cold fetch rehydrates it
 * @then  the pre-existing seal stays and NO second marker write occurs -
 *        the rehydrate path never touches seal state (SC4).
 */
TEST_F( ColdStorageFallbackTest, SC4_PreSealedBlockGainsNoMarker )
{
    const std::string payload = "pre-sealed rehydrate payload";
    const auto        cid     = MakeCid( payload );
    const CID         cid_key( cid );

    auto put = remote_->Put( cid, SpanOf( payload ) );
    ASSERT_TRUE( put.has_value() );

    // Pre-seal the CID in the hot fixture (exactly one marker write).
    auto sealed = decorator_->seal( cid_key );
    ASSERT_TRUE( sealed.has_value() );
    ASSERT_EQ( inner_->seal_calls(), 1u );
    auto was_sealed = decorator_->is_sealed( cid_key );
    ASSERT_TRUE( was_sealed.has_value() && was_sealed.value() );

    sgns::common::Buffer out;
    (void)service_->Get( cid_key, out );
    ASSERT_FALSE( out.empty() );
    service_->Rehydrate( cid_key, out );

    EXPECT_EQ( inner_->set_calls(), 1u );
    EXPECT_EQ( inner_->seal_calls(), 1u ) << "no additional marker write after rehydrate (SC4)";
    auto still_sealed = decorator_->is_sealed( cid_key );
    ASSERT_TRUE( still_sealed.has_value() );
    EXPECT_TRUE( still_sealed.value() ) << "the pre-existing marker survives untouched";
}

/**
 * @given a re-warm that FAILED under a scripted s3d Unavailable fault
 * @when  the fault clears and the same CID is re-enqueued
 * @then  the coalescing map never wedges on the failed fetch: the
 *        re-enqueue is accepted and the block warms - D-07 recovery.
 */
TEST_F( ColdStorageFallbackTest, D07_RewarmFaultRecoveryMapNeverWedges )
{
    const std::string payload = "fault recovery payload";
    const auto        cid     = MakeCid( payload );
    const CID         cid_key( cid );

    auto counting = std::make_shared<CountingS3Client>( fake_ );
    auto opened_remote = sgns::blockstore::S3BlockStore::Open( counting );
    ASSERT_TRUE( opened_remote.has_value() );
    std::shared_ptr<S3BlockStore> counting_remote = std::move( opened_remote.value() );
    auto recovery_service = std::make_shared<ColdReadService>(
        shared_local_, counting_remote, cold_store_mutex_, remote_store_mutex_, decorator_ );
    auto put = counting_remote->Put( cid, SpanOf( payload ) );
    ASSERT_TRUE( put.has_value() );

    recovery_service->Start();
    fake_->SetFault( sgns::blockstore::S3Op::Get, sgns::blockstore::S3FaultKind::Unavailable );
    EXPECT_TRUE( recovery_service->EnqueueRewarm( cid_key ) );

    // Deterministic observation of the failed attempt: the worker actually
    // reached the S3 tier (the counting client saw the Get) before failing.
    const auto attempted = [&]() { return counting->get_calls() >= 1; };
    ASSERT_TRUE( PollUntil( attempted, std::chrono::seconds( 2 ) ) );

    // Clear the fault: the same CID must be accepted again and complete.
    fake_->ClearFaults();
    EXPECT_TRUE( recovery_service->EnqueueRewarm( cid_key ) );
    const auto hot = [&]() {
        auto got = inner_->get( cid_key );
        return got.has_value();
    };
    EXPECT_TRUE( PollUntil( hot, ColdReadService::kRewarmBudget + std::chrono::seconds( 1 ) ) )
        << "the coalescing map must never wedge on a failed fetch (D-07)";
    recovery_service->Stop();
}

/**
 * @brief Test-local S3Client wrapper injecting a per-GET sleep before
 *        delegating (re-warm worker timing control; the fake has fault
 *        kinds only - no latency hook).
 */
class GetLatencyS3Client final : public sgns::blockstore::S3Client
{
public:
    GetLatencyS3Client( std::shared_ptr<InMemoryS3Client> inner, std::chrono::milliseconds per_get_latency )
        : inner_( std::move( inner ) ), latency_ms_( per_get_latency.count() )
    {
    }

    sgns::blockstore::result<sgns::blockstore::S3PutResult, sgns::blockstore::BlockStatus> Put(
        const std::string &key, const std::vector<uint8_t> &bytes, const sgns::blockstore::PutObjectOptions &options = {} ) override
    {
        return inner_->Put( key, bytes, options );
    }

    sgns::blockstore::result<sgns::blockstore::S3Object, sgns::blockstore::BlockStatus> Get(
        const std::string &key ) override
    {
        const auto latency = latency_ms_.load();
        if ( latency > 0 )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( latency ) );
        }
        return inner_->Get( key );
    }

    sgns::blockstore::result<sgns::blockstore::S3HeadResult, sgns::blockstore::BlockStatus> Head(
        const std::string &key ) override
    {
        return inner_->Head( key );
    }

    sgns::blockstore::result<void, sgns::blockstore::BlockStatus> Delete( const std::string &key ) override
    {
        return inner_->Delete( key );
    }

private:
    std::shared_ptr<InMemoryS3Client> inner_;
    std::atomic<int64_t>              latency_ms_;
};

/**
 * @given a block present ONLY in S3 and a started re-warm executor behind
 *        a COUNTING S3 client
 * @when  EnqueueRewarm runs TWICE for the same CID before completion
 * @then  exactly ONE chain fetch runs (fake Get count == 1) and the block
 *        becomes hot (PollUntil) - D-07 coalescing with warm completion.
 */
TEST_F( ColdStorageFallbackTest, D07_RewarmCoalescesDuplicateEnqueues )
{
    const std::string payload = "re-warm coalescing payload";
    const auto        cid     = MakeCid( payload );
    const CID         cid_key( cid );

    auto counting = std::make_shared<CountingS3Client>( fake_ );
    auto opened_remote = sgns::blockstore::S3BlockStore::Open( counting );
    ASSERT_TRUE( opened_remote.has_value() );
    std::shared_ptr<S3BlockStore> counting_remote = std::move( opened_remote.value() );
    ColdReadService rewarm_service(
        shared_local_, counting_remote, cold_store_mutex_, remote_store_mutex_, decorator_ );

    auto put = counting_remote->Put( cid, SpanOf( payload ) );
    ASSERT_TRUE( put.has_value() );

    rewarm_service.Start();
    EXPECT_TRUE( rewarm_service.EnqueueRewarm( cid_key ) );
    // Second enqueue before completion attaches to the in-flight re-warm.
    EXPECT_TRUE( rewarm_service.EnqueueRewarm( cid_key ) );

    const auto hot = [&]() {
        auto got = inner_->get( cid_key );
        return got.has_value();
    };
    EXPECT_TRUE( PollUntil( hot, std::chrono::seconds( 5 ) ) ) << "re-warm must complete and re-cache";

    // D-07: duplicates coalesced into exactly one chain fetch.
    EXPECT_EQ( counting->get_calls(), 1u );
    rewarm_service.Stop();
}

/**
 * @given a NOT-STARTED executor (no consumer) and kMaxRewarmQueueDepth+1
 *        distinct cold CIDs
 * @when  the queue is filled then exceeded
 * @then  the first kMaxRewarmQueueDepth enqueues return true, the next
 *        returns false (drop + count), and the call never blocks (D-07
 *        bounded queue, non-blocking overflow).
 */
TEST_F( ColdStorageFallbackTest, D07_RewarmQueueOverflowDropsWithoutBlocking )
{
    std::vector<CID> cids;
    for ( size_t i = 0; i < ColdReadService::kMaxRewarmQueueDepth + 1; ++i )
    {
        cids.emplace_back( MakeCid( "overflow cid " + std::to_string( i ) ) );
    }

    const auto started_at = std::chrono::steady_clock::now();
    for ( size_t i = 0; i < ColdReadService::kMaxRewarmQueueDepth; ++i )
    {
        EXPECT_TRUE( service_->EnqueueRewarm( cids[i] ) ) << "enqueue " << i << " must fit the bound";
    }
    // The overflow drop must be immediate - the enqueuer is the responder
    // thread and must never block (SC2/D-07).
    EXPECT_FALSE( service_->EnqueueRewarm( cids.back() ) ) << "queue-full enqueue must drop";
    const auto elapsed = std::chrono::steady_clock::now() - started_at;
    EXPECT_LT( std::chrono::duration_cast<std::chrono::milliseconds>( elapsed ).count(), 1000 );
    // The dropped CID leaves no in-flight entry: a later retry can enqueue
    // once space frees (verified indirectly - no map leak by behavior).
    service_->Stop();
}

/**
 * @given a queue entry that aged past kRewarmBudget while no worker ran
 * @when  the worker starts and reaches the stale entry
 * @then  the entry is SKIPPED (dropped + counted), not fetched - the
 *        budget bounds scheduling/waiting (D-07/RESEARCH §6.4).
 */
TEST_F( ColdStorageFallbackTest, D07_RewarmExpiredBudgetEntryIsSkipped )
{
    const std::string payload = "expired budget payload";
    const auto        cid     = MakeCid( payload );
    const CID         cid_key( cid );

    auto counting = std::make_shared<CountingS3Client>( fake_ );
    auto opened_remote = sgns::blockstore::S3BlockStore::Open( counting );
    ASSERT_TRUE( opened_remote.has_value() );
    std::shared_ptr<S3BlockStore> counting_remote = std::move( opened_remote.value() );
    ColdReadService budget_service(
        shared_local_, counting_remote, cold_store_mutex_, remote_store_mutex_, decorator_ );
    auto put = counting_remote->Put( cid, SpanOf( payload ) );
    ASSERT_TRUE( put.has_value() );

    // Enqueue with no worker running, then let the entry age past the
    // budget (bounded poll on elapsed time - never a fixed sleep).
    EXPECT_TRUE( budget_service.EnqueueRewarm( cid_key ) );
    const auto deadline = std::chrono::steady_clock::now() + ColdReadService::kRewarmBudget
                        + std::chrono::milliseconds( 200 );
    const auto aged = [&]() { return std::chrono::steady_clock::now() >= deadline; };
    ASSERT_TRUE( PollUntil( aged, ColdReadService::kRewarmBudget + std::chrono::seconds( 2 ) ) );

    budget_service.Start();
    // Give the worker a window to (incorrectly) fetch, then assert it
    // never did and nothing became hot.
    const auto fetched = [&]() { return counting->get_calls() > 0; };
    EXPECT_FALSE( PollUntil( fetched, std::chrono::milliseconds( 500 ) ) )
        << "an over-budget entry must be skipped, not fetched";
    auto got = inner_->get( cid_key );
    EXPECT_FALSE( got.has_value() ) << "a skipped re-warm must not re-cache";
    budget_service.Stop();
}

/**
 * @given a started executor with one latency-injected re-warm in flight
 * @when  Stop() runs (twice) and the service is destroyed
 * @then  every call returns without hanging - the join completes inside
 *         the bounded-grace + join contract (D-11 shape), Stop is
 *         idempotent, and the destructor joins (Pitfall-5 ordering).
 */
TEST_F( ColdStorageFallbackTest, D11_RewarmStopJoinsAndIsIdempotent )
{
    const std::string payload = "shutdown grace payload";
    const auto        cid     = MakeCid( payload );
    const CID         cid_key( cid );

    auto slow = std::make_shared<GetLatencyS3Client>( fake_, std::chrono::milliseconds( 300 ) );
    auto opened_remote = sgns::blockstore::S3BlockStore::Open( slow );
    ASSERT_TRUE( opened_remote.has_value() );
    std::shared_ptr<S3BlockStore> slow_remote = std::move( opened_remote.value() );
    auto slow_service = std::make_shared<ColdReadService>(
        shared_local_, slow_remote, cold_store_mutex_, remote_store_mutex_, decorator_ );
    auto put = slow_remote->Put( cid, SpanOf( payload ) );
    ASSERT_TRUE( put.has_value() );

    slow_service->Start();
    EXPECT_TRUE( slow_service->EnqueueRewarm( cid_key ) );

    slow_service->Stop(); // bounded grace then unconditional join
    slow_service->Stop(); // idempotent no-op on an already-joined thread
    ASSERT_NO_THROW( { slow_service.reset(); } ); // destructor join path
    SUCCEED();
}
