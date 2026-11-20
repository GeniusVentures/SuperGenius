/**
 * @file       cold_cross_feature_fault_race_test.cpp
 * @brief      Cross-feature fault-race matrix (v1.2 Phase 19, TEST-02):
 *             the erase x serve x recovery boundaries no single phase's
 *             suite owns, crossed case by case on ONE Tier-1 sibling
 *             suite (D-02 - the v1.1 cold_storage_fault_race_test.cpp
 *             13-case matrix stays byte-frozen; this suite re-declares
 *             its kit IN-FILE and never includes from or edits it).
 *
 * CASE GRAMMAR (D-01: a curated boundary list, not a formal product):
 *   X1  Cross_EraseCommitVsServe_EvidenceLandsMidConsult
 *       (serve consult S1 x erase marker commit E4; hazard: resurrection)
 *   X2  Cross_EraseCommitVsServe_InflightColdGetNoRehydrateAfterEvidence
 *       (cold Get S2 x evidence before Rehydrate S3; hazard: resurrection)
 *   X3  Cross_DurabilityFlipVsColdGate_ServeNeverUnlocksErase      (19-02)
 *   X4  Cross_GcEvictionMidWalkRestore_RestoreCompletesOrAborts    (19-03)
 *   X5  Cross_EraseMidWalkVisit_EvidenceSkipsNeverPartial          (19-02)
 *   X6  Cross_DowngradeArmsMidWalk_InterlockHoldsTimeline          (19-03)
 *   X7  Cross_ExemptionConsultVsConcurrentPrune_FailClosedControlsStay
 *                                                                   (19-03)
 *   X8  Cross_RehydrateVsSecondErase_GateDefersOnRejournaledPending (19-02)
 *   X9  Cross_PeerBudgetVsEvidenceTerminality_RefusalNotNotfound   (19-03)
 *   X10 Cross_SuiteEnd_AuditWalkAndCounterTeeth                    (19-04)
 *   X11 Cross_FullWire_RefusalFailoverUnderConcurrentErase         (19-04)
 * Every case carries (D-03, the Phase 16 D-06 two-layer discipline) a
 * deterministic seam-stepped leg - exact interleaving through the
 * serve-side interrupt seam (GraphsyncDAGSyncer::kInterruptAfterEviden-
 * ceConsult / kInterruptBeforeRehydrate / kInterruptAfterColdFetch, the
 * TxPruner seam family shape) or the existing GC/pruner seams - PLUS a
 * real-thread barrier leg (WindowGate + TimingS3Client op-barrier)
 * wherever true concurrency is the claim. Hazard coverage across the
 * matrix (D-04): resurrection = X1/X2/X5/X11; partial-history
 * validation = X4/X6/X7/X9; lost last copy = X3/X8/X11. The matrix
 * TABLE (case -> boundary -> hazard -> layer -> tier) is rendered ONCE
 * in notes/19-cross-feature-fault-race-three-platform-capstone.md
 * (plan 19-05) - this header points at it; the two never drift apart.
 *
 * RED-FIRST DECLARATION (the Phase 18 D-01 counter-test shape,
 * 19-RESEARCH Discretion 4): no live bug is assumed. Each case's FAULT
 * ARM runs only under SGNS_CROSS_RACE_RED_ARMS_ENABLE (never set in CI
 * lanes) and injects the BAD state the guard must catch - the evidence
 * lookup blinded to a /prune/blk row the store actually holds, i.e.
 * the serve completing as if NoEvidence had been consulted - so the
 * case's audit assertions FAIL against it (teeth, red-by-design). The
 * GREEN seam legs were authored EXPECTING a red-first find: the landed
 * serve path consulted evidence ONCE at the top (SERVE-01/02), leaving
 * the consult->Get and Get->Rehydrate windows unguarded - X1/X2's seam
 * legs observe RED against the unmodified line BY DESIGN until the
 * minimal evidence re-checks land (fix(19-01), the TxPruner guard-5
 * in-compose re-check precedent - repair the half, keep the design).
 * The fail-closed CONTROL legs (NoEvidence) are GREEN on the unmodified
 * line and must STAY green.
 *
 * BOTH-ORDERINGS discipline (TEST-02 boundary): every case proves BOTH
 * orderings of its boundary - the legal ordering green in the lane, the
 * illegal ordering red-by-design under the fault arm. EMPTY-EVIDENCE
 * discipline (TEST-02 empty): every consult-touching case carries a
 * NoEvidence control arm asserting the empty-input result (the body
 * served verbatim), never assuming it.
 *
 * Fixture posture (the 18-01 hermetic recipe verbatim,
 * cold_gc_walk_gap_exempt_test.cpp): real hot RocksDB + LocalBlockStore
 * cold dir + InMemoryS3Client/InMemoryS3AdminClient (installed through
 * real S3BlockStore/ColdStorageManager composition - hermetic fakes,
 * zero network, fresh-clone safe, D-10) + real ColdReadService /
 * GraphsyncDAGSyncer with the evidence lookup installed + real
 * CrdtDatastore + AttachGcDependencies + direct GcPassOnce drive with
 * gc_interval_ms = 0 (dormant cadence). Evidence becomes consultable
 * WITHOUT hand-built keys: the fixture installs a lookup on the REAL
 * ColdReadService via SetPruneEvidenceLookup whose verdicts derive
 * exclusively from PrunedBlockRecord bytes stored at keys produced by
 * the sanctioned PruneEligibility builder (BuildPruneBlockMarkerKey) -
 * the production lambda shape (native raw get + parse + fail-closed
 * Malformed). Zero string-formatted /prune/* keys anywhere in this
 * file (testing trap 1 / D-C1).
 *
 * Tiering (D-06): every case here is hermetic Tier-1 EXCEPT X11 (plan
 * 19-04) - the ONE full-wire leg, two real GlobalDBs over real gossip,
 * env-demotable via SGNS_CROSS_RACE_FULLWIRE_ENABLE per its promotion
 * record. Live MinIO stays Tier-3 dev-gated (Phase 18 precedent).
 *
 * Reproducibility contract: every nondeterministic leg reads
 * SGNS_CROSS_RACE_JITTER_SEED / SGNS_CROSS_RACE_JITTER_ITERS (default
 * 3) and EVERY case prints its knob values + effective seed in its
 * first log line (the compose-matrix convention). Red arms run only
 * under SGNS_CROSS_RACE_RED_ARMS_ENABLE. The suite joins the Tier1
 * label set (D-11 membership contract; NO new label name - the lane
 * stays ctest -L Tier1 exactly).
 *
 * Include discipline: every library result guarded with has_value()
 * BEFORE .value()/.error(). CID landmine: sgns::CID has NO implicit
 * ctor from the blockstore Cid - wrap explicitly (CID cid_key( cid ));
 * the reverse direction slices implicitly. Waits only via PollUntil
 * bounded predicates (the ONLY sleep_for in this file lives in the
 * re-declared PollUntil body); barriers are WindowGate cv-latches;
 * fresh fixture dir per case (distinct paths - Windows LOCK contenti-
 * on, Pitfall 4).
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
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

#include "account/proto/SGTransaction.pb.h"
#include "base/buffer.hpp"
#include "base/logger.hpp"
#include "crdt/broadcaster.hpp"
#include "crdt/cold_read_fallback.hpp"
#include "crdt/crdt_datastore.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/globaldb/BlockStoreBackedIpfsDatastore.hpp"
#include "crdt/globaldb/ColdReadService.hpp"
#include "crdt/globaldb/ColdStorageManager.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
#include "crdt/hierarchical_key.hpp"
#include "storage/database_error.hpp"
#include "storage/rocksdb/rocksdb.hpp"
#include "transaction/PruneEligibility.hpp"

#include <ipfs_lite/ipfs/impl/datastore_rocksdb.hpp>
#include <ipfs_lite/ipld/impl/ipld_node_impl.hpp>

namespace
{
    // IPFS resolves only inside namespace sgns - alias it for this TU.
    using IpfsDatastore      = sgns::ipfs_lite::ipfs::IpfsDatastore;
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
    using sgns::crdt::PruneEvidenceClass;
    using sgns::blockstore::Cid;
    using sgns::blockstore::InMemoryS3AdminClient;
    using sgns::blockstore::InMemoryS3Client;
    using sgns::blockstore::LocalBlockStore;
    using sgns::blockstore::LocalBlockStoreOptions;
    using sgns::blockstore::PutObjectOptions;
    using sgns::blockstore::result;
    using sgns::blockstore::S3BlockStore;
    using sgns::blockstore::S3Client;
    using sgns::blockstore::S3HeadResult;
    using sgns::blockstore::S3Object;
    using sgns::blockstore::S3Op;
    using sgns::blockstore::S3PutResult;
    using sgns::blockstore::UploadState;
    using sgns::storage::rocksdb;
    using libp2p::multi::MulticodecType;

    /// EnvOr (the v1.1 kit shape, re-declared in-file per D-05/Pitfall 2).
    uint64_t EnvOr( const char *name, uint64_t fallback )
    {
        const char *raw = std::getenv( name );
        return raw == nullptr ? fallback
                              : static_cast<uint64_t>( std::strtoull( raw, nullptr, 10 ) );
    }

    /// Presence-flag knob (non-empty value = ON; an empty string from a
    /// CI env-expression parses OFF - the SGNS_TEST_P99_BOUND_MS lesson).
    bool KnobEnabled( const char *name )
    {
        const char *raw = std::getenv( name );
        return raw != nullptr && *raw != '\0';
    }

    /// Bounded poll loop with deadline - never a fixed sleep, so the suite
    /// stays stable on loaded Windows CI-equivalent machines. The ONLY
    /// sleep_for in this file lives in this body (suite-header contract).
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

    /**
     * @brief One-shot enter/release latch: the deterministic barrier the
     *        real-thread legs park actors on (D-03 layer two). Enter()
     *        marks the actor inside the window and blocks until
     *        Release(); Release() is idempotent and lets any LATER Enter
     *        pass straight through, so a failing leg can never hang
     *        teardown (the v1.1 round-8 leak-proofing shape).
     *
     * cv-wait based throughout - no sleep_for.
     */
    class WindowGate
    {
    public:
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
     * @brief Test-local S3Client timing decorator (the v1.1 D-10/D-11
     *        instrument, re-declared in-file per D-05/Pitfall 2 with one
     *        suite-local addition: per-op counters for the "the barrier
     *        leg really went through the S3 tier" observable). Chained
     *        OUTERMOST around the in-memory fake.
     *
     * Lock discipline (Pitfall 7): the config mutex guards ONLY the
     * timing tables; the on-op hook fires and the delay elapses AFTER
     * the config mutex is released and OUTSIDE the inner fake's lock,
     * so a parked barrier never serializes unrelated ops. Delays are cv
     * wait_for (interruptible at destruction) - never sleep_for.
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
        /// lock. The hook MAY BLOCK - that is the barrier. Null clears.
        void SetOnOp( S3Op op, std::function<void()> hook )
        {
            std::lock_guard<std::mutex> lock( config_mutex_ );
            Entry( op ).on_op = std::move( hook );
        }

        /// Seeded bounded jitter for the op: each call draws a delay in
        /// [max(0, base - spread), base + spread] from the seeded engine
        /// (repro-on-failure via SGNS_CROSS_RACE_JITTER_SEED).
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

        /// Suite-local observable: how many times the op hit the S3 tier.
        uint64_t OpCount( S3Op op ) const
        {
            return op_counts_[static_cast<size_t>( op )].load( std::memory_order_relaxed );
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
            uint32_t              latency_ms{ 0 };
            std::string           key_filter; ///< empty = every key
            std::function<void()> on_op;      ///< pre-delegation barrier hook
            uint32_t              jitter_base_ms{ 0 };
            uint32_t              jitter_spread_ms{ 0 };
            std::mt19937_64       rng{ 1 };
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
            op_counts_[static_cast<size_t>( op )].fetch_add( 1, std::memory_order_relaxed );
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

        std::shared_ptr<S3Client>     inner_;
        std::array<OpTiming, kOpCount> entries_{};
        std::array<std::atomic<uint64_t>, kOpCount> op_counts_{};
        std::mutex                    config_mutex_;
        std::mutex                    delay_mutex_;
        std::condition_variable       delay_cv_;
        bool                          delay_abort_ = false;
    };

    /// Wall-clock unix-ms (the spill-timestamp clock).
    uint64_t UnixNowMs()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch() )
                .count() );
    }

    /// Short-interval drainer options (the durability-suite FastOptions shape).
    ColdStorageManager::BackpressureOptions FastOptions( uint32_t drain_interval_ms = 100 )
    {
        ColdStorageManager::BackpressureOptions options;
        options.drain_interval_ms = drain_interval_ms;
        options.warn_pending      = 10;
        options.critical_pending  = 20;
        options.backoff_cap_ms    = 1000;
        return options;
    }

    /// Fast flush cadence so the Durable wait completes in milliseconds.
    ColdStorageManager::DurabilityOptions FastDurability()
    {
        ColdStorageManager::DurabilityOptions durability;
        durability.flush_min_interval_ms = 50;
        durability.flush_max_interval_ms = 300;
        return durability;
    }

    /// Dormant-cadence retention: gc_interval_ms = 0 keeps the background
    /// GC thread dormant while tests drive passes directly; explicit zero
    /// grace (the 18-01 posture).
    ColdStorageManager::RetentionOptions RetentionDormant( uint64_t retention_age_ms = 5000 )
    {
        ColdStorageManager::RetentionOptions retention;
        retention.retention_age_ms = retention_age_ms;
        retention.dry_run          = false;
        retention.gc_interval_ms   = 0;
        retention.grace_ms         = 0;
        return retention;
    }

    gsl::span<const uint8_t> SpanOf( const std::string &s )
    {
        return gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( s.data() ), s.size() );
    }

    /// Verified CID for a payload (library constructor, RAW codec).
    Cid MakeCid( const std::string &payload )
    {
        auto cid = sgns::blockstore::CidOf( SpanOf( payload ), MulticodecType::Code::RAW );
        return cid.value(); // guarded by construction - payload is valid
    }

    // SHORT relative path under the build tree (Windows MAX_PATH).
    std::string NextUniqueDir( const char *lane )
    {
        static std::atomic<uint64_t> next{ 0 };
        return std::string( "test-data/cold-cross-race/" ) + lane + "-case-"
             + std::to_string( next.fetch_add( 1 ) );
    }

    // A truly FRESH store per case: wipe before Open (stale journals from
    // a previous process run must never be reopened).
    std::string FreshDir( const char *lane )
    {
        auto dir = NextUniqueDir( lane );
        std::error_code ec;
        std::filesystem::remove_all( dir, ec );
        return dir;
    }

    /**
     * @brief Test-local NO-OP broadcaster: Publish needs a broadcaster;
     *        nothing is ever received (the 18-01 NoopBroadcaster shape).
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
            return outcome::failure( boost::system::error_code{} );
        }

        bool HasTopic( const std::string & ) override
        {
            return true;
        }
    };

    /**
     * @brief      The cross-feature fault-race hermetic fixture (the
     *             18-01 recipe: real components everywhere the serve /
     *             erase / walk paths are real, InMemoryS3* fakes at the
     *             network boundary, real RocksDB evidence rows at
     *             sanctioned-builder keys).
     *
     * Suite-local deltas over 18-01: (a) the remote tier is wrapped in
     * the in-file TimingS3Client so barrier legs can park/widen the S3
     * boundary deterministically; (b) the evidence lookup consults an
     * atomic suppression flag - armed ONLY by fault arms under
     * SGNS_CROSS_RACE_RED_ARMS_ENABLE - that blinds the lookup to a
     * /prune/blk row the store really holds (the pre-guard continuation
     * injection, Discretion 4); (c) TearDown clears the serve interrupt
     * seam (leak discipline).
     */
    class ColdCrossFeatureFaultRaceTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            red_arms_enabled_  = KnobEnabled( "SGNS_CROSS_RACE_RED_ARMS_ENABLE" );
            fullwire_enabled_  = KnobEnabled( "SGNS_CROSS_RACE_FULLWIRE_ENABLE" );
            jitter_seed_raw_   = EnvOr( "SGNS_CROSS_RACE_JITTER_SEED", 20261119ULL );
            jitter_iters_      = EnvOr( "SGNS_CROSS_RACE_JITTER_ITERS", 3ULL );
            effective_seed_    = jitter_seed_raw_ != 0
                     ? jitter_seed_raw_
                     : static_cast<uint64_t>(
                         std::chrono::steady_clock::now().time_since_epoch().count() );

            // The REAL hot CRDT RocksDB store (heads + set rows + the
            // evidence rows land here). create does NOT create parent
            // directories - create them explicitly.
            hot_dir_ = FreshDir( "hot" );
            std::error_code hot_ec;
            std::filesystem::create_directories( hot_dir_, hot_ec );
            ASSERT_FALSE( hot_ec );
            rocksdb::Options hot_options;
            hot_options.create_if_missing = true;
            auto opened_hot = rocksdb::create( hot_dir_, hot_options );
            ASSERT_TRUE( opened_hot.has_value() );
            real_hot_ = std::move( opened_hot.value() );

            // The cold tier (the durability-suite fixture shape).
            shared_local_dir_ = FreshDir( "cold" );
            auto opened_local = LocalBlockStore::Open( LocalBlockStoreOptions{ shared_local_dir_ } );
            ASSERT_TRUE( opened_local.has_value() );
            shared_local_ = std::shared_ptr<LocalBlockStore>( std::move( opened_local.value() ) );

            fake_   = std::make_shared<InMemoryS3Client>();
            timing_ = std::make_shared<TimingS3Client>( fake_ );
            auto opened_remote = S3BlockStore::Open( timing_ );
            ASSERT_TRUE( opened_remote.has_value() );
            remote_ = std::shared_ptr<S3BlockStore>( std::move( opened_remote.value() ) );

            fake_admin_       = std::make_shared<InMemoryS3AdminClient>();
            cold_store_mutex_ = std::make_shared<std::mutex>();

            // The DAG layer (the production shared-DB shape): ONE
            // underlying RocksDB wrapped twice - sgns::storage::rocksdb
            // for the CrdtDatastore, ipfs_lite::rocksdb over the SAME
            // DB handle for the RocksdbDatastore the decorator wraps.
            auto ipfs_db = sgns::ipfs_lite::rocksdb::create( real_hot_->getDB() );
            ASSERT_TRUE( ipfs_db.has_value() );
            inner_     = std::make_shared<sgns::ipfs_lite::ipfs::RocksdbDatastore>( ipfs_db.value() );
            decorator_ = std::make_shared<BlockStoreBackedIpfsDatastore>(
                inner_, shared_local_, cold_store_mutex_, sgns::base::createLogger( "ColdXFeat" ) );

            // The REAL cold-read chain as the DAGSyncer hook.
            remote_store_mutex_ = std::make_shared<std::mutex>();
            cold_read_service_  = std::make_shared<ColdReadService>(
                shared_local_, remote_, cold_store_mutex_, remote_store_mutex_, decorator_ );

            // EVIDENCE INSTALL: the ONE predicate the serve path (and the
            // walk) consults. Verdicts derive exclusively from
            // PrunedBlockRecord rows at sanctioned-builder keys - the
            // production lambda shape (native raw read + parse +
            // fail-closed Malformed, Pitfall 7). No row = NoEvidence; an
            // unreadable row = Malformed, NEVER treated as absent.
            cold_read_service_->SetPruneEvidenceLookup(
                [this]( const CID &cid ) -> PruneEvidenceClass
                {
                    if ( evidence_suppressed_.load( std::memory_order_acquire ) )
                    {
                        // FAULT-ARM ONLY (SGNS_CROSS_RACE_RED_ARMS_ENABLE,
                        // never set in CI): the pre-guard continuation -
                        // the serve proceeds as if NoEvidence had been
                        // consulted while the evidence row exists in the
                        // store. This branch is the red-arm teeth probe.
                        return PruneEvidenceClass::NoEvidence;
                    }
                    const auto        cid_string_result = cid.toString();
                    const std::string cid_string = cid_string_result.has_value()
                                                     ? cid_string_result.value()
                                                     : std::string();
                    const std::string key_string
                        = sgns::PruneEligibility::BuildPruneBlockMarkerKey( cid_string );
                    Buffer marker_key;
                    marker_key.put( key_string );
                    auto marker = real_hot_->get( marker_key );
                    if ( marker.has_error() )
                    {
                        if ( marker.error() == sgns::storage::DatabaseError::NOT_FOUND )
                        {
                            return PruneEvidenceClass::NoEvidence;
                        }
                        return PruneEvidenceClass::Malformed;
                    }
                    SGTransaction::PrunedBlockRecord record;
                    if ( marker.value().empty()
                         || !record.ParseFromArray( marker.value().data(),
                                                    static_cast<int>( marker.value().size() ) ) )
                    {
                        return PruneEvidenceClass::Malformed;
                    }
                    return PruneEvidenceClass::Pruned;
                } );

            dagsyncer_ = std::make_shared<GraphsyncDAGSyncer>(
                decorator_, nullptr, nullptr /* no graphsync/host needed for the hermetic legs */,
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
            // LEAK DISCIPLINE (the seam's fixture contract): a hook armed
            // by a failing case must never leak further - clear FIRST so
            // even a torn-down-mid-case fixture leaves no armed hook.
            GraphsyncDAGSyncer::SetColdServeInterruptForTest( nullptr );
            evidence_suppressed_.store( false, std::memory_order_release );

            if ( manager_ )
            {
                manager_->Stop(); // joins drainer + GC threads first
            }
            if ( crdt_ )
            {
                crdt_->CancelAndCloseNow();
            }
        }

        /// Reproducibility contract: every case's FIRST log line.
        void LogKnobs( const char *case_id ) const
        {
            std::cout << "[" << case_id << "] knobs:"
                      << " SGNS_CROSS_RACE_JITTER_SEED=" << jitter_seed_raw_
                      << " SGNS_CROSS_RACE_JITTER_ITERS=" << jitter_iters_
                      << " SGNS_CROSS_RACE_RED_ARMS_ENABLE=" << ( red_arms_enabled_ ? 1 : 0 )
                      << " SGNS_CROSS_RACE_FULLWIRE_ENABLE=" << ( fullwire_enabled_ ? 1 : 0 )
                      << " effective_seed=" << effective_seed_ << std::endl;
        }

        /// Construct a manager directly over the fixture stores with the
        /// fake admin and the dormant-cadence retention posture.
        std::shared_ptr<ColdStorageManager> MakeManager()
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
                RetentionDormant( 5000 ) );
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

        /// True when every CID's journal record reads the requested state.
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
        /// running drainer.
        void WaitAllDurable( const std::vector<Cid> &cids )
        {
            ASSERT_TRUE( PollUntil( [&] { return AllInState( cids, UploadState::UploadedDurable ); },
                                    std::chrono::milliseconds( 10000 ) ) )
                << "seed blocks did not reach UploadedDurable within deadline";
        }

        /// Simulate the TxPruner erase's HOT half for one CID: delete the
        /// block's hot binary key (cid.toBytes()) and drop the LRU copy.
        void RemoveHotCopy( const CID &cid )
        {
            auto encoded = cid.toBytes();
            ASSERT_TRUE( encoded.has_value() );
            Buffer binary_key;
            binary_key.put( encoded.value() );
            auto removed = real_hot_->remove( binary_key );
            ASSERT_TRUE( removed.has_value() );
            (void)dagsyncer_->DeleteCIDBlock( cid ); // LRU invalidation
        }

        /// True when the CID's raw bytes sit in the hot store (the
        /// resurrection detector's byte-level half - the LRU half is
        /// IsCIDInCache).
        bool HotBytesPresent( const CID &cid ) const
        {
            auto encoded = cid.toBytes();
            if ( !encoded.has_value() )
            {
                return false;
            }
            Buffer binary_key;
            binary_key.put( encoded.value() );
            auto read = real_hot_->get( binary_key );
            return read.has_value();
        }

        /// Quiet variant of the evidence seeder for hooks and non-test
        /// threads (no gtest macros - the writer threads report through
        /// the return value).
        bool SeedPruneEvidenceQuiet( const CID &cid, bool corrupt = false )
        {
            const auto cid_string = cid.toString();
            if ( !cid_string.has_value() )
            {
                return false;
            }
            const std::string key_string
                = sgns::PruneEligibility::BuildPruneBlockMarkerKey( cid_string.value() );
            Buffer marker_key;
            marker_key.put( key_string );
            Buffer marker_value;
            if ( corrupt )
            {
                marker_value.put( std::string( "\x80\x80\x80\x80\x80", 5 ) );
            }
            else
            {
                SGTransaction::PrunedBlockRecord record;
                record.set_pruning_tx_hash( "test-pruning-tx-hash" );
                record.set_has_erased_at_generation( true );
                record.set_erased_at_generation( 1 );
                marker_value.put( record.SerializeAsString() );
            }
            auto written = real_hot_->put( marker_key, marker_value );
            return written.has_value();
        }

        /// Seed PRUNED evidence for one CID at a sanctioned-builder key
        /// (test-thread variant).
        void SeedPruneEvidence( const CID &cid, bool corrupt = false )
        {
            ASSERT_TRUE( SeedPruneEvidenceQuiet( cid, corrupt ) );
        }

        /// True when a parseable /prune/blk row exists at the
        /// sanctioned-builder key for this CID (the fault-arm state
        /// verifier: the evidence REALLY landed; only the lookup is
        /// blinded).
        bool EvidenceInStore( const CID &cid ) const
        {
            const auto cid_string = cid.toString();
            if ( !cid_string.has_value() )
            {
                return false;
            }
            const std::string key_string
                = sgns::PruneEligibility::BuildPruneBlockMarkerKey( cid_string.value() );
            Buffer marker_key;
            marker_key.put( key_string );
            auto marker = real_hot_->get( marker_key );
            return marker.has_value() && !marker.value().empty();
        }

        // Knobs (read once per fixture; printed by LogKnobs).
        bool     red_arms_enabled_  = false;
        bool     fullwire_enabled_  = false;
        uint64_t jitter_seed_raw_   = 0;
        uint64_t jitter_iters_      = 3;
        uint64_t effective_seed_    = 0;

        /// Fault-arm injection: blinds the installed lookup while the
        /// evidence row stays in the store (RED-ARMS knob only).
        std::atomic<bool> evidence_suppressed_{ false };

        // Declaration order == reverse destruction order: manager_ dies
        // first (its threads join against still-live stores), then the
        // CRDT layer, then the DAG layer, then the stores.
        std::string                             hot_dir_;
        std::shared_ptr<rocksdb>                real_hot_;
        std::string                             shared_local_dir_;
        std::shared_ptr<LocalBlockStore>        shared_local_;
        std::shared_ptr<InMemoryS3Client>       fake_;
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

// ---------------------------------------------------------------------------
// X1 (plan 19-01 Task 1): erase marker commit x serve consult.
// ---------------------------------------------------------------------------

/**
 * @given a published, Durable, cold-resident CID whose hot copy is
 *        removed (the serve must go through the cold chain), with NO
 *        evidence row at the consult's start
 * @when  the serve's LookupPruneEvidence consult returns NoEvidence and
 *        the erase cycle's /prune/blk marker commits INSIDE the
 *        consult-to-cold-Get gap (the step-1 seam: the hook writes the
 *        sanctioned evidence row at the boundary, then continues)
 * @then  the serve REFUSES with HISTORY_PRUNED_LOCALLY (the terminal
 *        refusal - SERVE-04 returns it unchanged, so a peer fall-through
 *        would instead surface ROUTE_NOT_FOUND), the CID never lands hot
 *        (neither LRU nor raw bytes), and the evidence row is really in
 *        the store. The NoEvidence control (no evidence written at the
 *        boundary) serves the cold body verbatim and rehydrates hot.
 *
 * PRIMARY HAZARD (D-04): RESURRECTION - evidenced bytes served/rehydrated
 * through the serve path.
 *
 * RED-FIRST (Discretion 4): the fault arm (SGNS_CROSS_RACE_RED_ARMS_
 * ENABLE only) blinds the lookup to the row it holds (the pre-guard
 * continuation): the serve completes as if NoEvidence had been consulted,
 * the evidenced CID lands hot, and this case's audit FAILS against it -
 * the teeth. The seam leg itself was authored RED against the unmodified
 * single-consult serve path (the consult-to-Get window unguarded) until
 * the fix(19-01) re-check lands - the red-first find.
 */
TEST_F( ColdCrossFeatureFaultRaceTest, Cross_EraseCommitVsServe_EvidenceLandsMidConsult )
{
    LogKnobs( "X1" );
    const bool red_arms = red_arms_enabled_;

    // ---- CONTROL (must stay green before AND after the fix): no
    // evidence anywhere - the cold body serves verbatim.
    {
        const CID control_cid = PublishElement( "x1/control/key", "control-value" );
        auto      manager     = MakeManager();
        manager->Start();
        WaitAllDurable( { Cid( control_cid ) } );
        RemoveHotCopy( control_cid );

        GraphsyncDAGSyncer::SetColdServeInterruptForTest(
            []( int ) { return false; } /* observe-and-continue at every step */ );
        auto served = dagsyncer_->getNode( control_cid );
        EXPECT_TRUE( served.has_value() )
            << "NoEvidence control: the cold body must serve verbatim";
        EXPECT_TRUE( dagsyncer_->IsCIDInCache( control_cid ) )
            << "NoEvidence control: the serve rehydrates the CID hot (the v1.1 path)";
        GraphsyncDAGSyncer::SetColdServeInterruptForTest( nullptr );

        manager_->Stop();
        manager_ = nullptr;
    }

    // ---- SEAM LEG (the boundary under test): evidence lands inside the
    // consult-to-Get gap.
    {
        const CID cid = PublishElement( "x1/seam/key", "seam-value" );
        auto      manager = MakeManager();
        manager->Start();
        WaitAllDurable( { Cid( cid ) } );
        RemoveHotCopy( cid );

        GraphsyncDAGSyncer::SetColdServeInterruptForTest(
            [this, cid]( int step ) {
                if ( step == GraphsyncDAGSyncer::kInterruptAfterEvidenceConsult )
                {
                    // The erase-side marker commit lands INSIDE the gap:
                    // sanctioned-builder row, real store, mid-serve.
                    SeedPruneEvidence( cid );
                }
                return false; // observe-and-continue: the serve runs on
            } );
        auto served = dagsyncer_->getNode( cid );
        GraphsyncDAGSyncer::SetColdServeInterruptForTest( nullptr );

        EXPECT_TRUE( served.has_error() )
            << "X1 seam leg: evidence landing mid-consult must refuse the serve";
        if ( served.has_error() )
        {
            EXPECT_EQ( served.error(),
                       sgns::crdt::GraphsyncDAGSyncer::Error::HISTORY_PRUNED_LOCALLY )
                << "the terminal refusal (never a peer fall-through shape)";
        }
        EXPECT_FALSE( dagsyncer_->IsCIDInCache( cid ) )
            << "no LRU re-add of an evidenced CID";
        EXPECT_FALSE( HotBytesPresent( cid ) ) << "no hot re-add of an evidenced CID";
        EXPECT_TRUE( EvidenceInStore( cid ) )
            << "the evidence row really landed at the boundary (sanctioned key)";

        manager_->Stop();
        manager_ = nullptr;
    }

    // ---- FAULT ARM (red-by-design, RED-ARMS knob only): the lookup is
    // blinded to the row it holds - the pre-guard continuation. The
    // audit below FAILS against the induced resurrection (teeth).
    if ( red_arms )
    {
        const CID cid = PublishElement( "x1/fault/key", "fault-value" );
        auto      manager = MakeManager();
        manager->Start();
        WaitAllDurable( { Cid( cid ) } );
        RemoveHotCopy( cid );

        GraphsyncDAGSyncer::SetColdServeInterruptForTest(
            [this, cid]( int step ) {
                if ( step == GraphsyncDAGSyncer::kInterruptAfterEvidenceConsult )
                {
                    SeedPruneEvidence( cid );
                    // The fault injection: from here on the lookup answers
                    // NoEvidence while the store holds the row.
                    evidence_suppressed_.store( true, std::memory_order_release );
                }
                return false;
            } );
        auto served = dagsyncer_->getNode( cid );
        GraphsyncDAGSyncer::SetColdServeInterruptForTest( nullptr );
        evidence_suppressed_.store( false, std::memory_order_release );

        EXPECT_TRUE( EvidenceInStore( cid ) )
            << "fault arm precondition: the evidence row is really in the store";
        EXPECT_FALSE( served.has_value() )
            << "RED-BY-DESIGN (fault arm): with the guard's input blinded the "
               "serve completes - the induced resurrection this arm must "
               "catch (teeth)";
        EXPECT_FALSE( dagsyncer_->IsCIDInCache( cid ) )
            << "RED-BY-DESIGN (fault arm): evidenced CID in LRU - the "
               "resurrection detector must fail loudly here";
        EXPECT_FALSE( HotBytesPresent( cid ) )
            << "RED-BY-DESIGN (fault arm): evidenced CID hot - the "
               "resurrection detector must fail loudly here";

        manager_->Stop();
        manager_ = nullptr;
    }
}
