/**
 * @file       cold_storage_resurrection_test.cpp
 * @brief      Hermetic Tier-1 resurrection counter-tests (TEST-02 SC2,
 *             v1.1 Phase 12 plan 12-01 Task 3 / D-13): after put ->
 *             CRDT delete -> forced GC eviction, re-delivery of the
 *             pre-delete delta through the pubsub path must NEVER
 *             resurrect the value.
 *
 * Eviction reality (Phase-11 exactness lemma, applied here): the
 * tombstone delta LINKS the pre-delete delta (every Publish points at
 * the previous heads), so a plain put+delete leaves BOTH deltas
 * reachable from heads and the retention GC (which only ever evicts
 * UNREACHABLE history, GC-01) would correctly pin them. The SC2 shape
 * therefore detaches the tombstone head after the delete (RemoveHead -
 * the Phase-11 EvictDetachedDelta mechanics): the tombstone ROWS
 * (/t/ namespace) are never purged, so the value stays deleted, while
 * both delta BLOCKS become cold-eligible and evict. This is exactly the
 * production GC contract - eviction never touches CRDT metadata.
 *
 * Three lines of proof:
 *  (1) Resurrect_MarkerPresent_DedupBlocksReintegration - the
 *      disposed-marker dedup line: the four-term HasBlock (the exact
 *      call shape the pubsub receive path drives per announced CID -
 *      the 11-03 storm-test mechanics) answers TRUE with ZERO S3 Gets,
 *      no re-journal, value absent.
 *  (2) Resurrect_TrimmedMarker_TombstoneDominanceKeepsValueAbsent -
 *      the trimmed-marker fallback: after TrimDisposed(0,0) erases the
 *      dedup line, the merge path runs but the CRDT-delete tombstone
 *      DOMINATES the re-delivered lower-height Add - value stays
 *      absent, remote copy intact.
 *  (3) Resurrect_FullWire_TwoGlobalDBRedeliveryStaysDeleted - the one
 *      full-fidelity case (D-13): two REAL GlobalDB instances over the
 *      pubsub_graphsync_test MultiGlobalDBTest topology (two
 *      GossipPubSub hosts, separate io contexts, real in-process
 *      libp2p + graphsync wire), cold storage ON on the receiving node
 *      through the TEST-ONLY stack client override
 *      (SetColdStorageStackClientsForTesting - hermetic fakes, zero
 *      network), real background GC eviction (GetColdStorageStatus
 *      evidence), then re-broadcast of the original pre-delete CID
 *      from node 1 over the real gossip wire: the receiver drops it at
 *      the disposed-marker dedup with zero S3 Gets and zero
 *      re-journal, and the value stays absent on BOTH nodes.
 *
 * Include discipline: every library result guarded with has_value()
 * BEFORE .value()/.error() (policy::terminate alias). CID landmine:
 * sgns::CID has NO implicit ctor from the blockstore Cid - wrap
 * explicitly (CID cid_key( cid )). No sleep_for outside PollUntil
 * (Tier-1 audit; Pitfall 4). Pubsub-path work stays non-blocking
 * (Pitfall 3).
 */

#include <gtest/gtest.h>

#include <atomic>
#include <iostream>
#include <sstream>
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
#include <sgns-blockstore/S3Client.hpp>

// Internal fakes - relative-include-dir privilege (test-surface only).
#include <sgns-blockstore/InMemoryS3AdminClient.hpp>
#include <sgns-blockstore/InMemoryS3Client.hpp>

#include "base/buffer.hpp"
#include "base/logger.hpp"
#include "crdt/broadcaster.hpp"
#include "crdt/crdt_datastore.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt_mirror_broadcaster.hpp"
#include "crdt/globaldb/pubsub_broadcaster_ext.hpp"
#include "crdt/proto/bcast.pb.h"
#include "crdt/globaldb/BlockStoreBackedIpfsDatastore.hpp"
#include "crdt/globaldb/ColdReadService.hpp"
#include "crdt/globaldb/ColdStorageManager.hpp"
#include "crdt/globaldb/ColdStorageStack.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
#include "crdt/hierarchical_key.hpp"
#include "storage/rocksdb/rocksdb.hpp"

#include <ipfs_lite/ipfs/impl/datastore_rocksdb.hpp>
#include <ipfs_lite/ipld/impl/ipld_node_impl.hpp>

// Full-wire leg (case 3): the MultiGlobalDBTest composition surface.
#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/log/configurator.hpp>
#include <soralog/impl/configurator_from_yaml.hpp>

namespace
{
    namespace ipfs_outcome = sgns::IPFS::outcome;
    using IpfsDatastore      = sgns::ipfs_lite::ipfs::IpfsDatastore;
    using CID                = sgns::CID;
    using sgns::base::Buffer;
    using sgns::crdt::BlockStoreBackedIpfsDatastore;
    using sgns::crdt::Broadcaster;
    using sgns::crdt::ColdReadService;
    using sgns::crdt::CrdtDatastore;
    using sgns::crdt::CrdtOptions;
    using sgns::crdt::CRDTMirrorBroadcaster;
    using sgns::crdt::PubSubBroadcasterExt;
    using sgns::crdt::GlobalDB;
    using sgns::crdt::GraphsyncDAGSyncer;
    using sgns::crdt::HierarchicalKey;
    using sgns::crdt::ColdStorageManager;
    using sgns::blockstore::Cid;
    using sgns::blockstore::CidOf;
    using sgns::blockstore::InMemoryS3AdminClient;
    using sgns::blockstore::InMemoryS3Client;
    using sgns::blockstore::LocalBlockStore;
    using sgns::blockstore::LocalBlockStoreOptions;
    using sgns::blockstore::S3AdminClient;
    using sgns::blockstore::S3BlockStore;
    using sgns::blockstore::S3Client;
    using sgns::blockstore::S3FaultKind;
    using sgns::blockstore::S3Object;
    using sgns::blockstore::S3Op;
    using sgns::blockstore::S3PutResult;
    using sgns::blockstore::PutObjectOptions;
    using sgns::blockstore::S3HeadResult;
    using sgns::blockstore::BlockStatus;
    using sgns::blockstore::result;
    using sgns::blockstore::UploadState;
    using sgns::storage::BufferStorage;
    using sgns::storage::rocksdb;
    using libp2p::multi::MulticodecType;

    /// Wall-clock unix-ms.
    uint64_t UnixNowMs()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch() )
                .count() );
    }

    /// Bounded poll loop with deadline - never a fixed sleep. The ONLY
    /// sleep_for in this file lives in this helper body (Pitfall 4).
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

    /// Short-interval drainer options.
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

    /// Fast flush cadence.
    ColdStorageManager::DurabilityOptions FastDurability( uint32_t flush_min_interval_ms = 50,
                                                          uint32_t flush_max_interval_ms = 300 )
    {
        ColdStorageManager::DurabilityOptions durability;
        durability.flush_min_interval_ms = flush_min_interval_ms;
        durability.flush_max_interval_ms = flush_max_interval_ms;
        return durability;
    }

    /// Retention for the resurrection legs: EXPLICIT tiny positive
    /// values (zero normalizes to 60 s grace / 10 min cadence - Pitfall
    /// 2) with a DORMANT background GC thread (tests drive GcPassOnce).
    ColdStorageManager::RetentionOptions RetentionTiny( uint64_t retention_age_ms = 1,
                                                        uint32_t grace_ms        = 1 )
    {
        ColdStorageManager::RetentionOptions retention;
        retention.retention_age_ms = retention_age_ms;
        retention.dry_run          = false;
        retention.gc_interval_ms   = 0;    // dormant; driven passes only
        retention.grace_ms         = grace_ms; // explicit - never normalized
        return retention;
    }

    std::string NextUniqueDir( const char *lane )
    {
        static std::atomic<uint64_t> next{ 0 };
        return std::string( "test-data/cold-storage-resurrect/" ) + lane + "-case-"
             + std::to_string( next.fetch_add( 1 ) );
    }

    std::string FreshDir( const char *lane )
    {
        auto           dir = NextUniqueDir( lane );
        std::error_code ec;
        std::filesystem::remove_all( dir, ec );
        return dir;
    }

    /**
     * @brief Test-local S3Client wrapper counting Get calls before
     *        delegating (the zero-S3-traffic probe).
     */
    class CountingS3Client final : public S3Client
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

        result<S3PutResult, BlockStatus> Put( const std::string              &key,
                                              const std::vector<uint8_t>     &bytes,
                                              const PutObjectOptions         &options = {} ) override
        {
            return inner_->Put( key, bytes, options );
        }

        result<S3Object, BlockStatus> Get( const std::string &key ) override
        {
            ++get_calls_;
            return inner_->Get( key );
        }

        result<S3HeadResult, BlockStatus> Head( const std::string &key ) override
        {
            return inner_->Head( key );
        }

        result<void, BlockStatus> Delete( const std::string &key ) override
        {
            return inner_->Delete( key );
        }

    private:
        std::shared_ptr<InMemoryS3Client> inner_;
        std::atomic<uint64_t>             get_calls_{ 0 };
    };

    /**
     * @brief The pubsub-path re-delivery encoder: the CRDTBroadcast
     *        envelope carrying one head CID (exactly what Publish
     *        broadcasts - EncodeBroadcast's wire shape, built test-side
     *        so the receive lane can be driven without protected
     *        surface).
     */
    Buffer EncodeCidAnnouncement( const CID &cid )
    {
        sgns::crdt::pb::CRDTBroadcast msg;
        msg.add_heads()->set_cid( cid.toString().value() );
        Buffer out;
        out.put( msg.SerializeAsString() );
        return out;
    }

    class ColdStorageResurrectionTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            hot_dir_ = FreshDir( "hot" );
            std::error_code hot_ec;
            std::filesystem::create_directories( hot_dir_, hot_ec );
            ASSERT_FALSE( hot_ec );
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
            auto opened_remote = S3BlockStore::Open(
                std::static_pointer_cast<S3Client>( counting_ ) );
            ASSERT_TRUE( opened_remote.has_value() );
            remote_ = std::shared_ptr<S3BlockStore>( std::move( opened_remote.value() ) );

            fake_admin_       = std::make_shared<InMemoryS3AdminClient>();
            cold_store_mutex_ = std::make_shared<std::mutex>();

            auto ipfs_db = sgns::ipfs_lite::rocksdb::create( real_hot_->getDB() );
            ASSERT_TRUE( ipfs_db.has_value() );
            inner_     = std::make_shared<sgns::ipfs_lite::ipfs::RocksdbDatastore>( ipfs_db.value() );
            decorator_ = std::make_shared<BlockStoreBackedIpfsDatastore>(
                inner_, shared_local_, cold_store_mutex_,
                sgns::base::createLogger( "ColdStorageResurrect" ) );
            remote_store_mutex_ = std::make_shared<std::mutex>();
            cold_read_service_  = std::make_shared<ColdReadService>(
                shared_local_, remote_, cold_store_mutex_, remote_store_mutex_, decorator_ );
            dagsyncer_ = std::make_shared<GraphsyncDAGSyncer>( decorator_, nullptr, nullptr,
                                                               cold_read_service_ );

            // The pubsub receive lane: the CRDT datastore runs over a
            // CRDTMirrorBroadcaster whose queue the FEEDER mirror writes
            // into (the crdt_datastore_test loop-back topology) - every
            // feeder_->Broadcast lands in the running HandleCIDBroadcast
            // thread: decode -> HasBlock dedup -> walk/merge. The REAL
            // pubsub-path processing, no protected surface.
            mirror_  = std::make_shared<CRDTMirrorBroadcaster>();
            feeder_  = std::make_shared<CRDTMirrorBroadcaster>();
            feeder_->SetMirrorCounterPart( mirror_ );
            crdt_    = CrdtDatastore::New(
                real_hot_, HierarchicalKey( "/namespace" ), dagsyncer_, mirror_,
                CrdtOptions::DefaultOptions() );
            ASSERT_TRUE( crdt_ != nullptr );
            crdt_->Start();
        }

        void TearDown() override
        {
            if ( manager_ )
            {
                manager_->Stop();
            }
            if ( crdt_ )
            {
                crdt_->CancelAndCloseNow();
            }
            if ( feeder_ )
            {
                feeder_->SetMirrorCounterPart( nullptr ); // drain-lane hygiene
            }
        }

        std::shared_ptr<ColdStorageManager> MakeManager(
            const ColdStorageManager::RetentionOptions &retention )
        {
            auto remote_store_mutex = std::make_shared<std::mutex>();
            manager_                = std::make_shared<ColdStorageManager>(
                shared_local_,
                remote_,
                cold_store_mutex_,
                remote_store_mutex,
                FastOptions(),
                FastDurability(),
                fake_admin_,
                retention );
            manager_->AttachGcDependencies( crdt_.get(), dagsyncer_.get(), real_hot_ );
            return manager_;
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

        /// True when the key's current-value query returns entries (the
        /// resurrection oracle: ANY entry = the value came back).
        bool ValueVisible( const std::string &key )
        {
            auto query = crdt_->QueryKeyValues( key );
            if ( query.has_error() )
            {
                return false;
            }
            return !query.value().empty();
        }

        /// Let an explicit tiny grace elapse (the PollUntil discipline).
        void GraceElapse()
        {
            const auto early = std::chrono::steady_clock::now() + std::chrono::milliseconds( 3 );
            ASSERT_TRUE( PollUntil( [&] { return std::chrono::steady_clock::now() >= early; },
                                    std::chrono::milliseconds( 1000 ) ) );
        }

        /**
         * @brief  The put -> delete -> detach -> evict-until-disposed
         *         sequence shared by the single-node legs.
         *
         * The delete delta LINKS the put delta (every Publish points at
         * the previous heads), so GC-01's reachability pin would keep
         * BOTH alive; the RemoveHead detach (the Phase-11
         * EvictDetachedDelta mechanics) makes both blocks
         * cold-eligible while the tombstone ROWS - which are never
         * purged - keep the value deleted. Returns the pre-delete delta
         * CID (the re-delivery vector).
         */
        CID DeleteAndEvict( const std::string &key, const std::string &value,
                            const ColdStorageManager::RetentionOptions &retention )
        {
            const CID created = PublishElement( key, value );
            EXPECT_TRUE( ValueVisible( key ) ) << "the value is visible before the delete";

            auto removed = crdt_->DeleteKey( HierarchicalKey( key ), { "topic" } );
            EXPECT_TRUE( removed.has_value() ) << "the CRDT delete must publish its tombstone";
            EXPECT_FALSE( ValueVisible( key ) ) << "tombstoned before GC - the baseline";
            const CID tombstone = removed.value();
            // The detach: the tombstone head leaves the heads set, both
            // delta blocks become unreachable (the GC-01 contract - only
            // unreachable history is evictable).
            EXPECT_TRUE( crdt_->RemoveHead( tombstone, "topic" ).has_value() );

            const Cid created_cid( created );
            const Cid tomb_cid( tombstone );
            auto manager = MakeManager( retention );
            manager->Start();
            EXPECT_TRUE( PollUntil(
                [&] {
                    auto s1 = manager_->GetUploadState( created_cid );
                    auto s2 = manager_->GetUploadState( tomb_cid );
                    return s1.has_value() && s1.value() == UploadState::UploadedDurable
                           && s2.has_value() && s2.value() == UploadState::UploadedDurable;
                },
                std::chrono::milliseconds( 10000 ) ) )
                << "the deltas never reached Durable";

            // Backdate + two driven passes: mark, then (after the tiny
            // explicit grace elapses) evict BOTH deltas.
            {
                std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
                EXPECT_TRUE(
                    shared_local_->SetSpilledAt( created_cid, UnixNowMs() - 60000 ).has_value() );
                EXPECT_TRUE(
                    shared_local_->SetSpilledAt( tomb_cid, UnixNowMs() - 60000 ).has_value() );
            }
            EXPECT_EQ( manager_->GcPassOnce().size(), 2u );
            GraceElapse();
            (void)manager_->GcPassOnce();

            {
                std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
                auto d1 = shared_local_->IsDisposed( created_cid );
                auto d2 = shared_local_->IsDisposed( tomb_cid );
                EXPECT_TRUE( d1.has_value() && d1.value() )
                    << "the pre-delete delta must be fully evicted before the re-delivery";
                EXPECT_TRUE( d2.has_value() && d2.value() )
                    << "the tombstone delta must be fully evicted before the re-delivery";
            }
            return created;
        }

        // Declaration order == reverse destruction order.
        std::string                             hot_dir_;
        std::shared_ptr<rocksdb>                real_hot_;
        std::string                             shared_local_dir_;
        std::shared_ptr<LocalBlockStore>        shared_local_;
        std::shared_ptr<InMemoryS3Client>       fake_;
        std::shared_ptr<CountingS3Client>       counting_;
        std::shared_ptr<S3BlockStore>           remote_;
        std::shared_ptr<InMemoryS3AdminClient>  fake_admin_;
        std::shared_ptr<std::mutex>             cold_store_mutex_;
        std::shared_ptr<sgns::ipfs_lite::ipfs::IpfsDatastore> inner_;
        std::shared_ptr<BlockStoreBackedIpfsDatastore> decorator_;
        std::shared_ptr<std::mutex>             remote_store_mutex_;
        std::shared_ptr<ColdReadService>        cold_read_service_;
        std::shared_ptr<GraphsyncDAGSyncer>     dagsyncer_;
        std::shared_ptr<CRDTMirrorBroadcaster> mirror_;
        std::shared_ptr<CRDTMirrorBroadcaster> feeder_;
        std::shared_ptr<CrdtDatastore>          crdt_;
        std::shared_ptr<ColdStorageManager>     manager_;
    };
} // namespace

/**
 * @given a value put, CRDT-deleted, head-detached, and GC-evicted past
 *        retention (both deltas carry disposed markers; the S3 fake
 *        holds the only copies)
 * @when  the pre-delete delta is re-delivered through the pubsub path
 *        (the four-term HasBlock call shape the pubsub receive path
 *        drives per announced CID - the 11-03 storm-test mechanics), K
 *        times
 * @then  EVERY delivery drops at the disposed-marker term with ZERO S3
 *        Gets (the message never reaches the merge path), no new
 *        Pending journal record appears, and the value stays absent.
 */
TEST_F( ColdStorageResurrectionTest, Resurrect_MarkerPresent_DedupBlocksReintegration )
{
    const std::string key     = "resurrect/marker";
    const std::string value   = "the-value";
    const CID         created = DeleteAndEvict( key, value, RetentionTiny() );

    const uint64_t gets_before = counting_->get_calls();
    const Cid     created_cid( created );
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto pending = shared_local_->GetPendingUploads();
        ASSERT_TRUE( pending.has_value() );
        EXPECT_TRUE( pending.value().empty() ) << "baseline: nothing pending";
    }
    EXPECT_FALSE( manager_->GetUploadState( created_cid ).has_value() )
        << "baseline: no journal record for the evicted delta";

    // The re-delivery through the REAL pubsub receive lane: the feeder
    // mirror lands the CID announcement in the running
    // HandleCIDBroadcast queue - decode, HasBlock dedup, (would-be)
    // walk/merge. K deliveries.
    constexpr int kRedeliveries = 25;
    for ( int i = 0; i < kRedeliveries; ++i )
    {
        ASSERT_TRUE(
            feeder_->Broadcast( EncodeCidAnnouncement( created ), "topic" ).has_value() );
    }

    // A bounded observation window for the would-be fetch/merge (the
    // PollUntil deadline discipline - never a bare sleep).
    const auto window_end = std::chrono::steady_clock::now() + std::chrono::milliseconds( 1500 );
    ASSERT_TRUE( PollUntil( [&] { return std::chrono::steady_clock::now() >= window_end; },
                            std::chrono::milliseconds( 2000 ) ) );

    // ZERO S3 traffic across the whole re-delivery storm.
    EXPECT_EQ( counting_->get_calls(), gets_before )
        << "dedup drops every re-delivery with ZERO S3 Gets";

    // No re-integration: no new journal record (the marker term answered
    // HasBlock before any walk started).
    EXPECT_FALSE( manager_->GetUploadState( created_cid ).has_value() )
        << "no re-journal - the message never merged";
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto pending = shared_local_->GetPendingUploads();
        ASSERT_TRUE( pending.has_value() );
        EXPECT_TRUE( pending.value().empty() ) << "no re-journal - the message never merged";
    }

    // The value stays absent.
    EXPECT_FALSE( ValueVisible( key ) ) << "the value must NOT resurrect";
}

/**
 * @given the same put -> delete -> detach -> evict state, with BOTH
 *        disposed markers TRIMMED (TrimDisposed(0, 0) erases the dedup
 *        line; the marker set is bounded in production by the D-10 dual
 *        bound)
 * @when  the pre-delete delta resolves through the cold chain and its
 *        content is re-merged through the REAL merge path
 *        (MergeDataFromDelta - exactly what the pubsub lane runs after
 *        a trimmed-marker miss)
 * @then  the CRDT-delete tombstone DOMINATES the re-delivered
 *        lower-height Add element: the value stays absent in the query,
 *        and the remote S3 copy of the delta stays intact.
 */
TEST_F( ColdStorageResurrectionTest, Resurrect_TrimmedMarker_TombstoneDominanceKeepsValueAbsent )
{
    const std::string key     = "resurrect/trimmed";
    const std::string value   = "the-value";
    const CID         created = DeleteAndEvict( key, value, RetentionTiny() );
    const Cid         cid( created );

    // Trim EVERY disposed marker: the dedup line is gone (the bounded
    // memory posture), so a re-delivery re-enters the merge path.
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        // count_cap 0 disables the count bound; max_age = now drops
        // every marker with disposed_at STRICTLY below now (both are).
        ASSERT_TRUE( shared_local_->TrimDisposed( 0, UnixNowMs() ).has_value() );
        auto disposed = shared_local_->IsDisposed( cid );
        ASSERT_TRUE( disposed.has_value() );
        EXPECT_FALSE( disposed.value() ) << "the marker is trimmed - dedup cannot fire";
    }

    // Re-deliver through the REAL pubsub receive lane: the dedup line
    // is gone, so the lane walks the CID - the cold chain resolves the
    // delta from the remote copy (the documented TrimDisposed
    // re-learn: re-journal -> re-spill -> re-drain) and the merge runs.
    const uint64_t gets_before = counting_->get_calls();
    ASSERT_TRUE(
        feeder_->Broadcast( EncodeCidAnnouncement( created ), "topic" ).has_value() );
    ASSERT_TRUE( PollUntil( [&] { return counting_->get_calls() > gets_before; },
                            std::chrono::milliseconds( 10000 ) ) )
        << "the trimmed-marker fallback must fetch the delta from the remote copy";

    // The merge ran (the S3 fetch IS the walk evidence); let the worker
    // finish processing, bounded.
    const auto settle_end = std::chrono::steady_clock::now() + std::chrono::milliseconds( 1500 );
    ASSERT_TRUE( PollUntil( [&] { return std::chrono::steady_clock::now() >= settle_end; },
                            std::chrono::milliseconds( 2000 ) ) );

    // TOMBSTONE DOMINANCE: the value stays absent despite the merge.
    EXPECT_FALSE( ValueVisible( key ) )
        << "the CRDT-delete tombstone dominates the re-delivered Add";

    // The delta stays retrievable after the re-learn: the merge's fetch
    // re-journaled + re-wrote the block locally (the documented
    // TrimDisposed re-learn contract), so the next read resolves WITHOUT
    // further S3 traffic - the remote copy made the re-learn safe.
    const uint64_t gets_after_merge = counting_->get_calls();
    auto again = dagsyncer_->GetNodeWithoutRequest( created );
    ASSERT_TRUE( again.has_value() ) << "the delta stays retrievable post re-learn";
    EXPECT_EQ( counting_->get_calls(), gets_after_merge )
        << "the re-learned local copy serves the next read";
}

namespace
{
    /// The libp2p/soralog bootstrap the full-wire hosts need (the
    /// pubsub_graphsync_test SetUpTestSuite shape, trimmed to the
    /// console sink).
    std::string ResurrectLoggingConfig()
    {
        return R"(
 sinks:
   - name: console
     type: console
     color: true
 groups:
   - name: gossip_pubsub_test
     sink: console
     level: error
     children:
       - name: libp2p
       - name: Gossip
       - name: debug
 )";
    }
} // namespace

/**
 * @given two REAL GlobalDB instances over the MultiGlobalDBTest
 *        topology (two GossipPubSub hosts on separate io contexts, real
 *        in-process libp2p + graphsync wire, cross-connected peers),
 *        the RECEIVER with cold storage ON through the TEST-ONLY stack
 *        client override (hermetic InMemoryS3Client/InMemoryS3AdminClient
 *        fakes - zero network) and tiny-positive retention so the real
 *        background GC thread evicts naturally
 * @when  node 1 puts the value (the delta propagates to node 2 over the
 *        wire and journals there), node 1 CRDT-deletes it (the
 *        tombstone propagates), node 2 detaches the tombstone head (the
 *        GC-01 reachability contract), the background GC evicts both
 *        deltas (GetColdStorageStatus disposed-marker evidence), and
 *        node 1 then RE-BROADCASTS the original pre-delete CID
 *        announcement over the gossip topic (the realistic peer
 *        re-broadcast vector)
 * @then  the receiver drops the re-announcement at the disposed-marker
 *        dedup: ZERO S3 Gets on the receiver's counting client, ZERO
 *        re-journal (pending stays 0), and the value stays absent in
 *        BOTH nodes' queries (D-13, the SC2 full-wire proof).
 */
TEST_F( ColdStorageResurrectionTest, Resurrect_FullWire_TwoGlobalDBRedeliveryStaysDeleted )
{
    // Logging bootstrap (once per process is fine; idempotent enough).
    {
        auto log_system = std::make_shared<soralog::LoggingSystem>(
            std::make_shared<soralog::ConfiguratorFromYAML>(
                std::make_shared<libp2p::log::Configurator>(), ResurrectLoggingConfig() ) );
        (void)log_system->configure();
        libp2p::log::setLoggingSystem( log_system );
    }

    const std::string topic = "resurrect-topic";
    const std::string key   = "resurrect/fullwire";
    const std::string value = "the-value";

    // --- The hermetic S3 pair node 2's FULL stack will run on.
    auto fake2      = std::make_shared<InMemoryS3Client>();
    auto counting2  = std::make_shared<CountingS3Client>( fake2 );
    auto fake2_admin = std::make_shared<InMemoryS3AdminClient>();

    // Two pubsub hosts on separate io contexts (the
    // pubsub_graphsync_test construction).
    auto pubs1        = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();
    auto start1_future = pubs1->Start( 0, {} );
    auto pubs2        = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>();
    auto start2_future = pubs2->Start( 0, {} );
    ASSERT_TRUE( PollUntil(
        [&] {
          if ( start1_future.wait_for( std::chrono::milliseconds( 0 ) ) != std::future_status::ready )
          {
              return false;
          }
          if ( start2_future.wait_for( std::chrono::milliseconds( 0 ) ) != std::future_status::ready )
          {
              return false;
          }
          return !start1_future.get() && !start2_future.get();
        },
        std::chrono::milliseconds( 20000 ) ) )
        << "pubsub hosts failed to start";

    // Each graphsync network rides its own host's scheduler (the
    // single-threaded-per-host discipline from the topology precedent).
    auto io_context1 = pubs1->GetAsioContext();
    auto io_context2 = pubs2->GetAsioContext();
    auto scheduler1  = std::make_shared<libp2p::basic::SchedulerImpl>(
        std::make_shared<libp2p::basic::AsioSchedulerBackend>( io_context1 ),
        libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
    auto network1 = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubs1->GetHost(),
                                                                          scheduler1 );
    auto generator1 = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();
    auto scheduler2  = std::make_shared<libp2p::basic::SchedulerImpl>(
        std::make_shared<libp2p::basic::AsioSchedulerBackend>( io_context2 ),
        libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
    auto network2 = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubs2->GetHost(),
                                                                          scheduler2 );
    auto generator2 = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();

    // --- Node 2's cold-storage posture: ON, tiny-positive retention so
    // the real background GC thread evicts naturally (driven passes are
    // not reachable inside a full GlobalDB - GetColdStorageStatus is the
    // observability surface), hermetic fakes via the TEST-ONLY override.
    GlobalDB::ColdStorageOptions cold;
    cold.enabled   = true;
    cold.endpoint  = "hermetic.invalid"; // overridden below - never contacted
    cold.bucket    = "resurrect";
    cold.accessKey = "test";
    cold.secretKey = "test";
    cold.backpressure.drain_interval_ms = 50;
    cold.backpressure.warn_pending      = 100;
    cold.backpressure.critical_pending  = 200;
    cold.backpressure.backoff_cap_ms    = 1000;
    cold.durability.flush_min_interval_ms = 30;
    cold.durability.flush_max_interval_ms = 100;
    cold.retention.retention_age_ms     = 1000;
    cold.retention.grace_ms             = 100;
    cold.retention.gc_interval_ms       = 200;
    sgns::crdt::SetColdStorageStackClientsForTesting(
        std::static_pointer_cast<S3Client>( counting2 ),
        std::static_pointer_cast<S3AdminClient>( fake2_admin ) );

    const std::string n1_path = FreshDir( "fw-n1" );
    const std::string n2_path = FreshDir( "fw-n2" );
    auto gdb1_ret = GlobalDB::New( io_context1, n1_path, pubs1,
                                   CrdtOptions::DefaultOptions(), network1, scheduler1,
                                   generator1, nullptr, GlobalDB::BackupOptions{ false, 15, 12, true },
                                   GlobalDB::ColdStorageOptions{} /* node 1: cold OFF */ );
    auto gdb2_ret = GlobalDB::New( io_context2, n2_path, pubs2,
                                   CrdtOptions::DefaultOptions(), network2, scheduler2,
                                   generator2, nullptr, GlobalDB::BackupOptions{ false, 15, 12, true },
                                   cold );
    ASSERT_TRUE( gdb1_ret.has_value() ) << "node 1 GlobalDB failed to construct";
    ASSERT_TRUE( gdb2_ret.has_value() ) << "node 2 GlobalDB failed to construct";
    auto gdb1 = std::move( gdb1_ret.value() );
    auto gdb2 = std::move( gdb2_ret.value() );
    // Hygiene: the process-global override has served its purpose.
    sgns::crdt::SetColdStorageStackClientsForTesting( nullptr, nullptr );

    gdb1->AddBroadcastTopic( topic );
    gdb2->AddBroadcastTopic( topic );
    gdb1->AddListenTopic( topic );
    gdb2->AddListenTopic( topic );
    gdb1->Start();
    gdb2->Start();
    pubs1->AddPeers( { pubs2->GetInterfaceAddress() } );

    // PUT on node 1: the delta must propagate to node 2 over the wire.
    Buffer value_buffer;
    value_buffer.put( value );
    auto put_ret = gdb1->Put( HierarchicalKey( key ), value_buffer, { topic } );
    ASSERT_TRUE( put_ret.has_value() ) << "node 1 put failed";
    const CID created = put_ret.value();
    const Cid  created_cid( created );

    // DELETE on node 1: the tombstone must propagate too.
    auto tomb_ret = gdb1->GetCRDTDataStore()->DeleteKey( HierarchicalKey( key ), { topic } );
    ASSERT_TRUE( tomb_ret.has_value() ) << "node 1 delete failed";
    const CID tombstone = tomb_ret.value();

    // Propagation evidence on node 2: BOTH deltas journaled AND drained
    // to Durable there (its decorator journals every received block) -
    // this proves the wire delivered the put AND the delete. 180 s: the
    // full-wire leg boots two real libp2p hosts and this suite runs
    // beside the FULL CI test load (-j) on shared runners (runs
    // 40840831965/40927749627: 90 s still tripped on the loaded mac).
    ASSERT_TRUE( PollUntil(
        [&] {
          auto status = gdb2->GetColdStorageStatus();
          return status.durable_upload_count >= 2;
        },
        std::chrono::milliseconds( 180000 ) ) )
        << "node 2 never received+drained both deltas";
    {
        auto q1 = gdb1->QueryKeyValues( key );
        auto q2 = gdb2->QueryKeyValues( key );
        ASSERT_TRUE( q1.has_value() && q2.has_value() );
        EXPECT_TRUE( q1.value().empty() ) << "node 1: value deleted";
        EXPECT_TRUE( q2.value().empty() ) << "node 2: value deleted after propagation";
    }

    // The GC-01 detach on the receiver: the tombstone head leaves the
    // heads set, both delta blocks become unreachable - the retention
    // contract's evictable shape (tombstone ROWS keep the value dead).
    ASSERT_TRUE( gdb2->GetCRDTDataStore()->RemoveHead( tombstone, topic ).has_value() );

    // Real background GC eviction evidence: disposed markers for (at
    // least) both deltas, observed through the operator status surface.
    // 180 s (CI-load deadline, runs 40840831965/40927749627: eviction
    // needs propagation + drain + flush + 1 s retention age + GC cadence,
    // all stretched by the full-suite -j load on shared runners).
    ASSERT_TRUE( PollUntil(
        [&] {
          auto status = gdb2->GetColdStorageStatus();
          return status.gc_enabled && status.gc_disposed_marker_count >= 2;
        },
        std::chrono::milliseconds( 180000 ) ) )
        << "node 2's background GC never evicted the detached deltas; final status: "
        << [ & ] {
             auto s = gdb2->GetColdStorageStatus();
             std::ostringstream oss;
             oss << "gc_enabled=" << s.gc_enabled
                 << " gc_inhibited_staging=" << s.gc_inhibited_staging
                 << " gc_evicting_count=" << s.gc_evicting_count
                 << " gc_last_pass_candidates=" << s.gc_last_pass_candidates
                 << " gc_last_pass_unix_ms=" << s.gc_last_pass_unix_ms
                 << " gc_disposed_marker_count=" << s.gc_disposed_marker_count
                 << " durable=" << s.durable_upload_count
                 << " staging_healthy=" << s.staging_healthy
                 << " flush_backoff=" << s.flush_backoff_active;
             return oss.str();
           }();

    // THE RE-DELIVERY over the real gossip wire: node 1 re-broadcasts
    // the ORIGINAL pre-delete CID announcement through its
    // PubSubBroadcasterExt (the public Broadcaster surface - the exact
    // publish path every Publish rides; a peer replays a stored head).
    const uint64_t gets_before     = counting2->get_calls();
    const auto     pending_before = gdb2->GetColdStorageStatus().pending_upload_count;
    auto           bcast1         = gdb1->GetBroadcaster();
    ASSERT_TRUE( bcast1 != nullptr );
    auto rebroadcast = bcast1->Broadcast( EncodeCidAnnouncement( created ), topic );
    ASSERT_TRUE( rebroadcast.has_value() ) << "the re-broadcast must publish";

    // A bounded observation window for the would-be fetch/merge (the
    // PollUntil deadline discipline - never a bare sleep).
    const auto window_end = std::chrono::steady_clock::now() + std::chrono::milliseconds( 1500 );
    ASSERT_TRUE( PollUntil( [&] { return std::chrono::steady_clock::now() >= window_end; },
                            std::chrono::milliseconds( 2000 ) ) );

    // DEDUP HELD: zero S3 Gets on the receiver, zero re-journal.
    EXPECT_EQ( counting2->get_calls(), gets_before )
        << "the disposed-marker dedup dropped the re-announcement with ZERO S3 Gets";
    EXPECT_EQ( gdb2->GetColdStorageStatus().pending_upload_count, pending_before )
        << "no re-integration - nothing re-journaled on the receiver";

    // The value stays absent on BOTH nodes.
    {
        auto q1 = gdb1->QueryKeyValues( key );
        auto q2 = gdb2->QueryKeyValues( key );
        ASSERT_TRUE( q1.has_value() && q2.has_value() );
        EXPECT_TRUE( q1.value().empty() ) << "node 1: still deleted";
        EXPECT_TRUE( q2.value().empty() ) << "node 2: the re-delivery did NOT resurrect";
    }
}
