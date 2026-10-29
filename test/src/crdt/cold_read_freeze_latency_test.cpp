/**
 * @file       cold_read_freeze_latency_test.cpp
 * @brief      SC2 latency-budget benchmark + responder-freeze regression
 *             (READ-02, v1.1 Phase 9 plan 09-02) - hermetic, Tier-1.
 *
 * Registered ONLY under SGNS_COLD_STORAGE. Direct construction (the
 * 09-RESEARCH §10.1 cut): the real ColdReadService (re-warm executor
 * started) over a real LocalBlockStore and an S3BlockStore backed by a
 * test-local instrumented S3Client (per-Get latency injection + Get call
 * counting - the fake has no native hooks for either), plus a minimal
 * fake inner MerkleDagService so ColdServeDagService is exercised at its
 * real seam without libp2p.
 *
 * ─── D-12 BUILT-IN DEFAULTS (this banner IS the documented measured
 *     cold-serve latency budget SC2 asks for) ───
 *
 * Serve posture (D-05): immediate honest don't-have + coalesced
 * background re-warm. The responder NEVER waits: a getNode miss
 * delegates, try-enqueues one re-warm, and returns the failure so the
 * graphsync layer answers RS_NOT_FOUND now; the peer's own retry ladder
 * (5 s -> 60 s failed-root backoff, 4 s per-(peer,cid) suppression)
 * finds the re-warmed block hot. Hold-to-deadline was rejected: it is
 * not implementable on the single-thread responder and would inflate
 * concurrent held requests across peers.
 *
 *   kRewarmBudget            = 4000 ms - must complete before the peer's
 *                               ~5 s first failed-root retry and matches
 *                               CID_FAILURE_SUPPRESSION so the retry
 *                               window and warm-completion window align;
 *                               far below the requester's 20 s total
 *                               fetch budget.
 *   re-warm worker count     = 1      - re-warms are rare; one worker
 *                               bounds pinned threads to at most one and
 *                               keeps remote-store serialization trivial.
 *   kMaxRewarmQueueDepth     = 64     - bounds re-warm memory; overflow
 *                               drops + counts and the next request
 *                               re-enqueues (D-07).
 *   kRewarmShutdownGraceWindow = 5000 ms - Phase-8 parity.
 *
 * ─── MEASURED (same-latency-distribution discipline: every number below
 *     is a distribution under identical injected latency - never an
 *     ON-vs-OFF build comparison; 08-CONTEXT SC1) ───
 *
 * (a) Inline chain-fetch latency (cold CID, Local miss + S3 hit), 8
 *     samples per injected per-Get latency, Windows dev machine
 *     2026-10-26 (observed via [sc2-evidence] output):
 *       injected  10 ms: p50 = 15 ms,   p95 = 21 ms
 *       injected  50 ms: p50 = 62 ms,   p95 = 63 ms
 *       injected 200 ms: p50 = 203 ms,  p95 = 204 ms
 *       injected 500 ms: p50 = 508 ms,  p95 = 515 ms
 *     (chain cost = injected S3 latency + ~5-15 ms fixed overhead; the
 *      500 ms p95 sits far under the 4 s re-warm budget)
 * (d) Concurrent local reads of ONE cold CID (4 threads, 50 ms
 *     injected): duplicate S3 Gets observed = 4 - read-path coalescing
 *     is deliberately NOT built this phase (09-RESEARCH §8.3); the
 *     idempotent re-cache makes duplicates harmless. Phase-12 soak data.
 *
 * Hard assertions in the suite: (b) time-to-warm < kRewarmBudget at
 * 500 ms injected latency (SC2_TimeToWarmWithinBudgetAt500MsLatency);
 * (c) hot-hit serve through the wrapper < 50 ms while >= 16 cold misses
 * are queued - both the slow-500 ms scenario
 * (SC2_HotServeUnderSaturatedSlowQueue) and the faulted fail-fast
 * outage with a churning queue (SC2_HotServeUnderOutageFailFastQueueChurning,
 * plus the miss-path bound SC2_OutageMissReturnsImmediately_FailFast) -
 * the freeze regression; measurements (a) and (d) are recorded above via
 * InlineChainLatencyByInjectedGetLatency and
 * ConcurrentColdReadsDuplicateGetCount.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
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

#include <ipfs_lite/ipld/impl/ipld_node_impl.hpp>
#include <ipfs_lite/ipfs/ipfs_datastore_error.hpp>

#include "base/logger.hpp"
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

    namespace ipfs_outcome = sgns::IPFS::outcome;

    using sgns::crdt::BlockStoreBackedIpfsDatastore;
    using sgns::crdt::ColdReadService;
    using sgns::crdt::ColdServeDagService;
    using IpfsDatastore      = sgns::ipfs_lite::ipfs::IpfsDatastore;
    using IpfsDatastoreError = sgns::ipfs_lite::ipfs::IpfsDatastoreError;
    using MerkleDagService   = sgns::ipfs_lite::ipfs::merkledag::MerkleDagService;
    using CID                = sgns::CID;

    /// Bounded poll loop with deadline - never a fixed sleep.
    bool PollUntil( const std::function<bool()> &predicate,
                    std::chrono::milliseconds       deadline,
                    std::chrono::milliseconds       step = std::chrono::milliseconds( 5 ) )
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

    Cid MakeCid( const std::string &payload )
    {
        auto cid = CidOf( SpanOf( payload ), MulticodecType::Code::RAW );
        return cid.value();
    }

    std::string NextUniqueDir()
    {
        static std::atomic<uint64_t> next{ 0 };
        return "test-data/cold-read-freeze/case-" + std::to_string( next.fetch_add( 1 ) );
    }

    std::string FreshDir()
    {
        auto dir = NextUniqueDir();
        std::error_code ec;
        std::filesystem::remove_all( dir, ec );
        return dir;
    }

    /// p50/p95 of a sample set (ms) - the SC2 distribution statistics.
    struct TimingStats
    {
        double p50_ms;
        double p95_ms;
    };

    TimingStats StatsOf( std::vector<double> samples )
    {
        EXPECT_FALSE( samples.empty() );
        std::sort( samples.begin(), samples.end() );
        TimingStats stats;
        stats.p50_ms = samples[samples.size() / 2];
        stats.p95_ms = samples[( samples.size() * 95 + 99 ) / 100 - 1]; // ceil(0.95*n)-1
        return stats;
    }

    /**
     * @brief Test-local in-memory inner IpfsDatastore - the hot store the
     *        re-warm re-caches into (spy-free; presence is the probe).
     */
    class MapInnerDatastore final : public IpfsDatastore
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
            (void)key;
            return ipfs_outcome::success();
        }

        ipfs_outcome::result<bool> is_sealed( const CID &key ) const override
        {
            (void)key;
            return false;
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
    };

    /**
     * @brief Instrumented S3Client: atomic per-Get latency injection +
     *        Get call counting in one wrapper (the fake has neither hook).
     */
    class InstrumentedS3Client final : public sgns::blockstore::S3Client
    {
    public:
        explicit InstrumentedS3Client( std::shared_ptr<InMemoryS3Client> inner ) : inner_( std::move( inner ) )
        {
        }

        void SetGetLatency( std::chrono::milliseconds per_get_latency )
        {
            latency_ms_.store( per_get_latency.count() );
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
        std::atomic<int64_t>              latency_ms_{ 0 };
        std::atomic<size_t>               get_calls_{ 0 };
    };

    /**
     * @brief Minimal fake inner MerkleDagService: one hot CID serves a
     *        cached node; everything else misses - ColdServeDagService is
     *        exercised at its real seam without libp2p.
     */
    class FakeHotMissDagService final : public MerkleDagService
    {
    public:
        FakeHotMissDagService()
            : hot_node_( sgns::ipfs_lite::ipld::IPLDNodeImpl::createFromString( "hot hit payload" ) )
        {
            hot_cid_key_ = CID( hot_node_->getCID() );
        }

        CID hot_cid() const
        {
            return hot_cid_key_;
        }

        ipfs_outcome::result<std::shared_ptr<sgns::ipfs_lite::ipld::IPLDNode>> getNode(
            const CID &cid ) const override
        {
            if ( cid == hot_cid_key_ )
            {
                return hot_node_;
            }
            return ipfs_outcome::failure( IpfsDatastoreError::NOT_FOUND );
        }

        ipfs_outcome::result<void> addNode( std::shared_ptr<const sgns::ipfs_lite::ipld::IPLDNode> node ) override
        {
            (void)node;
            return ipfs_outcome::success();
        }

        ipfs_outcome::result<void> markResolved( const CID &cid ) override
        {
            (void)cid;
            return ipfs_outcome::success();
        }

        ipfs_outcome::result<bool> isResolved( const CID &cid ) const override
        {
            (void)cid;
            return false;
        }

        ipfs_outcome::result<void> removeNode( const CID &cid ) override
        {
            (void)cid;
            return ipfs_outcome::success();
        }

        ipfs_outcome::result<size_t> select(
            gsl::span<const uint8_t> /*root_cid*/,
            gsl::span<const uint8_t> /*selector*/,
            std::function<bool( std::shared_ptr<const sgns::ipfs_lite::ipld::IPLDNode> node )> /*handler*/ ) const override
        {
            return ipfs_outcome::failure( IpfsDatastoreError::NOT_FOUND );
        }

        ipfs_outcome::result<std::shared_ptr<sgns::ipfs_lite::ipfs::merkledag::Leaf>> fetchGraph(
            const CID &cid ) const override
        {
            (void)cid;
            return ipfs_outcome::failure( IpfsDatastoreError::NOT_FOUND );
        }

        ipfs_outcome::result<std::shared_ptr<sgns::ipfs_lite::ipfs::merkledag::Leaf>> fetchGraphOnDepth(
            const CID &cid,
            uint64_t /*depth*/ ) const override
        {
            (void)cid;
            return ipfs_outcome::failure( IpfsDatastoreError::NOT_FOUND );
        }

    private:
        std::shared_ptr<sgns::ipfs_lite::ipld::IPLDNode> hot_node_;
        CID                                              hot_cid_key_;
    };

    class ColdReadFreezeLatencyTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            auto opened_local = LocalBlockStore::Open( LocalBlockStoreOptions{ FreshDir() } );
            ASSERT_TRUE( opened_local.has_value() );
            shared_local_ = std::shared_ptr<LocalBlockStore>( std::move( opened_local.value() ) );

            fake_     = std::make_shared<InMemoryS3Client>();
            injected_ = std::make_shared<InstrumentedS3Client>( fake_ );
            auto opened_remote = S3BlockStore::Open( injected_ );
            ASSERT_TRUE( opened_remote.has_value() );
            remote_ = std::move( opened_remote.value() );

            cold_store_mutex_   = std::make_shared<std::mutex>();
            remote_store_mutex_ = std::make_shared<std::mutex>();
            inner_              = std::make_shared<MapInnerDatastore>();
            decorator_          = std::make_shared<BlockStoreBackedIpfsDatastore>(
                inner_, shared_local_, cold_store_mutex_, sgns::base::createLogger( "ColdReadFreeze" ) );
            service_ = std::make_shared<ColdReadService>(
                shared_local_, remote_, cold_store_mutex_, remote_store_mutex_, decorator_ );
            fake_dag_ = std::make_shared<FakeHotMissDagService>();
            wrapper_  = std::make_shared<ColdServeDagService>( fake_dag_, service_.get() );
            service_->Start();
        }

        void TearDown() override
        {
            if ( service_ )
            {
                service_->Stop(); // bounded grace + join before members die
            }
        }

        /// Store a fresh S3-only block and return its CID key.
        CID SeedColdBlock( const std::string &payload )
        {
            const auto cid = MakeCid( payload );
            auto put = remote_->Put( cid, SpanOf( payload ) );
            EXPECT_TRUE( put.has_value() );
            return CID( cid );
        }

        std::shared_ptr<LocalBlockStore>               shared_local_;
        std::shared_ptr<InMemoryS3Client>              fake_;
        std::shared_ptr<InstrumentedS3Client>          injected_;
        std::shared_ptr<S3BlockStore>                  remote_;
        std::shared_ptr<std::mutex>                    cold_store_mutex_;
        std::shared_ptr<std::mutex>                    remote_store_mutex_;
        std::shared_ptr<MapInnerDatastore>             inner_;
        std::shared_ptr<BlockStoreBackedIpfsDatastore> decorator_;
        std::shared_ptr<ColdReadService>               service_;
        std::shared_ptr<FakeHotMissDagService>         fake_dag_;
        std::shared_ptr<ColdServeDagService>           wrapper_;
    };
} // namespace

/**
 * @given distinct cold CIDs at injected S3 Get latencies 10/50/200/500 ms
 * @when  the inline chain fetches each (Local miss + S3 hit)
 * @then  p50/p95 per latency level are RECORDED (banner above) as the
 *        READ-01 chain-cost evidence - the chain cost tracks the
 *        injected latency with single-digit-ms overhead, and the 500 ms
 *        p95 sits far under the 4 s re-warm budget.
 */
TEST_F( ColdReadFreezeLatencyTest, InlineChainLatencyByInjectedGetLatency )
{
    const std::vector<int64_t> latencies_ms = { 10, 50, 200, 500 };
    for ( const auto latency_ms : latencies_ms )
    {
        injected_->SetGetLatency( std::chrono::milliseconds( latency_ms ) );
        std::vector<double> samples;
        for ( int i = 0; i < 8; ++i )
        {
            const CID cid_key = SeedColdBlock( "latency sample " + std::to_string( latency_ms ) + "-" + std::to_string( i ) );
            sgns::common::Buffer out;
            const auto started_at = std::chrono::steady_clock::now();
            (void)service_->Get( cid_key, out );
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started_at );
            ASSERT_FALSE( out.empty() );
            samples.push_back( static_cast<double>( elapsed.count() ) );
        }
        const auto stats = StatsOf( samples );
        // Sanity bound only (the measured record lives in the banner):
        // overhead beyond the injected latency is single-digit ms.
        EXPECT_LT( stats.p95_ms, static_cast<double>( latency_ms ) + 50.0 );
        std::cout << "[sc2-evidence] inline chain: injected=" << latency_ms << "ms p50=" << stats.p50_ms
                  << "ms p95=" << stats.p95_ms << "ms\n";
    }
    injected_->SetGetLatency( std::chrono::milliseconds( 0 ) );
}

/**
 * @given a re-warm enqueued for an S3-only block at 500 ms injected Get
 *        latency
 * @when  the worker processes it
 * @then  the block is hot well INSIDE the 4 s kRewarmBudget (hard bound;
 *        SC2 time-to-warm evidence).
 */
TEST_F( ColdReadFreezeLatencyTest, SC2_TimeToWarmWithinBudgetAt500MsLatency )
{
    injected_->SetGetLatency( std::chrono::milliseconds( 500 ) );
    const CID cid_key = SeedColdBlock( "time to warm payload" );

    const auto started_at = std::chrono::steady_clock::now();
    EXPECT_TRUE( service_->EnqueueRewarm( cid_key ) );
    const auto hot = [&]() {
        auto got = inner_->get( cid_key );
        return got.has_value();
    };
    EXPECT_TRUE( PollUntil( hot, ColdReadService::kRewarmBudget ) ) << "re-warm must complete inside the budget";
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at );
    EXPECT_LT( elapsed_ms.count(), ColdReadService::kRewarmBudget.count() );
}

/**
 * @given a hot CID, >= 16 queued cold re-warms, and the single worker
 *        pinned by 500 ms Gets (a saturated re-warm pipeline)
 * @when  the hot CID is served through the ColdServeDagService wrapper
 * @then  the serve returns in under 50 ms - the wrapper NEVER waits on
 *        the executor; the whole-node-freeze hazard stays closed (SC2
 *        regression).
 */
TEST_F( ColdReadFreezeLatencyTest, SC2_HotServeUnderSaturatedSlowQueue )
{
    injected_->SetGetLatency( std::chrono::milliseconds( 500 ) );

    // Saturate: 16 distinct cold CIDs (queue depth >= 16, worker pinned).
    for ( int i = 0; i < 16; ++i )
    {
        const CID cid_key = SeedColdBlock( "saturating cold cid " + std::to_string( i ) );
        (void)service_->EnqueueRewarm( cid_key );
    }

    // Hot hit through the wrapper - delegation only, must be immediate.
    const auto hot_started = std::chrono::steady_clock::now();
    auto hot_result = wrapper_->getNode( fake_dag_->hot_cid() );
    const auto hot_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - hot_started );
    ASSERT_TRUE( hot_result.has_value() );
    EXPECT_LT( hot_elapsed_ms.count(), 50 ) << "SC2: hot serve must never wait on the re-warm executor";

    // Miss path (enqueue is a try-op on a full pipeline): also immediate.
    const auto miss_cid = MakeCid( "never stored anywhere" );
    const auto miss_started = std::chrono::steady_clock::now();
    auto miss_result = wrapper_->getNode( CID( miss_cid ) );
    const auto miss_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - miss_started );
    EXPECT_TRUE( miss_result.has_error() ) << "miss must return the inner failure (honest don't-have)";
    EXPECT_LT( miss_elapsed_ms.count(), 50 ) << "SC2: the miss path must return immediately";
}

/**
 * @given a scripted s3d Unavailable fault on Get (fail-fast transport
 *        outage)
 * @when  the wrapper serves a miss
 * @then  it returns immediately - an outage never delays the responder's
 *        path (SC2; the outage is logged + counted by the worker's chain
 *        run, not by the responder).
 */
TEST_F( ColdReadFreezeLatencyTest, SC2_OutageMissReturnsImmediately_FailFast )
{
    fake_->SetFault( sgns::blockstore::S3Op::Get, sgns::blockstore::S3FaultKind::Unavailable );
    const auto miss_cid = MakeCid( "unavailable outage cid" );

    const auto started_at = std::chrono::steady_clock::now();
    auto result = wrapper_->getNode( CID( miss_cid ) );
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at );
    EXPECT_TRUE( result.has_error() );
    EXPECT_LT( elapsed_ms.count(), 50 );
}

/**
 * @given a scripted s3d Unavailable outage WITH >= 16 cold re-warms in
 *        flight (each failing fast on the worker)
 * @when  a hot CID is served through the responder wrapper
 * @then  the serve returns in under 50 ms - the outage-side half of the
 *        SC2 freeze regression (the slow-500 ms half is
 *        SC2_HotServeUnderSaturatedSlowQueue): the responder never
 *        freezes libp2p stream processing while cold misses churn.
 */
TEST_F( ColdReadFreezeLatencyTest, SC2_HotServeUnderOutageFailFastQueueChurning )
{
    fake_->SetFault( sgns::blockstore::S3Op::Get, sgns::blockstore::S3FaultKind::Unavailable );

    // >= 16 cold misses in flight through the wrapper (each misses the
    // fake dag, enqueues, and the worker fails fast on the faulted S3).
    for ( int i = 0; i < 16; ++i )
    {
        const CID cid_key = SeedColdBlock( "outage churn cold cid " + std::to_string( i ) );
        auto miss = wrapper_->getNode( cid_key );
        EXPECT_TRUE( miss.has_error() );
    }

    // Hot hit through the wrapper while the outage churns: immediate.
    const auto hot_started = std::chrono::steady_clock::now();
    auto hot_result = wrapper_->getNode( fake_dag_->hot_cid() );
    const auto hot_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - hot_started );
    ASSERT_TRUE( hot_result.has_value() );
    EXPECT_LT( hot_elapsed_ms.count(), 50 ) << "SC2: an s3d outage must never delay hot serving";

    fake_->ClearFaults();
}

/**
 * @given 4 concurrent local reads of ONE cold CID at 50 ms injected
 *        latency (read-path coalescing deliberately not built, §8.3)
 * @when  all reads run in parallel
 * @then  every read resolves and the duplicate S3 Get count is RECORDED
 *        (banner above) - Phase-12 hardening data; the idempotent
 *        re-cache keeps duplicates harmless.
 */
TEST_F( ColdReadFreezeLatencyTest, ConcurrentColdReadsDuplicateGetCount )
{
    injected_->SetGetLatency( std::chrono::milliseconds( 50 ) );
    const CID cid_key = SeedColdBlock( "concurrent miss payload" );
    const auto shared_cid = std::make_shared<CID>( cid_key );

    std::vector<std::thread> readers;
    std::atomic<size_t> resolved{ 0 };
    for ( int t = 0; t < 4; ++t )
    {
        readers.emplace_back( [this, shared_cid, &resolved]() {
            sgns::common::Buffer out;
            (void)service_->Get( *shared_cid, out );
            if ( !out.empty() )
            {
                ++resolved;
            }
        } );
    }
    for ( auto &reader : readers )
    {
        reader.join();
    }

    EXPECT_EQ( resolved.load(), 4u );
    // Duplicate Gets are expected (no read-path coalescing by design) -
    // recorded, not asserted to a single value.
    std::cout << "[sc2-evidence] concurrent reads: duplicate S3 Gets for 4 parallel readers = "
              << injected_->get_calls() << "\n";
}
