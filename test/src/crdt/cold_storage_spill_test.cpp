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
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <system_error>
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
    using IpfsDatastore = sgns::ipfs_lite::ipfs::IpfsDatastore;
    using IpfsDatastoreError = sgns::ipfs_lite::ipfs::IpfsDatastoreError;
    using CID = sgns::CID;

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

        // local_ is moved into shared_local_ for the decorator; keep the
        // shared handle for direct journal assertions.
        std::unique_ptr<LocalBlockStore>          local_;
        std::shared_ptr<LocalBlockStore>          shared_local_;
        std::shared_ptr<InMemoryS3Client>         fake_;
        std::unique_ptr<S3BlockStore>             remote_;
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
