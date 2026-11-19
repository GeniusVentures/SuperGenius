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
#include <optional>
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
#include <sgns-blockstore/LocalBlockStore.hpp>
#include <sgns-blockstore/S3AdminClient.hpp>
#include <sgns-blockstore/S3BlockStore.hpp>
#include <sgns-blockstore/S3Client.hpp>

// Internal fakes - relative-include-dir privilege (test-surface only).
#include <sgns-blockstore/InMemoryS3AdminClient.hpp>
#include <sgns-blockstore/InMemoryS3Client.hpp>

#include "account/GeniusAccount.hpp"
#include "account/NodeType.hpp"
#include "account/TransferTransaction.hpp"
#include "account/UTXOManager.hpp"
#include "account/proto/SGTransaction.pb.h"
#include "base/logger.hpp"
#include "blockchain/Blockchain.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/crdt_datastore.hpp"
#include "crdt/globaldb/ColdStorageStack.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
#include "crdt/hierarchical_key.hpp"
#include "local_secure_storage/impl/MemorySecureStorage.hpp"
#include "testutil/wait_condition.hpp"
#include "transaction/PruneEligibility.hpp"
#include "transaction/TransactionManager.hpp"
#include "transaction/TxPruner.hpp"

using namespace sgns;

namespace sgns
{
    /**
     * Per-binary friend-seam definition (the tx_pruner_test shape): the
     * TxPruner tick gate requires State::READY, which no public API
     * reaches deterministically. This definition carries only
     * ChangeState - the one member this binary uses (the SC3 legs'
     * settled-cycle drive).
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
    /// The fixture's pre-seeded CRDT topic (the tx_pruner_test contract):
    /// puts on it create real blocks, heads, and seals.
    constexpr char kSeedTopic[] = "CRDT.Datastore.TEST.Channel";

    /// Finality window N (the gate-test recipes' value).
    constexpr uint64_t kWindow = 2;

    /// The account-generation tag the SC3 legs stamp onto cycles (D-C4).
    constexpr uint64_t kTestGeneration = 7;

    /// Settle window: waited out through the settled-cycle helper.
    constexpr uint64_t kSmallSettleSeconds = 1;

    const sgns::TokenID kTestTokenId = sgns::TokenID::FromBytes( { 0x00 } );

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
            sgns::TxPruner::SetCycleInterruptHookForTest( nullptr );
            crdt::GlobalDB::SetEraseWindowObserverForTest( nullptr );
            crdt::GlobalDB::SetColdUploadStateHookForTest( nullptr );
            crdt::GlobalDB::SetEraseInterruptHookForTest( nullptr );

            // Teardown invariant (asio, the tx_pruner_test order): stop the
            // blockchain FIRST so the ConsensusManager round-timer thread
            // joins before any db-backed handle is released.
            if ( blockchain_ )
            {
                (void)blockchain_->Stop();
            }
            tm_.reset();
            blockchain_.reset();
            account_.reset();

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
        /// WaitDurable polls are deterministic. @p retention_age_ms > 0
        /// ALSO arms the retention GC thread (tiny age + fast interval +
        /// 1 ms grace = deterministic hermetic disposal) - the S3a/S3c
        /// posture; 0 keeps the GC off (the D-02 absent=off posture, the
        /// W legs and S3b).
        void BuildStack( uint64_t retention_age_ms = 0 )
        {
            BuildStackOnDir( FreshDir( "hot" ), retention_age_ms );
        }

        /// Same as BuildStack but on an EXISTING hot dir (S3b's PHASE C
        /// reopen: the CRDT state, tx->CID map, UTXO records, and the
        /// MUTATED coldstore journal all survive the round trip).
        void BuildStackOnDir( const std::string &hot_dir, uint64_t retention_age_ms = 0 )
        {
            last_hot_dir_ = hot_dir;
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
            if ( retention_age_ms > 0 )
            {
                // The S3a/S3c disposal posture: retention configured arms
                // GlobalDB's OWN GC thread (deps attached in Initialize) -
                // tiny age + fast cadence + 1 ms grace make hermetic
                // disposals land within a few passes (PollUntil-bounded).
                cold.retention.retention_age_ms = retention_age_ms;
                cold.retention.gc_interval_ms   = 50;
                cold.retention.grace_ms         = 1;
            }
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

            // The tx_pruner_test SetUp recipe over the cold-capable db
            // (the SC3 legs' pruner wiring).
            sgns::GeniusAccount::SetSecureStorageFactory(
                []( const std::string &identifier ) -> std::shared_ptr<ISecureStorage> {
                    return std::make_shared<MemorySecureStorage>( identifier );
                } );

            account_ = sgns::GeniusAccount::New( kTestTokenId, boost::filesystem::path( hot_dir ) / "account" );
            ASSERT_NE( account_, nullptr );

            auto load_result = account_->GetUTXOManager().LoadUTXOs( db_->GetDataStore() );
            ASSERT_TRUE( load_result.has_value() );

            blockchain_ = sgns::Blockchain::New( db_, account_, pubs_, []( outcome::result<void> ) {} );
            ASSERT_NE( blockchain_, nullptr );

            constexpr auto kTimestampTolerance = std::chrono::milliseconds( 300000 );
            constexpr auto kMutabilityWindow   = std::chrono::milliseconds( 600000 );

            tm_ = sgns::TransactionManager::New( db_,
                                                 io_,
                                                 account_,
                                                 blockchain_,
                                                 sgns::NodeType::Light,
                                                 0,
                                                 kTimestampTolerance,
                                                 kMutabilityWindow );
            ASSERT_NE( tm_, nullptr );
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

        //
        // Tx seeding + pruner drive (the cold_prune_gate_test recipes
        // verbatim - the SC3 legs' machinery).
        //

        /// Transfer with @p output_count outputs owned by this account.
        std::shared_ptr<sgns::TransferTransaction> MakeTransfer( uint64_t nonce, size_t output_count )
        {
            SGTransaction::DAGStruct dag;
            dag.set_type( "transfer" );
            dag.set_source_addr( account_->GetAddress() );
            dag.set_nonce( nonce );
            dag.set_timestamp( static_cast<int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch() )
                    .count() ) );

            std::vector<OutputDestInfo> destinations;
            destinations.reserve( output_count );
            for ( size_t i = 0; i < output_count; ++i )
            {
                destinations.push_back( { 100 + i, account_->GetAddress(), kTestTokenId } );
            }
            return std::make_shared<sgns::TransferTransaction>(
                sgns::TransferTransaction::New( {}, std::move( destinations ), std::move( dag ) ) );
        }

        /// Real-topic value Put + raw tx->CID map entry (the gate-test
        /// StoreTransactionCID shapes).
        void SeedTransaction( const std::shared_ptr<sgns::GeniusTransaction> &transaction )
        {
            const std::string tx_path = sgns::TransactionManager::GetTransactionPath( *transaction );

            crdt::GlobalDB::Buffer serialized;
            serialized.put( transaction->SerializeByteVector() );
            auto put_result = db_->Put( { tx_path }, serialized, { kSeedTopic } );
            ASSERT_TRUE( put_result.has_value() );

            crdt::GlobalDB::Buffer key_buffer;
            key_buffer.put( tx_path );
            crdt::GlobalDB::Buffer value_buffer;
            value_buffer.put( put_result.value().toString().value() );
            ASSERT_TRUE( db_->GetDataStore()->put( key_buffer, value_buffer ).has_value() );
        }

        /// Plain topic Put of a raw string at @p key (driver heights).
        void PutSeedValue( const std::string &key, const std::string &value )
        {
            crdt::GlobalDB::Buffer buffer;
            buffer.put( value );
            ASSERT_TRUE( db_->Put( { key }, buffer, { kSeedTopic } ).has_value() );
        }

        /// Tracks a candidate's produced outputs in the UTXO manager.
        void TrackOutputs( const sgns::GeniusTransaction &transaction )
        {
            auto outputs = transaction.GetProducedUTXOs();
            ASSERT_TRUE( outputs.has_value() );
            for ( const auto &output : *outputs )
            {
                auto put = account_->GetUTXOManager().PutUTXO( output, output.GetOwnerAddress() );
                ASSERT_TRUE( put.has_value() );
                ASSERT_TRUE( put.value() );
            }
        }

        /// Spends one output by @p spender; returns ConsumeUTXOs's verdict.
        bool ConsumeOutput( const sgns::GeniusTransaction &transaction,
                            uint32_t                       output_idx,
                            const sgns::GeniusTransaction &spender )
        {
            auto txid = sgns::base::Hash256::fromReadableString( transaction.GetHash() );
            EXPECT_FALSE( txid.has_error() );
            auto consumed = account_->GetUTXOManager().ConsumeUTXOs( { { txid.value(), output_idx, {} } },
                                                                     account_->GetAddress(),
                                                                     sgns::UTXOManager::UTXOType::UTXO_NORMAL,
                                                                     SpenderHash( spender ) );
            EXPECT_TRUE( consumed.has_value() );
            return consumed.has_value() && consumed.value();
        }

        static sgns::base::Hash256 SpenderHash( const sgns::GeniusTransaction &transaction )
        {
            return sgns::base::Hash256::fromReadableString( transaction.GetHash() ).value();
        }

        /// The raw tx->CID map entry bytes at @p tx_path ("absent" when
        /// gone) - the group's constituent-CID source.
        std::string SnapshotMapEntry( const std::string &tx_path )
        {
            auto entry = db_->GetRaw( crdt::GlobalDB::Buffer{}.put( tx_path ) );
            return entry.has_value() ? std::string( entry.value().toString() ) : std::string( "absent" );
        }

        /// Constructs a Light TxPruner with the suite's window and
        /// dry_run=false.
        std::shared_ptr<sgns::TxPruner> MakePruner( uint64_t k_budget, uint64_t settle_seconds )
        {
            sgns::PruningConfig config;
            config.enabled              = true;
            config.dry_run              = false;
            config.finality_window      = kWindow;
            config.max_erases_per_cycle = k_budget;
            config.settle_seconds       = settle_seconds;
            auto pruner                 = sgns::TxPruner::New( sgns::NodeType::Light, config );
            EXPECT_TRUE( pruner.has_value() );
            return pruner.has_value() ? pruner.value() : nullptr;
        }

        /// Drives the manager into the tick gate's READY state (the
        /// friend seam).
        void ForceManagerReady()
        {
            TransactionManagerPendingLifecycleTestAccess::ChangeState( *tm_,
                                                                       sgns::TransactionManager::State::READY );
            ASSERT_EQ( tm_->GetState(), sgns::TransactionManager::State::READY );
        }

        /// Runs cycles until one is NOT gate-skipped, delivering its
        /// stats (the fresh-instance watermark discriminator).
        void RunSettledCycle( sgns::TxPruner &pruner, sgns::PruneCycleStats &out_stats )
        {
            ForceManagerReady();
            bool cycle_failed = false;
            bool ran          = false;
            ASSERT_WAIT_FOR_CONDITION(
                [&]
                {
                    auto result = pruner.RunCycle( *db_, *tm_, account_->GetUTXOManager(), kTestGeneration );
                    if ( result.has_error() )
                    {
                        cycle_failed = true;
                        return true; // exit the bounded wait; EXPECT below fails loudly
                    }
                    out_stats = result.value();
                    ran       = pruner.GetPruneStatus().watermark.has_value();
                    return ran;
                },
                std::chrono::milliseconds( kSmallSettleSeconds * 1000 + 8000 ),
                "tick gate settles and one prune cycle runs to completion",
                nullptr );
            EXPECT_FALSE( cycle_failed );
            EXPECT_TRUE( ran );
        }

        std::shared_ptr<boost::asio::io_context>                  io_;
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>          pubs_;
        std::shared_ptr<libp2p::basic::SchedulerImpl>             scheduler_;
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network> network_;
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator_;
        std::shared_ptr<sgns::crdt::GlobalDB>                      db_;
        std::shared_ptr<sgns::blockstore::InMemoryS3Client>       fake_s3_;
        std::shared_ptr<sgns::blockstore::InMemoryS3AdminClient>  fake_admin_;
        /// The hot dir of the CURRENT (or last torn-down) stack - S3b's
        /// journal-seeding derives its coldstore path from this.
        std::string                                                 last_hot_dir_;

        std::shared_ptr<sgns::GeniusAccount>      account_;
        std::shared_ptr<sgns::Blockchain>         blockchain_;
        std::shared_ptr<sgns::TransactionManager> tm_;
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

    //
    // Journal-CF seeding machinery (copied VERBATIM from
    // cold_prune_gate_test.cpp per 16-PATTERNS Layer 2's prescription -
    // the NAMED EXCEPTION to CLAUDE.md hard invariant 2, named in the
    // Task-2 commit message): deterministic arbitrary-state injection
    // against the coldstore's PUBLISHED upload_journal column family.
    // LOCK DISCIPLINE: legal ONLY while the fixture's GlobalDB is CLOSED.
    //

    /// The journal record codec (verbatim from LocalBlockStore.cpp):
    /// [version-byte 0x01][state-byte] with 0x00=PendingUpload,
    /// 0x01=UploadedAccepted, 0x02=UploadedDurable.
    const std::string kJournalDurable = std::string{ '\x01', '\x02' };

    /// The journal key for a CID string: byte-identical to
    /// LocalBlockStore's MakeKey (libp2p ContentIdentifierCodec::encode).
    std::string JournalKeyOf( const std::string &cid_string )
    {
        auto cid     = sgns::CID::fromString( cid_string );
        EXPECT_FALSE( cid.has_error() );
        auto encoded = libp2p::multi::ContentIdentifierCodec::encode( cid.value() );
        EXPECT_FALSE( encoded.has_error() );
        return std::string( encoded.value().begin(), encoded.value().end() );
    }

    /**
     * @brief  Rewrites (or deletes, when @p value_bytes is nullopt) one
     *         CID's record in the coldstore's upload_journal CF. Asserts
     *         the record EXISTS first (fixture hygiene) and that the
     *         journal is non-empty. Requires the GlobalDB over @p
     *         coldstore_dir's hot dir to be CLOSED (RocksDB dir lock).
     * @return true on success (false + ADD_FAILURE on any RocksDB error).
     */
    bool SeedJournalRecord( const std::string        &coldstore_dir,
                            const std::string        &cid_string,
                            std::optional<std::string> value_bytes )
    {
        const std::string key = JournalKeyOf( cid_string );

        rocksdb::Options                 list_options;
        std::vector<std::string>         cf_names;
        rocksdb::Status                  status = rocksdb::DB::ListColumnFamilies( list_options, coldstore_dir, &cf_names );
        if ( !status.ok() )
        {
            ADD_FAILURE() << "SeedJournalRecord: ListColumnFamilies failed at " << coldstore_dir << ": "
                          << status.ToString();
            return false;
        }
        std::vector<rocksdb::ColumnFamilyDescriptor> descriptors;
        for ( const auto &name : cf_names )
        {
            descriptors.emplace_back( name, rocksdb::ColumnFamilyOptions() );
        }
        rocksdb::DB                                 *raw_db = nullptr;
        std::vector<rocksdb::ColumnFamilyHandle *>  handles;
        status = rocksdb::DB::Open( rocksdb::DBOptions(), coldstore_dir, descriptors, &handles, &raw_db );
        if ( !status.ok() )
        {
            ADD_FAILURE() << "SeedJournalRecord: Open failed at " << coldstore_dir << ": " << status.ToString();
            return false;
        }
        std::unique_ptr<rocksdb::DB> db( raw_db );
        rocksdb::ColumnFamilyHandle *journal = nullptr;
        for ( auto *handle : handles )
        {
            if ( handle->GetName() == sgns::blockstore::kUploadJournalColumnFamilyName )
            {
                journal = handle;
            }
        }
        if ( journal == nullptr )
        {
            ADD_FAILURE() << "SeedJournalRecord: upload_journal CF not found at " << coldstore_dir;
            for ( auto *handle : handles )
            {
                delete handle;
            }
            return false;
        }

        std::string current;
        status = db->Get( rocksdb::ReadOptions(), journal, key, &current );
        if ( !status.ok() )
        {
            ADD_FAILURE() << "SeedJournalRecord: target record absent before seeding (populate bug): "
                          << status.ToString();
        }
        size_t record_count = 0;
        {
            std::unique_ptr<rocksdb::Iterator> it( db->NewIterator( rocksdb::ReadOptions(), journal ) );
            for ( it->SeekToFirst(); it->Valid(); it->Next() )
            {
                ++record_count;
            }
        }
        EXPECT_GE( record_count, 1u ) << "SeedJournalRecord: empty journal (populate bug)";

        if ( value_bytes.has_value() )
        {
            status = db->Put( rocksdb::WriteOptions(), journal, key, value_bytes.value() );
        }
        else
        {
            status = db->Delete( rocksdb::WriteOptions(), journal, key );
        }
        const bool ok = status.ok();
        if ( !ok )
        {
            ADD_FAILURE() << "SeedJournalRecord: seed write failed: " << status.ToString();
        }
        for ( auto *handle : handles )
        {
            delete handle;
        }
        return ok;
    }

    /**
     * LEG S3a (16-02 Task 2, COMP-03/D-10/D-11a - authored RED-FIRST
     * against the consult-absent line): a REAL hermetic GC disposal
     * (GlobalDB's own GC thread over the InMemory fakes - the genuine
     * post-disposal state: journal record deleted, disposed marker
     * written) strands the subsequent cycle's group behind the SPLIT
     * counter, erases NOTHING, and the last hot copy of the value
     * survives.
     *
     * Disposal mechanics (the cold_storage_gc_test recipe, live-stack
     * variant): the group's delta block is published LAST on the topic
     * (it IS the head), awaited to Durable, then RemoveHead detaches it -
     * the retention target (a REAL delta with real s-rows and seal
     * state that LOST head-reachability). The subsequent driver puts
     * start fresh roots (no parent links), so the detached block is
     * walk-unreachable and the tiny-age GC thread disposes it
     * (PollUntil-bounded on IsColdDisposed - the new bridge itself).
     */
    TEST_F( ColdComposeMatrixTest, S3a_DisposedGroupStrandsBehindSplitCounter )
    {
        // Retention 1ms + interval 50ms + grace 1ms: the GC thread runs.
        BuildStack( /*retention_age_ms=*/1 );
        ASSERT_NE( db_, nullptr );

        // The eligibility shape (the gate-test LEG-1 recipe, re-ordered so
        // the ERASABLE group's delta is the LAST chain link before the
        // detach): spender first, then the eligible, then the detach.
        auto spender    = MakeTransfer( 1, 0 );
        auto eligible_a = MakeTransfer( 2, 1 );

        SeedTransaction( spender );
        SeedTransaction( eligible_a );

        TrackOutputs( *eligible_a );
        ASSERT_TRUE( ConsumeOutput( *eligible_a, 0, *spender ) );

        const std::string cid_a = SnapshotMapEntry( sgns::TransactionManager::GetTransactionPath( *eligible_a ) );
        ASSERT_NE( cid_a, "absent" );
        auto cid_a_parsed = sgns::CID::fromString( cid_a );
        ASSERT_FALSE( cid_a_parsed.has_error() );
        const sgns::CID detached_cid = cid_a_parsed.value();

        // Real transitions to Durable BEFORE the detach (the spill record
        // + timestamp the GC's eligibility scan reads).
        ASSERT_TRUE( WaitDurable( detached_cid ) ) << "eligible's block never reached Durable";

        // Detach the head: the eligible's delta becomes walk-unreachable
        // (the retention target - the cold_storage_gc_test RemoveHead
        // recipe). The value row (k-namespace) and the raw tx->CID map
        // entry survive the detach.
        auto crdt_datastore = db_->GetCRDTDataStore();
        ASSERT_NE( crdt_datastore, nullptr );
        auto detached = crdt_datastore->RemoveHead( detached_cid, kSeedTopic );
        ASSERT_TRUE( detached.has_value() ) << "RemoveHead failed";

        // Fresh roots: the driver puts publish AFTER the detach (empty
        // head set, heights restarting at 1), so nothing links back to
        // the detached delta. FOUR puts regrow max_height past
        // spend_height(2) + window(2) - the window arithmetic needs
        // max_height - spend_height >= kWindow (the height restart is
        // the price of the detach; the count is load-bearing).
        PutSeedValue( "/seed/s3a/driver-1", "d1" );
        PutSeedValue( "/seed/s3a/driver-2", "d2" );
        PutSeedValue( "/seed/s3a/driver-3", "d3" );
        PutSeedValue( "/seed/s3a/driver-4", "d4" );

        // The genuine hermetic disposal: GlobalDB's OWN GC thread (deps
        // attached at Initialize, retention armed above) marks and evicts
        // the detached block - journal record deleted + disposed marker
        // written + hot cleanup committed (PollUntil-bounded).
        ASSERT_TRUE( PollUntil( [&] { return db_->IsColdDisposed( detached_cid ); },
                                std::chrono::milliseconds( 20000 ) ) )
            << "the GC thread never disposed the detached block (SC3-UNDIFFERENTIATED setup)";
        ASSERT_EQ( db_->GetColdUploadState( detached_cid ), crdt::ColdUploadState::NoRecord )
            << "disposal must leave the journal record deleted (NoRecord)";

        // The remote-unreachable posture at cycle time (the stranded
        // scenario's steady state): the evaluator's own block-gate parse
        // read (GetLocalDeltaKeys -> GetNodeWithoutRequest) would
        // otherwise re-hydrate the disposed block from the REACHABLE
        // remote and re-journal it (Pending - the correct self-healing
        // production path, which a later cycle then prunes normally).
        // Faulting the fake's Get models the remote-unreachable node:
        // the fetch fails, the block verdict takes the
        // BLOCK_ALREADY_ERASED branch, and the gate reads the journal
        // exactly as the disposal left it.
        fake_s3_->SetFault( sgns::blockstore::S3Op::Get, sgns::blockstore::S3FaultKind::Unavailable );

        auto pruner = MakePruner( 10, kSmallSettleSeconds );
        ASSERT_NE( pruner, nullptr );

        sgns::PruneCycleStats stats;
        RunSettledCycle( *pruner, stats );

        // The cycle must not have re-journaled the disposed block (the
        // faulted fetch posture): the gate's classification input is the
        // disposal's own NoRecord.
        EXPECT_EQ( db_->GetColdUploadState( detached_cid ), crdt::ColdUploadState::NoRecord )
            << "the cycle must not re-journal the disposed block under the faulted-fetch posture";

        // THE COMP-03 CONTRACT: under-prune, never destruction. The
        // disposed group strands behind the SPLIT counter and erases
        // NOTHING; the value row (the last hot copy) survives.
        EXPECT_EQ( stats.erased, 0u ) << "SC3-UNDIFFERENTIATED: a disposed group must never erase";
        EXPECT_EQ( stats.cold_disposed_stranded_groups, 1u )
            << "SC3-UNDIFFERENTIATED: the disposed group must count into the stranded counter";
        EXPECT_EQ( stats.cold_deferred_groups, 0u )
            << "SC3-UNDIFFERENTIATED: the disposed group must NOT count into the undifferentiated deferral";
        const std::string tx_path = sgns::TransactionManager::GetTransactionPath( *eligible_a );
        EXPECT_TRUE( db_->Get( { tx_path } ).has_value() ) << "the last hot copy of the value must survive";

        // The disposed marker's existence is asserted ABOVE through the
        // production consult chain itself (the PollUntil on
        // IsColdDisposed - GlobalDB -> ColdStorageManager::IsDisposed ->
        // LocalBlockStore::IsDisposed, the disposed-CF point lookup the
        // GetDisposed enumeration would read). A post-teardown
        // LocalBlockStore reopen for the full enumeration is NOT
        // possible in the retention posture: after a GC-bearing
        // GlobalDB shuts down, an in-process object retains the
        // coldstore's RocksDB handle indefinitely (diagnosed: raw
        // DB::Open fails with "LOCK ... being used by another process"
        // past 45 s, while the identical teardown WITHOUT retention
        // releases cleanly - S3b's direct RocksDB reopen works). The
        // leak is recorded as a deferred hardening item in the plan
        // SUMMARY; benign in production (the store outlives shutdown
        // interest), load-bearing only for this belt-and-braces probe.
    }

    /**
     * LEG S3b (16-02 Task 2) - the journal-deletion negative control: a
     * NoRecord constituent with NO disposed marker (the never-spilled
     * shape, kit-simulated by deleting the journal record directly)
     * keeps TODAY'S cold_deferred_groups classification - the split's
     * negative control, and proof the consult answers not-disposed on a
     * marker miss.
     */
    TEST_F( ColdComposeMatrixTest, S3b_JournalDeletionWithoutMarkerKeepsDeferredClassification )
    {
        BuildStack(); // retention 0 - the GC never runs
        ASSERT_NE( db_, nullptr );

        auto spender    = MakeTransfer( 1, 0 );
        auto eligible_a = MakeTransfer( 2, 1 );

        SeedTransaction( spender );
        SeedTransaction( eligible_a );
        PutSeedValue( "/seed/s3b/driver", "d" );

        TrackOutputs( *eligible_a );
        ASSERT_TRUE( ConsumeOutput( *eligible_a, 0, *spender ) );

        const std::string cid_a = SnapshotMapEntry( sgns::TransactionManager::GetTransactionPath( *eligible_a ) );
        ASSERT_NE( cid_a, "absent" );

        auto pruner = MakePruner( 10, kSmallSettleSeconds );
        ASSERT_NE( pruner, nullptr );

        // PHASE A tail: real transitions to Durable for the constituent.
        auto cid_a_parsed = sgns::CID::fromString( cid_a );
        ASSERT_FALSE( cid_a_parsed.has_error() );
        ASSERT_TRUE( WaitDurable( cid_a_parsed.value() ) );

        // PHASE B: close the stack, delete the journal record (the
        // kit's nullopt-deletes contract) - NoRecord with NO marker.
        TeardownStack();
        ASSERT_TRUE( SeedJournalRecord( last_hot_dir_ + "/coldstore", cid_a, std::nullopt ) )
            << "journal-record deletion failed";

        // PHASE C: reopen over the SAME hot dir and drive one cycle.
        BuildStackOnDir( last_hot_dir_ );
        ASSERT_NE( db_, nullptr );
        ASSERT_EQ( db_->GetColdUploadState( cid_a_parsed.value() ), crdt::ColdUploadState::NoRecord )
            << "the deleted record must read NoRecord after the reopen";

        auto pruner_c = MakePruner( 10, kSmallSettleSeconds );
        ASSERT_NE( pruner_c, nullptr );

        sgns::PruneCycleStats stats;
        RunSettledCycle( *pruner_c, stats );

        // The negative control: NoRecord WITHOUT a marker keeps today's
        // classification - deferred, never stranded, never erased.
        EXPECT_EQ( stats.erased, 0u );
        EXPECT_EQ( stats.cold_deferred_groups, 1u )
            << "SC3-UNDIFFERENTIATED: marker-miss NoRecord must keep the cold_deferred classification";
        EXPECT_EQ( stats.cold_disposed_stranded_groups, 0u );
        const std::string tx_path = sgns::TransactionManager::GetTransactionPath( *eligible_a );
        EXPECT_TRUE( db_->Get( { tx_path } ).has_value() ) << "the deferred group's value must survive";
    }

    /**
     * LEG S3c (16-02 Task 2) - the Pending no-consult control: a disposed
     * constituent (real marker present from the hermetic disposal) whose
     * journal view the state hook overrides to Pending NEVER reaches the
     * consult - the Pending arm defers even when a marker exists (the
     * misclassification vector closed; the plan's accepted simplification:
     * marker from the prior disposal, journal view overridden via the
     * Phase-14 D-07 injector).
     */
    TEST_F( ColdComposeMatrixTest, S3c_PendingArmNeverConsultsEvenWithMarker )
    {
        BuildStack( /*retention_age_ms=*/1 );
        ASSERT_NE( db_, nullptr );

        auto spender    = MakeTransfer( 1, 0 );
        auto eligible_a = MakeTransfer( 2, 1 );

        SeedTransaction( spender );
        SeedTransaction( eligible_a );

        TrackOutputs( *eligible_a );
        ASSERT_TRUE( ConsumeOutput( *eligible_a, 0, *spender ) );

        const std::string cid_a = SnapshotMapEntry( sgns::TransactionManager::GetTransactionPath( *eligible_a ) );
        ASSERT_NE( cid_a, "absent" );
        auto cid_a_parsed = sgns::CID::fromString( cid_a );
        ASSERT_FALSE( cid_a_parsed.has_error() );
        const sgns::CID detached_cid = cid_a_parsed.value();

        ASSERT_TRUE( WaitDurable( detached_cid ) );

        auto crdt_datastore = db_->GetCRDTDataStore();
        ASSERT_NE( crdt_datastore, nullptr );
        ASSERT_TRUE( crdt_datastore->RemoveHead( detached_cid, kSeedTopic ).has_value() );

        PutSeedValue( "/seed/s3c/driver-1", "d1" );
        PutSeedValue( "/seed/s3c/driver-2", "d2" );
        PutSeedValue( "/seed/s3c/driver-3", "d3" );
        PutSeedValue( "/seed/s3c/driver-4", "d4" );

        // The real disposal: the marker EXISTS after this poll.
        ASSERT_TRUE( PollUntil( [&] { return db_->IsColdDisposed( detached_cid ); },
                                std::chrono::milliseconds( 20000 ) ) )
            << "the GC thread never disposed the detached block";

        // The journal view override: the state hook answers Pending for
        // the disposed CID - the gate must take the Pending arm (defer)
        // and NEVER consult the marker (Phase-14 D-07 injector, the
        // plan's accepted simplification).
        crdt::GlobalDB::SetColdUploadStateHookForTest(
            [detached_cid]( const sgns::CID &cid ) -> std::optional<crdt::ColdUploadState> {
                if ( cid == detached_cid )
                {
                    return crdt::ColdUploadState::Pending;
                }
                return std::nullopt;
            } );

        auto pruner = MakePruner( 10, kSmallSettleSeconds );
        ASSERT_NE( pruner, nullptr );

        sgns::PruneCycleStats stats;
        RunSettledCycle( *pruner, stats );

        crdt::GlobalDB::SetColdUploadStateHookForTest( nullptr );

        // Pending never consults: deferred, never stranded, never erased.
        EXPECT_EQ( stats.erased, 0u );
        EXPECT_EQ( stats.cold_deferred_groups, 1u )
            << "SC3-UNDIFFERENTIATED: the Pending arm must defer without consulting the marker";
        EXPECT_EQ( stats.cold_disposed_stranded_groups, 0u );
        const std::string tx_path = sgns::TransactionManager::GetTransactionPath( *eligible_a );
        EXPECT_TRUE( db_->Get( { tx_path } ).has_value() );
    }
} // namespace
