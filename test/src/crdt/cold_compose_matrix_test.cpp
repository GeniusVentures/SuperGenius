/**
 * @file       cold_compose_matrix_test.cpp
 * @brief      TxPruner x GC composition hardening (v1.2 Phase 16):
 *             the SC1 stale-LRU window close proven at both D-06 layers,
 *             the SC2 interleave matrix, and the SC3 disposal-starvation
 *             deterministic leg - all on one Tier-1 hermetic suite (the
 *             phase's evidence home).
 *
 *             Fixture posture (the Phase-14 kit, D-08a lineage): a REAL
 *             cold-enabled GlobalDB (real RocksDB hot store + real
 *             coldstore journal) over SetColdStorageStackClientsForTesting-
 *             injected InMemoryS3Client/InMemoryS3AdminClient fakes with
 *             endpoint "hermetic.invalid" (never contacted),
 *             hygiene-cleared immediately after construction. The
 *             healthy-fakes + fast-cadence posture (drain 50 ms, flush
 *             50/300 ms) drives REAL journal transitions Pending ->
 *             Accepted -> Durable, polled to evidence before any erase
 *             (the cold_prune_gate LEG-1 discipline). Zero network, zero
 *             services; the syncer is reached through the syncerOf idiom
 *             (globaldb_erase_test) - a concrete GraphsyncDAGSyncer for
 *             AddCIDBlock / GrabCIDBlock / IsCIDInCache /
 *             GetNodeFromMerkleDAG.
 *
 *             LEG W1 (16-01 Task 1, COMP-01/D-04/D-06, authored RED-FIRST):
 *             the deterministic window counter-test. SetEraseWindowObserver
 *             ForTest - the NEW observe-and-continue sibling of the
 *             interrupt hook - fires once per erase immediately before the
 *             single batch commit; the armed observer performs the
 *             mid-window AddCIDBlock insert (the BlockReceivedCallback
 *             inserter primitive), the erase proceeds UNCONDITIONALLY, and
 *             the post-return world must show the insert evicted:
 *             GrabCIDBlock fails and IsCIDInCache is false. Against the
 *             seam-only line (the close absent) this leg serves the stale
 *             node through the LRU - the SC1-STALE-SERVE red signature,
 *             captured in the plan SUMMARY before the fix landed.
 *
 *             LEG W2 (16-01 Task 2, D-06's true-concurrency layer): a
 *             barrier-driven real-thread leg parks the erase thread INSIDE
 *             the window (WindowGate entered from the observer) while a
 *             second thread inserts through AddCIDBlock - barriers and
 *             poll predicates only, never fixed sleeps.
 *
 *             LEG W3 (16-01 Task 2): abort-path survival - an interrupt
 *             hook aborting at step 1 after inserting into the LRU must
 *             leave that entry ALIVE (nothing was erased, so nothing is
 *             stale), and the observer must never fire on an aborted
 *             erase.
 *
 *             LEG W4 (16-01 Task 2): never-cached idempotence - erasing a
 *             CID that was never LRU-cached succeeds with the LRU
 *             unchanged (the empty-input edge of the closing primitive),
 *             and a second erase of the now-absent key stays side-effect
 *             free.
 *
 *             16-02 extends this suite with the SC3 deterministic leg
 *             (journal-injection disposal simulation, red-first); 16-03
 *             extends it with the SC2 interleave matrix product. Tier-1
 *             (Phase 14 D-09 contract): hermetic, deterministic,
 *             fresh-clone-safe.
 * @date       2026-11-18
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

#include <rocksdb/db.h>
#include <rocksdb/options.h>

#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <libp2p/basic/scheduler.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <libp2p/host/host.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>
#include <soralog/impl/configurator_from_yaml.hpp>

#include <sgns-blockstore/Cid.hpp>
#include <sgns-blockstore/S3AdminClient.hpp>
#include <sgns-blockstore/S3BlockStore.hpp>
#include <sgns-blockstore/S3Client.hpp>

// Internal fakes - relative-include-dir privilege (test-surface only).
#include <sgns-blockstore/InMemoryS3AdminClient.hpp>
#include <sgns-blockstore/InMemoryS3Client.hpp>

#include "base/logger.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/globaldb/ColdStorageStack.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
#include "crdt/hierarchical_key.hpp"

using namespace sgns;

namespace
{
    /// The fixture's pre-seeded CRDT topic (the tx_pruner_test contract):
    /// puts on it create real blocks, heads, and seals.
    constexpr char kSeedTopic[] = "CRDT.Datastore.TEST.Channel";

    /// Bounded poll loop with deadline - never a fixed sleep (Tier-1 audit;
    /// the durability/resurrection suite discipline).
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

    /// SHORT relative path under the build tree (Windows MAX_PATH; the
    /// upload_drainer_test FreshDir/NextUniqueDir discipline).
    std::string NextUniqueDir( const char *lane )
    {
        static std::atomic<uint64_t> next{ 0 };
        return std::string( "test-data/cold-compose-matrix/" ) + lane + "-"
             + std::to_string( next.fetch_add( 1, std::memory_order_relaxed ) );
    }

    /// A truly FRESH store per case: the monotonic counter resets every
    /// process run, so a leftover dir from a previous run would reopen its
    /// stale state - wipe before use.
    std::string FreshDir( const char *lane )
    {
        auto           dir = NextUniqueDir( lane );
        std::error_code ec;
        std::filesystem::remove_all( dir, ec );
        return dir;
    }

    /// The suite's logging config (the resurrection-test shape: a
    /// std::string-returning function avoids the ConfiguratorFromYAML
    /// ctor's const-char* overload ambiguity).
    std::string LoggingConfig()
    {
        return R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: cold_compose_matrix_test
    sink: console
    level: error
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )";
    }

    /**
     * @brief  Hermetic cold-enabled GlobalDB stack (the Phase-14 kit
     *         posture, lean: no account/blockchain/transaction layer -
     *         the W legs drive EraseEntry directly). One stack per test.
     */
    class ColdComposeMatrixTest : public ::testing::Test
    {
    public:
        static void SetUpTestSuite()
        {
            auto log_system = std::make_shared<soralog::LoggingSystem>(
                std::make_shared<soralog::ConfiguratorFromYAML>(
                    std::make_shared<libp2p::log::Configurator>(), LoggingConfig() ) );
            (void)log_system->configure();
            libp2p::log::setLoggingSystem( log_system );
        }

        ~ColdComposeMatrixTest() override
        {
            TeardownStack();
        }

        /// The stack teardown sequence (the cold_prune_gate discipline
        /// copied wholesale): clear EVERY static seam (including the NEW
        /// observe-and-continue window observer), quiesce the GlobalDB
        /// (stops the cold drainer + GC threads and releases the
        /// coldstore RocksDB dir lock), stop the pubsub host LAST (the
        /// CRDTFixture invariant: db_ must be released BEFORE pubs_->Stop()).
        void TeardownStack()
        {
            // Fixture discipline: a hook armed by a failing case must never
            // leak further - clear EVERY static seam before any teardown
            // touches the stores.
            crdt::GlobalDB::SetEraseWindowObserverForTest( nullptr );
            crdt::GlobalDB::SetColdUploadStateHookForTest( nullptr );
            crdt::GlobalDB::SetEraseInterruptHookForTest( nullptr );

            if ( db_ )
            {
                db_->ShutdownNow();
            }
            db_.reset();
            try
            {
                if ( pubs_ )
                {
                    pubs_->Stop();
                }
            }
            catch ( const std::exception &err )
            {
                std::cerr << "GossipPubSub::Stop() exception: " << err.what() << std::endl;
            }
            pubs_.reset();
            io_.reset();
        }

    protected:
        //
        // Stack construction (the cold_prune_gate_test recipe, lean).
        //

        /// Builds the single-node cold-enabled stack on a FRESH dir with
        /// the HEALTHY hermetic fakes and the fast cadence (drain 50 ms,
        /// flush 50/300 ms): REAL journal transitions fire fast, so
        /// WaitDurable polls are deterministic. Retention stays 0 = the
        /// GC never runs (the D-02 absent=off posture) - nothing but the
        /// erase under test may touch the stores.
        void BuildStack()
        {
            const std::string hot_dir      = FreshDir( "hot" );
            const std::string keypair_path = FreshDir( "keypair" );

            // Application-work pool (the CRDTFixture contract): tests drive
            // it by hand; it stays separate from the host's context.
            io_ = std::make_shared<boost::asio::io_context>();

            pubs_ = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
                crdt::KeyPairFileStorage( keypair_path ).GetKeyPair().value() );
            ASSERT_NE( pubs_, nullptr );
            auto start_future = pubs_->Start( 0, {} );
            ASSERT_TRUE( PollUntil(
                [&] { return start_future.wait_for( std::chrono::milliseconds( 0 ) ) == std::future_status::ready; },
                std::chrono::milliseconds( 20000 ) ) )
                << "pubsub host failed to start";
            ASSERT_FALSE( start_future.get() ) << "GossipPubSub::Start failed";

            // The scheduler rides the host's io_context (the
            // single-threaded-per-host discipline).
            scheduler_ = std::make_shared<libp2p::basic::SchedulerImpl>(
                std::make_shared<libp2p::basic::AsioSchedulerBackend>( pubs_->GetAsioContext() ),
                libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
            network_   = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubs_->GetHost(), scheduler_ );
            generator_ = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();

            // --- The hermetic cold posture (the resurrection recipe):
            // cold ON through the TEST-ONLY client override with the
            // never-contacted endpoint.
            crdt::GlobalDB::ColdStorageOptions cold;
            cold.enabled   = true;
            cold.endpoint  = "hermetic.invalid"; // overridden below - never contacted
            cold.bucket    = "compose-matrix";
            cold.accessKey = "test";
            cold.secretKey = "test";
            cold.backpressure.drain_interval_ms = 50;
            cold.backpressure.warn_pending      = 100000;
            cold.backpressure.critical_pending  = 200000;
            cold.backpressure.backoff_cap_ms    = 1000;
            // FastDurability (50/300 ms): the fast flush cadence that
            // makes Durable waits deterministic (the LEG-1 drive).
            cold.durability.flush_min_interval_ms = 50;
            cold.durability.flush_max_interval_ms = 300;
            fake_s3_                                = std::make_shared<sgns::blockstore::InMemoryS3Client>();
            fake_admin_                             = std::make_shared<sgns::blockstore::InMemoryS3AdminClient>();
            sgns::crdt::SetColdStorageStackClientsForTesting(
                std::static_pointer_cast<sgns::blockstore::S3Client>( fake_s3_ ),
                std::static_pointer_cast<sgns::blockstore::S3AdminClient>( fake_admin_ ) );

            auto globaldb_ret = crdt::GlobalDB::New( io_,
                                                     hot_dir,
                                                     pubs_,
                                                     crdt::CrdtOptions::DefaultOptions(),
                                                     network_,
                                                     scheduler_,
                                                     generator_,
                                                     nullptr,
                                                     crdt::GlobalDB::BackupOptions{ false, 15, 12, true },
                                                     cold );
            ASSERT_TRUE( globaldb_ret.has_value() ) << "GlobalDB failed to construct";
            db_ = std::move( globaldb_ret.value() );

            // Hygiene: the process-global override has served its purpose.
            sgns::crdt::SetColdStorageStackClientsForTesting( nullptr, nullptr );

            db_->AddListenTopic( kSeedTopic );
            db_->AddBroadcastTopic( kSeedTopic );
            db_->Start();
            ASSERT_TRUE( db_->HasColdStorage() ) << "tier-presence signal mismatch";
        }

        //
        // Seeding helpers (the globaldb_erase_test recipes).
        //

        /// One seeded entry: the delta CID of the put (head at its
        /// moment) and the CRDT element key, which doubles as the raw
        /// tx->CID map key.
        struct SeededEntry
        {
            sgns::CID   cid;
            std::string tx_path;
            std::string value;
        };

        /// Puts one value through the public CRDT path on the topic and
        /// returns the delta CID. Put returns only after the self-created
        /// root job completed (heads updated, seal written by
        /// UpdateCRDTHeads) - and the local publish never inserts the
        /// block into the LRU (the BlockReceivedCallback inserter runs on
        /// RECEIVED blocks only), which is exactly the W legs' not-LRU-
        /// resident precondition.
        static SeededEntry SeedEntry( sgns::crdt::GlobalDB &db, const std::string &tx_path, const std::string &value )
        {
            sgns::base::Buffer value_buffer;
            value_buffer.put( value );
            auto put_result = db.Put( sgns::crdt::HierarchicalKey( tx_path ), value_buffer, { kSeedTopic } );
            EXPECT_TRUE( put_result.has_value() );
            return { put_result.value(), tx_path, value };
        }

        /// True when @p cid is the head of ANY topic (guard 1's surface).
        static bool headListContains( sgns::crdt::GlobalDB &db, const sgns::CID &cid )
        {
            auto heads = db.GetCRDTHeadList();
            if ( heads.has_error() )
            {
                return false;
            }
            for ( const auto &[topic, topic_heads] : heads.value().first )
            {
                if ( topic_heads.count( cid ) > 0 )
                {
                    return true;
                }
            }
            return false;
        }

        /// Polls the REAL journal to Durable for @p cid (the LEG-1
        /// deterministic drive: healthy fakes + fast cadence advance
        /// Pending -> Accepted -> Durable within milliseconds).
        bool WaitDurable( const sgns::CID &cid )
        {
            return PollUntil( [&] { return db_->GetColdUploadState( cid ) == crdt::ColdUploadState::Durable; },
                              std::chrono::milliseconds( 20000 ) );
        }

        /// The concrete DAG syncer behind the broadcaster (the
        /// globaldb_erase_test syncerOf accessor idiom).
        static std::shared_ptr<sgns::crdt::GraphsyncDAGSyncer> syncerOf( sgns::crdt::GlobalDB &db )
        {
            return std::static_pointer_cast<sgns::crdt::GraphsyncDAGSyncer>( db.GetBroadcaster()->GetDagSyncer() );
        }

        std::shared_ptr<boost::asio::io_context>                  io_;
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>          pubs_;
        std::shared_ptr<libp2p::basic::SchedulerImpl>             scheduler_;
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network> network_;
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator_;
        std::shared_ptr<sgns::crdt::GlobalDB>                      db_;
        std::shared_ptr<sgns::blockstore::InMemoryS3Client>       fake_s3_;
        std::shared_ptr<sgns::blockstore::InMemoryS3AdminClient>  fake_admin_;
    };

    /**
     * LEG W1 (16-01 Task 1, COMP-01/D-04/D-06) - the deterministic window
     * counter-test, authored RED-FIRST: the pre-fix line (seam present,
     * post-commit close absent) MUST fail this leg by SERVING the stale
     * node through the LRU (the SC1-STALE-SERVE signature); the landed
     * close evicts the mid-window insert and the leg goes green.
     */
    TEST_F( ColdComposeMatrixTest, W1_DeterministicWindowInsertCannotOutliveErase )
    {
        BuildStack();
        ASSERT_NE( db_, nullptr );

        // Two seeded entries: A is the erase target, B advances the heads
        // so A is not any topic's head (guard 1). Both blocks must read
        // Durable before the erase (guard 5's cold conjunct).
        const auto entry_a = SeedEntry( *db_, "/compose/w1/victim", "W1-VICTIM" );
        const auto entry_b = SeedEntry( *db_, "/compose/w1/driver", "W1-DRIVER" );
        ASSERT_FALSE( headListContains( *db_, entry_a.cid ) ) << "precondition: target is not a head";
        ASSERT_TRUE( WaitDurable( entry_a.cid ) ) << "victim block never reached Durable";
        ASSERT_TRUE( WaitDurable( entry_b.cid ) ) << "driver block never reached Durable";

        auto syncer = syncerOf( *db_ );
        ASSERT_FALSE( syncer->IsCIDInCache( entry_a.cid ) )
            << "precondition: target not LRU-resident (guard 3 must pass)";

        // The node the mid-window insert will (re)insert - captured from
        // the hot store BEFORE the erase starts (GetNodeFromMerkleDAG
        // never touches the LRU).
        auto node_result = syncer->GetNodeFromMerkleDAG( entry_a.cid );
        ASSERT_TRUE( node_result.has_value() ) << node_result.error().message();
        const auto node = node_result.value();

        // Arm the observe-and-continue seam: the insert lands INSIDE the
        // guard-3-to-commit window (after every guard, before the single
        // batch commit), then the erase proceeds UNCONDITIONALLY.
        crdt::GlobalDB::SetEraseWindowObserverForTest(
            [&] { (void)syncer->AddCIDBlock( entry_a.cid, node ); } );

        const auto erase_result = db_->EraseEntry( entry_a.tx_path, entry_a.cid );
        // Clear the seam immediately: the assertions below observe the
        // post-erase world, and no later case may inherit the hook.
        crdt::GlobalDB::SetEraseWindowObserverForTest( nullptr );

        ASSERT_TRUE( erase_result.has_value() ) << erase_result.error().message();

        // THE COMP-01 CONTRACT: the mid-window insert cannot outlive the
        // erase. Post-close the stale entry is evicted on BOTH committed
        // exits; pre-close (the red signature) GrabCIDBlock serves the
        // stale node - erased bytes through the LRU term.
        const auto stale_serve = syncer->GrabCIDBlock( entry_a.cid );
        EXPECT_TRUE( stale_serve.has_error() )
            << "SC1-STALE-SERVE: GrabCIDBlock still serves the mid-window-inserted LRU entry for the erased CID";
        EXPECT_FALSE( syncer->IsCIDInCache( entry_a.cid ) )
            << "SC1-STALE-SERVE: IsCIDInCache still true for the erased CID after EraseEntry returned";
    }

    /**
     * @brief One-shot enter/release latch: the deterministic barrier the
     *        race legs park actors on (D-11). Enter() marks the actor
     *        inside the window and blocks until Release(); Release() is
     *        idempotent and lets any LATER Enter pass straight through,
     *        so a failing leg can never hang teardown.
     *
     * Copied VERBATIM from cold_storage_fault_race_test.cpp (a NAMED
     * EXCEPTION to SuperGenius/CLAUDE.md hard invariant 2 - the
     * 16-01 Task-2 commit message names this copy).
     *
     * cv-wait based throughout - no sleep_for (Pitfall 4).
     */
    class WindowGate
    {
    public:
        // Leak-proofing (SC4 round 8, verbatim from the source suite): a
        // gtest ASSERT between arming the hook (Enter parked on the actor
        // thread) and the test's explicit Release() early-returns the test
        // body with the actor still parked; the fixture teardown then
        // joins that actor thread and deadlocks until ctest's kill timer.
        // Releasing from the destructor guarantees the parked actor is
        // freed at test-body exit, before teardown joins - a failed
        // deadline now surfaces as the ASSERT failure it is, never a hang.
        // Release() is idempotent for the normal path.
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
     * LEG W2 (16-01 Task 2, COMP-01/D-06 true-concurrency layer) - a
     * barrier-driven real-thread leg: the erase thread parks INSIDE the
     * window (gate.Enter() from the observer) while a SECOND thread
     * inserts through the AddCIDBlock primitive; the insert cannot
     * outlive the released erase. Barriers and poll predicates only,
     * never fixed sleeps.
     */
    TEST_F( ColdComposeMatrixTest, W2_RealThreadWindowInsertCannotOutliveErase )
    {
        BuildStack();
        ASSERT_NE( db_, nullptr );

        const auto entry_a = SeedEntry( *db_, "/compose/w2/victim", "W2-VICTIM" );
        const auto entry_b = SeedEntry( *db_, "/compose/w2/driver", "W2-DRIVER" );
        ASSERT_FALSE( headListContains( *db_, entry_a.cid ) ) << "precondition: target is not a head";
        ASSERT_TRUE( WaitDurable( entry_a.cid ) ) << "victim block never reached Durable";
        ASSERT_TRUE( WaitDurable( entry_b.cid ) ) << "driver block never reached Durable";

        auto syncer = syncerOf( *db_ );
        ASSERT_FALSE( syncer->IsCIDInCache( entry_a.cid ) )
            << "precondition: target not LRU-resident (guard 3 must pass)";

        auto node_result = syncer->GetNodeFromMerkleDAG( entry_a.cid );
        ASSERT_TRUE( node_result.has_value() ) << node_result.error().message();
        const auto node = node_result.value();

        // Park the erase thread inside the guard-3-to-commit window: the
        // observer calls gate.Enter() on the ERASE thread; the erase
        // proceeds only after Release().
        WindowGate erase_window;
        crdt::GlobalDB::SetEraseWindowObserverForTest( [&erase_window] { erase_window.Enter(); } );

        outcome::result<void> erase_result = outcome::failure( std::errc::operation_canceled );
        std::thread          erase_thread(
             [&] { erase_result = db_->EraseEntry( entry_a.tx_path, entry_a.cid ); } );

        const bool in_window = erase_window.WaitInside( std::chrono::milliseconds( 10000 ) );
        EXPECT_TRUE( in_window ) << "erase thread never parked inside the compose window";

        if ( in_window )
        {
            // The concurrent insert - a REAL second thread driving the
            // BlockReceivedCallback inserter primitive while the erase
            // thread sits between guard 3 and the commit.
            std::thread insert_thread( [&] { (void)syncer->AddCIDBlock( entry_a.cid, node ); } );
            insert_thread.join();
            // Mid-window assertion: the insert landed while the erase is
            // parked - the exact SC1 race posture (not vacuous: guard 3
            // already passed before the park).
            EXPECT_TRUE( syncer->IsCIDInCache( entry_a.cid ) )
                << "mid-window insert did not land while the erase was parked";
        }

        erase_window.Release();
        erase_thread.join();
        crdt::GlobalDB::SetEraseWindowObserverForTest( nullptr );

        ASSERT_TRUE( erase_result.has_value() ) << erase_result.error().message();

        const auto stale_serve = syncer->GrabCIDBlock( entry_a.cid );
        EXPECT_TRUE( stale_serve.has_error() )
            << "SC1-STALE-SERVE: real-thread mid-window insert outlived the erase (GrabCIDBlock serves)";
        EXPECT_FALSE( syncer->IsCIDInCache( entry_a.cid ) )
            << "SC1-STALE-SERVE: IsCIDInCache true after the real-thread window erase returned";
    }

    /**
     * LEG W3 (16-01 Task 2) - abort-path survival (the anti-pattern
     * discipline from 16-RESEARCH): an interrupt hook aborting the erase
     * BEFORE the commit (step 1) after inserting into the LRU must leave
     * that entry ALIVE - nothing was erased, so nothing is stale; the
     * pre-commit early returns never reach the post-commit delete. The
     * observer must NEVER fire on an aborted erase (its fire site is
     * after all interrupt steps).
     */
    TEST_F( ColdComposeMatrixTest, W3_AbortPathNeverEvicts )
    {
        BuildStack();
        ASSERT_NE( db_, nullptr );

        const auto entry_a = SeedEntry( *db_, "/compose/w3/victim", "W3-VICTIM" );
        const auto entry_b = SeedEntry( *db_, "/compose/w3/driver", "W3-DRIVER" );
        ASSERT_FALSE( headListContains( *db_, entry_a.cid ) ) << "precondition: target is not a head";
        ASSERT_TRUE( WaitDurable( entry_a.cid ) ) << "victim block never reached Durable";
        ASSERT_TRUE( WaitDurable( entry_b.cid ) ) << "driver block never reached Durable";

        auto syncer = syncerOf( *db_ );
        ASSERT_FALSE( syncer->IsCIDInCache( entry_a.cid ) )
            << "precondition: target not LRU-resident";

        auto node_result = syncer->GetNodeFromMerkleDAG( entry_a.cid );
        ASSERT_TRUE( node_result.has_value() ) << node_result.error().message();
        const auto node = node_result.value();

        // Arm BOTH seams: the interrupt hook inserts then aborts at step
        // 1 (before the raw-map staging and long before the observer's
        // fire site); the observer records a failure if EVER invoked.
        crdt::GlobalDB::SetEraseInterruptHookForTest(
            [&]( int step ) {
                (void)syncer->AddCIDBlock( entry_a.cid, node );
                return true; // abort - the dying batch proves all-or-nothing
            } );
        crdt::GlobalDB::SetEraseWindowObserverForTest(
            [] { ADD_FAILURE() << "SC1-STALE-SERVE: the window observer fired on an ABORTED erase"; } );

        const auto erase_result = db_->EraseEntry( entry_a.tx_path, entry_a.cid );

        crdt::GlobalDB::SetEraseInterruptHookForTest( nullptr );
        crdt::GlobalDB::SetEraseWindowObserverForTest( nullptr );

        ASSERT_TRUE( erase_result.has_error() );
        EXPECT_EQ( erase_result.error(), crdt::GlobalDB::Error::INVALID_PARAMETERS )
            << "the step-1 abort must report the abort value";

        // The SURVIVAL contract: the hook-inserted LRU entry is alive
        // (nothing was erased, so nothing is stale), and the value row is
        // still readable through the CRDT path.
        const auto survived = syncer->GrabCIDBlock( entry_a.cid );
        EXPECT_TRUE( survived.has_value() ) << "aborted erase must NOT evict the hook-inserted LRU entry";
        if ( survived.has_value() )
        {
            EXPECT_EQ( survived.value()->getCID(), entry_a.cid );
        }
        EXPECT_TRUE( syncer->IsCIDInCache( entry_a.cid ) );
        const auto value_read = db_->Get( sgns::crdt::HierarchicalKey( entry_a.tx_path ) );
        EXPECT_TRUE( value_read.has_value() ) << "the value row must survive the aborted erase";
    }

    /**
     * LEG W4 (16-01 Task 2) - never-cached idempotence (the COMP-01 empty
     * edge): erasing a CID that was never LRU-cached succeeds and leaves
     * the LRU unchanged (the closing delete is a no-op on absent keys),
     * and a SECOND erase of the now-absent key is side-effect free - the
     * idempotence D-01's convergence proof leans on, proven here at the
     * primitive level.
     */
    TEST_F( ColdComposeMatrixTest, W4_NeverCachedEraseIsIdempotent )
    {
        BuildStack();
        ASSERT_NE( db_, nullptr );

        const auto entry_a = SeedEntry( *db_, "/compose/w4/victim", "W4-VICTIM" );
        const auto entry_b = SeedEntry( *db_, "/compose/w4/driver", "W4-DRIVER" );
        ASSERT_FALSE( headListContains( *db_, entry_a.cid ) ) << "precondition: target is not a head";
        ASSERT_TRUE( WaitDurable( entry_a.cid ) ) << "victim block never reached Durable";
        ASSERT_TRUE( WaitDurable( entry_b.cid ) ) << "driver block never reached Durable";

        auto syncer = syncerOf( *db_ );
        EXPECT_FALSE( syncer->IsCIDInCache( entry_a.cid ) ) << "pre-state: never-cached CID";

        // NO hooks armed: the plain production path over a never-cached CID.
        const auto erase_result = db_->EraseEntry( entry_a.tx_path, entry_a.cid );
        ASSERT_TRUE( erase_result.has_value() ) << erase_result.error().message();

        EXPECT_FALSE( syncer->IsCIDInCache( entry_a.cid ) ) << "post-state: LRU still empty for the CID";
        const auto read = syncer->GrabCIDBlock( entry_a.cid );
        EXPECT_TRUE( read.has_error() ) << "GrabCIDBlock must fail for the never-cached erased CID";

        // The second erase of the now-absent key: every staging remove is
        // a harmless no-op on absent keys (the StageEraseElement
        // contract), so the erase SUCCEEDS idempotently - and the LRU is
        // untouched (the empty-input edge of the closing primitive).
        const auto second_erase = db_->EraseEntry( entry_a.tx_path, entry_a.cid );
        EXPECT_TRUE( second_erase.has_value() )
            << "second erase of the absent key must succeed idempotently (no-op staging, no LRU side effects)";
        EXPECT_FALSE( syncer->IsCIDInCache( entry_a.cid ) )
            << "SC1-STALE-SERVE: the second erase must not touch the LRU";
    }
} // namespace
