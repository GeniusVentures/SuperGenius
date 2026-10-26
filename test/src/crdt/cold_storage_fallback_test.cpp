/**
 * @file       cold_storage_fallback_test.cpp
 * @brief      Hermetic Tier-1 cold-read fallback tests (READ-01, v1.1
 *             Phase 9 plan 09-01).
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
#include "InMemoryS3Client.hpp"

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
TEST_F( ColdStorageFallbackTest, LocalTierHitEndToEnd )
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
TEST_F( ColdStorageFallbackTest, S3TierHitEndToEnd )
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
TEST_F( ColdStorageFallbackTest, NullHookNoOpReturnsHotErrorUnchanged )
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
TEST_F( ColdStorageFallbackTest, LocalTierHitPerformsZeroS3Gets )
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
TEST_F( ColdStorageFallbackTest, DefinitiveAbsenceClassifiesNotFound )
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
TEST_F( ColdStorageFallbackTest, S3UnavailableNeverSurfacesAsNotFound )
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
TEST_F( ColdStorageFallbackTest, RemoteCorruptionAttributesToRemoteTier )
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
TEST_F( ColdStorageFallbackTest, LocalCorruptionAttributesToLocalTier )
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
TEST( ColdReadStatusMapping, MapColdReadStatusExhaustiveTable )
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
TEST_F( ColdStorageFallbackTest, HasLocalColdBoolContractUnderFault )
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
