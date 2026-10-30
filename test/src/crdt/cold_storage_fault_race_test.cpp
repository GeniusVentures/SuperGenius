/**
 * @file       cold_storage_fault_race_test.cpp
 * @brief      Hermetic Tier-1 fault-race matrix (TEST-02 SC1, v1.1
 *             Phase 12 plan 12-01): the D-11 evict-x-serve-x-drain
 *             crossings driven by DETERMINISTIC barriers (never fixed
 *             sleeps - Pitfall 4) plus seeded jitter legs (D-10) and
 *             the suite-end D-12 audit walk.
 *
 * Registered ONLY under SGNS_COLD_STORAGE (links
 * sgns-blockstore::sgns-blockstore plus the InMemoryS3Client /
 * InMemoryS3AdminClient fakes via the relative include dir - both fake
 * headers are deliberately not installed). The fixture is the
 * cold_storage_gc_test composition (real hot RocksDB in the SC1 prefix
 * spy, LocalBlockStore cold tier, InMemoryS3Client + counting wrapper,
 * S3BlockStore, ONE underlying RocksDB wrapped twice via
 * BlockStoreBackedIpfsDatastore, ColdReadService, GraphsyncDAGSyncer,
 * CrdtDatastore, driven GcPassOnce passes).
 *
 * Instruments this suite adds:
 *  - TimingS3Client: a test-local S3Client decorator chained OUTERMOST
 *    (around the counting wrapper) exposing per-op latency, an on-op
 *    barrier hook fired BEFORE delegation and OUTSIDE every inner lock
 *    (Pitfall 7), and seeded bounded jitter - zero library change, no
 *    D-14 re-cut.
 *  - The D-16 GC-side test seams on ColdStorageManager
 *    (SetGcBetweenHalvesHookForTesting here; the pre-mark-process and
 *    pre-half-one seams arrive with the full matrix task).
 *
 * Include discipline: every library result is guarded with has_value()
 * BEFORE .value()/.error() (policy::terminate alias). CID landmine:
 * sgns::CID has NO implicit ctor from the blockstore Cid - wrap
 * explicitly (CID cid_key( cid )); the reverse direction slices
 * implicitly (Cid lib( sgns_cid )). No sleep_for outside the PollUntil
 * helper body (Tier-1 audit discipline): delays are cv-wait_for based
 * and ordering is barrier-driven.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
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
#include "crdt/globaldb/BlockStoreBackedIpfsDatastore.hpp"
#include "crdt/globaldb/ColdReadService.hpp"
#include "crdt/globaldb/ColdStorageManager.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
#include "crdt/hierarchical_key.hpp"
#include "storage/rocksdb/rocksdb.hpp"

#include <ipfs_lite/ipfs/impl/datastore_rocksdb.hpp>
#include <ipfs_lite/ipld/impl/ipld_node_impl.hpp>

namespace
{
    // IPFS resolves only inside namespace sgns - alias it for this TU
    // (the decorator's set()/get() surface is IpfsDatastore-typed).
    namespace ipfs_outcome = sgns::IPFS::outcome;
    using MerkleDagServiceImpl = sgns::ipfs_lite::ipfs::merkledag::MerkleDagServiceImpl;
    using IpfsDatastore      = sgns::ipfs_lite::ipfs::IpfsDatastore;
    using IpfsDatastoreError = sgns::ipfs_lite::ipfs::IpfsDatastoreError;
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
    using sgns::blockstore::BlockStatus;
    using sgns::blockstore::result;
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
    using sgns::blockstore::UploadState;
    using sgns::storage::BufferStorage;
    using sgns::storage::rocksdb;
    using libp2p::multi::MulticodecType;

    /// Wall-clock unix-ms (the spill-timestamp clock, D-01).
    uint64_t UnixNowMs()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch() )
                .count() );
    }

    /// Bounded poll loop with deadline - never a fixed sleep, so the suite
    /// stays stable on loaded Windows CI-equivalent machines. The ONLY
    /// sleep_for in this file lives in this helper body.
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

    /// Short-interval drainer options (the durability-suite FastOptions shape).
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

    /// Fast flush cadence so the Durable wait completes in milliseconds.
    ColdStorageManager::DurabilityOptions FastDurability( uint32_t flush_min_interval_ms = 50,
                                                          uint32_t flush_max_interval_ms = 300 )
    {
        ColdStorageManager::DurabilityOptions durability;
        durability.flush_min_interval_ms = flush_min_interval_ms;
        durability.flush_max_interval_ms = flush_max_interval_ms;
        return durability;
    }

    /// Retention for the race legs: EXPLICIT tiny positive values (zero
    /// normalizes to kDefaultGraceMs / kDefaultGcIntervalMs - Pitfall 2)
    /// with the DEFAULT cadence knob (0 -> dormant background GC thread;
    /// the tests drive GcPassOnce() directly).
    ColdStorageManager::RetentionOptions RetentionRace( uint64_t retention_age_ms = 1,
                                                        uint32_t grace_ms        = 1 )
    {
        ColdStorageManager::RetentionOptions retention;
        retention.retention_age_ms = retention_age_ms;
        retention.dry_run          = false;
        retention.gc_interval_ms   = 0; // 0 -> kDefaultGcIntervalMs (dormant)
        retention.grace_ms         = grace_ms; // explicit - never normalized
        return retention;
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
    std::string NextUniqueDir( const char *lane )
    {
        static std::atomic<uint64_t> next{ 0 };
        return std::string( "test-data/cold-storage-race/" ) + lane + "-case-"
             + std::to_string( next.fetch_add( 1 ) );
    }

    // A truly FRESH store per case: the monotonic counter resets every
    // process run, so a leftover dir from a previous run would reopen its
    // stale journal - wipe before Open.
    std::string FreshDir( const char *lane )
    {
        auto dir = NextUniqueDir( lane );
        std::error_code ec;
        std::filesystem::remove_all( dir, ec );
        return dir;
    }

    /// Unsigned env override with fallback (the jitter legs' repro
    /// knobs SGNS_RACE_JITTER_SEED / SGNS_RACE_JITTER_ITERS).
    uint64_t EnvOr( const char *name, uint64_t fallback )
    {
        const char *raw = std::getenv( name );
        return raw == nullptr ? fallback
                             : static_cast<uint64_t>( std::strtoull( raw, nullptr, 10 ) );
    }

    /**
     * @brief One-shot enter/release latch: the deterministic barrier the
     *        race legs park actors on (D-11). Enter() marks the actor
     *        inside the window and blocks until Release(); Release() is
     *        idempotent and lets any LATER Enter pass straight through,
     *        so a failing leg can never hang teardown.
     *
     * cv-wait based throughout - no sleep_for (Pitfall 4).
     */
    class WindowGate
    {
    public:
        // SC4 round 8: leak-proofing. A gtest ASSERT between arming the
        // hook (Enter parked on the actor thread) and the test's explicit
        // Release() early-returns the test body with the actor still
        // parked; the fixture teardown then joins that actor thread and
        // deadlocks until ctest's 600 s kill (run 41024756227 OSX
        // Debug+Release: EvictServe_RewarmAttachVsMark_NoLostBlock
        // timeout). Releasing from the destructor guarantees the parked
        // actor is freed at test-body exit, before teardown joins - a
        // failed deadline now surfaces as the ASSERT failure it is,
        // never a hang. Release() is idempotent for the normal path.
        ~WindowGate()
        {
            Release();
        }

        /// Actor side: mark inside + block until released.
        void Enter()
        {
            std::unique_lock<std::mutex> lock( mutex_ );
            inside_ = true;
            cv_.notify_all();
            cv_.wait( lock, [this] { return released_; } );
        }

        /// Test side: bounded wait until the actor sits inside the window.
        bool WaitInside( std::chrono::milliseconds deadline )
        {
            std::unique_lock<std::mutex> lock( mutex_ );
            return cv_.wait_until( lock, std::chrono::steady_clock::now() + deadline,
                                   [this] { return inside_; } );
        }

        /// Test side: let the actor (and every later Enter) proceed.
        void Release()
        {
            std::lock_guard<std::mutex> lock( mutex_ );
            released_ = true;
            cv_.notify_all();
        }

    private:
        std::mutex              mutex_;
        std::condition_variable cv_;
        bool                    inside_   = false;
        bool                    released_ = false;
    };

    /**
     * @brief Test-local S3Client timing decorator (the D-10/D-11 timing
     *        instrument - CountingS3Client precedent, zero library
     *        change, no D-14 re-cut). Chained OUTERMOST around the
     *        counting wrapper so its barriers observe the exact op
     *        boundary the S3BlockStore consumes.
     *
     * Lock discipline (Pitfall 7): the config mutex guards ONLY the
     * timing tables; the on-op hook fires and the delay elapses AFTER
     * the config mutex is released and OUTSIDE the inner fake's lock, so
     * a parked barrier never serializes unrelated ops or the counting
     * probes. Delays are cv wait_for (interruptible at destruction) -
     * never sleep_for (Pitfall 4 discipline).
     */
    class TimingS3Client final : public S3Client
    {
    public:
        explicit TimingS3Client( std::shared_ptr<S3Client> inner )
            : inner_( std::move( inner ) )
        {
        }

        ~TimingS3Client() override
        {
            // Unblock any in-flight timed wait so a failing test's
            // teardown cannot hang inside the decorator.
            {
                std::lock_guard<std::mutex> lock( delay_mutex_ );
                delay_abort_ = true;
            }
            delay_cv_.notify_all();
        }

        /// Fixed bounded delay for every matching op (empty key filter
        /// matches every key) applied BEFORE delegation.
        void SetLatencyMs( S3Op op, uint32_t latency_ms, const std::string &key_filter = {} )
        {
            std::lock_guard<std::mutex> lock( config_mutex_ );
            auto                       &entry = Entry( op );
            entry.latency_ms = latency_ms;
            entry.key_filter = key_filter;
        }

        /// Barrier hook fired BEFORE delegation and OUTSIDE every inner
        /// lock. The hook MAY BLOCK - that is the barrier (D-11). Null
        /// clears.
        void SetOnOp( S3Op op, std::function<void()> hook )
        {
            std::lock_guard<std::mutex> lock( config_mutex_ );
            Entry( op ).on_op = std::move( hook );
        }

        /// Seeded bounded jitter for the op: each call draws a delay in
        /// [max(0, base - spread), base + spread] from the seeded engine
        /// (repro-on-failure via SGNS_RACE_JITTER_SEED, D-10).
        void SetSeededJitter( S3Op op, uint32_t base_ms, uint32_t spread_ms, uint64_t seed )
        {
            std::lock_guard<std::mutex> lock( config_mutex_ );
            auto                       &entry = Entry( op );
            entry.jitter_base_ms   = base_ms;
            entry.jitter_spread_ms = spread_ms;
            entry.rng.seed( seed );
        }

        /// Drop every timing/hook (between legs of one scenario).
        void ClearTimings()
        {
            std::lock_guard<std::mutex> lock( config_mutex_ );
            entries_.fill( OpTiming{} );
        }

        result<S3PutResult, sgns::blockstore::BlockStatus> Put(
            const std::string              &key,
            const std::vector<uint8_t>     &bytes,
            const PutObjectOptions         &options = {} ) override
        {
            ApplyTiming( S3Op::Put, key );
            return inner_->Put( key, bytes, options );
        }

        result<S3Object, sgns::blockstore::BlockStatus> Get( const std::string &key ) override
        {
            ApplyTiming( S3Op::Get, key );
            return inner_->Get( key );
        }

        result<S3HeadResult, sgns::blockstore::BlockStatus> Head( const std::string &key ) override
        {
            ApplyTiming( S3Op::Head, key );
            return inner_->Head( key );
        }

        result<void, sgns::blockstore::BlockStatus> Delete( const std::string &key ) override
        {
            ApplyTiming( S3Op::Delete, key );
            return inner_->Delete( key );
        }

    private:
        struct OpTiming
        {
            uint32_t            latency_ms{ 0 };
            std::string         key_filter; ///< empty = every key
            std::function<void()> on_op;    ///< pre-delegation barrier hook
            uint32_t            jitter_base_ms{ 0 };
            uint32_t            jitter_spread_ms{ 0 };
            std::mt19937_64     rng{ 1 };
        };

        static constexpr size_t kOpCount = 4;

        OpTiming &Entry( S3Op op )
        {
            return entries_[static_cast<size_t>( op )];
        }

        /// Snapshot the op's timing config under the config mutex, then
        /// fire the hook and elapse the delay with EVERY lock released.
        void ApplyTiming( S3Op op, const std::string &key )
        {
            std::function<void()> hook;
            uint32_t              delay_ms = 0;
            {
                std::lock_guard<std::mutex> lock( config_mutex_ );
                auto                       &entry = Entry( op );
                if ( entry.key_filter.empty() || entry.key_filter == key )
                {
                    hook     = entry.on_op;
                    delay_ms = entry.latency_ms;
                    if ( entry.jitter_base_ms > 0 || entry.jitter_spread_ms > 0 )
                    {
                        const uint64_t span = 2ull * entry.jitter_spread_ms + 1ull;
                        const uint64_t draw = entry.rng() % span;
                        const int64_t  jittered =
                            static_cast<int64_t>( entry.jitter_base_ms )
                            - static_cast<int64_t>( entry.jitter_spread_ms )
                            + static_cast<int64_t>( draw );
                        const uint32_t bounded = static_cast<uint32_t>(
                            std::max<int64_t>( jittered, 0 ) );
                        delay_ms = std::max<uint32_t>( delay_ms, bounded );
                    }
                }
            }
            if ( hook )
            {
                hook(); // outside config mutex, outside every inner lock
            }
            if ( delay_ms > 0 )
            {
                DelayFor( delay_ms );
            }
        }

        /// Interruptible bounded delay (cv wait_for + destructor abort).
        void DelayFor( uint32_t ms )
        {
            std::unique_lock<std::mutex> lock( delay_mutex_ );
            delay_cv_.wait_for( lock, std::chrono::milliseconds( ms ),
                                [this] { return delay_abort_; } );
        }

        std::shared_ptr<S3Client> inner_;
        std::array<OpTiming, kOpCount> entries_{};
        std::mutex                config_mutex_;
        std::mutex                delay_mutex_;
        std::condition_variable   delay_cv_;
        bool                      delay_abort_ = false;
    };

    /**
     * @brief Test-local S3Client wrapper counting Get calls before
     *        delegating to the in-memory fake - the zero-S3-traffic
     *        probe (the gc-suite CountingS3Client shape).
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

        result<S3PutResult, sgns::blockstore::BlockStatus> Put(
            const std::string              &key,
            const std::vector<uint8_t>     &bytes,
            const PutObjectOptions         &options = {} ) override
        {
            return inner_->Put( key, bytes, options );
        }

        result<S3Object, sgns::blockstore::BlockStatus> Get( const std::string &key ) override
        {
            ++get_calls_;
            return inner_->Get( key );
        }

        result<S3HeadResult, sgns::blockstore::BlockStatus> Head( const std::string &key ) override
        {
            return inner_->Head( key );
        }

        result<void, sgns::blockstore::BlockStatus> Delete( const std::string &key ) override
        {
            return inner_->Delete( key );
        }

    private:
        std::shared_ptr<InMemoryS3Client> inner_;
        std::atomic<uint64_t>             get_calls_{ 0 };
    };

    /**
     * @brief The spy's batch wrapper: every put/remove key tallies (as a
     *        WRITE) before forwarding, so batch commits - the hot-cleanup
     *        path's deletes - are inside the keyspace proof.
     */
    class SpyBufferBatch final : public sgns::storage::BufferBatch
    {
    public:
        SpyBufferBatch( std::unique_ptr<sgns::storage::BufferBatch> inner,
                        std::function<void( const std::string & )> record_write )
            : inner_( std::move( inner ) ), record_write_( std::move( record_write ) )
        {
        }

        outcome::result<void> put( const Buffer &key, const Buffer &value ) override
        {
            record_write_( std::string( reinterpret_cast<const char *>( key.data() ), key.size() ) );
            return inner_->put( key, value );
        }

        outcome::result<void> put( const Buffer &key, Buffer &&value ) override
        {
            record_write_( std::string( reinterpret_cast<const char *>( key.data() ), key.size() ) );
            return inner_->put( key, std::move( value ) );
        }

        outcome::result<void> remove( const Buffer &key ) override
        {
            record_write_( std::string( reinterpret_cast<const char *>( key.data() ), key.size() ) );
            return inner_->remove( key );
        }

        outcome::result<void> commit() override
        {
            return inner_->commit();
        }

        void clear() override
        {
            inner_->clear();
        }

    private:
        std::unique_ptr<sgns::storage::BufferBatch> inner_;
        std::function<void( const std::string & )>   record_write_;
    };

    /**
     * @brief SC1 prefix-spy hot datastore (test-local BufferStorage
     *        decorator - the cold_storage_gc_test instrument, kept for
     *        the D-12 audit walk's hot-bytes observability).
     *
     * Forwards every operation to the wrapped REAL hot RocksDB face while
     * tallying per-key statistics. The GC is the spy's ONLY client in this
     * fixture (the CrdtDatastore holds the unwrapped face), so the tallies
     * observe exactly the GC's hot-store footprint.
     */
    class PrefixSpyHotStore final : public BufferStorage
    {
    public:
        explicit PrefixSpyHotStore( std::shared_ptr<rocksdb> inner )
            : inner_( std::move( inner ) )
        {
        }

        /// Operations whose key CONTAINS the needle (prefix segments like
        /// "/t/", "/h/", "/k/", "/s/" match under any namespace root).
        uint64_t OpsMatching( const std::string &needle ) const
        {
            std::lock_guard<std::mutex> lock( mutex_ );
            uint64_t                    total = 0;
            for ( const auto &[key, ops] : op_by_key_ )
            {
                if ( key.find( needle ) != std::string::npos )
                {
                    total += ops;
                }
            }
            return total;
        }

        /// The SC1 protected keyspace footprint: tombstone (/t/), heads
        /// (/h/), current-value (/k/), and set-row (/s/) keyspaces.
        uint64_t ProtectedPrefixOps() const
        {
            return OpsMatching( "/t/" ) + OpsMatching( "/h/" ) + OpsMatching( "/k/" )
                 + OpsMatching( "/s/" );
        }

        std::unique_ptr<sgns::storage::BufferMapCursor> cursor() override
        {
            Record( "<cursor>" );
            return inner_->cursor();
        }

        std::unique_ptr<sgns::storage::BufferBatch> batch() override
        {
            Record( "<batch>" );
            // Wrap the inner batch so every put/remove key tallies as a
            // write - batch commits (the hot-cleanup deletes) stay inside
            // the keyspace proof.
            auto shared_this = std::shared_ptr<PrefixSpyHotStore>( this, []( PrefixSpyHotStore * ) {} );
            return std::make_unique<SpyBufferBatch>(
                inner_->batch(),
                [shared_this]( const std::string &key ) { shared_this->RecordWrite( key ); } );
        }

        outcome::result<Buffer> get( const Buffer &key ) const override
        {
            Record( KeyString( key ) );
            return inner_->get( key );
        }

        bool contains( const Buffer &key ) const override
        {
            Record( KeyString( key ) );
            return inner_->contains( key );
        }

        bool empty() const override
        {
            return inner_->empty();
        }

        outcome::result<void> put( const Buffer &key, const Buffer &value ) override
        {
            Record( KeyString( key ) );
            RecordWrite( KeyString( key ) );
            return inner_->put( key, value );
        }

        outcome::result<void> put( const Buffer &key, Buffer &&value ) override
        {
            Record( KeyString( key ) );
            RecordWrite( KeyString( key ) );
            return inner_->put( key, std::move( value ) );
        }

        outcome::result<void> remove( const Buffer &key ) override
        {
            Record( KeyString( key ) );
            RecordWrite( KeyString( key ) );
            return inner_->remove( key );
        }

        std::string GetName() override
        {
            return "PrefixSpyHotStore";
        }

    private:
        static std::string KeyString( const Buffer &key )
        {
            return std::string( reinterpret_cast<const char *>( key.data() ), key.size() );
        }

        void Record( const std::string &key ) const
        {
            std::lock_guard<std::mutex> lock( mutex_ );
            ++op_by_key_[key];
        }

        void RecordWrite( const std::string &key ) const
        {
            std::lock_guard<std::mutex> lock( mutex_ );
            ++write_op_by_key_[key];
        }

        mutable std::map<std::string, uint64_t> op_by_key_;
        mutable std::map<std::string, uint64_t> write_op_by_key_;
        mutable std::mutex                      mutex_;
        std::shared_ptr<rocksdb>                inner_;
    };

    /**
     * @brief Test-local NO-OP broadcaster (the CRDTMirrorBroadcaster
     *        no-more-broadcast posture without the mirror queue): Publish
     *        needs a broadcaster; nothing is ever received.
     */
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
            // ErrNoMoreBroadcast posture - the receive thread exits on the
            // first poll (the CRDTMirrorBroadcaster empty-queue shape).
            return outcome::failure( boost::system::error_code{} );
        }

        bool HasTopic( const std::string & ) override
        {
            return true;
        }
    };

    class ColdStorageFaultRaceTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            // The REAL hot CRDT RocksDB store (heads + set rows + tombstones
            // land here through the CrdtDatastore), wrapped in the spy.
            // storage::rocksdb::create does NOT create parent directories
            // (unlike LocalBlockStore::Open) - create them explicitly.
            hot_dir_ = FreshDir( "hot" );
            std::error_code hot_ec;
            std::filesystem::create_directories( hot_dir_, hot_ec );
            ASSERT_FALSE( hot_ec );
            rocksdb::Options hot_options;
            hot_options.create_if_missing = true;
            auto opened_hot = rocksdb::create( hot_dir_, hot_options );
            ASSERT_TRUE( opened_hot.has_value() );
            real_hot_ = std::move( opened_hot.value() );
            spy_      = std::make_shared<PrefixSpyHotStore>( real_hot_ );

            // The cold tier (the durability-suite fixture shape).
            shared_local_dir_ = FreshDir( "cold" );
            auto opened_local = LocalBlockStore::Open( LocalBlockStoreOptions{ shared_local_dir_ } );
            ASSERT_TRUE( opened_local.has_value() );
            shared_local_ = std::shared_ptr<LocalBlockStore>( std::move( opened_local.value() ) );

            // The timing chain: fake <- counting <- timing. The TimingS3Client
            // rides OUTERMOST so its barriers observe the exact op boundary
            // the S3BlockStore consumes (12-01 Task 1).
            fake_     = std::make_shared<InMemoryS3Client>();
            counting_ = std::make_shared<CountingS3Client>( fake_ );
            timing_   = std::make_shared<TimingS3Client>(
                std::static_pointer_cast<S3Client>( counting_ ) );
            auto opened_remote = S3BlockStore::Open(
                std::static_pointer_cast<S3Client>( timing_ ) );
            ASSERT_TRUE( opened_remote.has_value() );
            remote_ = std::shared_ptr<S3BlockStore>( std::move( opened_remote.value() ) );

            fake_admin_       = std::make_shared<InMemoryS3AdminClient>();
            cold_store_mutex_ = std::make_shared<std::mutex>();

            // The DAG layer (the production shared-DB shape): ONE
            // underlying RocksDB, wrapped TWICE - sgns::storage::rocksdb
            // for the CrdtDatastore + the manager's hot face (the spy),
            // and ipfs_lite::rocksdb over the SAME DB handle for the
            // RocksdbDatastore the decorator wraps (globaldb.cpp:803's
            // IpfsRocksDb::create( m_datastore->getDB() ) shape).
            auto ipfs_db = sgns::ipfs_lite::rocksdb::create( real_hot_->getDB() );
            ASSERT_TRUE( ipfs_db.has_value() );
            inner_     = std::make_shared<sgns::ipfs_lite::ipfs::RocksdbDatastore>( ipfs_db.value() );
            decorator_ = std::make_shared<BlockStoreBackedIpfsDatastore>(
                inner_, shared_local_, cold_store_mutex_, sgns::base::createLogger( "ColdStorageRace" ) );
            // The REAL cold-read chain as the DAGSyncer hook (Phase 9):
            // the cold-dir term, the disposed-marker term, and the
            // direct-read cold chain all live here (the re-warm worker
            // stays unstarted - these paths are synchronous).
            remote_store_mutex_ = std::make_shared<std::mutex>();
            cold_read_service_  = std::make_shared<ColdReadService>(
                shared_local_, remote_, cold_store_mutex_, remote_store_mutex_, decorator_ );
            dagsyncer_ = std::make_shared<GraphsyncDAGSyncer>(
                decorator_, nullptr, nullptr /* no graphsync/host needed for the no-request path */,
                cold_read_service_ );

            broadcaster_ = std::make_shared<NoopBroadcaster>();
            crdt_        = CrdtDatastore::New(
                real_hot_, HierarchicalKey( "/namespace" ), dagsyncer_, broadcaster_,
                CrdtOptions::DefaultOptions() );
            ASSERT_TRUE( crdt_ != nullptr );
            crdt_->Start();

            // The re-warm executor is LIVE (the Phase-9 SC2 posture: the
            // serve path never waits) - dormant until a responder miss
            // enqueues work, so the non-responder legs stay unaffected.
            cold_read_service_->Start();

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
                manager_->Stop(); // joins drainer + GC threads first
            }
            if ( cold_read_service_ )
            {
                cold_read_service_->Stop(); // joins the re-warm worker
            }
            if ( crdt_ )
            {
                crdt_->CancelAndCloseNow();
            }
        }

        /// Construct a manager directly over the fixture stores with the
        /// fake admin (D-10) and the given retention posture.
        std::shared_ptr<ColdStorageManager> MakeManager(
            const ColdStorageManager::BackpressureOptions &backpressure,
            const ColdStorageManager::DurabilityOptions   &durability,
            const ColdStorageManager::RetentionOptions    &retention )
        {
            auto remote_store_mutex = std::make_shared<std::mutex>();
            manager_                = std::make_shared<ColdStorageManager>(
                shared_local_,
                remote_,
                cold_store_mutex_,
                remote_store_mutex,
                backpressure,
                durability,
                fake_admin_,
                retention );
            manager_->AttachGcDependencies( crdt_.get(), dagsyncer_.get(), spy_ );
            return manager_;
        }

        /// Publish one element through the REAL CRDT Put path: the delta
        /// node lands in the DAG through the decorator (journaled = the
        /// spill-on-write shape) and the heads mutate (the walk universe).
        CID PublishElement( const std::string                       &key,
                            const std::string                       &value,
                            const std::unordered_set<std::string> &topics = { "topic" } )
        {
            auto delta = CrdtDatastore::CreateDeltaToAdd( key, value );
            EXPECT_TRUE( delta.has_value() );
            auto published = crdt_->Publish( delta.value(), topics );
            EXPECT_TRUE( published.has_value() );
            return published.value();
        }

        /// Journal an ORPHAN block (no delta, no head, never reachable)
        /// through the decorator - the spill shape without CRDT presence.
        Cid SetOrphan( const std::string &payload )
        {
            const auto cid = MakeCid( payload );
            IpfsDatastore::Value value;
            value.put( payload );
            auto set = decorator_->set( CID( cid ), std::move( value ) );
            EXPECT_TRUE( set.has_value() );
            return cid;
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

        /// Wait for the given CIDs to reach UploadedDurable through the
        /// running drainer (fast options + healthy fake admin).
        void WaitAllDurable( const std::vector<Cid> &cids )
        {
            ASSERT_TRUE( PollUntil( [&] { return AllInState( cids, UploadState::UploadedDurable ); },
                                    std::chrono::milliseconds( 10000 ) ) )
                << "seed blocks did not reach UploadedDurable within deadline";
        }

        /// Backdate a block's spill timestamp (the D-01 age evidence).
        void Backdate( const Cid &cid, uint64_t age_ms )
        {
            std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
            auto written = shared_local_->SetSpilledAt( cid, UnixNowMs() - age_ms );
            ASSERT_TRUE( written.has_value() );
        }

        /// Byte content of an IPLD node (the comparison oracle shape).
        static std::vector<uint8_t> ContentBytes(
            const std::shared_ptr<sgns::ipfs_lite::ipld::IPLDNode> &node )
        {
            const auto &content = node->content();
            return std::vector<uint8_t>( content.data(), content.data() + content.size() );
        }

        /// Publish a REAL delta (CIDv0/DAG_PB/sha256 - the 11-01 tracer
        /// correction), snapshot its content oracle, detach the head
        /// (unreachable => evictable), drop the LRU copy, and optionally
        /// strip the hot bytes so serve paths must resolve through the
        /// cold chain.
        struct SeededBlock
        {
            CID                  cid_key;
            std::vector<uint8_t> content;
        };

        SeededBlock SeedDetachedDelta( const std::string &key, const std::string &value,
                                       bool strip_hot )
        {
            const CID head = PublishElement( key, value );
            auto      node = dagsyncer_->GetNodeWithoutRequest( head );
            EXPECT_TRUE( node.has_value() );
            SeededBlock block;
            block.cid_key = head;
            if ( node.has_value() )
            {
                block.content = ContentBytes( node.value() );
            }
            EXPECT_TRUE( crdt_->RemoveHead( head, "topic" ).has_value() );
            (void)dagsyncer_->DeleteCIDBlock( head ); // drop the LRU copy
            if ( strip_hot )
            {
                EXPECT_TRUE( decorator_->remove( head ).has_value() ) << "hot-only delete";
            }
            return block;
        }

        /// Let an explicit tiny grace elapse without a fixed sleep (the
        /// predicate flips true after 3 ms; sleeps live in PollUntil).
        void GraceElapse()
        {
            const auto early = std::chrono::steady_clock::now() + std::chrono::milliseconds( 3 );
            ASSERT_TRUE( PollUntil( [&] { return std::chrono::steady_clock::now() >= early; },
                                    std::chrono::milliseconds( 1000 ) ) );
        }

        /// The BACK-07 object key for a DAG_PB fixture CID (S3BlockStore's
        /// DeriveKey shape - the delta codec segment).
        static std::string DagPbObjectKey( const CID &cid )
        {
            auto             cid_string = libp2p::multi::ContentIdentifierCodec::toString( cid );
            static const char *kHex   = "0123456789abcdef";
            const auto         digest = cid.content_address.getHash();
            const uint8_t      first  = digest.empty() ? 0 : digest[0];
            std::string        fanout{ kHex[first >> 4], kHex[first & 0x0F] };
            return std::string( "v1/blocks" ) + "/dag-pb/" + fanout + "/" + cid_string.value();
        }

        /// The BACK-07 object key for a RAW fixture CID (the control-key
        /// shape for key-scoped fault scripting).
        static std::string DeriveTestKey( const Cid &cid )
        {
            auto             cid_string = libp2p::multi::ContentIdentifierCodec::toString( cid );
            static const char *kHex   = "0123456789abcdef";
            const auto         digest = cid.content_address.getHash();
            const uint8_t      first  = digest.empty() ? 0 : digest[0];
            std::string        fanout{ kHex[first >> 4], kHex[first & 0x0F] };
            return std::string( "v1/blocks" ) + "/raw/" + fanout + "/" + cid_string.value();
        }

        // Declaration order == reverse destruction order: manager_ dies
        // first (its threads join against still-live stores), then the
        // CRDT layer, then the DAG layer, then the stores.
        std::string                             hot_dir_;
        std::shared_ptr<rocksdb>                real_hot_;
        std::shared_ptr<PrefixSpyHotStore>      spy_;
        std::string                             shared_local_dir_;
        std::shared_ptr<LocalBlockStore>        shared_local_;
        std::shared_ptr<InMemoryS3Client>       fake_;
        std::shared_ptr<CountingS3Client>       counting_;
        std::shared_ptr<TimingS3Client>         timing_;
        std::shared_ptr<S3BlockStore>           remote_;
        std::shared_ptr<InMemoryS3AdminClient>  fake_admin_;
        std::shared_ptr<std::mutex>             cold_store_mutex_;
        std::shared_ptr<sgns::ipfs_lite::ipfs::IpfsDatastore> inner_;
        std::shared_ptr<BlockStoreBackedIpfsDatastore> decorator_;
        std::shared_ptr<std::mutex>             remote_store_mutex_;
        std::shared_ptr<ColdReadService>        cold_read_service_;
        std::shared_ptr<GraphsyncDAGSyncer>     dagsyncer_;
        std::shared_ptr<NoopBroadcaster>        broadcaster_;
        std::shared_ptr<CrdtDatastore>          crdt_;
        std::shared_ptr<ColdStorageManager>     manager_;
        std::shared_ptr<MerkleDagServiceImpl>   dag_service_;
        std::shared_ptr<ColdServeDagService>    wrapper_;
    };
} // namespace

/**
 * @given a REAL detached delta (CIDv0/DAG_PB/sha256 - the 11-01 tracer
 *        correction: never fabricate CIDv1-only fixtures), Durable and
 *        backdated past retention, stripped from hot + LRU so the serve
 *        path can only resolve through the cold chain, with the GC's
 *        D-16 between-halves seam and the TimingS3Client Get barrier
 *        armed
 * @when  pass 2 runs on its own thread and parks inside the
 *        HALF1-committed / HALF2-not-started window while the serve
 *        thread's S3 fetch is held mid-flight; the fetch is released
 *        FIRST (cold tier + journal already deleted underneath it),
 *        then the GC finishes HALF2
 * @then  the in-flight fetch resolves CID-verified from S3 with the
 *        ORIGINAL bytes (zero Corrupt), the counting client proves the
 *        S3 tier served it, the end state is disposed-with-remote-copy
 *        (the marker survives the re-learn), and the re-learned copy
 *        serves the next read with zero further S3 Gets (D-11
 *        evict-x-serve, the SC1 tracer).
 */
TEST_F( ColdStorageFaultRaceTest, EvictServe_HalfWindowS3Fetch_ResolvesCidVerified )
{
    // SEED: publish a real delta, snapshot its content (the byte oracle),
    // detach the head (unreachable => evictable), drop the LRU copy, and
    // delete the hot bytes (decorator remove is hot-only by contract -
    // the journal record + eventual S3 copy survive).
    const CID head = PublishElement( "race/evict-serve", "value" );
    auto oracle_node = dagsyncer_->GetNodeWithoutRequest( head );
    ASSERT_TRUE( oracle_node.has_value() );
    const std::vector<uint8_t> oracle_bytes = ContentBytes( oracle_node.value() );
    ASSERT_TRUE( crdt_->RemoveHead( head, "topic" ).has_value() );
    (void)dagsyncer_->DeleteCIDBlock( head ); // drop the LRU copy
    ASSERT_TRUE( decorator_->remove( head ).has_value() ) << "hot-only delete of the delta bytes";

    const Cid cid( head );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionRace() );
    manager->Start();
    WaitAllDurable( { cid } );
    Backdate( cid, 60000 );

    // Pass 1: EvaluatePass marks the detached delta (the one candidate);
    // ProcessDueMarks defers it (same tick, grace not yet elapsed).
    ASSERT_EQ( manager->GcPassOnce().size(), 1u );

    // Let the explicit 1 ms grace elapse without a fixed sleep (the
    // predicate flips true after the deadline; sleeps live in PollUntil).
    const auto grace_early = std::chrono::steady_clock::now() + std::chrono::milliseconds( 3 );
    ASSERT_TRUE( PollUntil( [&] { return std::chrono::steady_clock::now() >= grace_early; },
                            std::chrono::milliseconds( 1000 ) ) );

    // The deterministic instruments: the GC parks between the D-16 halves;
    // the serve's S3 fetch parks inside the TimingS3Client Get hook.
    WindowGate       gc_window;
    WindowGate       s3_hold;
    std::atomic<bool> s3_get_fired{ false };

    manager->SetGcBetweenHalvesHookForTesting( [&] { gc_window.Enter(); } );
    timing_->SetOnOp( S3Op::Get, [&] {
        s3_get_fired.store( true );
        s3_hold.Enter();
    } );

    const uint64_t gets_before_serve = counting_->get_calls();

    // Pass 2 on its own thread: defer check passes (later tick + elapsed
    // grace), heads re-check passes (heads unchanged since the mark),
    // HALF1 commits, then the hook parks the GC inside the window.
    std::thread gc_thread( [&] { (void)manager->GcPassOnce(); } );

    const bool gc_in_window = gc_window.WaitInside( std::chrono::milliseconds( 10000 ) );
    EXPECT_TRUE( gc_in_window ) << "GC never parked inside the between-halves window";
    if ( !gc_in_window )
    {
        // Unconditional release + join: a parked thread must never leak
        // past a failing leg (std::terminate on joinable destruct).
        s3_hold.Release();
        gc_window.Release();
        gc_thread.join();
        FAIL() << "cannot drive the crossing without the between-halves window";
    }

    // HALF1 is committed atomically at this point: cold bytes + journal
    // record + spill timestamp deleted, disposed marker written. The
    // serve's only remaining resolution is the S3 tier.
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto disposed = shared_local_->IsDisposed( cid );
        ASSERT_TRUE( disposed.has_value() );
        EXPECT_TRUE( disposed.value() ) << "HALF1 wrote the disposed marker";
    }
    EXPECT_FALSE( manager->GetUploadState( cid ).has_value() )
        << "HALF1 deleted the journal record (no record reads as NotFound)";

    // The serve thread: LRU miss + hot miss + local-cold miss (HALF1) =>
    // the S3 fetch, which the TimingS3Client hook holds in flight.
    // (decltype names the dagsyncer's own result envelope - the file-scope
    std::optional<decltype( dagsyncer_->GetNodeWithoutRequest( head ) )> served;
    std::thread serve_thread( [&] { served = dagsyncer_->GetNodeWithoutRequest( head ); } );

    ASSERT_TRUE( PollUntil( [&] { return s3_get_fired.load(); },
                            std::chrono::milliseconds( 10000 ) ) )
        << "the serve path never reached its S3 fetch";

    // D-11 release order: the FETCH resolves first - over the deleted
    // cold tier - then the GC finishes HALF2 (hot cleanup + LRU drop).
    s3_hold.Release();
    serve_thread.join();
    gc_window.Release();
    gc_thread.join();

    // The in-flight fetch resolved CID-verified with the ORIGINAL bytes
    // (the library's Get verification ran inside the chain - zero
    // Corrupt-from-race by construction of this assert).
    ASSERT_TRUE( served.has_value() && served.value().has_value() )
        << "the held S3 fetch must resolve after eviction";
    EXPECT_EQ( ContentBytes( served.value().value() ), oracle_bytes );
    EXPECT_GT( counting_->get_calls(), gets_before_serve )
        << "the S3 tier served the in-flight fetch";

    // End state: disposed-with-remote-copy. The marker SURVIVED the
    // re-learn (rehydrate re-journals fresh bytes; the marker coexists
    // with re-learned content - the Phase-11 proven shape).
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto disposed = shared_local_->IsDisposed( cid );
        ASSERT_TRUE( disposed.has_value() );
        EXPECT_TRUE( disposed.value() ) << "the disposed marker survived the re-learn";
    }

    // The re-learned copy serves the next read with ZERO further S3 Gets
    // (local re-learned bytes / re-cached copy - never the remote tier).
    const uint64_t gets_after_serve = counting_->get_calls();
    auto second = dagsyncer_->GetNodeWithoutRequest( head );
    ASSERT_TRUE( second.has_value() );
    EXPECT_EQ( ContentBytes( second.value() ), oracle_bytes );
    EXPECT_EQ( counting_->get_calls(), gets_after_serve )
        << "the re-learned copy must serve the second read locally";
}

/**
 * @given a detached Durable delta backdated past retention whose hot
 *        bytes are still present, with the GC parked inside the D-16
 *        between-halves window (HALF1 committed, HALF2 not started)
 * @when  a reader resolves the CID mid-window, then again after HALF2
 *        completes
 * @then  the mid-window read hits the intact hot bytes (never a torn
 *        subset), the post-HALF2 read falls to the cold chain and
 *        resolves CID-verified from S3, and HasBlock never answers
 *        true-with-no-bytes (the disposed term's copy is provably in
 *        S3) - D-11 evict-x-serve crossing 2.
 */
TEST_F( ColdStorageFaultRaceTest, EvictServe_HotCleanupWindow_ReaderNeverTorn )
{
    const auto block = SeedDetachedDelta( "race/hot-cleanup-window", "value", false );
    const Cid  cid( block.cid_key );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionRace() );
    manager->Start();
    WaitAllDurable( { cid } );
    Backdate( cid, 60000 );
    ASSERT_EQ( manager->GcPassOnce().size(), 1u );
    GraceElapse();

    WindowGate gc_window;
    manager->SetGcBetweenHalvesHookForTesting( [&] { gc_window.Enter(); } );
    std::thread gc_thread( [&] { (void)manager->GcPassOnce(); } );
    const bool in_window = gc_window.WaitInside( std::chrono::milliseconds( 10000 ) );
    EXPECT_TRUE( in_window ) << "GC never parked inside the between-halves window";

    // Mid-window read: hot bytes are still present (HALF2 not started).
    if ( in_window )
    {
        auto mid = dagsyncer_->GetNodeWithoutRequest( block.cid_key );
        ASSERT_TRUE( mid.has_value() );
        EXPECT_EQ( ContentBytes( mid.value() ), block.content );
    }

    gc_window.Release();
    gc_thread.join();

    // Post-HALF2 read: hot + LRU gone; the cold chain serves CID-verified
    // from the S3 tier (never NotFound-shaped absence on a read).
    const uint64_t gets_before = counting_->get_calls();
    auto late = dagsyncer_->GetNodeWithoutRequest( block.cid_key );
    ASSERT_TRUE( late.has_value() ) << "reader must fall to the cold chain after HALF2";
    EXPECT_EQ( ContentBytes( late.value() ), block.content );
    EXPECT_GT( counting_->get_calls(), gets_before );

    // HasBlock honesty: never true-with-no-bytes (the marker term answers
    // true with the S3 copy holding the bytes).
    auto has = dagsyncer_->HasBlock( block.cid_key );
    ASSERT_TRUE( has.has_value() && has.value() );
}

/**
 * @given a fully-evicted delta re-attached through the responder's
 *        re-warm executor (fresh Pending journal record + hot bytes +
 *        surviving disposed marker)
 * @when  the drainer's next Put is parked so the re-learned record stays
 *        deterministically Pending, and a GC pass runs
 * @then  the pass nominates NOTHING (kEvictionDurabilityGate ==
 *        UploadedDurable, compile-time: a mid-drain re-learn is never
 *        evicted), the marker coexists with the re-learned bytes, the
 *        block never went missing, and after release the record
 *        converges Durable - D-11 evict-x-serve crossing 3.
 */
TEST_F( ColdStorageFaultRaceTest, EvictServe_RewarmAttachVsMark_NoLostBlock )
{
    const auto block = SeedDetachedDelta( "race/rewarm-attach", "value", false );
    const Cid  cid( block.cid_key );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionRace() );
    manager->Start();
    WaitAllDurable( { cid } );
    Backdate( cid, 60000 );
    ASSERT_EQ( manager->GcPassOnce().size(), 1u );
    GraceElapse();
    (void)manager->GcPassOnce(); // full eviction
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto disposed = shared_local_->IsDisposed( cid );
        ASSERT_TRUE( disposed.has_value() && disposed.value() );
    }

    // Park the drainer's Put BEFORE the re-learn so the re-journaled
    // record provably sits Pending (run 40949986714 OSX-Debug: arming
    // AFTER the relearn let the 100 ms drainer finish the re-Put first -
    // the record legitimately reached Durable and the pass correctly
    // nominated the backdated block; the test premise, not the product,
    // was racy). The re-warm worker's re-learn path only GETs; the held
    // op is the drain's re-Put.
    WindowGate put_hold;
    timing_->SetOnOp( S3Op::Put, [&] { put_hold.Enter(); } );

    // The responder miss re-attaches the block: immediate don't-have,
    // then the re-warm executor re-learns the bytes off the responder's
    // path.
    auto first = wrapper_->getNode( block.cid_key );
    EXPECT_TRUE( first.has_error() ) << "responder cold miss answers don't-have immediately";
    // SC4 round 8: 10 s -> 60 s (CI-load deadline discipline, rounds 4-6
    // precedent): the re-warm re-learn rides one hermetic S3 Get through
    // the loaded runner. Leading suspect for run 41024756227's 600 s
    // timeout in THIS case: the 10 s budget tripping under the -j24
    // full-suite load, whose ASSERT early-return then left the drainer
    // parked in the put_hold hook (deadlock until the kill) - the
    // truncated log buffer hid the failure text. ~WindowGate now
    // leak-proofs that path so a recurrence surfaces as the ASSERT
    // failure it is, never a hang.
    ASSERT_TRUE( PollUntil(
        [&] { return dagsyncer_->GetNodeWithoutRequest( block.cid_key ).has_value(); },
        std::chrono::milliseconds( 60000 ) ) )
        << "the re-warm executor did not re-learn the block";
    auto relearned = dagsyncer_->GetNodeWithoutRequest( block.cid_key );
    ASSERT_TRUE( relearned.has_value() );
    EXPECT_EQ( ContentBytes( relearned.value() ), block.content );

    // While the record is non-Durable (mid-drain), a GC pass must NOT
    // nominate it - no config knob can lower the compile-time gate.
    Backdate( cid, 60000 );
    EXPECT_TRUE( manager->GcPassOnce().empty() )
        << "a non-Durable re-learned record must never be an eviction candidate";

    // The marker coexists with the re-learned bytes (Phase-11 proven) and
    // the block never went missing.
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto disposed = shared_local_->IsDisposed( cid );
        ASSERT_TRUE( disposed.has_value() && disposed.value() );
    }
    auto still = dagsyncer_->GetNodeWithoutRequest( block.cid_key );
    ASSERT_TRUE( still.has_value() );
    EXPECT_EQ( ContentBytes( still.value() ), block.content );

    // Release the drainer: the record converges (createOnly - no
    // duplication).
    put_hold.Release();
    ASSERT_TRUE( PollUntil( [&] { return AllInState( { cid }, UploadState::UploadedDurable ); },
                            std::chrono::milliseconds( 10000 ) ) );
}

/**
 * @given a detached delta whose drain cycle is parked at its FIRST
 *        remote Put (the journal record sits deterministically Pending,
 *        mid-advance) and is backdated past retention
 * @when  a GC pass runs while the record is Pending, then again after
 *        the advance completes to Durable
 * @then  the Pending record is never evictable (the eligibility gate
 *        reads EXACTLY kEvictionDurabilityGate == UploadedDurable), and
 *        the same block becomes evictable once the evidence-backed
 *        Durable mark lands - safe in BOTH directions - D-11
 *        evict-x-drain crossing 1.
 */
TEST_F( ColdStorageFaultRaceTest, EvictDrain_EligibilityVsDurabilityAdvance_NeverEvictsNonDurable )
{
    const auto block = SeedDetachedDelta( "race/eligibility-vs-advance", "value", false );
    const Cid  cid( block.cid_key );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionRace() );
    manager->Start();

    // Park the drain cycle's remote Put: the record sits Pending (the
    // Accepted/Durable marks happen only after the Put phase).
    WindowGate put_hold;
    timing_->SetOnOp( S3Op::Put, [&] { put_hold.Enter(); } );

    Backdate( cid, 60000 );
    EXPECT_TRUE( manager->GcPassOnce().empty() )
        << "a Pending CID mid-advance must never be an eviction candidate";
    ASSERT_TRUE( manager->GetUploadState( cid ).has_value() );

    // After the advance completes, the SAME block becomes evictable.
    put_hold.Release();
    ASSERT_TRUE( PollUntil( [&] { return AllInState( { cid }, UploadState::UploadedDurable ); },
                            std::chrono::milliseconds( 10000 ) ) );
    Backdate( cid, 60000 );
    ASSERT_EQ( manager->GcPassOnce().size(), 1u );
    GraceElapse();
    (void)manager->GcPassOnce();
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto disposed = shared_local_->IsDisposed( cid );
        ASSERT_TRUE( disposed.has_value() && disposed.value() );
    }
}

/**
 * @given a detached delta evicted to the between-halves window (HALF1
 *        committed: journal record + cold bytes deleted atomically,
 *        disposed marker written) while the drainer cycles at 100 ms
 * @when  the drain snapshot runs inside the window, then a post-window
 *        re-learn re-journals fresh
 * @then  no drain work survives the HALF1 atomic batch (a snapshot can
 *        never hold a bytes-evicted item), and the re-learned record
 *        drains to Durable with no duplication - D-11 evict-x-drain
 *        crossing 2.
 */
TEST_F( ColdStorageFaultRaceTest, EvictDrain_DrainSnapshotVsHalfOne_NoEvictedBytesInDrain )
{
    const auto block = SeedDetachedDelta( "race/drain-snapshot-vs-half1", "value", false );
    const Cid  cid( block.cid_key );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionRace() );
    manager->Start();
    WaitAllDurable( { cid } );
    Backdate( cid, 60000 );
    ASSERT_EQ( manager->GcPassOnce().size(), 1u );
    GraceElapse();

    WindowGate gc_window;
    manager->SetGcBetweenHalvesHookForTesting( [&] { gc_window.Enter(); } );
    std::thread gc_thread( [&] { (void)manager->GcPassOnce(); } );
    ASSERT_TRUE( gc_window.WaitInside( std::chrono::milliseconds( 10000 ) ) );

    // HALF1 committed: the journal record is GONE atomically with the
    // bytes - no drain work item may survive the batch.
    EXPECT_EQ( manager->GetStatus().pending_upload_count, 0u )
        << "no drain work may survive the HALF1 atomic batch";
    EXPECT_FALSE( manager->GetUploadState( cid ).has_value() );

    gc_window.Release();
    gc_thread.join();

    // A post-window re-learn re-journals FRESH: the drain converges on
    // the createOnly remote (no duplication), and no drain item ever
    // observes evicted bytes.
    auto relearned = dagsyncer_->GetNodeWithoutRequest( block.cid_key );
    ASSERT_TRUE( relearned.has_value() );
    EXPECT_EQ( ContentBytes( relearned.value() ), block.content );
    ASSERT_TRUE( PollUntil( [&] { return AllInState( { cid }, UploadState::UploadedDurable ); },
                            std::chrono::milliseconds( 10000 ) ) );
    EXPECT_EQ( manager->GetStatus().pending_upload_count, 0u );
}

/**
 * @given a fully-evicted CID whose only copy is the S3 fake, with 250 ms
 *        per-op latency injected on BOTH remote ops (every op pays the
 *        latency under remote_store_mutex_ - serialized per-op, never
 *        parked; Pitfall 7)
 * @when  the cold chain fetches the CID under that latency while the
 *        drainer cycles, then the S3 tier is faulted Unavailable
 * @then  the fetch completes CID-verified well inside the re-warm budget
 *        (kRewarmBudget = 4 s), and the outage surfaces with the COLD
 *        taxonomy - never a hang, never a peer attribution - D-11
 *        serve-x-drain.
 */
TEST_F( ColdStorageFaultRaceTest, ServeDrain_RemoteLockLatency_ColdFetchWithinBudget )
{
    // TWO evicted blocks: A carries the latency leg, B (never read
    // before its fault) carries the honest-decline leg - A's latency
    // read re-learns A locally, so a fault on A could never fire.
    const auto block_a = SeedDetachedDelta( "race/remote-lock-latency", "value", true );
    const auto block_b = SeedDetachedDelta( "race/remote-lock-decline", "value", true );
    const Cid  cid_a( block_a.cid_key );
    const Cid  cid_b( block_b.cid_key );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionRace() );
    manager->Start();
    WaitAllDurable( { cid_a, cid_b } );
    Backdate( cid_a, 60000 );
    Backdate( cid_b, 60000 );
    ASSERT_EQ( manager->GcPassOnce().size(), 2u );
    GraceElapse();
    (void)manager->GcPassOnce();

    // Inject per-op S3 latency (latency, not outage - Pitfall 7).
    timing_->SetLatencyMs( S3Op::Get, 250 );
    timing_->SetLatencyMs( S3Op::Put, 250 );

    const auto     fetch_start = std::chrono::steady_clock::now();
    const uint64_t gets_before = counting_->get_calls();
    auto resolved = dagsyncer_->GetNodeWithoutRequest( block_a.cid_key );
    const int64_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - fetch_start ).count();
    ASSERT_TRUE( resolved.has_value() ) << "the cold fetch must complete under injected latency";
    EXPECT_EQ( ContentBytes( resolved.value() ), block_a.content );
    EXPECT_GT( counting_->get_calls(), gets_before );
    EXPECT_LT( elapsed_ms, 4000 )
        << "fetch must stay inside the re-warm budget (serialized, never deadlocked)";

    // The honest-decline side (block B, never read before the fault): an
    // S3 outage surfaces the COLD taxonomy - never a hang, never a peer
    // attribution.
    fake_->SetFaultForKey( DagPbObjectKey( block_b.cid_key ), S3Op::Get, S3FaultKind::Unavailable );
    auto declined = dagsyncer_->GetNodeWithoutRequest( block_b.cid_key );
    ASSERT_TRUE( declined.has_error() );
    EXPECT_EQ( declined.error(), GraphsyncDAGSyncer::Error::COLD_STORAGE_TEMPORARILY_UNAVAILABLE )
        << "storage faults classify cold - never PEER_BLACKLISTED";
    fake_->ClearFaults();

    // Recovery after the outage clears: the retry serves CID-verified.
    auto recovered = dagsyncer_->GetNodeWithoutRequest( block_b.cid_key );
    ASSERT_TRUE( recovered.has_value() );
    EXPECT_EQ( ContentBytes( recovered.value() ), block_b.content );
    timing_->ClearTimings();
}

/**
 * @given a due evicting mark on a detached Durable delta, with a serve
 *        read in flight and a publish firing at the TOP of the mark's
 *        processing (the pre-mark seam) making CURRENT heads differ from
 *        the MARK'S OWN pass-1 snapshot (Pitfall 5: the comparison basis
 *        is the mark's snapshot, never the current tick's)
 * @when  the delete-time re-check runs
 * @then  the mark CANCELS: bytes kept, no disposed marker, the journal
 *        record intact, the concurrent serve resolved the original
 *        bytes; with heads then stable the block returns to normal
 *        evaluation and a later pass evicts - D-11 triple crossing 1.
 */
TEST_F( ColdStorageFaultRaceTest, Triple_GraceExpiryReachableAgain_CancelsKeepsBytes )
{
    const auto block = SeedDetachedDelta( "race/triple-reachable-again", "value", false );
    const Cid  cid( block.cid_key );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionRace() );
    manager->Start();
    WaitAllDurable( { cid } );
    Backdate( cid, 60000 );
    ASSERT_EQ( manager->GcPassOnce().size(), 1u );
    GraceElapse();

    // The pre-mark seam fires BEFORE the re-check: the publish here
    // mutates heads against the MARK'S pass-1 snapshot while a serve
    // read runs concurrently.
    manager->SetGcPreMarkProcessHookForTesting( [this] {
        const CID rescue = PublishElement( "race/triple-rescue", "reach" );
        (void)rescue;
    } );

    std::thread serve_thread( [&] {
        auto served = dagsyncer_->GetNodeWithoutRequest( block.cid_key );
        EXPECT_TRUE( served.has_value() );
    } );

    (void)manager->GcPassOnce();
    serve_thread.join();
    manager->SetGcPreMarkProcessHookForTesting( nullptr );

    // CANCEL outcome: bytes kept, no marker, record intact.
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto disposed = shared_local_->IsDisposed( cid );
        ASSERT_TRUE( disposed.has_value() );
        EXPECT_FALSE( disposed.value() ) << "cancel keeps bytes - no disposed marker";
    }
    EXPECT_TRUE( manager->GetUploadState( cid ).has_value() ) << "journal record intact after cancel";
    auto kept = dagsyncer_->GetNodeWithoutRequest( block.cid_key );
    ASSERT_TRUE( kept.has_value() );
    EXPECT_EQ( ContentBytes( kept.value() ), block.content );

    // Normal evaluation resumes: with heads stable the block re-marks and
    // a later pass evicts (under-eviction is bounded, never permanent).
    Backdate( cid, 60000 );
    ASSERT_EQ( manager->GcPassOnce().size(), 1u );
    GraceElapse();
    (void)manager->GcPassOnce();
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto disposed = shared_local_->IsDisposed( cid );
        ASSERT_TRUE( disposed.has_value() && disposed.value() );
    }
    auto after = dagsyncer_->GetNodeWithoutRequest( block.cid_key );
    ASSERT_TRUE( after.has_value() );
    EXPECT_EQ( ContentBytes( after.value() ), block.content );
}

/**
 * @given a GC thread parked between the D-16 halves (HALF1 atomic batch
 *        committed) with the pubsub re-delivery probe and the drainer
 *        tick both running inside the window
 * @when  the same CID is re-delivered (the four-term HasBlock dedup
 *        entry) and a drain snapshot runs
 * @then  the disposed marker answers the probe TRUE with ZERO S3 Gets
 *        (the message drops before any merge; Pitfall 3: point lookups
 *        never block the delivery lane), nothing is pending, and the
 *        post-window re-learn drains to Durable without duplication -
 *        D-11 triple crossing 2.
 */
TEST_F( ColdStorageFaultRaceTest, Triple_DeleteBatchMidFlight_RedeliveryDedupDrainUnaffected )
{
    const auto block = SeedDetachedDelta( "race/triple-delete-midflight", "value", false );
    const Cid  cid( block.cid_key );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionRace() );
    manager->Start();
    WaitAllDurable( { cid } );
    Backdate( cid, 60000 );
    ASSERT_EQ( manager->GcPassOnce().size(), 1u );
    GraceElapse();

    WindowGate gc_window;
    manager->SetGcBetweenHalvesHookForTesting( [&] { gc_window.Enter(); } );
    std::thread gc_thread( [&] { (void)manager->GcPassOnce(); } );
    ASSERT_TRUE( gc_window.WaitInside( std::chrono::milliseconds( 10000 ) ) );

    // Pubsub re-delivery mid-flight: the disposed marker (written
    // atomically in HALF1) answers the dedup probe - the message drops
    // BEFORE any merge and BEFORE any S3 traffic.
    const uint64_t gets_before = counting_->get_calls();
    auto has = dagsyncer_->HasBlock( block.cid_key );
    ASSERT_TRUE( has.has_value() && has.value() )
        << "the disposed marker answers the re-delivery dedup probe";
    EXPECT_EQ( counting_->get_calls(), gets_before )
        << "dedup drops the re-delivery with ZERO S3 Gets";

    // The drainer tick inside the window: nothing pending.
    EXPECT_EQ( manager->GetStatus().pending_upload_count, 0u );

    gc_window.Release();
    gc_thread.join();

    // A post-window re-learn re-journals FRESH (exactly-once semantics).
    auto relearned = dagsyncer_->GetNodeWithoutRequest( block.cid_key );
    ASSERT_TRUE( relearned.has_value() );
    EXPECT_EQ( ContentBytes( relearned.value() ), block.content );
    ASSERT_TRUE( PollUntil( [&] { return AllInState( { cid }, UploadState::UploadedDurable ); },
                            std::chrono::milliseconds( 10000 ) ) );
}

/**
 * @given a due evicting mark whose delete-time re-check has PASSED, with
 *        heads mutated in the re-check-to-HALF1 gap (the pre-half-one
 *        seam - the mutation raced the check)
 * @when  HALF1 + HALF2 proceed
 * @then  the COMMIT side is legal (the check raced a last-moment
 *        publish) and the outcome is WHOLE, never torn: journal record
 *        gone + disposed marker present + remote copy CID-verifiable -
 *        D-11 cancel-vs-commit knife edge.
 */
TEST_F( ColdStorageFaultRaceTest, Evict_CancelVsCommitKnifeEdge_HeadsMutationBetweenRecheckAndHalfOne )
{
    const auto block = SeedDetachedDelta( "race/knife-edge", "value", false );
    const Cid  cid( block.cid_key );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionRace() );
    manager->Start();
    WaitAllDurable( { cid } );
    Backdate( cid, 60000 );
    ASSERT_EQ( manager->GcPassOnce().size(), 1u );
    GraceElapse();

    // The knife edge: heads mutate AFTER the re-check passed, BEFORE
    // HALF1 commits.
    manager->SetGcPreHalfOneHookForTesting( [this] {
        const CID mutation = PublishElement( "race/knife-mutation", "mut" );
        (void)mutation;
    } );
    (void)manager->GcPassOnce();
    manager->SetGcPreHalfOneHookForTesting( nullptr );

    // COMMIT outcome (whole, never torn): record deleted in HALF1,
    // marker present, remote copy resolvable CID-verified.
    EXPECT_FALSE( manager->GetUploadState( cid ).has_value() )
        << "committed: journal record deleted in HALF1";
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto disposed = shared_local_->IsDisposed( cid );
        ASSERT_TRUE( disposed.has_value() );
        EXPECT_TRUE( disposed.value() ) << "committed: marker written in HALF1";
    }
    auto resolved = dagsyncer_->GetNodeWithoutRequest( block.cid_key );
    ASSERT_TRUE( resolved.has_value() );
    EXPECT_EQ( ContentBytes( resolved.value() ), block.content );
}

/**
 * @given seeded jitter on both remote ops plus a randomized
 *        key-scoped-away fault draw per iteration (D-10)
 * @when  N seeded iterations each run a publish -> drain -> evict-or-
 *        cancel -> read-back cycle under that jitter (seed via
 *        SGNS_RACE_JITTER_SEED, count via SGNS_RACE_JITTER_ITERS;
 *        defaults reproduce deterministically)
 * @then  every iteration's CID stays resolvable with the ORIGINAL bytes
 *        (local OR remote - never lost), and NO iteration ever observes
 *        a Corrupt attributable to the race (the injected CorruptData is
 *        key-scoped away from the iteration's CIDs).
 */
TEST_F( ColdStorageFaultRaceTest, Jitter_PairwiseSeededIterations_EndStateInvariants )
{
    const uint64_t seed  = EnvOr( "SGNS_RACE_JITTER_SEED", 20261028ULL );
    const uint64_t iters = EnvOr( "SGNS_RACE_JITTER_ITERS", 200ULL );

    // Fast cadence so the default 200 iterations stay Tier-1-friendly.
    auto manager = MakeManager( FastOptions( 10, 10, 20 ), FastDurability( 10, 100 ),
                                RetentionRace() );
    manager->Start();

    for ( uint64_t i = 0; i < iters; ++i )
    {
        std::mt19937_64 rng( seed + i );

        // Seeded jitter on both remote ops (bounded, reproducible).
        timing_->SetSeededJitter( S3Op::Get, static_cast<uint32_t>( rng() % 20 ), 5, seed + i );
        timing_->SetSeededJitter( S3Op::Put, static_cast<uint32_t>( rng() % 20 ), 5, seed + i );

        // Randomized failure kind on a SACRIFICIAL control key (None /
        // Unavailable / CorruptData) - key-scoped AWAY from this
        // iteration's CID: any Corrupt observed on the iteration's CID
        // is race-attributable by construction.
        const std::string control_payload = "jitter-control-" + std::to_string( i );
        const Cid         control_cid     = MakeCid( control_payload );
        const std::string control_key     = DeriveTestKey( control_cid );
        const uint32_t    fault_draw      = static_cast<uint32_t>( rng() % 3 );
        if ( fault_draw == 1 )
        {
            fake_->SetFaultForKey( control_key, S3Op::Get, S3FaultKind::Unavailable );
        }
        else if ( fault_draw == 2 )
        {
            fake_->SetFaultForKey( control_key, S3Op::Get, S3FaultKind::CorruptData );
        }

        // The pairwise crossing under jitter: publish (write through the
        // decorator), drain under Put jitter, evict-or-cancel by a
        // seeded draw, read back under Get jitter.
        const auto block = SeedDetachedDelta( "race/jitter-" + std::to_string( i ), "v", false );
        const Cid  cid( block.cid_key );
        WaitAllDurable( { cid } );
        Backdate( cid, 60000 );
        (void)manager->GcPassOnce();
        if ( ( rng() % 2 ) == 0 )
        {
            // Cancel draw: heads mutate against the mark's snapshot
            // before the processing pass (the pre-mark shape, driven
            // test-side before the pass - the same interleaving class).
            const CID rescue = PublishElement( "race/jitter-rescue-" + std::to_string( i ), "r" );
            (void)rescue;
        }
        GraceElapse();
        (void)manager->GcPassOnce();

        // Per-iteration invariants (D-10): resolvable with ORIGINAL bytes
        // (never lost), and never Corrupt (the read would surface
        // COLD_STORAGE_CORRUPT on verification failure).
        auto resolved = dagsyncer_->GetNodeWithoutRequest( block.cid_key );
        ASSERT_TRUE( resolved.has_value() )
            << "iter " << i << ": block lost (neither local nor remote)";
        EXPECT_EQ( ContentBytes( resolved.value() ), block.content ) << "iter " << i;

        fake_->SetFaultForKey( control_key, S3Op::Get, S3FaultKind::None );
    }
    timing_->ClearTimings();
}

/**
 * @given a scenario mix inside ONE fixture: a hot reachable block, a
 *        cold-local block (journal Durable, hot bytes stripped), and a
 *        disposed-with-remote-copy block (the S3-retrievable evidence
 *        rides the same read)
 * @when  the D-12 audit walk visits every published CID
 * @then  each is EXACTLY one legal end state: hot (zero-S3-Get local
 *        resolution + journal record), cold-local (zero-S3-Get journal
 *        resolution), or disposed-with-remote-copy (marker + CID-
 *        verified remote resolution) - never lost, never Corrupt.
 */
TEST_F( ColdStorageFaultRaceTest, SuiteEnd_AuditWalk_EveryCidInLegalState )
{
    // Huge retention keeps (a)/(b) un-evicted while (c) is backdated
    // past its threshold (explicit tiny grace; zero normalizes -
    // Pitfall 2).
    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionRace( 60000, 1 ) );
    manager->Start();

    // (a) hot: reachable head, never a candidate.
    const CID hot_head = PublishElement( "audit/hot", "v" );
    auto hot_node = dagsyncer_->GetNodeWithoutRequest( hot_head );
    ASSERT_TRUE( hot_node.has_value() );
    SeededBlock hot_block{ hot_head, ContentBytes( hot_node.value() ) };

    // (b) cold-local: detached, Durable, hot bytes stripped - resolves
    // from the journal's local bytes with zero S3 traffic.
    SeededBlock cold_block = SeedDetachedDelta( "audit/cold-local", "v", true );
    WaitAllDurable( { Cid( cold_block.cid_key ) } );

    // (c) disposed-with-remote-copy: detached, backdated past the 60 s
    // threshold, evicted through two passes.
    SeededBlock gone_block = SeedDetachedDelta( "audit/disposed", "v", false );
    const Cid   gone_cid( gone_block.cid_key );
    WaitAllDurable( { gone_cid } );
    Backdate( gone_cid, 120000 );
    ASSERT_EQ( manager->GcPassOnce().size(), 1u );
    GraceElapse();
    (void)manager->GcPassOnce();
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto disposed = shared_local_->IsDisposed( gone_cid );
        ASSERT_TRUE( disposed.has_value() && disposed.value() );
    }

    // THE WALK (D-12): every published CID is EXACTLY one legal state.
    const SeededBlock entries[] = { hot_block, cold_block, gone_block };
    for ( const auto &entry : entries )
    {
        const Cid cid( entry.cid_key );
        bool disposed = false;
        {
            std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
            auto probe = shared_local_->IsDisposed( cid );
            ASSERT_TRUE( probe.has_value() );
            disposed = probe.value();
        }
        if ( disposed )
        {
            // disposed-with-remote-copy: the S3 object serves CID-VERIFIED
            // bytes (the library's Get verification runs inside the
            // chain).
            const uint64_t gets_before = counting_->get_calls();
            auto resolved = dagsyncer_->GetNodeWithoutRequest( entry.cid_key );
            ASSERT_TRUE( resolved.has_value() ) << "disposed CID must stay remote-retrievable";
            EXPECT_EQ( ContentBytes( resolved.value() ), entry.content );
            EXPECT_GT( counting_->get_calls(), gets_before )
                << "the remote tier served the disposed CID";
        }
        else
        {
            // hot or cold-local: journal record present + resolvable with
            // the ORIGINAL bytes and ZERO S3 traffic.
            EXPECT_TRUE( manager->GetUploadState( cid ).has_value() )
                << "non-disposed CID must still hold a journal record";
            const uint64_t gets_before = counting_->get_calls();
            auto resolved = dagsyncer_->GetNodeWithoutRequest( entry.cid_key );
            ASSERT_TRUE( resolved.has_value() );
            EXPECT_EQ( ContentBytes( resolved.value() ), entry.content );
            EXPECT_EQ( counting_->get_calls(), gets_before )
                << "hot/cold-local CIDs resolve with zero S3 traffic";
        }
    }
}

/**
 * @given a fully-evicted CID read through the cold chain while the S3
 *        tier is faulted Unavailable (op-wide)
 * @when  the fetch fails, then the fault clears and the read retries
 * @then  the outage surfaces with the honest COLD taxonomy (never
 *        PEER_BLACKLISTED - storage faults must never attribute to
 *        peers), and recovery serves CID-verified bytes. State-based
 *        zero-blacklist evidence: the blacklist write sites
 *        (graphsync_dagsyncer.cpp :421/:466/:523) are request-path-only
 *        and this fixture has NO peers and NO graphsync object - no
 *        scenario in this suite can reach them.
 */
TEST_F( ColdStorageFaultRaceTest, ZeroBlacklistWrites_AfterAllScenarios )
{
    const auto block = SeedDetachedDelta( "race/blacklist", "value", true );
    const Cid  cid( block.cid_key );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionRace() );
    manager->Start();
    WaitAllDurable( { cid } );
    Backdate( cid, 60000 );
    (void)manager->GcPassOnce();
    GraceElapse();
    (void)manager->GcPassOnce();

    // The S3 outage classifies COLD - never a peer-shaped failure.
    fake_->SetFault( S3Op::Get, S3FaultKind::Unavailable );
    (void)dagsyncer_->DeleteCIDBlock( block.cid_key );
    auto failed = dagsyncer_->GetNodeWithoutRequest( block.cid_key );
    ASSERT_TRUE( failed.has_error() );
    EXPECT_EQ( failed.error(), GraphsyncDAGSyncer::Error::COLD_STORAGE_TEMPORARILY_UNAVAILABLE )
        << "storage failures must never surface as PEER_BLACKLISTED";
    fake_->ClearFaults();

    // Recovery after the outage clears: CID-verified (no peer attribution
    // ever happened).
    auto recovered = dagsyncer_->GetNodeWithoutRequest( block.cid_key );
    ASSERT_TRUE( recovered.has_value() );
    EXPECT_EQ( ContentBytes( recovered.value() ), block.content );
}

/**
 * @given a fully-evicted CID whose ONLY remaining copy (the S3 object)
 *        is byte-flipped through the fake's flip-bytes fault kit
 * @when  the cold chain reads it
 * @then  the verification failure surfaces with the honest
 *        COLD_STORAGE_CORRUPT taxonomy - never the flipped bytes, never
 *        a NotFound-shaped success. This is the control proving the
 *        suite's zero-Corrupt assertions have teeth.
 */
TEST_F( ColdStorageFaultRaceTest, CorruptAssertionHasTeeth_ByteFlipStillCorrupt )
{
    const auto block = SeedDetachedDelta( "race/byte-flip-teeth", "value", true );
    const Cid  cid( block.cid_key );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionRace() );
    manager->Start();
    WaitAllDurable( { cid } );
    Backdate( cid, 60000 );
    (void)manager->GcPassOnce();
    GraceElapse();
    (void)manager->GcPassOnce();

    // Flip the only remaining copy - the zero-Corrupt assertions across
    // this suite are honest ONLY if this still surfaces Corrupt.
    fake_->SetFlipBytesForKey( DagPbObjectKey( block.cid_key ), true );
    (void)dagsyncer_->DeleteCIDBlock( block.cid_key );
    auto resolved = dagsyncer_->GetNodeWithoutRequest( block.cid_key );
    ASSERT_TRUE( resolved.has_error() ) << "flipped bytes must NEVER be served as good";
    EXPECT_EQ( resolved.error(), GraphsyncDAGSyncer::Error::COLD_STORAGE_CORRUPT );
    fake_->SetFlipBytesForKey( DagPbObjectKey( block.cid_key ), false );
}
