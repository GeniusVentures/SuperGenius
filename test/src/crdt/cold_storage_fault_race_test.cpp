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
#include <ipfs_lite/ipld/impl/ipld_node_impl.hpp>

namespace
{
    // IPFS resolves only inside namespace sgns - alias it for this TU
    // (the decorator's set()/get() surface is IpfsDatastore-typed).
    namespace ipfs_outcome = sgns::IPFS::outcome;
    using IpfsDatastore      = sgns::ipfs_lite::ipfs::IpfsDatastore;
    using IpfsDatastoreError = sgns::ipfs_lite::ipfs::IpfsDatastoreError;
    using CID                = sgns::CID;
    using sgns::base::Buffer;
    using sgns::crdt::BlockStoreBackedIpfsDatastore;
    using sgns::crdt::Broadcaster;
    using sgns::crdt::ColdReadService;
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
        }

        void TearDown() override
        {
            if ( manager_ )
            {
                manager_->Stop(); // joins drainer + GC threads first
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
    // TU cannot name sgns::crdt's outcome alias directly.)
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
