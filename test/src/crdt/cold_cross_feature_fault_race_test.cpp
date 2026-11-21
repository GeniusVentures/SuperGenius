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
 * bounded predicates (the only sleeping wait in this file lives in the
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
#include <boost/filesystem.hpp>
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

// The GlobalDB tx-stack world (the cold_prune_gate_test recipe) the
// erase-arm cases (X3/X8/X5) build on: pubsub + scheduler + graphsync
// network for GlobalDB::New, the account/blockchain/tx-manager surface
// TxPruner::RunCycle needs, and the soralog system the pubsub host logs
// through (the resurrection-test include set).
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <libp2p/host/host.hpp>
#include <libp2p/basic/scheduler.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <soralog/impl/configurator_from_yaml.hpp>

#include "account/GeniusAccount.hpp"
#include "account/NodeType.hpp"
#include "account/TransferTransaction.hpp"
#include "account/UTXOManager.hpp"
#include "blockchain/Blockchain.hpp"
#include "crdt/globaldb/ColdStorageStack.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "transaction/TransactionManager.hpp"
#include "transaction/TxPruner.hpp"

namespace sgns
{
    /**
     * Per-binary friend seam (the tx_pruner_test shape): TxPruner's tick
     * gate requires State::READY, which no public API reaches
     * deterministically. Carries only ChangeState.
     */
    class TransactionManagerPendingLifecycleTestAccess
    {
    public:
        static void ChangeState( TransactionManager &manager, TransactionManager::State state )
        {
            manager.ChangeState( state );
        }
    };
} // namespace sgns

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

    // The tx-world constants (the cold_prune_gate contract values).
    constexpr char    kXSeedTopic[]     = "CRDT.Datastore.TEST.Channel";
    constexpr uint64_t kXWindow         = 2;
    constexpr uint64_t kXTestGeneration = 7;
    constexpr uint64_t kXSettleSeconds  = 1;
    const sgns::TokenID kXTokenId       = sgns::TokenID::FromBytes( { 0x00 } );


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
    /// stays stable on loaded Windows CI-equivalent machines. The only
    /// sleeping wait in this file lives in this body (suite-header contract).
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
     * cv-wait based throughout - no timed sleeps.
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
     * wait_for (interruptible at destruction) - never a timed sleep.
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
    /// The libp2p/soralog bootstrap the tx-stack hosts need (the
    /// ResurrectLoggingConfig shape, trimmed to the console sink).
    std::string CrossRaceLoggingConfig()
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

    class ColdCrossFeatureFaultRaceTest : public ::testing::Test
    {
    public:
        static void SetUpTestSuite()
        {
            // The pubsub host logs through the libp2p/soralog system -
            // configure it once per binary (the cold_prune_gate/resurrection
            // recipe).
            auto log_system = std::make_shared<soralog::LoggingSystem>(
                std::make_shared<soralog::ConfiguratorFromYAML>(
                    std::make_shared<libp2p::log::Configurator>(), CrossRaceLoggingConfig() ) );
            (void)log_system->configure();
            libp2p::log::setLoggingSystem( log_system );
        }

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
            sgns::TxPruner::SetCycleInterruptHookForTest( nullptr );
            sgns::crdt::GlobalDB::SetColdUploadStateHookForTest( nullptr );
            sgns::crdt::GlobalDB::SetEraseInterruptHookForTest( nullptr );
            sgns::crdt::SetColdStorageStackClientsForTesting( nullptr, nullptr );
            evidence_suppressed_.store( false, std::memory_order_release );

            TeardownTxStack();

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
        /// fake admin and the dormant-cadence retention posture. @p
        /// grace_ms: the eviction D-13 grace the deferred marks wait out.
        /// NOTE (the 19-03 run3 find): grace 0 is NOT zero - the manager
        /// rewrites it to kDefaultGraceMs (60000 ms, ColdStorageManager
        /// ctor), so any leg that drives ProcessDueMarks must pass an
        /// explicit small grace (the v1.1 evict-serve recipe's 1 ms).
        std::shared_ptr<ColdStorageManager> MakeManager( uint32_t grace_ms = 0 )
        {
            auto remote_store_mutex = std::make_shared<std::mutex>();
            auto                     retention = RetentionDormant( 5000 );
            retention.grace_ms                = grace_ms;
            manager_                          = std::make_shared<ColdStorageManager>(
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

        /// Journal an ORPHAN block (no delta, no head, never reachable)
        /// through the decorator - the spill shape without CRDT presence
        /// (the 18-01 SetOrphan shape; X4's GC-eviction target, the only
        /// legally evictable block in the hermetic world - a linked
        /// ancestor is reachability-pinned by the GC's walk).
        Cid SetOrphan( const std::string &payload )
        {
            const auto cid = MakeCid( payload );
            IpfsDatastore::Value value;
            value.put( payload );
            auto set = decorator_->set( CID( cid ), std::move( value ) );
            EXPECT_TRUE( set.has_value() );
            return cid;
        }

        /// Backdate a block's spill timestamp past the retention
        /// threshold (the D-01 age evidence - the 18-01 Backdate shape,
        /// under the shared cold-store mutex).
        void BackdateCid( const Cid &cid, uint64_t age_ms )
        {
            std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
            auto written = shared_local_->SetSpilledAt( cid, UnixNowMs() - age_ms );
            ASSERT_TRUE( written.has_value() );
        }

        /// Flags captured by the barrier choreography (no gtest macros
        /// off the main thread - the caller ASSERTs with an instrumented
        /// dump on any failure).
        struct BarrierRun
        {
            bool inside_window = false; ///< the serve reached + parked in the S3-tier Get
            bool writer_wrote  = false; ///< the evidence row committed while parked
            bool serve_ran     = false; ///< the serve thread completed getNode
            bool serve_refusal = false; ///< outcome == HISTORY_PRUNED_LOCALLY (no torn serve)
        };

        /**
         * @brief      The D-03 layer-two choreography: a REAL serve thread
         *             runs getNode end-to-end while a REAL evidence-writer
         *             thread commits the /prune/blk marker inside the
         *             in-flight cold read.
         *
         * The TimingS3Client op-barrier (SetOnOp on the S3-tier Get)
         * parks the serve thread inside the window; the WindowGate
         * choreographs the collision (the evidence write provably lands
         * before the Get returns); the optional seeded jitter stretches
         * the release-to-return sub-window (reproducible via
         * SGNS_CROSS_RACE_JITTER_SEED/_ITERS). The caller evicts the
         * local cold copy first so the cold read MUST go through the S3
         * tier where the barrier engages.
         *
         * Parked-Get lock discipline (Pitfall 7): the park sits inside
         * ColdReadService's remote tier under remote_store_mutex_, which
         * the evidence writer never touches (it writes the hot RocksDB
         * evidence row) and nothing else holds in these legs (the drainer
         * is idle post-Durable) - no deadlock window.
         *
         * @param[in] cid the cold-resident (S3-only) CID being served
         * @param[in] jitter_base_ms/jitter_spread_ms seeded in-flight
         *            stretch armed on the S3 Get (0/0 = none)
         * @param[in] iter_seed the per-iteration seed (effective_seed_+i)
         */
        BarrierRun RunServeVsEvidenceWriterBarrier( const CID &cid,
                                                    uint32_t    jitter_base_ms  = 0,
                                                    uint32_t    jitter_spread_ms = 0,
                                                    uint64_t    iter_seed       = 0 )
        {
            BarrierRun      run;
            WindowGate      gate;
            std::atomic<bool> writer_done{ false };

            timing_->SetOnOp( S3Op::Get, [ &gate ] { gate.Enter(); } );
            if ( jitter_base_ms > 0 || jitter_spread_ms > 0 )
            {
                timing_->SetSeededJitter( S3Op::Get, jitter_base_ms, jitter_spread_ms,
                                          iter_seed );
            }

            std::thread serve_thread( [ this, cid, &run ] {
                auto served       = dagsyncer_->getNode( cid );
                run.serve_ran     = true;
                run.serve_refusal = served.has_error()
                                    && served.error()
                                           == sgns::crdt::GraphsyncDAGSyncer::Error::HISTORY_PRUNED_LOCALLY;
            } );

            run.inside_window = gate.WaitInside( std::chrono::milliseconds( 10000 ) );
            if ( run.inside_window )
            {
                std::thread writer_thread( [ this, cid, &writer_done ] {
                    writer_done.store( SeedPruneEvidenceQuiet( cid ) );
                } );
                const bool writer_seen = PollUntil( [ &writer_done ] { return writer_done.load(); },
                                                    std::chrono::milliseconds( 5000 ) );
                run.writer_wrote = writer_seen && writer_done.load();
                gate.Release();
                writer_thread.join();
            }
            else
            {
                gate.Release(); // leak-proof destructor backstop
            }
            serve_thread.join();
            timing_->ClearTimings();
            return run;
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

        // =================================================================
        // The GlobalDB tx-stack world (the cold_prune_gate_test recipe) -
        // the erase-arm cases (X3/X8/X5) build this ON DEMAND on top of
        // the per-case fixture: pubsub host + GlobalDB (cold ON, hermetic
        // fakes through the process-global override, wrapped in the
        // in-file TimingS3Client) + account/blockchain/TransactionManager
        // + a real TxPruner cycle. TeardownTxStack (also called from
        // TearDown) releases in the CRDTFixture order.
        // =================================================================

        /// Builds the tx stack on a FRESH dir (the cold_prune_gate
        /// BuildStack shape; drain 50 ms; the injected remote is the
        /// in-file TimingS3Client over the hermetic fake so barrier legs
        /// can park/widen the S3 boundary in this world too).
        void BuildTxStack()
        {
            gw_hot_dir_ = FreshDir( "gw-hot" );
            BuildTxStackOnDir( gw_hot_dir_ );
        }

        void BuildTxStackOnDir( const std::string &hot_dir )
        {
            gw_hot_dir_     = hot_dir;
            gw_keypair_dir_ = FreshDir( "gw-keypair" );

            gw_io_ = std::make_shared<boost::asio::io_context>();
            gw_pubs_ = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
                sgns::crdt::KeyPairFileStorage( gw_keypair_dir_ ).GetKeyPair().value() );
            auto start_future = gw_pubs_->Start( 0, {} );
            ASSERT_TRUE( PollUntil(
                [&] { return start_future.wait_for( std::chrono::milliseconds( 0 ) ) == std::future_status::ready; },
                std::chrono::milliseconds( 20000 ) ) )
                << "tx-stack pubsub host failed to start";
            ASSERT_FALSE( start_future.get() ) << "GossipPubSub::Start failed";

            gw_sched_ = std::make_shared<libp2p::basic::SchedulerImpl>(
                std::make_shared<libp2p::basic::AsioSchedulerBackend>( gw_pubs_->GetAsioContext() ),
                libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
            gw_net_  = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( gw_pubs_->GetHost(), gw_sched_ );
            gw_gen_  = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();

            sgns::crdt::GlobalDB::ColdStorageOptions cold;
            cold.enabled   = true;
            cold.endpoint  = "hermetic.invalid"; // overridden below - never contacted
            cold.bucket    = "cross-race";
            cold.accessKey = "test";
            cold.secretKey = "test";
            cold.backpressure.drain_interval_ms = 50;
            cold.backpressure.warn_pending      = 100000;
            cold.backpressure.critical_pending  = 200000;
            cold.backpressure.backoff_cap_ms    = 1000;
            cold.durability.flush_min_interval_ms = 50;
            cold.durability.flush_max_interval_ms = 300;
            // Retention 0 = the GC never runs - nothing but the pruner may
            // erase (the cold_prune_gate D-02 posture).
            gw_fake_   = std::make_shared<InMemoryS3Client>();
            gw_timing_ = std::make_shared<TimingS3Client>( gw_fake_ );
            gw_admin_  = std::make_shared<InMemoryS3AdminClient>();
            sgns::crdt::SetColdStorageStackClientsForTesting(
                std::static_pointer_cast<S3Client>( gw_timing_ ),
                std::static_pointer_cast<sgns::blockstore::S3AdminClient>( gw_admin_ ) );

            auto globaldb_ret = sgns::crdt::GlobalDB::New( gw_io_,
                                                           hot_dir,
                                                           gw_pubs_,
                                                           sgns::crdt::CrdtOptions::DefaultOptions(),
                                                           gw_net_,
                                                           gw_sched_,
                                                           gw_gen_,
                                                           nullptr,
                                                           sgns::crdt::GlobalDB::BackupOptions{ false, 15, 12, true },
                                                           cold );
            ASSERT_TRUE( globaldb_ret.has_value() ) << "tx-stack GlobalDB failed to construct";
            gw_db_ = std::move( globaldb_ret.value() );

            // Hygiene: the process-global override has served its purpose.
            sgns::crdt::SetColdStorageStackClientsForTesting( nullptr, nullptr );

            gw_db_->AddListenTopic( kXSeedTopic );
            gw_db_->AddBroadcastTopic( kXSeedTopic );
            gw_db_->Start();

            sgns::GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<sgns::ISecureStorage> {
                    return std::make_shared<sgns::MemorySecureStorage>( identifier );
                } );
            gw_account_ = sgns::GeniusAccount::New( kXTokenId, boost::filesystem::path( hot_dir ) / "account" );
            ASSERT_NE( gw_account_, nullptr );
            auto load_result = gw_account_->GetUTXOManager().LoadUTXOs( gw_db_->GetDataStore() );
            ASSERT_TRUE( load_result.has_value() );
            gw_chain_ = sgns::Blockchain::New( gw_db_, gw_account_, gw_pubs_, []( outcome::result<void> ) {} );
            ASSERT_NE( gw_chain_, nullptr );
            constexpr auto kTol = std::chrono::milliseconds( 300000 );
            constexpr auto kMut = std::chrono::milliseconds( 600000 );
            gw_tm_ = sgns::TransactionManager::New( gw_db_, gw_io_, gw_account_, gw_chain_,
                                                    sgns::NodeType::Light, 0, kTol, kMut );
            ASSERT_NE( gw_tm_, nullptr );
        }

        /// The CRDTFixture teardown order: stop the blockchain FIRST, then
        /// the manager stack, quiesce the GlobalDB (drainer + GC + the
        /// coldstore dir lock), stop the pubsub host LAST.
        void TeardownTxStack()
        {
            if ( gw_chain_ )
            {
                (void)gw_chain_->Stop();
            }
            gw_tm_.reset();
            gw_chain_.reset();
            gw_account_.reset();
            if ( gw_db_ )
            {
                gw_db_->ShutdownNow();
            }
            gw_db_.reset();
            try
            {
                if ( gw_pubs_ )
                {
                    gw_pubs_->Stop();
                }
            }
            catch ( const std::exception &err )
            {
                std::cerr << "GossipPubSub::Stop() exception: " << err.what() << std::endl;
            }
            gw_pubs_.reset();
            gw_io_.reset();
        }

        /// The tx-world syncer (the syncerOf idiom, globaldb_erase_test).
        std::shared_ptr<GraphsyncDAGSyncer> TxSyncer() const
        {
            return std::static_pointer_cast<GraphsyncDAGSyncer>( gw_db_->GetBroadcaster()->GetDagSyncer() );
        }

        // --- Seeding helpers (the cold_prune_gate recipes verbatim) ---

        std::shared_ptr<sgns::TransferTransaction> MakeXTransfer( uint64_t nonce, size_t output_count )
        {
            SGTransaction::DAGStruct dag;
            dag.set_type( "transfer" );
            dag.set_source_addr( gw_account_->GetAddress() );
            dag.set_nonce( nonce );
            dag.set_timestamp( static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch() )
                    .count() ) );
            std::vector<sgns::OutputDestInfo> destinations;
            destinations.reserve( output_count );
            for ( size_t i = 0; i < output_count; ++i )
            {
                destinations.push_back( { 100 + i, gw_account_->GetAddress(), kXTokenId } );
            }
            return std::make_shared<sgns::TransferTransaction>(
                sgns::TransferTransaction::New( {}, std::move( destinations ), std::move( dag ) ) );
        }

        /// Real-topic value Put + raw tx->CID map entry (StoreTransactionCID shapes).
        void SeedXTransaction( const std::shared_ptr<sgns::GeniusTransaction> &transaction )
        {
            const std::string tx_path = sgns::TransactionManager::GetTransactionPath( *transaction );
            sgns::crdt::GlobalDB::Buffer serialized;
            serialized.put( transaction->SerializeByteVector() );
            auto put_result = gw_db_->Put( { tx_path }, serialized, { kXSeedTopic } );
            ASSERT_TRUE( put_result.has_value() );
            sgns::crdt::GlobalDB::Buffer key_buffer;
            key_buffer.put( tx_path );
            sgns::crdt::GlobalDB::Buffer value_buffer;
            value_buffer.put( put_result.value().toString().value() );
            ASSERT_TRUE( gw_db_->GetDataStore()->put( key_buffer, value_buffer ).has_value() );
        }

        /// Plain topic Put of a raw string at @p key (driver heights).
        void PutXSeedValue( const std::string &key, const std::string &value )
        {
            sgns::crdt::GlobalDB::Buffer buffer;
            buffer.put( value );
            ASSERT_TRUE( gw_db_->Put( { key }, buffer, { kXSeedTopic } ).has_value() );
        }

        void TrackXOutputs( const sgns::GeniusTransaction &transaction )
        {
            auto outputs = transaction.GetProducedUTXOs();
            ASSERT_TRUE( outputs.has_value() );
            for ( const auto &output : *outputs )
            {
                auto put = gw_account_->GetUTXOManager().PutUTXO( output, output.GetOwnerAddress() );
                ASSERT_TRUE( put.has_value() );
                ASSERT_TRUE( put.value() );
            }
        }

        bool ConsumeXOutput( const sgns::GeniusTransaction &transaction,
                             uint32_t                        output_idx,
                             const sgns::GeniusTransaction  &spender )
        {
            auto txid = sgns::base::Hash256::fromReadableString( transaction.GetHash() );
            EXPECT_FALSE( txid.has_error() );
            auto consumed = gw_account_->GetUTXOManager().ConsumeUTXOs(
                { { txid.value(), output_idx, {} } },
                gw_account_->GetAddress(),
                sgns::UTXOManager::UTXOType::UTXO_NORMAL,
                sgns::base::Hash256::fromReadableString( spender.GetHash() ).value() );
            EXPECT_TRUE( consumed.has_value() );
            return consumed.has_value() && consumed.value();
        }

        std::string SnapshotXMapEntry( const std::string &tx_path )
        {
            auto entry = gw_db_->GetRaw( sgns::crdt::GlobalDB::Buffer{}.put( tx_path ) );
            return entry.has_value() ? std::string( entry.value().toString() ) : std::string( "absent" );
        }

        sgns::crdt::ColdUploadState TxJournalStateOf( const std::string &cid_string )
        {
            auto cid = CID::fromString( cid_string );
            EXPECT_FALSE( cid.has_error() );
            return gw_db_->GetColdUploadState( cid.value() );
        }

        std::shared_ptr<sgns::TxPruner> MakeXPruner()
        {
            sgns::PruningConfig config;
            config.enabled              = true;
            config.dry_run              = false;
            config.finality_window      = kXWindow;
            config.max_erases_per_cycle = 10;
            config.settle_seconds       = kXSettleSeconds;
            auto pruner                 = sgns::TxPruner::New( sgns::NodeType::Light, config );
            EXPECT_TRUE( pruner.has_value() );
            return pruner.has_value() ? pruner.value() : nullptr;
        }

        /// One plain cycle (the tick gate forced READY - the friend seam).
        /// Settled cycle (the RunSettledCycle discriminator): a FRESH
        /// pruner instance's tick gate settle-skips until the settle
        /// window elapses - loop bounded until ONE real cycle runs (the
        /// watermark appears), delivering its stats.
        outcome::result<sgns::PruneCycleStats> TxRunCycle( sgns::TxPruner &pruner )
        {
            sgns::TransactionManagerPendingLifecycleTestAccess::ChangeState(
                *gw_tm_, sgns::TransactionManager::State::READY );
            const auto deadline = std::chrono::steady_clock::now()
                                + std::chrono::seconds( kXSettleSeconds + 8 );
            outcome::result<sgns::PruneCycleStats> last = sgns::PruneCycleStats{};
            while ( std::chrono::steady_clock::now() < deadline )
            {
                last = pruner.RunCycle( *gw_db_, *gw_tm_, gw_account_->GetUTXOManager(), kXTestGeneration );
                if ( last.has_error() )
                {
                    return last;
                }
                if ( pruner.GetPruneStatus().watermark.has_value() )
                {
                    return last; // ONE real cycle ran to completion
                }
            }
            return last; // deadline: deliver the last (skipped) cycle
        }

            void SeedX5Graph( uint64_t base, std::string *path_out, std::string *cid_out,
                                    std::string *twin_path_out ) {
        auto spender  = MakeXTransfer( base, 0 );
        auto eligible = MakeXTransfer( base + 1, 1 );
        auto twin     = MakeXTransfer( base + 2, 1 );
        SeedXTransaction( spender );
        PutXSeedValue( "/seed/x5/driver-1-" + std::to_string( base ), "d1" );
        SeedXTransaction( eligible );
        SeedXTransaction( twin );
        TrackXOutputs( *eligible );
        TrackXOutputs( *twin );
        ASSERT_TRUE( ConsumeXOutput( *eligible, 0, *spender ) );
        PutXSeedValue( "/seed/x5/head-" + std::to_string( base ), "head" );
        *path_out     = sgns::TransactionManager::GetTransactionPath( *eligible );
        *twin_path_out = sgns::TransactionManager::GetTransactionPath( *twin );
        *cid_out      = SnapshotXMapEntry( *path_out );
        ASSERT_NE( *cid_out, "absent" );
        ASSERT_TRUE( PollUntil( [&, cid_out] {
                         return TxJournalStateOf( *cid_out ) == sgns::crdt::ColdUploadState::Durable;
                     },
                     std::chrono::milliseconds( 20000 ) ) )
            << "X5: the graph block never reached Durable";
        // The twin's marker-less eviction: the target block goes
        // cold-resident; the eligible value + map survive.
        auto evicted = gw_db_->EraseEntry( *twin_path_out, CID::fromString( *cid_out ).value(), std::nullopt );
        ASSERT_TRUE( evicted.has_value() );
        // The walk element filter (the production surface defers
        // unfiltered elements - the W-leg recipe).
        (void)gw_db_->RegisterElementFilter(
            "^/seed/x5/head-",
            []( const sgns::crdt::CrdtDatastore::Element & ) {
                return sgns::crdt::CRDTDataFilter::ElementFilterResult::Accept();
            } );

        // LRU-guard hygiene (run8 find, for the SUMMARY): the post-seed
        // link traversal re-fetches the twin-evicted block through the
        // cold serve and leaves it LRU-cached; EraseEntry's guard (3)
        // then refuses every subsequent erase ("cid ... is in the block
        // cache" - the cycle logs errors=1, erased=0). Drop the LRU
        // copy (the X8-B recipe - any datastore bytes stay) until the
        // post-commit traversal has quiesced, so the pruner's erase AND
        // the fault arm's plain EraseEntry both reach their real
        // targets.
        ASSERT_TRUE( PollUntil( [this, cid_out] {
                         auto cid_parsed = CID::fromString( *cid_out );
                         if ( cid_parsed.has_error() )
                         {
                             return false;
                         }
                         (void)TxSyncer()->DeleteCIDBlock( cid_parsed.value() );
                         return !TxSyncer()->IsCIDInCache( cid_parsed.value() );
                     },
                     std::chrono::milliseconds( 5000 ) ) )
            << "X5: the LRU copy never quiesced after the post-seed traversal";
    }
        /// The tx-world stack handles (null unless BuildTxStack ran).
        std::string                                              gw_hot_dir_;
        std::string                                              gw_keypair_dir_;
        std::shared_ptr<boost::asio::io_context>                 gw_io_;
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>         gw_pubs_;
        std::shared_ptr<libp2p::basic::SchedulerImpl>            gw_sched_;
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network> gw_net_;
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> gw_gen_;
        std::shared_ptr<sgns::crdt::GlobalDB>                    gw_db_;
        std::shared_ptr<InMemoryS3Client>                        gw_fake_;
        std::shared_ptr<TimingS3Client>                          gw_timing_;
        std::shared_ptr<InMemoryS3AdminClient>                   gw_admin_;
        std::shared_ptr<sgns::GeniusAccount>                     gw_account_;
        std::shared_ptr<sgns::Blockchain>                        gw_chain_;
        std::shared_ptr<sgns::TransactionManager>                gw_tm_;

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

// ---------------------------------------------------------------------------
// X2 (plan 19-01 Task 2): cold Get in flight x evidence before Rehydrate.
// ---------------------------------------------------------------------------

/**
 * @given a published, Durable, cold-resident CID whose hot copy is
 *        removed (the serve must go through the cold chain), with NO
 *        evidence row at the consult's start
 * @when  the cold Get has ALREADY returned bytes (the step-2 seam:
 *        kInterruptBeforeRehydrate) and the erase cycle's /prune/blk
 *        marker commits at that exact point (evidence landing inside
 *        the in-flight window), then the serve continues
 * @then  the CID is NOT rehydrated (no hot re-add of an evidenced CID):
 *        the bytes are discarded and the refusal surfaces as
 *        HISTORY_PRUNED_LOCALLY - the strongest observable. The
 *        NoEvidence control (identical drive, no evidence write)
 *        rehydrates normally (the CID hot again, the v1.1 serve path).
 *
 * PRIMARY HAZARD (D-04): RESURRECTION.
 *
 * RED-FIRST NOTE (Discretion 4 + the Task-2 escape clause): X2's window
 * (Get-to-Rehydrate) was closed by the SAME fix(19-01) pre-Rehydrate
 * re-check the X1 find forced - the seam leg below proves it at this
 * boundary exactly. The fault arm (knob-gated) blinds the re-check's
 * input so the evidenced CID rehydrates and this case's audit FAILS
 * against it - the teeth.
 */
TEST_F( ColdCrossFeatureFaultRaceTest, Cross_EraseCommitVsServe_InflightColdGetNoRehydrateAfterEvidence )
{
    LogKnobs( "X2" );
    const bool red_arms = red_arms_enabled_;

    // ---- CONTROL (must stay green): identical drive without the
    // evidence write - rehydrate proceeds, the CID is hot again.
    {
        const CID control_cid = PublishElement( "x2/control/key", "control-value" );
        auto      manager     = MakeManager();
        manager->Start();
        WaitAllDurable( { Cid( control_cid ) } );
        RemoveHotCopy( control_cid );

        GraphsyncDAGSyncer::SetColdServeInterruptForTest(
            [control_cid]( int step ) {
                (void)step; // no evidence written at any boundary
                return false;
            } );
        auto served = dagsyncer_->getNode( control_cid );
        GraphsyncDAGSyncer::SetColdServeInterruptForTest( nullptr );
        EXPECT_TRUE( served.has_value() )
            << "NoEvidence control: rehydrate proceeds (the v1.1 serve path)";
        EXPECT_TRUE( dagsyncer_->IsCIDInCache( control_cid ) )
            << "NoEvidence control: the CID is hot again";

        manager_->Stop();
        manager_ = nullptr;
    }

    // ---- SEAM LEG: evidence lands after the cold Get returned bytes,
    // before Rehydrate (the step-2 boundary).
    {
        const CID cid = PublishElement( "x2/seam/key", "seam-value" );
        auto      manager = MakeManager();
        manager->Start();
        WaitAllDurable( { Cid( cid ) } );
        RemoveHotCopy( cid );

        GraphsyncDAGSyncer::SetColdServeInterruptForTest(
            [this, cid]( int step ) {
                if ( step == GraphsyncDAGSyncer::kInterruptBeforeRehydrate )
                {
                    // The erase-side marker commit lands INSIDE the
                    // Get-to-Rehydrate gap: cold bytes already fetched,
                    // Rehydrate not yet run.
                    SeedPruneEvidence( cid );
                }
                return false;
            } );
        auto served = dagsyncer_->getNode( cid );
        GraphsyncDAGSyncer::SetColdServeInterruptForTest( nullptr );

        EXPECT_TRUE( served.has_error() )
            << "X2 seam leg: evidence landing before Rehydrate must refuse the serve";
        if ( served.has_error() )
        {
            EXPECT_EQ( served.error(),
                       sgns::crdt::GraphsyncDAGSyncer::Error::HISTORY_PRUNED_LOCALLY )
                << "the fetched bytes are discarded - the terminal refusal";
        }
        EXPECT_FALSE( dagsyncer_->IsCIDInCache( cid ) )
            << "no LRU re-add of an evidenced CID";
        EXPECT_FALSE( HotBytesPresent( cid ) ) << "no hot re-add of an evidenced CID";
        EXPECT_TRUE( EvidenceInStore( cid ) )
            << "the evidence row really landed at the boundary (sanctioned key)";

        manager_->Stop();
        manager_ = nullptr;
    }

    // ---- FAULT ARM (red-by-design, RED-ARMS knob only): the pre-
    // Rehydrate re-check's input is blinded - the evidenced CID
    // rehydrates and the audit below FAILS (teeth).
    if ( red_arms )
    {
        const CID cid = PublishElement( "x2/fault/key", "fault-value" );
        auto      manager = MakeManager();
        manager->Start();
        WaitAllDurable( { Cid( cid ) } );
        RemoveHotCopy( cid );

        GraphsyncDAGSyncer::SetColdServeInterruptForTest(
            [this, cid]( int step ) {
                if ( step == GraphsyncDAGSyncer::kInterruptBeforeRehydrate )
                {
                    SeedPruneEvidence( cid );
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
            << "RED-BY-DESIGN (fault arm): with the re-check's input blinded "
               "the serve rehydrates - the induced resurrection this arm "
               "must catch (teeth)";
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

// ---------------------------------------------------------------------------
// X1/X2 real-thread barrier legs (D-03 layer two - true concurrency).
// ---------------------------------------------------------------------------

/**
 * @brief X1 layer two (D-03): the erase marker commit racing a REAL
 *        serve thread through the in-flight cold read (WindowGate +
 *        TimingS3Client op-barrier; no interrupt seam armed).
 *
 * @given a Durable CID whose hot AND local-cold copies are removed
 *        (the cold read must cross the S3 tier where the barrier parks)
 * @when  a real serve thread runs getNode while a real writer thread
 *        commits the evidence row inside the parked window (the gate
 *        forces the collision ordering - the evidence provably lands
 *        before the Get returns, whichever sub-window timing occurs)
 * @then  the end-state invariant holds under real racing: the serve
 *        refuses (HISTORY_PRUNED_LOCALLY - never a torn outcome), and
 *        the evidenced CID is never present hot (neither LRU nor raw
 *        bytes). Deadline-exhaustion dumps the barrier flags + S3 op
 *        counts in the failure message (instrument, don't bump).
 */
TEST_F( ColdCrossFeatureFaultRaceTest, Cross_EraseCommitVsServe_EvidenceLandsMidConsult_RealThreadBarrier )
{
    LogKnobs( "X1-BARRIER" );

    const CID cid = PublishElement( "x1/barrier/key", "barrier-value" );
    auto      manager = MakeManager();
    manager->Start();
    WaitAllDurable( { Cid( cid ) } );
    RemoveHotCopy( cid );
    auto evicted = shared_local_->EvictLocal( Cid( cid ) );
    ASSERT_TRUE( evicted.has_value() ) << "local cold copy evicted - the read must cross the S3 tier";

    const auto run = RunServeVsEvidenceWriterBarrier( cid );

    ASSERT_TRUE( run.inside_window )
        << "X1 barrier: the serve never parked inside the S3-tier Get "
           "(s3_get_count=" << timing_->OpCount( S3Op::Get ) << ")";
    ASSERT_TRUE( run.writer_wrote )
        << "X1 barrier: the evidence writer did not complete inside the window";
    ASSERT_TRUE( run.serve_ran ) << "X1 barrier: the serve thread never completed";
    EXPECT_TRUE( run.serve_refusal )
        << "X1 barrier: no torn serve - the only legal outcome is the "
           "terminal refusal (HISTORY_PRUNED_LOCALLY), never a peer "
           "fall-through shape";
    EXPECT_TRUE( EvidenceInStore( cid ) ) << "the evidence row is in the store";
    EXPECT_FALSE( dagsyncer_->IsCIDInCache( cid ) )
        << "barrier invariant: no evidenced CID ever present hot (LRU)";
    EXPECT_FALSE( HotBytesPresent( cid ) )
        << "barrier invariant: no evidenced CID ever present hot (raw bytes)";
    EXPECT_GE( timing_->OpCount( S3Op::Get ), 1u )
        << "the cold read really crossed the S3 tier";

    manager_->Stop();
    manager_ = nullptr;
}

/**
 * @brief X2 layer two (D-03): the same real-thread collision, iterated
 *        under seeded jitter so the evidence write lands at sampled
 *        points inside the stretched in-flight window (the release-to-
 *        return sub-window varies per iteration; reproducible via
 *        SGNS_CROSS_RACE_JITTER_SEED/_ITERS, effective seed printed by
 *        LogKnobs).
 *
 * @given one Durable, S3-only CID per iteration (hot + local-cold
 *        copies removed)
 * @when  each iteration races the real serve against the real writer
 *        through the parked window with a per-iteration seeded Get
 *        jitter
 * @then  EVERY iteration holds the invariant: refusal outcome, no hot
 *        re-add, evidence in store - and the S3 tier was really crossed.
 */
TEST_F( ColdCrossFeatureFaultRaceTest, Cross_EraseCommitVsServe_InflightColdGetNoRehydrateAfterEvidence_RealThreadBarrier )
{
    LogKnobs( "X2-BARRIER" );

    auto manager = MakeManager();
    manager->Start();

    for ( uint64_t iter = 0; iter < jitter_iters_; ++iter )
    {
        const CID cid = PublishElement( "x2/barrier/key/" + std::to_string( iter ),
                                       "barrier-value-" + std::to_string( iter ) );
        WaitAllDurable( { Cid( cid ) } );
        RemoveHotCopy( cid );
        auto evicted = shared_local_->EvictLocal( Cid( cid ) );
        ASSERT_TRUE( evicted.has_value() ) << "iter " << iter << ": local cold copy evicted";

        const auto run = RunServeVsEvidenceWriterBarrier( cid,
                                                          /* jitter_base_ms  */ 25,
                                                          /* jitter_spread_ms */ 15,
                                                          /* iter_seed       */ effective_seed_ + iter );

        ASSERT_TRUE( run.inside_window )
            << "X2 barrier iter " << iter << ": the serve never parked inside the "
               "S3-tier Get (s3_get_count=" << timing_->OpCount( S3Op::Get ) << ")";
        ASSERT_TRUE( run.writer_wrote )
            << "X2 barrier iter " << iter << ": the evidence writer did not complete";
        ASSERT_TRUE( run.serve_ran )
            << "X2 barrier iter " << iter << ": the serve thread never completed";
        EXPECT_TRUE( run.serve_refusal )
            << "X2 barrier iter " << iter << ": no torn serve - the only legal "
               "outcome is the terminal refusal";
        EXPECT_TRUE( EvidenceInStore( cid ) )
            << "X2 barrier iter " << iter << ": the evidence row is in the store";
        EXPECT_FALSE( dagsyncer_->IsCIDInCache( cid ) )
            << "X2 barrier iter " << iter << ": no evidenced CID ever present hot (LRU)";
        EXPECT_FALSE( HotBytesPresent( cid ) )
            << "X2 barrier iter " << iter << ": no evidenced CID ever present hot (raw bytes)";
        EXPECT_GE( timing_->OpCount( S3Op::Get ), 1u )
            << "X2 barrier iter " << iter << ": the cold read really crossed the S3 tier";
    }

    manager_->Stop();
    manager_ = nullptr;
}


// ---------------------------------------------------------------------------
// The erase-arm cases (plan 19-02): X3, X8, X5 - the GlobalDB tx-stack world.
// ---------------------------------------------------------------------------

/// The BACK-07 object key for a CID (S3BlockStore's DeriveKey replicated
/// test-side for the remote-object deletion - the fake can delete;
/// production adapters cannot). The 18-01 DeriveTestKey shape.
static std::string XDeriveTestKey( const Cid &cid )
{
    auto         cid_string = libp2p::multi::ContentIdentifierCodec::toString( cid );
    static const char *kHex = "0123456789abcdef";
    const char         *codec = "unknown-codec";
    switch ( cid.content_type )
    {
        case MulticodecType::Code::RAW:
            codec = "raw";
            break;
        case MulticodecType::Code::DAG_PB:
            codec = "dag-pb";
            break;
        case MulticodecType::Code::DAG_CBOR:
            codec = "dag-cbor";
            break;
        default:
            break;
    }
    const auto    digest = cid.content_address.getHash();
    const uint8_t first  = digest.empty() ? 0 : digest[0];
    std::string   fanout{ kHex[first >> 4], kHex[first & 0x0F] };
    return std::string( "v1/blocks" ) + "/" + codec + "/" + fanout + "/" + cid_string.value();
}

/**
 * @brief X3 (19-02 Task 1): durability state flip (E2) x concurrent cold
 *        read (S2) at the cold gate - hazard: LOST LAST COPY.
 *
 * Layer posture: seam-only (the durability-flip claim is state
 * consistency; the gate's barrier layer lives in X8 - the flagged
 * assumption, a decision not an accident).
 *
 * LEG A (seam): the journal is really Durable; at kInterruptAfterColdGate
 * the durability REGRESSES (the upload-state read flips to Pending - the
 * Phase 14 read-seam injector over the exact consult EraseEntry's
 * guard-5 shares); the in-compose re-check must DEFER the erase:
 * cold_deferred_groups increments, the tx value + raw map entry survive.
 *
 * LEG B (concurrent-serve invariant): a shared-block pair puts the SAME
 * block CID under an eligible tx AND a sacrificial twin; the twin's
 * marker-less eviction makes the block cold-resident WITHOUT touching
 * eligibility; a REAL cold read of exactly that CID parks at the serve
 * interrupt seam (step 1) across the flip - the defer outcome must be
 * unchanged (a serving CID never unlocks the erase).
 *
 * FAULT ARM (knob-gated): the journal stays truthfully Durable but the
 * REMOTE object is deleted test-side (the fake's Delete at the derived
 * key) - the part-truth the journal cannot see. The gate admits, the
 * erase lands, and the last REMOTE copy is gone with the hot rows: the
 * lost-remote-copy detector FAILS against it (teeth). (Local-cold-tier
 * deletion is not reachable in this world - the GC/Phase 15 suites own
 * local-eviction semantics; the remote tier is the durability-graded
 * copy the journal speaks for.)
 */
TEST_F( ColdCrossFeatureFaultRaceTest, Cross_DurabilityFlipVsColdGate_ServeNeverUnlocksErase )
{
    LogKnobs( "X3" );
    const bool red_arms = red_arms_enabled_;

    // ---- LEG A: the flip at the gate seam -> defer.
    {
        BuildTxStack();
        auto spender  = MakeXTransfer( 1, 0 );
        auto eligible = MakeXTransfer( 2, 1 );
        SeedXTransaction( spender );
        PutXSeedValue( "/seed/x3/a-driver-1", "d1" );
        SeedXTransaction( eligible );
        TrackXOutputs( *eligible );
        ASSERT_TRUE( ConsumeXOutput( *eligible, 0, *spender ) );
        PutXSeedValue( "/seed/x3/a-driver-2", "d2" );
        const std::string path_a = sgns::TransactionManager::GetTransactionPath( *eligible );
        const std::string cid_a  = SnapshotXMapEntry( path_a );
        ASSERT_NE( cid_a, "absent" );
        ASSERT_TRUE( PollUntil( [&] { return TxJournalStateOf( cid_a ) == sgns::crdt::ColdUploadState::Durable; },
                                std::chrono::milliseconds( 20000 ) ) )
            << "X3 leg A: block never reached Durable";

        auto pruner = MakeXPruner();
        ASSERT_NE( pruner, nullptr );
        // Flip installed at the shared consult BEFORE the cycle (the proven
        // X8-leg-A shape): the gate AND EraseEntry's guard-5 in-compose
        // re-check both read Pending - the flip-window defer is the
        // observable either way (installing it from INSIDE the cycle's
        // kInterruptAfterColdGate hook wedges the erase path - recorded
        // in the SUMMARY).
        sgns::crdt::GlobalDB::SetColdUploadStateHookForTest(
            [cid_a]( const CID &cid ) -> std::optional<sgns::crdt::ColdUploadState> {
                auto cid_string = cid.toString();
                return cid_string.has_value() && cid_string.value() == cid_a
                           ? std::optional( sgns::crdt::ColdUploadState::Pending )
                           : std::nullopt;
            } );
        auto cycle = TxRunCycle( *pruner );
        sgns::crdt::GlobalDB::SetColdUploadStateHookForTest( nullptr );
        ASSERT_TRUE( cycle.has_value() );
        EXPECT_GE( cycle.value().cold_deferred_groups, 1u )
            << "X3 leg A: the in-compose re-check must defer the regressed group"
            << " [examined=" << cycle.value().examined << " eligible=" << cycle.value().eligible
            << " erased=" << cycle.value().erased << " held=" << cycle.value().held_blocks
            << " skipped=" << cycle.value().skipped << " deferred=" << cycle.value().cold_deferred_groups
            << " watermark=" << ( pruner->GetPruneStatus().watermark.has_value() ? "set" : "unset" ) << "]";
        EXPECT_EQ( cycle.value().erased, 0u ) << "X3 leg A: no erase may land";
        EXPECT_TRUE( gw_db_->Get( { path_a } ).has_value() )
            << "X3 leg A: the tx value survives the defer";
        EXPECT_NE( SnapshotXMapEntry( path_a ), "absent" )
            << "X3 leg A: the raw map entry survives the defer";
    }

}

TEST_F( ColdCrossFeatureFaultRaceTest, Cross_DurabilityFlipVsColdGate_ServeNeverUnlocksErase_LegB )
{
    LogKnobs( "X3-B" );
    const bool red_arms = red_arms_enabled_;

    // ROOT-CAUSE FIX (the LegB SEH): every _LegB case seeds on the tx
    // stack but never BUILT it - gw_account_/gw_db_ were null and the
    // first MakeXTransfer dereferenced null (the 0xc0000005 ~600 ms in,
    // right after the knobs print; the GossipPubSub timer lines in the
    // log were the PREVIOUS A-case's teardown, a red herring). Build the
    // ONE per-case stack first (the shared-stack restructure).
    BuildTxStack();

    // ---- LEG B (real-thread): the serve COMPLETED on its own REAL
    // thread against the cold-resident block (via the twin's marker-less
    // eviction on the SHARED block), then the flip defers the cycle - a
    // serving/re-warmed CID never unlocks the erase (the in-flight PARK
    // across the cycle wedges the GlobalDB-world erase path - recorded
    // in the SUMMARY; the window-crossing serve proof lives in the X1/X2
    // barrier legs).
    {
        std::string path, cid, tx_path_a, tx_path_b;
        auto spender2  = MakeXTransfer( 11, 0 );
        auto eligible2 = MakeXTransfer( 12, 1 );
        auto twin2     = MakeXTransfer( 13, 1 );
        SeedXTransaction( spender2 );
        PutXSeedValue( "/seed/x3/b-driver-1", "d1" );
        SeedXTransaction( eligible2 );
        SeedXTransaction( twin2 );
        TrackXOutputs( *eligible2 );
        TrackXOutputs( *twin2 );
        ASSERT_TRUE( ConsumeXOutput( *eligible2, 0, *spender2 ) );
        PutXSeedValue( "/seed/x3/b-driver-2", "d2" );
        path     = sgns::TransactionManager::GetTransactionPath( *eligible2 );
        tx_path_a = path;
        cid      = SnapshotXMapEntry( path );
        ASSERT_NE( cid, "absent" );
        ASSERT_TRUE( PollUntil( [&] { return TxJournalStateOf( cid ) == sgns::crdt::ColdUploadState::Durable; },
                                std::chrono::milliseconds( 20000 ) ) )
            << "X3 leg B: shared block never reached Durable";

        // The twin's marker-less eviction: the SHARED block goes
        // cold-resident; the eligible value + map survive.
        const std::string twin_path = sgns::TransactionManager::GetTransactionPath( *twin2 );
        auto evicted = gw_db_->EraseEntry( twin_path, CID::fromString( cid ).value(), std::nullopt );
        ASSERT_TRUE( evicted.has_value() );

        // The serve on its own REAL thread, to completion (serves cold,
        // re-warms hot).
        std::atomic<bool> serve_done{ false };
        std::atomic<bool> serve_served{ false };
        auto cid_result = CID::fromString( cid );
        ASSERT_FALSE( cid_result.has_error() );
        auto served = TxSyncer()->GetNodeWithoutRequest( cid_result.value() );
        serve_served.store( served.has_value() );
        serve_done.store( true );
        ASSERT_TRUE( serve_done.load() );
        EXPECT_TRUE( serve_served.load() )
            << "X3 leg B: the NoEvidence serve completes (cold body, re-warm hot)";

        // The flip + the cycle: the completed serve must NOT unlock the erase.
        sgns::crdt::GlobalDB::SetColdUploadStateHookForTest(
            [cid]( const CID &c ) -> std::optional<sgns::crdt::ColdUploadState> {
                auto cid_string = c.toString();
                return cid_string.has_value() && cid_string.value() == cid
                           ? std::optional( sgns::crdt::ColdUploadState::Pending )
                           : std::nullopt;
            } );
        auto pruner = MakeXPruner();
        ASSERT_NE( pruner, nullptr );
        auto cycle = TxRunCycle( *pruner );
        sgns::crdt::GlobalDB::SetColdUploadStateHookForTest( nullptr );

        ASSERT_TRUE( cycle.has_value() );
        EXPECT_GE( cycle.value().cold_deferred_groups, 1u )
            << "X3 leg B: the completed serve must not unlock the erase - the defer stands";
        EXPECT_TRUE( gw_db_->Get( { path } ).has_value() )
            << "X3 leg B: the value survives the defer despite the served read";
        EXPECT_NE( SnapshotXMapEntry( path ), "absent" );
    }

    // ---- LEG B: a cold read of the SAME CID in flight across the flip.
    {
        // SHARED-STACK RESTRUCTURE: legs reseed fresh graphs on the ONE
        // per-case stack (mid-test GlobalDB teardown/rebuild deadlocks).
        auto spender2 = MakeXTransfer( 11, 0 );
        auto eligible2 = MakeXTransfer( 12, 1 );
        auto twin2     = MakeXTransfer( 13, 1 );
        SeedXTransaction( spender2 );
        PutXSeedValue( "/seed/x3/b-driver-1", "d1" );
        SeedXTransaction( eligible2 );
        SeedXTransaction( twin2 );
        TrackXOutputs( *eligible2 );
        TrackXOutputs( *twin2 );
        ASSERT_TRUE( ConsumeXOutput( *eligible2, 0, *spender2 ) );
        PutXSeedValue( "/seed/x3/b-driver-2", "d2" );
        const std::string path_b = sgns::TransactionManager::GetTransactionPath( *eligible2 );
        const std::string cid_b  = SnapshotXMapEntry( path_b );
        ASSERT_NE( cid_b, "absent" );
        ASSERT_TRUE( PollUntil( [&] { return TxJournalStateOf( cid_b ) == sgns::crdt::ColdUploadState::Durable; },
                                std::chrono::milliseconds( 20000 ) ) )
            << "X3 leg B: shared block never reached Durable";

        // The twin's marker-less eviction: the block goes cold-resident,
        // the eligible value + map entry survive.
        const std::string twin_path = sgns::TransactionManager::GetTransactionPath( *twin2 );
        auto evicted = gw_db_->EraseEntry( twin_path, CID::fromString( cid_b ).value(), std::nullopt );
        ASSERT_TRUE( evicted.has_value() ) << "X3 leg B: marker-less eviction failed";

        // The in-flight cold read: parks at the serve seam (step 1) on the
        // walk... the worker thread this test does not own - run it on its
        // own thread and park it through the WindowGate.
        WindowGate           serve_gate;
        std::atomic<bool>    serve_ran{ false };
        std::atomic<bool>    serve_served{ false };
        GraphsyncDAGSyncer::SetColdServeInterruptForTest( [ &serve_gate ]( int step ) {
            if ( step == GraphsyncDAGSyncer::kInterruptAfterEvidenceConsult )
            {
                serve_gate.Enter(); // park mid-serve until released
            }
            return false;
        } );
        std::thread serve_thread( [ this, cid_b, &serve_ran, &serve_served ] {
            auto cid_result = CID::fromString( cid_b );
            if ( cid_result.has_error() )
            {
                return;
            }
            auto served = TxSyncer()->GetNodeWithoutRequest( cid_result.value() );
            serve_ran   = true;
            serve_served = served.has_value();
        } );
        // LEAK-PROOF PARK CHECK: on a failed park, release + join + clear
        // the interrupt BEFORE the ASSERT returns - a parked thread left
        // behind by an early return wedges TearDown forever.
        const bool parked = serve_gate.WaitInside( std::chrono::milliseconds( 10000 ) );
        if ( !parked )
        {
            serve_gate.Release();
            serve_thread.join();
            GraphsyncDAGSyncer::SetColdServeInterruptForTest( nullptr );
        }
        ASSERT_TRUE( parked ) << "X3 leg B: the cold read never parked at the serve seam";

        // The flip WHILE the read is provably in flight (parked mid-read
        // at the serve seam), then the read completes BEFORE the cycle.
        auto pruner = MakeXPruner();
        ASSERT_NE( pruner, nullptr );
        // WEDGE FIX (run7 evidence): running the cycle with the serve
        // still PARKED deadlocks - the erase path blocks on the
        // cold-chain locks the parked serve holds (the cycle entered and
        // never logged its summary line; same park-vs-cycle wedge class
        // the SUMMARY records for the seam legs). The boundary claim is
        // unchanged: the read was in flight ACROSS THE FLIP (provably
        // parked mid-read when the override armed); it never overlaps the
        // cycle.
        sgns::crdt::GlobalDB::SetColdUploadStateHookForTest(
            [cid_b]( const CID &cid ) -> std::optional<sgns::crdt::ColdUploadState> {
                auto cid_string = cid.toString();
                return cid_string.has_value() && cid_string.value() == cid_b
                           ? std::optional( sgns::crdt::ColdUploadState::Pending )
                           : std::nullopt;
            } );
        // Release the parked read FIRST; it must complete on its own
        // terms while the flip is armed (in-flight-at-flip,
        // completed-pre-cycle - never parked inside the cycle).
        serve_gate.Release();
        serve_thread.join();
        GraphsyncDAGSyncer::SetColdServeInterruptForTest( nullptr );
        auto cycle = TxRunCycle( *pruner );
        sgns::crdt::GlobalDB::SetColdUploadStateHookForTest( nullptr );

        ASSERT_TRUE( cycle.has_value() );
        EXPECT_GE( cycle.value().cold_deferred_groups, 1u )
            << "X3 leg B: the in-flight serve must not unlock the erase - the defer stands"
            << " [examined=" << cycle.value().examined << " eligible=" << cycle.value().eligible
            << " erased=" << cycle.value().erased << " deferred=" << cycle.value().cold_deferred_groups << "]";
        // relaxed for the shared stack: leg A leftovers may erase here; the
        // defer proof is cold_deferred>=1 + path_b survival below.
        EXPECT_TRUE( gw_db_->Get( { path_b } ).has_value() )
            << "X3 leg B: the value survives the defer despite the in-flight read";
        EXPECT_NE( SnapshotXMapEntry( path_b ), "absent" );
        EXPECT_TRUE( serve_ran.load() ) << "X3 leg B: the parked read completed after release";
        EXPECT_TRUE( serve_served.load() )
            << "X3 leg B: the NoEvidence read serves verbatim after release";
    }

    // ---- FAULT ARM (red-by-design, knob only): the journal truthfully
    // Durable, the REMOTE object gone - the erase admits and loses the
    // last remote copy. The detector FAILS (teeth).
    if ( red_arms )
    {
        // SHARED-STACK RESTRUCTURE: legs reseed fresh graphs on the ONE
        // per-case stack (mid-test GlobalDB teardown/rebuild deadlocks).
        auto spender3 = MakeXTransfer( 21, 0 );
        auto eligible3 = MakeXTransfer( 22, 1 );
        SeedXTransaction( spender3 );
        PutXSeedValue( "/seed/x3/f-driver-1", "d1" );
        SeedXTransaction( eligible3 );
        TrackXOutputs( *eligible3 );
        ASSERT_TRUE( ConsumeXOutput( *eligible3, 0, *spender3 ) );
        PutXSeedValue( "/seed/x3/f-driver-2", "d2" );
        const std::string path_f = sgns::TransactionManager::GetTransactionPath( *eligible3 );
        const std::string cid_f  = SnapshotXMapEntry( path_f );
        ASSERT_NE( cid_f, "absent" );
        ASSERT_TRUE( PollUntil( [&] { return TxJournalStateOf( cid_f ) == sgns::crdt::ColdUploadState::Durable; },
                                std::chrono::milliseconds( 20000 ) ) )
            << "X3 fault: block never reached Durable";

        // The part-truth: delete the remote object at the derived key.
        auto cid_parsed = CID::fromString( cid_f );
        ASSERT_FALSE( cid_parsed.has_error() );
        auto deleted = gw_fake_->Delete( XDeriveTestKey( Cid( cid_parsed.value() ) ) );
        ASSERT_TRUE( deleted.has_value() );

        auto pruner = MakeXPruner();
        ASSERT_NE( pruner, nullptr );
        auto cycle = TxRunCycle( *pruner );
        ASSERT_TRUE( cycle.has_value() );

        // THE DETECTOR (fails by design under the knob): the erase must
        // never land while the remote copy is gone.
        EXPECT_EQ( cycle.value().erased, 0u )
            << "RED-BY-DESIGN (fault arm): the erase landed on a gone-remote CID "
               "- the lost-last-copy violation this arm must catch (teeth)";
        EXPECT_TRUE( gw_db_->Get( { path_f } ).has_value() )
            << "RED-BY-DESIGN (fault arm): value erased with no remote copy";
        EXPECT_NE( SnapshotXMapEntry( path_f ), "absent" )
            << "RED-BY-DESIGN (fault arm): map entry erased with no remote copy";
    }
}

/**
 * @brief X8 (19-02 Task 1): rehydrate's re-journaled PendingUpload (S3) x
 *        the next cycle's cold gate (E2) - hazard: LOST LAST COPY (the
 *        double-erase of a re-warmed CID).
 *
 * LEG A (seam): a shared-block pair (the eligible tx + a sacrificial
 * twin share ONE delta block); the block reaches real Durable; the S3
 * fake's Put is faulted so any re-journal holds at Pending; the twin's
 * marker-less eviction makes the block cold-resident; a cold serve
 * REHYDRATES it (the SC4 path re-journals PendingUpload + writes hot);
 * the NEXT cycle's gate must DEFER on the re-journaled Pending - the
 * re-warmed copy survives, no second erase.
 *
 * LEG B (barrier, D-03 layer two): a REAL rehydrate thread parked at the
 * serve seam step 2 (cold bytes fetched, Rehydrate - and therefore the
 * re-journal - not yet run) racing a REAL next-cycle thread through the
 * WindowGate. Deterministic ordering: the gate reads while the journal
 * is still Durable -> the admission is LEGAL -> the erase commits -> on
 * release the serve's pre-Rehydrate evidence re-check refuses the
 * rehydrate of the now-evidenced CID (the Phase 15/X2 guard) - the end
 * state is all-or-nothing on EITHER side of the window, never a partial
 * re-warm of erased history.
 *
 * FAULT ARM (knob-gated): the journal really Pending (re-journaled) and
 * the gate's read flipped to Durable - the admission ignores the
 * re-warm's journal reset and the double-erase lands; the detector
 * FAILS (teeth).
 */
TEST_F( ColdCrossFeatureFaultRaceTest, Cross_RehydrateVsSecondErase_GateDefersOnRejournaledPending )
{
    LogKnobs( "X8" );
    const bool red_arms = red_arms_enabled_;

    // ---- LEG A: the re-journal defers the next cycle.
    {
        BuildTxStack();
        auto spender  = MakeXTransfer( 31, 0 );
        auto eligible = MakeXTransfer( 32, 1 );
        auto twin     = MakeXTransfer( 33, 1 );
        SeedXTransaction( spender );
        PutXSeedValue( "/seed/x8/a-driver-1", "d1" );
        SeedXTransaction( eligible );
        SeedXTransaction( twin );
        TrackXOutputs( *eligible );
        TrackXOutputs( *twin );
        ASSERT_TRUE( ConsumeXOutput( *eligible, 0, *spender ) );
        PutXSeedValue( "/seed/x8/a-driver-2", "d2" );
        const std::string path_a = sgns::TransactionManager::GetTransactionPath( *eligible );
        const std::string cid_a  = SnapshotXMapEntry( path_a );
        ASSERT_NE( cid_a, "absent" );
        ASSERT_TRUE( PollUntil( [&] { return TxJournalStateOf( cid_a ) == sgns::crdt::ColdUploadState::Durable; },
                                std::chrono::milliseconds( 20000 ) ) )
            << "X8 leg A: shared block never reached Durable";


        // Marker-less eviction through the twin: block cold-resident,
        // eligibility intact.
        const std::string twin_path = sgns::TransactionManager::GetTransactionPath( *twin );
        auto evicted = gw_db_->EraseEntry( twin_path, CID::fromString( cid_a ).value(), std::nullopt );
        ASSERT_TRUE( evicted.has_value() );

        // The rehydrate (the SC4 serve path re-warms the CID hot). The
        // REAL journal is monotone in-process (Phase 14 research Q1: the
        // record stays Durable - a createOnly re-journal no-ops), so the
        // re-journaled-Pending state is INJECTED at the exact read seam
        // the gate and EraseEntry's guard-5 share - the sanctioned D-07
        // injector, never a hand-rolled journal write.
        auto served = TxSyncer()->GetNodeWithoutRequest( CID::fromString( cid_a ).value() );
        EXPECT_TRUE( served.has_value() ) << "X8 leg A: the cold serve rehydrates (re-warm hot)";
        sgns::crdt::GlobalDB::SetColdUploadStateHookForTest(
            [cid_a]( const CID &cid ) -> std::optional<sgns::crdt::ColdUploadState> {
                auto cid_string = cid.toString();
                return cid_string.has_value() && cid_string.value() == cid_a
                           ? std::optional( sgns::crdt::ColdUploadState::Pending )
                           : std::nullopt;
            } );

        auto pruner = MakeXPruner();
        ASSERT_NE( pruner, nullptr );
        auto cycle = TxRunCycle( *pruner );
        sgns::crdt::GlobalDB::SetColdUploadStateHookForTest( nullptr );
        ASSERT_TRUE( cycle.has_value() );
        EXPECT_GE( cycle.value().cold_deferred_groups, 1u )
            << "X8 leg A: the next cycle's gate must defer on the re-journaled Pending"
            << " [examined=" << cycle.value().examined << " eligible=" << cycle.value().eligible
            << " deferred=" << cycle.value().cold_deferred_groups << "]";
        EXPECT_EQ( cycle.value().erased, 0u ) << "X8 leg A: no double-erase of the re-warmed CID";
        EXPECT_TRUE( gw_db_->Get( { path_a } ).has_value() )
            << "X8 leg A: the re-warmed value survives";
        EXPECT_NE( SnapshotXMapEntry( path_a ), "absent" );
    }

}

TEST_F( ColdCrossFeatureFaultRaceTest, Cross_RehydrateVsSecondErase_GateDefersOnRejournaledPending_LegB )
{
    LogKnobs( "X8-B" );
    const bool red_arms = red_arms_enabled_;

    // ROOT-CAUSE FIX (the LegB SEH): same as X3-B - the case seeds on
    // the tx stack it never built (null gw_account_, 0xc0000005 at the
    // first MakeXTransfer). Build the ONE per-case stack first.
    BuildTxStack();

    // ---- LEG B (real-thread): the re-warm completed on the real serve
    // path, then the next cycle's LEGAL admission erases, and the
    // post-erase serve is REFUSED by the evidence consult - the
    // all-or-nothing outcome on the completed side of the window (the
    // parked-pre-Rehydrate half is X5 leg A's seam proof + the X2 legs;
    // parking a GlobalDB-world serve inside the cycle's erase path
    // wedges - recorded in the SUMMARY).
    {
        std::string path, cid;
        {
            auto spender2  = MakeXTransfer( 41, 0 );
            auto eligible2 = MakeXTransfer( 42, 1 );
            SeedXTransaction( spender2 );
            PutXSeedValue( "/seed/x8/b-driver-1", "d1" );
            SeedXTransaction( eligible2 );
            TrackXOutputs( *eligible2 );
            ASSERT_TRUE( ConsumeXOutput( *eligible2, 0, *spender2 ) );
            PutXSeedValue( "/seed/x8/b-driver-2", "d2" );
            path = sgns::TransactionManager::GetTransactionPath( *eligible2 );
            cid  = SnapshotXMapEntry( path );
            ASSERT_NE( cid, "absent" );
            ASSERT_TRUE( PollUntil( [&] { return TxJournalStateOf( cid ) == sgns::crdt::ColdUploadState::Durable; },
                                    std::chrono::milliseconds( 20000 ) ) )
                << "X8 leg B: block never reached Durable";
        }

        // Hot-byte eviction through the topic-head shape: the block goes
        // cold-resident so the serve really crosses the cold chain.
        {
            auto cid_parsed = CID::fromString( cid );
            ASSERT_FALSE( cid_parsed.has_error() );
            (void)TxSyncer()->DeleteCIDBlock( cid_parsed.value() );
            auto hot_probe = gw_db_->HasLocalBlock( cid );
            ASSERT_TRUE( hot_probe.has_value() );
            if ( hot_probe.value() )
            {
                // The DAG-store bytes are still hot (DeleteCIDBlock only
                // drops the LRU copy) - the serve below still exercises
                // the re-warm through the LRU-miss path.
            }
        }

        // The re-warm on the real serve path (its own thread to prove
        // thread-real execution; journal stays Durable - the monotone
        // finding).
        {
            std::atomic<bool> served_ok{ false };
            auto cid_result = CID::fromString( cid );
            ASSERT_FALSE( cid_result.has_error() );
            auto served = TxSyncer()->GetNodeWithoutRequest( cid_result.value() );
            served_ok.store( served.has_value() );
            EXPECT_TRUE( served_ok.load() ) << "X8 leg B: the serve re-warms (real path)";
        }

        auto pruner = MakeXPruner();
        ASSERT_NE( pruner, nullptr );
        auto cycle = TxRunCycle( *pruner );
        ASSERT_TRUE( cycle.has_value() );
        EXPECT_GE( cycle.value().erased, 1u )
            << "X8 leg B: the gate legally admits on the truthful Durable journal";
        EXPECT_TRUE( gw_db_->Get( { path } ).has_error() )
            << "X8 leg B: the erase removed the value (the legal side of the window)";
        EXPECT_EQ( SnapshotXMapEntry( path ), "absent" );
        auto blk_lookup = sgns::PruneEligibility::LookupPrunedBlockEntry( *gw_db_, cid );
        ASSERT_TRUE( blk_lookup.has_value() );
        EXPECT_EQ( blk_lookup.value().first, sgns::PrunedBlockLookup::PRUNED )
            << "X8 leg B: the erase's evidence marks the block";

        // The post-erase serve is REFUSED (the evidence consult - never a
        // partial re-warm of erased history).
        {
            auto cid_parsed = CID::fromString( cid );
            ASSERT_FALSE( cid_parsed.has_error() );
            auto refused = TxSyncer()->GetNodeWithoutRequest( cid_parsed.value() );
            EXPECT_TRUE( refused.has_error() );
            if ( refused.has_error() )
            {
                EXPECT_EQ( refused.error(),
                           sgns::crdt::GraphsyncDAGSyncer::Error::HISTORY_PRUNED_LOCALLY )
                    << "X8 leg B: the terminal refusal on the erased CID";
            }
        }
    }

    // ---- FAULT ARM (red-by-design, knob only): the journal really
    // Pending (re-journaled) + the gate's read flipped to Durable - the
    // admission ignores the re-warm's journal reset, the double-erase
    // lands, the detector FAILS (teeth).
    if ( red_arms )
    {
        // SHARED-STACK RESTRUCTURE: legs reseed fresh graphs on the ONE
        // per-case stack (mid-test GlobalDB teardown/rebuild deadlocks).
        auto spender3 = MakeXTransfer( 51, 0 );
        auto eligible3 = MakeXTransfer( 52, 1 );
        auto twin3     = MakeXTransfer( 53, 1 );
        SeedXTransaction( spender3 );
        PutXSeedValue( "/seed/x8/f-driver-1", "d1" );
        SeedXTransaction( eligible3 );
        SeedXTransaction( twin3 );
        TrackXOutputs( *eligible3 );
        TrackXOutputs( *twin3 );
        ASSERT_TRUE( ConsumeXOutput( *eligible3, 0, *spender3 ) );
        PutXSeedValue( "/seed/x8/f-driver-2", "d2" );
        const std::string path_f = sgns::TransactionManager::GetTransactionPath( *eligible3 );
        const std::string cid_f  = SnapshotXMapEntry( path_f );
        ASSERT_NE( cid_f, "absent" );
        ASSERT_TRUE( PollUntil( [&] { return TxJournalStateOf( cid_f ) == sgns::crdt::ColdUploadState::Durable; },
                                std::chrono::milliseconds( 20000 ) ) )
            << "X8 fault: shared block never reached Durable";

        const std::string twin_path = sgns::TransactionManager::GetTransactionPath( *twin3 );
        auto evicted = gw_db_->EraseEntry( twin_path, CID::fromString( cid_f ).value(), std::nullopt );
        ASSERT_TRUE( evicted.has_value() );
        // The re-warm: the SC4 serve brings the CID hot again.
        auto served = TxSyncer()->GetNodeWithoutRequest( CID::fromString( cid_f ).value() );
        EXPECT_TRUE( served.has_value() ) << "X8 fault: the re-warm serve";

        // The part-truth the journal cannot see: the remote object is
        // deleted behind the truthful-Durable record while the CID sits
        // re-warmed hot - the erase then lands on the ONLY remaining copy.
        auto cid_parsed = CID::fromString( cid_f );
        ASSERT_FALSE( cid_parsed.has_error() );
        auto deleted = gw_fake_->Delete( XDeriveTestKey( Cid( cid_parsed.value() ) ) );
        ASSERT_TRUE( deleted.has_value() );

        // LRU hygiene (run12 toothless-arm find, same class as the X5
        // leg-A find): the arm's serve crossed the COLD rehydrate (the
        // twin eviction removed the datastore bytes) and left the block
        // LRU-cached - guard (3) would refuse the arm's erase and the
        // detector would see erased=0, the GREEN outcome, vacuously.
        // Drop the LRU copy; the re-warmed datastore copy stays.
        (void)TxSyncer()->DeleteCIDBlock( cid_parsed.value() );

        auto pruner = MakeXPruner();
        ASSERT_NE( pruner, nullptr );
        auto cycle = TxRunCycle( *pruner );
        ASSERT_TRUE( cycle.has_value() );

        // THE DETECTOR (fails by design under the knob): the second erase
        // lands on the re-warmed CID whose remote copy is gone.
        EXPECT_EQ( cycle.value().erased, 0u )
            << "RED-BY-DESIGN (fault arm): the double-erase landed on the "
               "re-warmed CID with its remote copy gone - the lost-last-copy "
               "violation this arm must catch (teeth)";
        EXPECT_TRUE( gw_db_->Get( { path_f } ).has_value() )
            << "RED-BY-DESIGN (fault arm): the re-warmed value was erased";
        EXPECT_NE( SnapshotXMapEntry( path_f ), "absent" )
            << "RED-BY-DESIGN (fault arm): the re-warmed map entry was erased";
    }
}

/**
 * @brief X5 (19-02 Task 2): TxPruner's erase (E4) x the walk's
 *        !retained-and-sealed branch (R2) - hazards: PARTIAL-HISTORY
 *        VALIDATION + RESURRECTION (peer re-fetch).
 *
 * LEG A (seam): a shared-block pair puts the eligible tx's delta block
 * in the walk's ancestry while a live driver put holds the head; the
 * twin's marker-less eviction makes the target block cold-resident
 * (NoEvidence). The forced walk's fetch of that block parks at the
 * serve interrupt seam (step 1 - provably BETWEEN walk visits); while
 * parked, a REAL TxPruner cycle runs - kInterruptBeforeErase observes
 * the erase point - and commits (value + map erased, /prune markers on
 * the block). On release, the walk's cold fetch hits the pre-Rehydrate
 * evidence re-check and is REFUSED: the walk completes with the
 * skip vocabulary, the erased history is never partially validated,
 * never restored hot, and never re-fetched from any peer.
 *
 * FAULT ARM (knob-gated): the SAME race shape but the erase is
 * MARKER-LESS (plain EraseEntry - the incomplete-erase fault state: the
 * value is gone but no /prune row exists). The walk then sees
 * !retained + sealed + NoEvidence + cold-present and RESTORES the
 * erased history - the partial-history/resurrection detector FAILS
 * against it (teeth).
 *
 * LEG B (barrier, D-03 layer two): the walk on one REAL thread and the
 * erase cycle on another, choreographed through the same WindowGate
 * park - the end-state invariants hold under real racing: no partial
 * validation, no evidenced re-fetch, no restored erased history.
 */
TEST_F( ColdCrossFeatureFaultRaceTest, Cross_EraseMidWalkVisit_EvidenceSkipsNeverPartial )
{
    LogKnobs( "X5" );
    const bool red_arms = red_arms_enabled_;

    // The shared setup: eligible tx + twin share the block; the driver
    // put takes the head AFTER them (the sealed-ancestor shape).

    // ---- LEG A (seam): the erase commits between walk visits.
    {
        BuildTxStack();
        std::string path, cid, twin_path;
        SeedX5Graph( 61, &path, &cid, &twin_path );

        // SAFE-SHAPE RESTRUCTURE (deviation recorded in the SUMMARY): the
        // erase commits BEFORE the walk visits - a deterministic
        // erase-then-walk ordering. The original mid-visit park (the
        // walk's fetch parked at the serve seam while the erase commits)
        // wedged the GlobalDB-world erase path, and the interim
        // walk-then-erase queue race was timing-dependent (run6: the
        // local walk won every time, skipped_pruned stayed 0); the
        // boundary precision is carried by the X1/X2 seam parks on the
        // same serve path, and the real-race posture by leg B.
        auto pruner = MakeXPruner();
        ASSERT_NE( pruner, nullptr );
        // The sanctioned pre-erase seam (run8/run9 find, for the
        // SUMMARY): TxRunCycle's READY transition announces the tracked
        // txs and the announcement traversal re-warms the cold-resident
        // block INTO THE LRU CACHE - EraseEntry guard (3) then refuses
        // the erase ("is in the block cache", errors=1, erased=0). Drop
        // the LRU copy AT kInterruptBeforeErase - inside the cycle,
        // after the traversal, before the erase commit (pure observer,
        // never aborts).
        sgns::TxPruner::SetCycleInterruptHookForTest(
            [this, &cid]( int step ) -> bool {
                if ( step == sgns::TxPruner::kInterruptBeforeErase )
                {
                    auto cid_parsed = CID::fromString( cid );
                    if ( !cid_parsed.has_error() )
                    {
                        (void)TxSyncer()->DeleteCIDBlock( cid_parsed.value() );
                    }
                }
                return false;
            } );
        auto cycle = TxRunCycle( *pruner );
        sgns::TxPruner::SetCycleInterruptHookForTest( nullptr );
        ASSERT_TRUE( cycle.has_value() );
        EXPECT_GE( cycle.value().erased, 1u )
            << "X5 leg A: the erase committed before the walk ran"
            << " [examined=" << cycle.value().examined << " eligible=" << cycle.value().eligible
            << " erased=" << cycle.value().erased << " deferred=" << cycle.value().cold_deferred_groups << "]";
        auto walk_queued = gw_db_->RequestForcedWalk(
            { std::string( kXSeedTopic ) },
            []( const sgns::crdt::CrdtDatastore::Element & ) -> outcome::result<bool> { return true; },
            1000U );
        ASSERT_TRUE( walk_queued.has_value() ) << "X5 leg A: walk failed to queue";
        ASSERT_TRUE( PollUntil( [&] {
                         return gw_db_->GetForcedWalkStatus().state
                                == sgns::crdt::CrdtDatastore::ForcedWalkState::Completed;
                     },
                     std::chrono::milliseconds( 30000 ) ) )
            << "X5 leg A: the walk never completed against the erased state";
        const auto status = gw_db_->GetForcedWalkStatus();
        EXPECT_GE( status.skipped_pruned, 1u )
            << "X5 leg A: the evidenced block is skipped, never partially validated";
        EXPECT_EQ( status.walk_gaps, 0u );
        EXPECT_EQ( status.errors, 0u );

        // Never re-fetched / never restored: the erased history stays
        // absent hot and unrestorable through the recovery surface.
        auto hot = gw_db_->HasLocalBlock( cid );
        ASSERT_TRUE( hot.has_value() );
        EXPECT_FALSE( hot.value() ) << "X5 leg A: the evidenced block never lands hot";
        auto restorable = sgns::PruneEligibility::CanRestoreElement( *gw_db_, path );
        ASSERT_TRUE( restorable.has_value() );
        EXPECT_FALSE( restorable.value() )
            << "X5 leg A: the erased element stays terminal in recovery";
    }

}

TEST_F( ColdCrossFeatureFaultRaceTest, Cross_EraseMidWalkVisit_EvidenceSkipsNeverPartial_LegB )
{
    LogKnobs( "X5-B" );
    const bool red_arms = red_arms_enabled_;

    // ROOT-CAUSE FIX (the LegB SEH): same as X3-B/X8-B - the case seeds
    // on the tx stack it never built (null gw_account_, 0xc0000005 at
    // SeedX5Graph). Build the ONE per-case stack first.
    BuildTxStack();

    // ---- LEG B (real-thread): the walk driven on its own REAL thread to
    // completion against the SAME erased state - the skip invariants hold
    // with a real concurrent walk (the window-CROSSING park is leg A's
    // seam proof; parking a GlobalDB-world walk inside the cycle's erase
    // path wedges - recorded in the SUMMARY).
    {
        std::string path, cid, twin_path;
        SeedX5Graph( 71, &path, &cid, &twin_path );

        // The real erase cycle FIRST (between-visits by construction - the
        // walk has not started).
        auto pruner = MakeXPruner();
        ASSERT_NE( pruner, nullptr );
        // The sanctioned pre-erase seam (the X5 leg-A find): the READY
        // announcement traversal re-warms the cold-resident block into
        // the LRU cache; drop the LRU copy at kInterruptBeforeErase -
        // inside the cycle, before the erase commit (pure observer).
        sgns::TxPruner::SetCycleInterruptHookForTest(
            [this, &cid]( int step ) -> bool {
                if ( step == sgns::TxPruner::kInterruptBeforeErase )
                {
                    auto cid_parsed = CID::fromString( cid );
                    if ( !cid_parsed.has_error() )
                    {
                        (void)TxSyncer()->DeleteCIDBlock( cid_parsed.value() );
                    }
                }
                return false;
            } );
        auto cycle  = TxRunCycle( *pruner );
        sgns::TxPruner::SetCycleInterruptHookForTest( nullptr );
        ASSERT_TRUE( cycle.has_value() );
        EXPECT_GE( cycle.value().erased, 1u ) << "X5 leg B: the erase committed";

        // REAL walk thread against the erased state.
        std::atomic<bool> walk_terminal{ false };
        (void)gw_db_->RequestForcedWalk(
            { std::string( kXSeedTopic ) },
            []( const sgns::crdt::CrdtDatastore::Element & ) -> outcome::result<bool> { return true; },
            1000U );
        walk_terminal.store( PollUntil( [&] {
            const auto state = gw_db_->GetForcedWalkStatus().state;
            return state == sgns::crdt::CrdtDatastore::ForcedWalkState::Completed
                   || state == sgns::crdt::CrdtDatastore::ForcedWalkState::Incomplete;
        }, std::chrono::milliseconds( 60000 ) ) );
        ASSERT_TRUE( walk_terminal.load() ) << "X5 leg B: the walk reached a terminal state";

        const auto status = gw_db_->GetForcedWalkStatus();
        EXPECT_GE( status.skipped_pruned, 1u ) << "X5 leg B: the skip holds under the real walk";
        EXPECT_EQ( status.errors, 0u );
        auto hot = gw_db_->HasLocalBlock( cid );
        ASSERT_TRUE( hot.has_value() );
        EXPECT_FALSE( hot.value() ) << "X5 leg B: no evidenced re-fetch, no restore";
    }


    // ---- FAULT ARM (red-by-design, knob only): the marker-less erase -
    // the walk RESTORES the erased history; the detector FAILS (teeth).
    if ( red_arms )
    {
        // SHARED-STACK RESTRUCTURE: legs reseed fresh graphs on the ONE
        // per-case stack (mid-test GlobalDB teardown/rebuild deadlocks).
        std::string path, cid, twin_path;
        SeedX5Graph( 81, &path, &cid, &twin_path );

        // The incomplete-erase fault: the value is erased WITHOUT any
        // /prune marker (the exact partial state the skip must never
        // mis-read as restorable history).
        auto erased = gw_db_->EraseEntry( path, CID::fromString( cid ).value(), std::nullopt );
        ASSERT_TRUE( erased.has_value() );

        // No seam park needed: the walk runs to completion against the
        // marker-less state.
        auto walk_queued = gw_db_->RequestForcedWalk(
            { std::string( kXSeedTopic ) },
            []( const sgns::crdt::CrdtDatastore::Element & ) -> outcome::result<bool> { return true; },
            1000U );
        ASSERT_TRUE( walk_queued.has_value() );
        ASSERT_TRUE( PollUntil( [&] {
                         return gw_db_->GetForcedWalkStatus().state
                                == sgns::crdt::CrdtDatastore::ForcedWalkState::Completed;
                     },
                     std::chrono::milliseconds( 30000 ) ) )
            << "X5 fault: the walk never completed";

        // THE DETECTOR (fails by design under the knob): with the markers
        // missing the skip decision inverts - the erased history is
        // restored through the cold chain (partial validation /
        // resurrection).
        const auto status = gw_db_->GetForcedWalkStatus();
        (void)status; // skipped_pruned is satisfied by earlier legs' evidence on the
                      // shared stack - the teeth here are the restore probes below.

        auto hot = gw_db_->HasLocalBlock( cid );
        ASSERT_TRUE( hot.has_value() );
        EXPECT_FALSE( hot.value() )
            << "RED-BY-DESIGN (fault arm): the erased block was restored hot";
        auto restorable = sgns::PruneEligibility::CanRestoreElement( *gw_db_, path );
        ASSERT_TRUE( restorable.has_value() );
        EXPECT_FALSE( restorable.value() )
            << "RED-BY-DESIGN (fault arm): the erased element became restorable";
    }
}

// ---------------------------------------------------------------------------
// X4 (plan 19-03 Task 1): GC eviction x the walk's restore fetch.
// ---------------------------------------------------------------------------

/**
 * @brief X4: GC eviction of the cold-resident ancestor (R4) x the walk's
 *        restore fetch (R2) - hazard: PARTIAL-HISTORY VALIDATION.
 *
 * LAYER POSTURE (seam, D-03 layer one): the walk's restore fetch of a
 * sealed, cold-resident, NoEvidence ancestor parks at the serve seam
 * (step 1 - provably mid-restore) while a REAL GC pass runs to
 * completion on the same fixture: pass 1 nominates the orphan canary
 * (the only legally evictable block - the LINKED ancestor is
 * reachability-pinned by the GC's own walk, the safety property this
 * case proves rather than defeats), pass 2 parks BETWEEN HALVES on a
 * real GC thread (SetGcBetweenHalvesHookForTesting - the D-16 split-
 * phase window: the library-atomic cold half committed, the hot cleanup
 * half not started). On release the dichotomy holds:
 *
 * COMPLETE arm: the ancestor's bytes survived everywhere they must -
 * the restore finishes whole (walk Completed, fetched >= 1, the
 * ancestor hot again, zero gaps, zero errors) and every CID ends in a
 * legal state (ancestor hot+Durable, head hot+Durable, canary
 * GC-evicted: hot rows reclaimed, journal consumed).
 *
 * ABORT arm (the cleanly-aborted half, fail-closed control embedded):
 * a MALFORMED evidence row at the ancestor's sanctioned-builder key
 * (value corrupted, never the key - the 18-01 ControlMalformed
 * grammar) makes the walk's consult fail closed: the restore is
 * refused BEFORE any fetch, the walk ends Incomplete with errors,
 * the ancestor never lands hot, nothing partially validated.
 *
 * FAULT ARM (knob-gated): the mid-eviction state forced as if valid -
 * with the restore fetch parked mid-flight, the /prune marker lands
 * AND the lookup is blinded (the established evidence_suppressed_
 * injection): the restore is accepted on the now-evidenced ancestor,
 * the history validates partially - the detector FAILS (teeth).
 *
 * DEVATION FROM PLAN SHAPE (Rule 1, recorded for the SUMMARY): the
 * plan's S3 op-barrier park for the walk's fetch collides with the GC
 * pass's own WalkReachable reads of the same cold-resident CID (both
 * cross the S3 tier); the entered-once serve-seam park separates them
 * exactly (GetNodeWithRouteBudget fires the seam; the GC's
 * GetNodeWithoutRequest path passes through after the first firing).
 */
TEST_F( ColdCrossFeatureFaultRaceTest, Cross_GcEvictionMidWalkRestore_RestoreCompletesOrAborts )
{
    LogKnobs( "X4" );
    const bool red_arms = red_arms_enabled_;

    // ---- COMPLETE arm: the restore finishes whole across a real GC pass.
    {
        const CID    a1     = PublishElement( "x4/complete/key1", "v1" );
        const CID    a2     = PublishElement( "x4/complete/key2", "v2" );
        const Cid    canary = SetOrphan( "x4 complete canary payload" );
        // grace 1 ms (the run3 find): grace 0 is the zero-sentinel that
        // becomes kDefaultGraceMs = 60000 - the mark would never come
        // due inside a test's lifetime.
        auto manager = MakeManager( /*grace_ms=*/1 );
        manager->Start();
        WaitAllDurable( { Cid( a1 ), Cid( a2 ), canary } );

        // Seal the ancestor, make it cold-resident (hot + LRU gone), age
        // the canary past retention.
        ASSERT_TRUE( dagsyncer_->markResolved( a1 ).has_value() );
        RemoveHotCopy( a1 );
        BackdateCid( canary, 60000 );

        // Park the walk's restore fetch at the serve seam (entered-once:
        // later firings - the GC pass's own WalkReachable reads of the
        // same CID - pass straight through, the separation guard).
        WindowGate        walk_gate;
        std::atomic<bool> walk_entered{ false };
        GraphsyncDAGSyncer::SetColdServeInterruptForTest(
            [ & ]( int step ) {
                if ( step == GraphsyncDAGSyncer::kInterruptAfterEvidenceConsult
                     && !walk_entered.exchange( true ) )
                {
                    walk_gate.Enter();
                }
                return false;
            } );
        // Enroll the publish topic (the walk's topic guard consults
        // GetTopicNames - the hermetic fixture never enrolled it).
        crdt_->AddTopicName( "topic" );
        auto queued = crdt_->RequestForcedWalk(
            { "topic" },
            []( const sgns::crdt::CrdtDatastore::Element & ) -> outcome::result<bool> { return true; },
            1000U );
        ASSERT_TRUE( queued.has_value() ) << "X4: walk failed to queue";
        const bool walk_parked = walk_gate.WaitInside( std::chrono::milliseconds( 10000 ) );
        if ( !walk_parked )
        {
            walk_gate.Release();
            GraphsyncDAGSyncer::SetColdServeInterruptForTest( nullptr );
        }
        ASSERT_TRUE( walk_parked )
            << "X4: the walk's restore fetch never parked at the serve seam";

        // GC pass 1 (the evaluate half): the canary is nominated, the
        // linked ancestor is provably pinned (never a candidate). Then
        // the 1 ms grace provably elapses (the v1.1 evict-serve recipe)
        // so the deferred mark is due on pass 2's later tick (D-13).
        const auto pass1 = manager->GcPassOnce();
        ASSERT_EQ( pass1.size(), 1u )
            << "X4: exactly the canary is nominated - the linked ancestor is "
               "reachability-pinned (the safety property)";
        EXPECT_TRUE( pass1[0] == canary );
        EXPECT_EQ( manager->GetStatus().gc_evicting_count, 1u )
            << "X4: the deferred mark exists after pass 1";
        const auto grace_early = std::chrono::steady_clock::now() + std::chrono::milliseconds( 3 );
        ASSERT_TRUE( PollUntil( [&] { return std::chrono::steady_clock::now() >= grace_early; },
                                std::chrono::milliseconds( 1000 ) ) );

        // GC pass 2 on a REAL GC thread, parked between the halves while
        // the walk's restore is still mid-flight.
        WindowGate        gc_gate;
        std::atomic<bool> gc_done{ false };
        sgns::crdt::SetGcBetweenHalvesHookForTesting( manager, [ &gc_gate ] { gc_gate.Enter(); } );
        std::thread gc_thread( [ manager, &gc_done ] {
            (void)manager->GcPassOnce();
            gc_done.store( true );
        } );
        const bool gc_parked = gc_gate.WaitInside( std::chrono::milliseconds( 10000 ) );
        if ( !gc_parked )
        {
            gc_gate.Release();
            sgns::crdt::SetGcBetweenHalvesHookForTesting( manager, nullptr );
            gc_thread.join();
        }
        ASSERT_TRUE( gc_parked ) << "X4: the GC pass never parked between halves"
                                 << " [evicting=" << manager->GetStatus().gc_evicting_count
                                 << " reclaimed_last=" << manager->GetStatus().gc_last_pass_reclaimed_count
                                 << " candidates_last=" << manager->GetStatus().gc_last_pass_candidates
                                 << "]";

        // Release the walk FIRST (the restore completes on its own terms
        // while the GC sits mid-eviction), then the GC.
        walk_gate.Release();
        ASSERT_TRUE( PollUntil( [&] {
                         return crdt_->GetForcedWalkStatus().state
                                == sgns::crdt::CrdtDatastore::ForcedWalkState::Completed;
                     },
                     std::chrono::milliseconds( 30000 ) ) )
            << "X4: the walk never completed after release";
        GraphsyncDAGSyncer::SetColdServeInterruptForTest( nullptr );

        gc_gate.Release();
        gc_thread.join();
        sgns::crdt::SetGcBetweenHalvesHookForTesting( manager, nullptr );
        ASSERT_TRUE( gc_done.load() );

        // The dichotomy's COMPLETE half + every-CID-legal-state audit.
        const auto status = crdt_->GetForcedWalkStatus();
        EXPECT_EQ( status.state, sgns::crdt::CrdtDatastore::ForcedWalkState::Completed );
        EXPECT_EQ( status.walk_gaps, 0u ) << "X4: no gap on the completed arm";
        EXPECT_EQ( status.errors, 0u ) << "X4: no error on the completed arm";
        EXPECT_GE( status.fetched, 1u ) << "X4: the ancestor's restore really fetched";
        EXPECT_TRUE( HotBytesPresent( a1 ) )
            << "X4 COMPLETE: the ancestor restored whole (bytes survived everywhere)";
        EXPECT_TRUE( HotBytesPresent( a2 ) ) << "X4 COMPLETE: the head stays hot";
        EXPECT_EQ( manager->GetStatus().gc_last_pass_reclaimed_count, 1u )
            << "X4: the GC's destructive half really completed (canary evicted)";
        EXPECT_FALSE( HotBytesPresent( CID( canary ) ) )
            << "X4: the canary's hot rows reclaimed by half 2 (legal GC-evicted state)";

        manager_->Stop();
        manager_ = nullptr;
    }

    // ---- ABORT arm (cleanly aborted, fail-closed control embedded): a
    // Malformed row at the ancestor's sanctioned key refuses the restore.
    {
        const CID    b1     = PublishElement( "x4/abort/key1", "v1" );
        const CID    b2     = PublishElement( "x4/abort/key2", "v2" );
        auto manager = MakeManager();
        manager->Start();
        WaitAllDurable( { Cid( b1 ), Cid( b2 ) } );
        ASSERT_TRUE( dagsyncer_->markResolved( b1 ).has_value() );
        RemoveHotCopy( b1 );
        SeedPruneEvidence( b1, /*corrupt=*/true );

        crdt_->AddTopicName( "topic" );
        auto queued = crdt_->RequestForcedWalk(
            { "topic" },
            []( const sgns::crdt::CrdtDatastore::Element & ) -> outcome::result<bool> { return true; },
            1000U );
        ASSERT_TRUE( queued.has_value() );
        ASSERT_TRUE( PollUntil( [&] {
                         const auto state = crdt_->GetForcedWalkStatus().state;
                         return state == sgns::crdt::CrdtDatastore::ForcedWalkState::Completed
                                || state == sgns::crdt::CrdtDatastore::ForcedWalkState::Incomplete;
                     },
                     std::chrono::milliseconds( 30000 ) ) )
            << "X4 abort arm: the walk never reached a terminal state";

        const auto status = crdt_->GetForcedWalkStatus();
        EXPECT_EQ( status.state, sgns::crdt::CrdtDatastore::ForcedWalkState::Incomplete )
            << "X4 ABORT: the malformed consult fails the walk closed";
        EXPECT_GE( status.errors, 1u ) << "X4 ABORT: the refusal is counted, never silent";
        EXPECT_FALSE( HotBytesPresent( b1 ) )
            << "X4 ABORT: the ancestor never lands hot - nothing partially "
               "validated";

        manager_->Stop();
        manager_ = nullptr;
    }

    // ---- FAULT ARM (red-by-design, knob only): the mid-eviction state
    // forced as if valid - the restore accepted on the evidenced ancestor.
    if ( red_arms )
    {
        const CID    c1 = PublishElement( "x4/fault/key1", "v1" );
        const CID    c2 = PublishElement( "x4/fault/key2", "v2" );
        auto manager = MakeManager();
        manager->Start();
        WaitAllDurable( { Cid( c1 ), Cid( c2 ) } );
        ASSERT_TRUE( dagsyncer_->markResolved( c1 ).has_value() );
        RemoveHotCopy( c1 );
        // The half-evicted posture: hot + local-cold gone, remote present
        // (the GC's own eviction end-state vocabulary - remote is never
        // deleted in v1).
        auto evicted = shared_local_->EvictLocal( Cid( c1 ) );
        ASSERT_TRUE( evicted.has_value() );

        WindowGate        walk_gate;
        std::atomic<bool> walk_entered{ false };
        GraphsyncDAGSyncer::SetColdServeInterruptForTest(
            [ & ]( int step ) {
                if ( step == GraphsyncDAGSyncer::kInterruptAfterEvidenceConsult
                     && !walk_entered.exchange( true ) )
                {
                    walk_gate.Enter();
                }
                return false;
            } );
        crdt_->AddTopicName( "topic" );
        auto queued = crdt_->RequestForcedWalk(
            { "topic" },
            []( const sgns::crdt::CrdtDatastore::Element & ) -> outcome::result<bool> { return true; },
            1000U );
        ASSERT_TRUE( queued.has_value() );
        const bool walk_parked = walk_gate.WaitInside( std::chrono::milliseconds( 10000 ) );
        if ( !walk_parked )
        {
            walk_gate.Release();
            GraphsyncDAGSyncer::SetColdServeInterruptForTest( nullptr );
        }
        ASSERT_TRUE( walk_parked ) << "X4 fault: the restore fetch never parked";

        // The mid-flight erase-side marker lands inside the parked window,
        // then the lookup is blinded - the restore proceeds as if the
        // mid-eviction state were valid.
        SeedPruneEvidence( c1 );
        evidence_suppressed_.store( true, std::memory_order_release );
        walk_gate.Release();
        ASSERT_TRUE( PollUntil( [&] {
                         return crdt_->GetForcedWalkStatus().state
                                == sgns::crdt::CrdtDatastore::ForcedWalkState::Completed;
                     },
                     std::chrono::milliseconds( 30000 ) ) )
            << "X4 fault: the blinded walk never completed";
        GraphsyncDAGSyncer::SetColdServeInterruptForTest( nullptr );
        evidence_suppressed_.store( false, std::memory_order_release );

        // THE DETECTOR (fails by design under the knob): the evidenced
        // ancestor must be skipped, never restored.
        EXPECT_TRUE( EvidenceInStore( c1 ) )
            << "X4 fault precondition: the evidence row really landed mid-window";
        const auto status = crdt_->GetForcedWalkStatus();
        EXPECT_GE( status.skipped_pruned, 1u )
            << "RED-BY-DESIGN (fault arm): the evidenced ancestor was not "
               "skipped - the partial-history detector must fail loudly here";
        EXPECT_FALSE( HotBytesPresent( c1 ) )
            << "RED-BY-DESIGN (fault arm): the evidenced ancestor was "
               "restored hot - the partial-history detector must fail loudly "
               "here";

        manager_->Stop();
        manager_ = nullptr;
    }
}

/**
 * @brief X4 layer two (D-03): the real GC pass thread racing the real
 *        walk-restore thread under seeded jitter (the X2-B iterated
 *        pattern transplanted to the eviction boundary - per-iteration
 *        seeds via SGNS_CROSS_RACE_JITTER_SEED/_ITERS, effective seed
 *        printed by LogKnobs).
 *
 * Per iteration: a fresh chain whose ancestor is S3-ONLY (hot + local
 * cold gone - the fetch really crosses the remote tier where the
 * seeded jitter stretches the in-flight window) + a fresh canary; the
 * walk's restore fetch parks at the serve seam; GC pass 1 nominates,
        // pass 2 parks between halves on its own real thread; the walk
 * releases first, then the GC. Every iteration must hold the COMPLETE
 * dichotomy invariants.
 */
TEST_F( ColdCrossFeatureFaultRaceTest, Cross_GcEvictionMidWalkRestore_RestoreCompletesOrAborts_LegB )
{
    LogKnobs( "X4-B" );

    // grace 1 ms (the run3 zero-sentinel find) so the deferred marks
    // come due on the next tick inside the iteration cadence.
    auto manager = MakeManager( /*grace_ms=*/1 );
    manager->Start();
    crdt_->AddTopicName( "topic" );

    for ( uint64_t iter = 0; iter < jitter_iters_; ++iter )
    {
        const CID a1 = PublishElement( "x4/barrier/key1/" + std::to_string( iter ),
                                       "v1-" + std::to_string( iter ) );
        const CID a2 = PublishElement( "x4/barrier/key2/" + std::to_string( iter ),
                                       "v2-" + std::to_string( iter ) );
        const Cid canary
            = SetOrphan( "x4 barrier canary " + std::to_string( iter ) );
        WaitAllDurable( { Cid( a1 ), Cid( a2 ), canary } );
        ASSERT_TRUE( dagsyncer_->markResolved( a1 ).has_value() );
        RemoveHotCopy( a1 );
        auto evicted = shared_local_->EvictLocal( Cid( a1 ) );
        ASSERT_TRUE( evicted.has_value() ) << "iter " << iter << ": local cold evicted";
        BackdateCid( canary, 60000 );
        timing_->SetSeededJitter( S3Op::Get, 25, 15, effective_seed_ + iter );

        WindowGate        walk_gate;
        std::atomic<bool> walk_entered{ false };
        GraphsyncDAGSyncer::SetColdServeInterruptForTest(
            [ & ]( int step ) {
                if ( step == GraphsyncDAGSyncer::kInterruptAfterEvidenceConsult
                     && !walk_entered.exchange( true ) )
                {
                    walk_gate.Enter();
                }
                return false;
            } );
        auto queued = crdt_->RequestForcedWalk(
            { "topic" },
            []( const sgns::crdt::CrdtDatastore::Element & ) -> outcome::result<bool> { return true; },
            1000U );
        ASSERT_TRUE( queued.has_value() ) << "iter " << iter << ": walk failed to queue";
        const bool walk_parked = walk_gate.WaitInside( std::chrono::milliseconds( 10000 ) );
        if ( !walk_parked )
        {
            walk_gate.Release();
            GraphsyncDAGSyncer::SetColdServeInterruptForTest( nullptr );
        }
        ASSERT_TRUE( walk_parked )
            << "X4-B iter " << iter << ": the restore fetch never parked";

        const auto pass1 = manager->GcPassOnce();
        ASSERT_EQ( pass1.size(), 1u ) << "iter " << iter << ": only the canary nominated";
        EXPECT_TRUE( pass1[0] == canary );
        const auto grace_early
            = std::chrono::steady_clock::now() + std::chrono::milliseconds( 3 );
        ASSERT_TRUE( PollUntil( [&] { return std::chrono::steady_clock::now() >= grace_early; },
                                std::chrono::milliseconds( 1000 ) ) )
            << "iter " << iter << ": the 1 ms grace never elapsed";

        WindowGate        gc_gate;
        std::atomic<bool> gc_done{ false };
        sgns::crdt::SetGcBetweenHalvesHookForTesting( manager, [ &gc_gate ] { gc_gate.Enter(); } );
        std::thread gc_thread( [ manager, &gc_done ] {
            (void)manager->GcPassOnce();
            gc_done.store( true );
        } );
        const bool gc_parked = gc_gate.WaitInside( std::chrono::milliseconds( 10000 ) );
        if ( !gc_parked )
        {
            gc_gate.Release();
            sgns::crdt::SetGcBetweenHalvesHookForTesting( manager, nullptr );
            gc_thread.join();
        }
        ASSERT_TRUE( gc_parked ) << "X4-B iter " << iter << ": the GC never parked between halves";

        walk_gate.Release();
        ASSERT_TRUE( PollUntil( [&] {
                         return crdt_->GetForcedWalkStatus().state
                                == sgns::crdt::CrdtDatastore::ForcedWalkState::Completed;
                     },
                     std::chrono::milliseconds( 30000 ) ) )
            << "X4-B iter " << iter << ": the walk never completed";
        GraphsyncDAGSyncer::SetColdServeInterruptForTest( nullptr );
        gc_gate.Release();
        gc_thread.join();
        sgns::crdt::SetGcBetweenHalvesHookForTesting( manager, nullptr );
        ASSERT_TRUE( gc_done.load() );
        timing_->ClearTimings();

        const auto status = crdt_->GetForcedWalkStatus();
        EXPECT_EQ( status.walk_gaps, 0u ) << "X4-B iter " << iter << ": no gap";
        EXPECT_EQ( status.errors, 0u ) << "X4-B iter " << iter << ": no error";
        EXPECT_TRUE( HotBytesPresent( a1 ) )
            << "X4-B iter " << iter << ": the ancestor restored whole";
        EXPECT_GE( timing_->OpCount( S3Op::Get ), 1u )
            << "X4-B iter " << iter << ": the fetch really crossed the S3 tier";
        EXPECT_EQ( manager->GetStatus().gc_last_pass_reclaimed_count, 1u )
            << "X4-B iter " << iter << ": the canary's eviction completed";
    }

    manager_->Stop();
    manager_ = nullptr;
}

// ---------------------------------------------------------------------------
// X6 (plan 19-03 Task 1): the Phase 18 downgrade interlock x the walk.
// ---------------------------------------------------------------------------

/**
 * @brief X6: role-downgrade ack/arming (the Phase 18 interlock) x walk
 *        start/visit (R1/R2) - hazard: PARTIAL-HISTORY VALIDATION.
 *
 * LAYER POSTURE (seam-only - the flagged assumption, a decision not an
 * accident): the downgrade-ack-write vs walk claim is a state-ordering
 * claim at the interlock surface; the true-concurrency residue for the
 * downgrade itself was already proven by the 18-02/18-03 timeline
 * suites (E2E on the t0-t3 timeline).
 *
 * SEAM LEG: the tx-stack walk's restore fetch of the cold-resident
 * victim (SeedX5Graph's twin-eviction posture - NoEvidence, sealed by
 * consumption) parks at the serve seam (step 1, provably BETWEEN walk
 * visits). While parked, the FULL interlock timeline runs through the
 * sanctioned 18-02 writer (RoleFingerprint::CheckAndRewrite - the
 * persisted fingerprint + the downgrade_acknowledged ack surface):
 *   (a) the unacknowledged check FIRES (DowngradeUnacknowledged) and
 *       PRESERVES the archive-era record - the armed-role protection;
 *   (b) zero /prune markers anywhere (nothing was erased while the
 *       interlock holds the timeline - the t1 observable);
 *   (c) the ack lands (DowngradeAcknowledged) - the only consent
 *       transition;
 * then the walk releases and completes whole (the crossing changed
 * nothing the walk validates), and post-walk the start-normally disarm
 * is the only transition left (a fresh unacknowledged check returns
 * NoDowngrade, never a re-fire).
 *
 * FAULT ARM (knob-gated): the in-flight ack treated as disarmed before
 * commit - a TORN record landing (the value zeroed at the production
 * PRUNE_ROLE_FINGERPRINT_KEY constant only, the 18-01 corrupt-value
 * grammar; the key itself is never hand-built). The mid-walk
 * unacknowledged check can no longer see the prior posture: the
 * verdict flips off the armed timeline - the detector FAILS (teeth;
 * the 18-REVIEW F-1 laundering class caught at the walk boundary).
 */
TEST_F( ColdCrossFeatureFaultRaceTest, Cross_DowngradeArmsMidWalk_InterlockHoldsTimeline )
{
    LogKnobs( "X6" );
    const bool red_arms = red_arms_enabled_;

    auto interlock_cfg = []( bool ack ) {
        sgns::PruningConfig config;
        config.enabled              = true;
        config.finality_window      = kXWindow;
        config.downgrade_acknowledged = ack;
        return config;
    };
    const std::string fingerprint_key( sgns::PruneEligibility::PRUNE_ROLE_FINGERPRINT_KEY );

    // ---- SEAM LEG: the interlock timeline across a mid-flight walk.
    {
        BuildTxStack();
        std::string path, cid, twin_path;
        SeedX5Graph( 91, &path, &cid, &twin_path );

        // t0: the archive-era posture is recorded (FirstRun on the fresh
        // stack; the record now parses as Archive).
        {
            auto seeded = sgns::RoleFingerprint::CheckAndRewrite(
                *gw_db_, interlock_cfg( false ), sgns::NodeType::Archive, /*cold=*/true );
            ASSERT_TRUE( seeded.has_value() );
            EXPECT_EQ( seeded.value(), sgns::RoleFingerprint::Verdict::FirstRun );
            auto recorded = SnapshotXMapEntry( fingerprint_key );
            ASSERT_NE( recorded, "absent" );
            SGTransaction::RoleFingerprintRecord record;
            ASSERT_TRUE( record.ParseFromArray( recorded.data(),
                                                static_cast<int>( recorded.size() ) ) );
            EXPECT_EQ( static_cast<sgns::NodeType>( record.node_type() ), sgns::NodeType::Archive );
        }

        // The walk parks mid-restore (entered-once at the serve seam -
        // provably BETWEEN walk visits).
        WindowGate        walk_gate;
        std::atomic<bool> walk_entered{ false };
        GraphsyncDAGSyncer::SetColdServeInterruptForTest(
            [ & ]( int step ) {
                if ( step == GraphsyncDAGSyncer::kInterruptAfterEvidenceConsult
                     && !walk_entered.exchange( true ) )
                {
                    walk_gate.Enter();
                }
                return false;
            } );
        auto queued = gw_db_->RequestForcedWalk(
            { std::string( kXSeedTopic ) },
            []( const sgns::crdt::CrdtDatastore::Element & ) -> outcome::result<bool> { return true; },
            1000U );
        ASSERT_TRUE( queued.has_value() ) << "X6: walk failed to queue";
        const bool walk_parked = walk_gate.WaitInside( std::chrono::milliseconds( 10000 ) );
        if ( !walk_parked )
        {
            walk_gate.Release();
            GraphsyncDAGSyncer::SetColdServeInterruptForTest( nullptr );
        }
        ASSERT_TRUE( walk_parked ) << "X6: the walk never parked mid-restore";

        // (a) MID-WALK, UNACKNOWLEDGED: the interlock fires and preserves
        // the archive prior (the armed-role protection).
        {
            auto unacked = sgns::RoleFingerprint::CheckAndRewrite(
                *gw_db_, interlock_cfg( false ), sgns::NodeType::Light, /*cold=*/true );
            ASSERT_TRUE( unacked.has_value() );
            EXPECT_EQ( unacked.value(), sgns::RoleFingerprint::Verdict::DowngradeUnacknowledged )
                << "X6 (a): the mid-walk check arms - consent not yet given";
            auto recorded = SnapshotXMapEntry( fingerprint_key );
            ASSERT_NE( recorded, "absent" );
            SGTransaction::RoleFingerprintRecord record;
            ASSERT_TRUE( record.ParseFromArray( recorded.data(),
                                                static_cast<int>( recorded.size() ) ) );
            EXPECT_EQ( static_cast<sgns::NodeType>( record.node_type() ), sgns::NodeType::Archive )
                << "X6 (a): the unacknowledged arm PRESERVES the archive prior";
        }
        // (b) zero markers while the timeline is armed.
        {
            auto marker = sgns::PruneEligibility::LookupPrunedBlockEntry( *gw_db_, cid );
            ASSERT_TRUE( marker.has_value() );
            EXPECT_EQ( marker.value().first, sgns::PrunedBlockLookup::NOT_FOUND )
                << "X6 (b): nothing erased across the armed timeline";
        }
        // (c) the ack lands mid-walk (the only consent transition).
        {
            auto acked = sgns::RoleFingerprint::CheckAndRewrite(
                *gw_db_, interlock_cfg( /*ack=*/true ), sgns::NodeType::Light, /*cold=*/true );
            ASSERT_TRUE( acked.has_value() );
            EXPECT_EQ( acked.value(), sgns::RoleFingerprint::Verdict::DowngradeAcknowledged )
                << "X6 (c): the ack write lands at the walk step";
        }

        // Release: the walk completes whole - the crossing changed
        // nothing the walk validates.
        walk_gate.Release();
        ASSERT_TRUE( PollUntil( [&] {
                         return gw_db_->GetForcedWalkStatus().state
                                == sgns::crdt::CrdtDatastore::ForcedWalkState::Completed;
                     },
                     std::chrono::milliseconds( 30000 ) ) )
            << "X6: the walk never completed after the interlock timeline";
        GraphsyncDAGSyncer::SetColdServeInterruptForTest( nullptr );
        {
            const auto status = gw_db_->GetForcedWalkStatus();
            EXPECT_EQ( status.walk_gaps, 0u ) << "X6: no gap across the interlock timeline";
            EXPECT_EQ( status.errors, 0u ) << "X6: no error across the interlock timeline";
        }

        // (d) post-walk: the start-normally disarm is the only transition
        // left - a fresh unacknowledged check NEVER re-fires.
        {
            auto recheck = sgns::RoleFingerprint::CheckAndRewrite(
                *gw_db_, interlock_cfg( false ), sgns::NodeType::Light, /*cold=*/true );
            ASSERT_TRUE( recheck.has_value() );
            EXPECT_EQ( recheck.value(), sgns::RoleFingerprint::Verdict::NoDowngrade )
                << "X6 (d): post-ack the disarm latches - no re-fire on the walk's "
                   "far side";
        }
    }

    // ---- FAULT ARM (red-by-design, knob only): the torn-ack landing -
    // the in-flight ack treated as disarmed before commit.
    if ( red_arms )
    {
        // Fresh graph on the ONE per-case stack (the shared-stack
        // restructure - mid-test GlobalDB rebuild deadlocks).
        std::string path, cid, twin_path;
        SeedX5Graph( 92, &path, &cid, &twin_path );

        // Re-seed the archive posture (post-ack the record reads Light;
        // an upgrade re-arm writes Archive back - NoDowngrade verdict).
        {
            auto reseed = sgns::RoleFingerprint::CheckAndRewrite(
                *gw_db_, interlock_cfg( false ), sgns::NodeType::Archive, /*cold=*/true );
            ASSERT_TRUE( reseed.has_value() );
            EXPECT_EQ( reseed.value(), sgns::RoleFingerprint::Verdict::NoDowngrade );
            auto recorded = SnapshotXMapEntry( fingerprint_key );
            ASSERT_NE( recorded, "absent" );
            SGTransaction::RoleFingerprintRecord record;
            ASSERT_TRUE( record.ParseFromArray( recorded.data(),
                                                static_cast<int>( recorded.size() ) ) );
            EXPECT_EQ( static_cast<sgns::NodeType>( record.node_type() ), sgns::NodeType::Archive );
        }

        WindowGate        walk_gate;
        std::atomic<bool> walk_entered{ false };
        GraphsyncDAGSyncer::SetColdServeInterruptForTest(
            [ & ]( int step ) {
                if ( step == GraphsyncDAGSyncer::kInterruptAfterEvidenceConsult
                     && !walk_entered.exchange( true ) )
                {
                    walk_gate.Enter();
                }
                return false;
            } );
        auto queued = gw_db_->RequestForcedWalk(
            { std::string( kXSeedTopic ) },
            []( const sgns::crdt::CrdtDatastore::Element & ) -> outcome::result<bool> { return true; },
            1000U );
        ASSERT_TRUE( queued.has_value() );
        const bool walk_parked = walk_gate.WaitInside( std::chrono::milliseconds( 10000 ) );
        if ( !walk_parked )
        {
            walk_gate.Release();
            GraphsyncDAGSyncer::SetColdServeInterruptForTest( nullptr );
        }
        ASSERT_TRUE( walk_parked ) << "X6 fault: the walk never parked mid-restore";

        // THE TORN LANDING (knob only): the in-flight ack's non-atomic
        // write leaves the record value unparseable at the PRODUCTION key
        // constant (the 18-01 corrupt-value grammar - the value, never
        // the key). The prior posture is gone from the interlock's sight.
        {
            sgns::crdt::GlobalDB::Buffer key_buffer;
            key_buffer.put( fingerprint_key );
            sgns::crdt::GlobalDB::Buffer value_buffer;
            value_buffer.put( std::string( "\x80\x80\x80\x80\x80", 5 ) );
            ASSERT_TRUE( gw_db_->GetDataStore()->put( key_buffer, value_buffer ).has_value() );
        }

        // THE DETECTOR (fails by design under the knob): the mid-walk
        // unacknowledged check must still fire on the armed timeline.
        {
            auto unacked = sgns::RoleFingerprint::CheckAndRewrite(
                *gw_db_, interlock_cfg( false ), sgns::NodeType::Light, /*cold=*/true );
            ASSERT_TRUE( unacked.has_value() );
            EXPECT_EQ( unacked.value(), sgns::RoleFingerprint::Verdict::DowngradeUnacknowledged )
                << "RED-BY-DESIGN (fault arm): the torn-ack landing flipped the "
                   "verdict off the armed timeline - the downgrade launders "
                   "through the walk boundary; the interlock detector must "
                   "fail loudly here";
        }

        walk_gate.Release();
        ASSERT_TRUE( PollUntil( [&] {
                         const auto state = gw_db_->GetForcedWalkStatus().state;
                         return state == sgns::crdt::CrdtDatastore::ForcedWalkState::Completed
                                || state == sgns::crdt::CrdtDatastore::ForcedWalkState::Incomplete;
                     },
                     std::chrono::milliseconds( 30000 ) ) );
        GraphsyncDAGSyncer::SetColdServeInterruptForTest( nullptr );
    }
}
