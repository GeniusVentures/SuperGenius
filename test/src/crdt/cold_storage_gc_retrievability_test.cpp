/**
 * @file       cold_storage_gc_retrievability_test.cpp
 * @brief      GC-03 hermetic Tier-1 proofs: an evicted CID remains
 *             retrievable end-to-end on BOTH legs, CID-verified (v1.1
 *             Phase 11 plan 11-03).
 *
 * Registered ONLY under SGNS_COLD_STORAGE (links
 * sgns-blockstore::sgns-blockstore plus the InMemoryS3Client /
 * InMemoryS3AdminClient fakes via the relative include dir). The fixture
 * clones cold_storage_gc_test's proven topology (real hot RocksDB shared
 * by two wrappers, spill decorator, REAL ColdReadService with the
 * re-warm executor STARTED, real GraphsyncDAGSyncer, direct manager
 * construction) and drives REAL evictions through the manager's pass.
 *
 * Legs proven:
 *  - LOCAL READ: GetNodeWithoutRequest over a disposed CID resolves from
 *    the S3 tier CID-verified, re-caches hot, marker survives re-learn.
 *  - VERIFICATION HONESTY: a byte-flipped S3 object is REJECTED (the
 *    COLD_STORAGE_CORRUPT taxonomy) - never served as good bytes, never
 *    NotFound.
 *  - RESPONDER: the ColdServeDagService serving path returns don't-have
 *    IMMEDIATELY, the re-warm executor pulls from the S3 fake OFF the
 *    responder's path, and the retry serves CID-verified.
 *  - REQUESTER (graphsync request entry, cold-before-peers): getNode -
 *    the graphsync request resolution path - resolves the evicted CID
 *    from the blockstore chain before any peer leg, with a SHARED S3
 *    fake as the only copy holder (the two-node shared-fake topology;
 *    the libp2p transport mediation is Phase-9-proven territory and the
 *    live wire belongs to 11-04's session).
 *
 * Include discipline: has_value() BEFORE .value()/.error() everywhere
 * (policy::terminate alias); CID wraps explicit (CID cid_key( cid )).
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <vector>

#include <libp2p/multi/content_identifier_codec.hpp>

#include <sgns-blockstore/Cid.hpp>
#include <sgns-blockstore/LocalBlockStore.hpp>
#include <sgns-blockstore/S3AdminClient.hpp>
#include <sgns-blockstore/S3BlockStore.hpp>

#include "InMemoryS3AdminClient.hpp"
#include "InMemoryS3Client.hpp"

#include "base/buffer.hpp"
#include "base/logger.hpp"
#include "crdt/broadcaster.hpp"
#include "crdt/crdt_datastore.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/globaldb/BlockStoreBackedIpfsDatastore.hpp"
#include "crdt/globaldb/ColdReadService.hpp"
#include "crdt/globaldb/ColdStorageManager.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
#include "crdt/hierarchical_key.hpp"
#include "storage/rocksdb/rocksdb.hpp"

#include <ipfs_lite/ipfs/impl/datastore_rocksdb.hpp>

namespace
{
    namespace ipfs_outcome = sgns::IPFS::outcome;
    using IpfsDatastore      = sgns::ipfs_lite::ipfs::IpfsDatastore;
    using CID                = sgns::CID;
    using sgns::base::Buffer;
    using sgns::crdt::BlockStoreBackedIpfsDatastore;
    using sgns::crdt::Broadcaster;
    using sgns::crdt::ColdReadService;
    using sgns::crdt::ColdServeDagService;
    using sgns::crdt::CrdtDatastore;
    using sgns::crdt::CrdtOptions;
    using sgns::crdt::GraphsyncDAGSyncer;
    using sgns::crdt::HierarchicalKey;
    using sgns::crdt::ColdStorageManager;
    using sgns::blockstore::Cid;
    using sgns::blockstore::CidOf;
    using sgns::blockstore::InMemoryS3AdminClient;
    using sgns::blockstore::InMemoryS3Client;
    using sgns::blockstore::LocalBlockStore;
    using sgns::blockstore::LocalBlockStoreOptions;
    using sgns::blockstore::S3BlockStore;
    using sgns::blockstore::UploadState;
    using sgns::blockstore::UploadStats;
    using sgns::storage::rocksdb;
    using libp2p::multi::MulticodecType;
    using MerkleDagServiceImpl = sgns::ipfs_lite::ipfs::merkledag::MerkleDagServiceImpl;

    /// Wall-clock unix-ms.
    uint64_t UnixNowMs()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch() )
                .count() );
    }

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

    ColdStorageManager::BackpressureOptions FastOptions()
    {
        ColdStorageManager::BackpressureOptions options;
        options.drain_interval_ms = 100;
        options.warn_pending      = 10;
        options.critical_pending  = 20;
        options.backoff_cap_ms    = 1000;
        return options;
    }

    ColdStorageManager::DurabilityOptions FastDurability()
    {
        ColdStorageManager::DurabilityOptions durability;
        durability.flush_min_interval_ms = 50;
        durability.flush_max_interval_ms = 300;
        return durability;
    }

    ColdStorageManager::RetentionOptions RetentionFastGrace( uint64_t age_ms, uint32_t grace_ms )
    {
        ColdStorageManager::RetentionOptions retention;
        retention.retention_age_ms = age_ms;
        retention.grace_ms         = grace_ms;
        return retention;
    }

    gsl::span<const uint8_t> SpanOf( const std::string &s )
    {
        return gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( s.data() ), s.size() );
    }

    Cid MakeCid( const std::string &payload )
    {
        auto cid = CidOf( SpanOf( payload ), MulticodecType::Code::RAW );
        return cid.value();
    }

    std::string NextUniqueDir( const char *lane )
    {
        static std::atomic<uint64_t> next{ 0 };
        return std::string( "test-data/cold-storage-gc-r/case-" ) + lane + "-"
             + std::to_string( next.fetch_add( 1 ) );
    }

    std::string FreshDir( const char *lane )
    {
        auto dir = NextUniqueDir( lane );
        std::error_code ec;
        std::filesystem::remove_all( dir, ec );
        std::filesystem::create_directories( dir, ec );
        return dir;
    }

    /// @brief Test-local NO-OP broadcaster (the empty-queue posture).
    class NoopBroadcaster final : public Broadcaster
    {
    public:
        outcome::result<void> Broadcast( const Buffer &,
                                        std::string,
                                        boost::optional<libp2p::peer::PeerInfo> = boost::none ) override
        {
            return outcome::success();
        }

        outcome::result<Buffer> Next() override
        {
            return outcome::failure( boost::system::error_code{} );
        }

        bool HasTopic( const std::string & ) override
        {
            return true;
        }
    };

    /// @brief Test-local S3Client wrapper counting Get calls (the
    ///        tier-selection + zero-traffic probe).
    class CountingS3Client final : public sgns::blockstore::S3Client
    {
    public:
        explicit CountingS3Client( std::shared_ptr<InMemoryS3Client> inner )
            : inner_( std::move( inner ) )
        {
        }

        uint64_t get_calls() const
        {
            return get_calls_;
        }

        sgns::blockstore::result<sgns::blockstore::S3PutResult, sgns::blockstore::BlockStatus> Put(
            const std::string &key, const std::vector<uint8_t> &bytes,
            const sgns::blockstore::PutObjectOptions &options = {} ) override
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

        sgns::blockstore::result<void, sgns::blockstore::BlockStatus> Delete(
            const std::string &key ) override
        {
            return inner_->Delete( key );
        }

    private:
        std::shared_ptr<InMemoryS3Client> inner_;
        std::atomic<uint64_t>             get_calls_{ 0 };
    };

    class ColdStorageGcRetrievabilityTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            hot_dir_ = FreshDir( "hot" );
            rocksdb::Options hot_options;
            hot_options.create_if_missing = true;
            auto opened_hot = rocksdb::create( hot_dir_, hot_options );
            ASSERT_TRUE( opened_hot.has_value() );
            real_hot_ = std::move( opened_hot.value() );

            shared_local_dir_ = FreshDir( "cold" );
            auto opened_local = LocalBlockStore::Open( LocalBlockStoreOptions{ shared_local_dir_ } );
            ASSERT_TRUE( opened_local.has_value() );
            shared_local_ = std::shared_ptr<LocalBlockStore>( std::move( opened_local.value() ) );

            fake_     = std::make_shared<InMemoryS3Client>();
            counting_ = std::make_shared<CountingS3Client>( fake_ );
            auto opened_remote = S3BlockStore::Open( counting_ );
            ASSERT_TRUE( opened_remote.has_value() );
            remote_ = std::shared_ptr<S3BlockStore>( std::move( opened_remote.value() ) );

            fake_admin_       = std::make_shared<InMemoryS3AdminClient>();
            cold_store_mutex_ = std::make_shared<std::mutex>();

            auto ipfs_db = sgns::ipfs_lite::rocksdb::create( real_hot_->getDB() );
            ASSERT_TRUE( ipfs_db.has_value() );
            inner_     = std::make_shared<sgns::ipfs_lite::ipfs::RocksdbDatastore>( ipfs_db.value() );
            decorator_ = std::make_shared<BlockStoreBackedIpfsDatastore>(
                inner_, shared_local_, cold_store_mutex_, sgns::base::createLogger( "GcRetr" ) );
            remote_store_mutex_ = std::make_shared<std::mutex>();
            cold_read_service_  = std::make_shared<ColdReadService>(
                shared_local_, remote_, cold_store_mutex_, remote_store_mutex_, decorator_ );
            // GC-03: the re-warm executor is LIVE for the responder leg
            // (the Phase-9 SC2 posture: serve path never waits).
            cold_read_service_->Start();
            dagsyncer_ = std::make_shared<GraphsyncDAGSyncer>( decorator_, nullptr, nullptr,
                                                               cold_read_service_ );

            broadcaster_ = std::make_shared<NoopBroadcaster>();
            crdt_        = CrdtDatastore::New(
                real_hot_, HierarchicalKey( "/namespace" ), dagsyncer_, broadcaster_,
                CrdtOptions::DefaultOptions() );
            ASSERT_TRUE( crdt_ != nullptr );
            crdt_->Start();

            // The serving wrapper over the REAL chain (the graphsync
            // responder's serving path - StartSync's own composition).
            dag_service_ = std::make_shared<MerkleDagServiceImpl>( decorator_ );
            wrapper_     = std::make_shared<ColdServeDagService>( dag_service_,
                                                             cold_read_service_.get() );
        }

        void TearDown() override
        {
            if ( manager_ )
            {
                manager_->Stop();
            }
            if ( cold_read_service_ )
            {
                cold_read_service_->Stop();
            }
            if ( crdt_ )
            {
                crdt_->CancelAndCloseNow();
            }
        }

        std::shared_ptr<ColdStorageManager> MakeManager( uint64_t age_ms = 5000, uint32_t grace_ms = 1 )
        {
            manager_ = std::make_shared<ColdStorageManager>(
                shared_local_, remote_, cold_store_mutex_, remote_store_mutex_, FastOptions(),
                FastDurability(), fake_admin_, RetentionFastGrace( age_ms, grace_ms ) );
            manager_->AttachGcDependencies( crdt_.get(), dagsyncer_.get(), real_hot_ );
            manager_->Start();
            return manager_;
        }

        /// Publish a REAL delta, detach its head (unreachable), drive it
        /// to Durable, backdate, and evict it through TWO driven passes.
        /// Raw orphans are NOT dag-pb nodes and can never resolve through
        /// the node paths - the GC-03 legs must prove DELTA blocks (the
        /// only shape the read paths serve). Returns the CID and the
        /// node-content snapshot taken BEFORE the eviction (the
        /// CID-verified byte-equality oracle).
        /// EXPECT (not ASSERT) throughout: the helper returns values.
        struct EvictedDelta
        {
            CID                  cid_key;
            std::vector<uint8_t> content;
        };

        EvictedDelta EvictDetachedDelta( const std::string &key, const std::string &value )
        {
            const CID head = PublishElement( key, value );
            EXPECT_TRUE( crdt_->RemoveHead( head, "topic" ).has_value() );

            // The pre-eviction content snapshot (the resolution oracle).
            auto before = dagsyncer_->GetNodeWithoutRequest( head );
            EXPECT_TRUE( before.has_value() );
            EvictedDelta evicted;
            evicted.cid_key = head;
            if ( before.has_value() )
            {
                const auto &content = before.value()->content();
                evicted.content.assign( content.data(), content.data() + content.size() );
            }

            const Cid cid( head );
            auto manager = MakeManager();
            EXPECT_TRUE( PollUntil( [&] { return manager->GetUploadState( cid ).has_value()
                                          && manager->GetUploadState( cid ).value()
                                                 == UploadState::UploadedDurable; },
                                    std::chrono::milliseconds( 10000 ) ) );

            {
                std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
                EXPECT_TRUE( shared_local_->SetSpilledAt( cid, UnixNowMs() - 60000 ).has_value() );
            }
            EXPECT_EQ( manager->GcPassOnce().size(), 1u );
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
            (void)manager->GcPassOnce();

            std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
            auto disposed = shared_local_->IsDisposed( cid );
            EXPECT_TRUE( disposed.has_value() && disposed.value() );
            return evicted;
        }

        /// Publish one element through the REAL CRDT Put path.
        CID PublishElement( const std::string &key, const std::string &value )
        {
            auto delta = CrdtDatastore::CreateDeltaToAdd( key, value );
            EXPECT_TRUE( delta.has_value() );
            auto published = crdt_->Publish( delta.value(), { "topic" } );
            EXPECT_TRUE( published.has_value() );
            return published.value();
        }

        std::string                             hot_dir_;
        std::shared_ptr<rocksdb>                real_hot_;
        std::string                             shared_local_dir_;
        std::shared_ptr<LocalBlockStore>        shared_local_;
        std::shared_ptr<InMemoryS3Client>       fake_;
        std::shared_ptr<CountingS3Client>       counting_;
        std::shared_ptr<S3BlockStore>           remote_;
        std::shared_ptr<InMemoryS3AdminClient>  fake_admin_;
        std::shared_ptr<std::mutex>             cold_store_mutex_;
        std::shared_ptr<IpfsDatastore>          inner_;
        std::shared_ptr<BlockStoreBackedIpfsDatastore> decorator_;
        std::shared_ptr<std::mutex>             remote_store_mutex_;
        std::shared_ptr<ColdReadService>        cold_read_service_;
        std::shared_ptr<GraphsyncDAGSyncer>     dagsyncer_;
        std::shared_ptr<NoopBroadcaster>        broadcaster_;
        std::shared_ptr<CrdtDatastore>          crdt_;
        std::shared_ptr<MerkleDagServiceImpl>   dag_service_;
        std::shared_ptr<ColdServeDagService>    wrapper_;
        std::shared_ptr<ColdStorageManager>     manager_;
    };
} // namespace

/**
 * @given a fully-evicted CID (hot + cold bytes gone, marker present, the
 *        S3 fake is the ONLY remaining copy holder)
 * @when  GetNodeWithoutRequest reads it (the local-read leg)
 * @then  the read resolves from the S3 tier CID-VERIFIED with the
 *        ORIGINAL bytes (the fake's Get counter is the tier evidence),
 *        the block re-caches hot (a second read costs ZERO S3 gets), and
 *        the disposed marker SURVIVES the re-learn (dedup continues
 *        until trim).
 */
TEST_F( ColdStorageGcRetrievabilityTest, LocalReadLegResolvesEvictedCidVerified )
{
    const auto evicted = EvictDetachedDelta( "gc/r/local-read", "value" );
    const CID cid_key  = evicted.cid_key;
    const Cid cid( cid_key );

    const uint64_t gets_before = counting_->get_calls();
    auto resolved = dagsyncer_->GetNodeWithoutRequest( cid_key );
    ASSERT_TRUE( resolved.has_value() ) << "an evicted CID stays readable through the cold chain";
    EXPECT_GT( counting_->get_calls(), gets_before ) << "the S3 tier served the read";

    // CID-verified: the resolved content is byte-equal to the
    // pre-eviction snapshot (the library's Get verification ran inside
    // the chain).
    const auto &content = resolved.value()->content();
    EXPECT_EQ( std::vector<uint8_t>( content.data(), content.data() + content.size() ),
               evicted.content );

    // Re-cached hot: the second read costs zero S3 gets.
    const uint64_t gets_after_first = counting_->get_calls();
    auto again = dagsyncer_->GetNodeWithoutRequest( cid_key );
    ASSERT_TRUE( again.has_value() );
    EXPECT_EQ( counting_->get_calls(), gets_after_first );

    // The marker survived the re-learn (dedup continues until trim).
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto disposed = shared_local_->IsDisposed( cid );
        ASSERT_TRUE( disposed.has_value() );
        EXPECT_TRUE( disposed.value() );
    }
}

/**
 * @given a fully-evicted CID whose S3 object is byte-FLIPPED (the
 *        negative-verification instrument)
 * @when  GetNodeWithoutRequest reads it
 * @then  the verification failure surfaces with the honest
 *        COLD_STORAGE_CORRUPT taxonomy - NEVER the flipped bytes, NEVER
 *        a NotFound-shaped success (GC-03's verification-honesty leg).
 */
TEST_F( ColdStorageGcRetrievabilityTest, VerificationHonestyRejectsFlippedBytes )
{
    const auto evicted = EvictDetachedDelta( "gc/r/flip", "value" );
    const Cid cid( evicted.cid_key );

    // Flip the ONLY remaining copy (the fake's flip-bytes fault kit).
    // The delta node's CID carries the DAG_PB codec - the object key is
    // v1/blocks/dag-pb/<fanout>/<cid> (S3BlockStore's DeriveKey shape).
    fake_->SetFlipBytesForKey(
        [&] {
            auto s = libp2p::multi::ContentIdentifierCodec::toString( cid );
            static const char *kHex = "0123456789abcdef";
            const auto digest = cid.content_address.getHash();
            const uint8_t first = digest.empty() ? 0 : digest[0];
            std::string fanout{ kHex[first >> 4], kHex[first & 0x0F] };
            return std::string( "v1/blocks" ) + "/dag-pb/" + fanout + "/" + s.value();
        }(),
        true );

    // Drop the LRU copy the pre-eviction oracle read left behind - the
    // honest fresh-request shape must consult the flipped cold tier.
    (void)dagsyncer_->DeleteCIDBlock( evicted.cid_key );

    auto resolved = dagsyncer_->GetNodeWithoutRequest( evicted.cid_key );
    ASSERT_TRUE( resolved.has_error() ) << "flipped bytes must NEVER be served as good";
    EXPECT_EQ( resolved.error(),
               GraphsyncDAGSyncer::Error::COLD_STORAGE_CORRUPT )
        << "the honest corrupt taxonomy - never NotFound, never success";
}

/**
 * @given a fully-evicted CID served through the graphsync responder's
 *        serving path (ColdServeDagService over the REAL chain with the
 *        re-warm executor live)
 * @when  a request misses, then is retried
 * @then  the FIRST answer is an immediate don't-have (the responder
 *        NEVER waits inline - the miss enqueues a background re-warm),
 *        the executor pulls the block from the S3 fake off the
 *        responder's path, and the retry serves it CID-verified (D-12:
 *        the miss is don't-have, NEVER NotFound-as-absence on a read).
 */
TEST_F( ColdStorageGcRetrievabilityTest, ResponderLegDontHaveThenRewarmServes )
{
    const auto evicted = EvictDetachedDelta( "gc/r/responder", "value" );
    const CID cid_key  = evicted.cid_key;

    // First request: immediate miss (don't-have) - the responder path
    // returns the inner failure unchanged and never blocks.
    auto first = wrapper_->getNode( cid_key );
    EXPECT_TRUE( first.has_error() ) << "the cold miss answers don't-have immediately";

    // The re-warm executor pulls from the S3 fake (bounded, background).
    const uint64_t gets_before = counting_->get_calls();
    ASSERT_TRUE( PollUntil(
        [&] { return counting_->get_calls() > gets_before; },
        std::chrono::milliseconds( 10000 ) ) )
        << "the re-warm executor did not fetch within its budget";

    // The retry serves CID-verified.
    ASSERT_TRUE( PollUntil(
        [&] {
            auto retry = wrapper_->getNode( cid_key );
            return retry.has_value();
        },
        std::chrono::milliseconds( 10000 ) ) )
        << "the retry did not serve the re-warmed block";
    auto served = wrapper_->getNode( cid_key );
    ASSERT_TRUE( served.has_value() );
    const auto &content = served.value()->content();
    EXPECT_EQ( std::vector<uint8_t>( content.data(), content.data() + content.size() ),
               evicted.content );
}

/**
 * @given a fully-evicted CID read through getNode - the graphsync
 *        REQUEST resolution path (cold-before-peers) - with the S3 fake
 *        as the ONLY copy holder (the two-node shared-fake topology:
 *        this node holds nothing local anymore)
 * @when  getNode runs with NO graphsync object wired (no peers exist)
 * @then  the request resolves from the blockstore chain BEFORE any peer
 *        leg: CID-verified bytes return, the fake's Get counter is the
 *        evidence the cold tier served, and no peer machinery was ever
 *        consulted (GC-03's graphsync-leg letter at the request entry).
 */
TEST_F( ColdStorageGcRetrievabilityTest, RequesterLegColdBeforePeersResolvesEvicted )
{
    const auto evicted = EvictDetachedDelta( "gc/r/requester", "value" );
    const CID cid_key  = evicted.cid_key;

    const uint64_t gets_before = counting_->get_calls();
    auto resolved = dagsyncer_->getNode( cid_key );
    ASSERT_TRUE( resolved.has_value() )
        << "the request path resolves the evicted CID cold-before-peers";
    EXPECT_GT( counting_->get_calls(), gets_before ) << "the blockstore chain served the request";

    const auto &content = resolved.value()->content();
    EXPECT_EQ( std::vector<uint8_t>( content.data(), content.data() + content.size() ),
               evicted.content );
}
