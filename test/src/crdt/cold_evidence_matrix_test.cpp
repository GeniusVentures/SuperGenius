/**
 * @file       cold_evidence_matrix_test.cpp
 * @brief      Evidence-aware cold-serving matrix (v1.2 Phase 15 plan
 *             15-01 Task 2 / SERVE-01, SERVE-02, SERVE-04, TEST-01):
 *             "evidence outranks bytes" on every cold serve surface -
 *             a /prune/blk marker refuses the serve even when the cold
 *             chain HOLDS the bytes, malformed evidence fails closed,
 *             no-evidence cold bytes serve + re-warm verbatim (evict =
 *             DETACH), and the cold-DISABLED build never consults
 *             evidence at all (the Phase-7 byte-identical bar).
 *
 *             Fixture posture (TEST-01 PRIMARY, Phase 14 D-08 lineage):
 *             a REAL cold-enabled GlobalDB (real RocksDB hot store +
 *             real coldstore journal) over
 *             SetColdStorageStackClientsForTesting-injected
 *             InMemoryS3Client/InMemoryS3AdminClient fakes with endpoint
 *             "hermetic.invalid" (never contacted), hygiene-cleared
 *             immediately after construction. Cold-only blocks arrive
 *             through the PHASE P / PHASE B flow: a THROWAWAY publisher
 *             stack publishes real delta blocks through the REAL topic
 *             Put path and captures (CID, raw stored bytes); the
 *             fixture node's coldstore is then seeded directly via
 *             LocalBlockStore::Put BEFORE the fixture GlobalDB is ever
 *             constructed - the fixture node never sees the blocks hot
 *             (the SC1 cold-bytes-present and SC2 evict-equals-DETACH
 *             postures, with zero hot-store surgery). /prune/blk marker
 *             rows are seeded onto the LIVE raw surface
 *             (WritePrunedBlockEntryForTest / direct PutRaw) - which
 *             also PINS the two prefix homes together: the test writes
 *             through PruneEligibility::PRUNE_BLOCK_MARKER_PREFIX while
 *             the production consult reads through globaldb.cpp's
 *             file-local mirror; a drifted prefix on either side fails
 *             these legs loudly (the layering ruling's guard).
 *
 *             LEG S1 - SC1 refusal with cold bytes present (the
 *             resurrection-gap kill): getNode and GetNodeWithoutRequest
 *             both answer Error::HISTORY_PRUNED_LOCALLY, the hot store
 *             never receives the bytes (no Rehydrate), repeat reads are
 *             stable (the stateless point-lookup posture), and the
 *             no-evidence sibling still serves (per-CID evidence, not a
 *             stack outage).
 *
 *             LEG S2 - SC2 serve + rehydrate without evidence: the
 *             v1.1 serve body runs VERBATIM - getNode resolves the CID,
 *             the returned node's CID matches, and the block is hot
 *             afterwards (Rehydrate happened).
 *
 *             LEG S3 - full-stack evidence production (D-01/D-02
 *             end-to-end): a real TransactionManager + TxPruner
 *             RunCycle (the Phase-14 gate-test recipe) erases a
 *             Durable-backed transaction; ONE commit stages the /prune/tx
 *             AND the /prune/blk rows; the block record carries the tx
 *             hash back-reference and the RunCycle generation tag; the
 *             cold bytes survive; getNode for the erased CID now refuses
 *             with HISTORY_PRUNED_LOCALLY - evidence built by the
 *             production path, read by the production consult.
 *
 *             LEG S4 - malformed fail-closed (Pitfall 7 / T-17-0502):
 *             garbage bytes AND a zero-length /prune/blk value both
 *             answer Error::PRUNE_EVIDENCE_CORRUPT on both read
 *             surfaces - never a serve, never a silent NoEvidence.
 *
 *             LEG S5 - disabled-build byte-identical (Phase-7 bar): a
 *             cold-disabled GlobalDB carries a well-formed marker row
 *             yet getNode answers the PRE-PHASE not-found shape -
 *             ROUTE_NOT_FOUND or CID_NOT_FOUND, never either refusal
 *             value (the null hook is never consulted).
 *
 *             The responder-side third surface (the re-warm worker
 *             gate) runs on the cold_storage_fallback_test kit: a
 *             directly-constructed ColdReadService over a seeded
 *             LocalBlockStore + InMemory S3 fake + InMemory hot store,
 *             with the evidence predicate installed through the
 *             production SetPruneEvidenceLookup seam (TEST-01
 *             SECONDARY - the table-driven stub for the syncer-layer
 *             classification, RESEARCH's sanctioned split).
 *
 *             Tier-1 (Phase 14 D-09 contract): hermetic, deterministic,
 *             fresh-clone-safe, zero network, zero services.
 * @date       2026-11-17
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
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
#include <ipfs_lite/ipfs/impl/in_memory_datastore.hpp>
#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <libp2p/basic/scheduler.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <libp2p/host/host.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>
#include <libp2p/multi/multicodec_type.hpp>
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
#include "crdt/cold_read_fallback.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/globaldb/BlockStoreBackedIpfsDatastore.hpp"
#include "crdt/globaldb/ColdReadService.hpp"
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
     * TxPruner tick gate requires State::READY, which no public API reaches
     * deterministically. This definition carries only ChangeState — the one
     * member this binary uses (LEG S3's settled-cycle drive).
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
    const sgns::TokenID kTestTokenId = sgns::TokenID::FromBytes( { 0x00 } );

    /// The fixture's pre-seeded CRDT topic (the tx_pruner_test contract):
    /// puts on it create real blocks, heads, and seals.
    constexpr char kSeedTopic[] = "CRDT.Datastore.TEST.Channel";

    /// Finality window N (the 17-04 recipes' value).
    constexpr uint64_t kWindow = 2;

    /// The account-generation tag the tests stamp onto cycles (D-C4) -
    /// LEG S3 asserts it lands in the /prune/blk record (D-02).
    constexpr uint64_t kTestGeneration = 7;

    /// Settle window: waited out through the settled-cycle helper.
    constexpr uint64_t kSmallSettleSeconds = 1;

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
        return std::string( "test-data/cold-evidence-matrix/" ) + lane + "-"
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
  - name: cold_evidence_matrix_test
    sink: console
    level: error
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )";
    }

    /// Test-fabricated CID (never published, never sealed) - the LEG S5
    /// unseen-CID shape (the globaldb_erase_test makeUnseenCid recipe).
    sgns::CID makeUnseenCid( const std::vector<uint8_t> &digest )
    {
        using libp2p::multi::HashType;
        using libp2p::multi::Multihash;
        return sgns::CID( sgns::CID::Version::V1,
                          sgns::CID::Multicodec::SHA2_256,
                          Multihash::create( HashType::sha256, digest ).value() );
    }

    gsl::span<const uint8_t> SpanOf( const std::string &s )
    {
        return gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( s.data() ), s.size() );
    }

    /// Verified CID for a payload (library constructor, RAW codec) - the
    /// re-warm leg's cold-tier seeding shape (the fallback-test recipe).
    sgns::blockstore::Cid MakeCid( const std::string &payload )
    {
        auto cid = sgns::blockstore::CidOf( SpanOf( payload ), libp2p::multi::MulticodecType::Code::RAW );
        return cid.value(); // guarded by construction - payload is valid
    }

    /// Raw block-bytes key (vendored contract: Buffer(cid.toBytes()) is the
    /// one home of the block-key format, datastore_rocksdb.cpp encode()).
    sgns::base::Buffer blockKeyOf( const sgns::CID &cid )
    {
        return sgns::base::Buffer( cid.toBytes().value() );
    }

    //
    // The PRIMARY fixture: a REAL cold-enabled GlobalDB stack (the
    // cold_prune_gate_test kit posture) + the cold-only-block seeding
    // flow.
    //
    class ColdEvidenceMatrixTest : public ::testing::Test
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

        ~ColdEvidenceMatrixTest() override
        {
            TeardownStack();
        }

        /// The stack teardown sequence (the cold_prune_gate discipline
        /// copied wholesale): clear seams, stop the blockchain, release
        /// the manager stack, quiesce the GlobalDB (stops the cold drainer
        /// + GC threads and releases the coldstore RocksDB dir lock), stop
        /// the pubsub host LAST (the CRDTFixture invariant).
        void TeardownStack()
        {
            // Fixture discipline: a hook armed by a failing case must never
            // leak further - clear EVERY static seam before any teardown
            // touches the stores.
            sgns::TxPruner::SetCycleInterruptHookForTest( nullptr );
            crdt::GlobalDB::SetColdUploadStateHookForTest( nullptr );
            crdt::GlobalDB::SetEraseInterruptHookForTest( nullptr );

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
        // Stack construction (the cold_prune_gate_test recipe verbatim).
        //

        /// Builds the single-node stack on a FRESH dir. @p cold_enabled
        /// installs the hermetic cold tier (fake S3 + fake admin,
        /// "hermetic.invalid"); @p drain_interval_ms shapes the journal's
        /// determinism: 50 ms = real transitions fire fast (LEG S3),
        /// 60000 ms = seeded records persist held (the matrix legs).
        void BuildStack( bool cold_enabled, uint32_t drain_interval_ms, bool inject_hermetic_clients = true )
        {
            BuildStackOnDir( cold_enabled, drain_interval_ms, FreshDir( "hot" ), inject_hermetic_clients );
        }

        /// Same as BuildStack but on an EXISTING hot dir (the pre-seeded
        /// coldstore's reopen: the directly-seeded cold bytes survive the
        /// open and the cold chain serves them).
        void BuildStackOnDir( bool               cold_enabled,
                              uint32_t           drain_interval_ms,
                              const std::string &hot_dir,
                              bool               inject_hermetic_clients = true )
        {
            keypair_path_ = FreshDir( "keypair" );

            io_ = std::make_shared<boost::asio::io_context>();

            pubs_ = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>(
                crdt::KeyPairFileStorage( keypair_path_ ).GetKeyPair().value() );
            ASSERT_NE( pubs_, nullptr );
            auto start_future = pubs_->Start( 0, {} );
            ASSERT_TRUE( PollUntil(
                [&] { return start_future.wait_for( std::chrono::milliseconds( 0 ) ) == std::future_status::ready; },
                std::chrono::milliseconds( 20000 ) ) )
                << "pubsub host failed to start";
            ASSERT_FALSE( start_future.get() ) << "GossipPubSub::Start failed";

            scheduler_ = std::make_shared<libp2p::basic::SchedulerImpl>(
                std::make_shared<libp2p::basic::AsioSchedulerBackend>( pubs_->GetAsioContext() ),
                libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );
            network_   = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubs_->GetHost(), scheduler_ );
            generator_ = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();

            // --- The hermetic cold posture (the resurrection recipe):
            // cold ON through the TEST-ONLY client override with the
            // never-contacted endpoint; cold OFF passes default options.
            crdt::GlobalDB::ColdStorageOptions cold;
            if ( cold_enabled )
            {
                cold.enabled   = true;
                cold.endpoint  = "hermetic.invalid"; // overridden below - never contacted
                cold.bucket    = "evidence-matrix";
                cold.accessKey = "test";
                cold.secretKey = "test";
                cold.backpressure.drain_interval_ms = drain_interval_ms;
                cold.backpressure.warn_pending      = 100000;
                cold.backpressure.critical_pending  = 200000;
                cold.backpressure.backoff_cap_ms    = 1000;
                // FastDurability (50/300 ms): LEG S3's Durable waits.
                // Retention stays 0 = the GC never runs.
                cold.durability.flush_min_interval_ms = 50;
                cold.durability.flush_max_interval_ms = 300;
                fake_s3_    = std::make_shared<sgns::blockstore::InMemoryS3Client>();
                fake_admin_ = std::make_shared<sgns::blockstore::InMemoryS3AdminClient>();
                if ( !inject_hermetic_clients )
                {
                    // The HELD-journal posture: the same hermetic fakes,
                    // faulted - no drain pass or flush cycle can advance
                    // any journal record (seed determinism).
                    fake_s3_->SetFault( sgns::blockstore::S3Op::Put,
                                        sgns::blockstore::S3FaultKind::Unavailable );
                    fake_admin_->SetAdminFault( sgns::blockstore::S3AdminFaultKind::Unreachable );
                }
                sgns::crdt::SetColdStorageStackClientsForTesting(
                    std::static_pointer_cast<sgns::blockstore::S3Client>( fake_s3_ ),
                    std::static_pointer_cast<sgns::blockstore::S3AdminClient>( fake_admin_ ) );
            }

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
            (void)db_->Start(); // [[nodiscard]] - failure surfaces via the asserts below
            ASSERT_EQ( db_->HasColdStorage(), cold_enabled ) << "tier-presence signal mismatch";

            // The tx_pruner_test SetUp recipe over the cold-capable db.
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
        // Seeding helpers - the tx_pruner_test recipes verbatim (LEG S3).
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

        /// Real-topic value Put + raw tx->CID map entry.
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
                            uint32_t                        output_idx,
                            const sgns::GeniusTransaction  &spender )
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

        /// The raw tx->CID map entry bytes at @p tx_path ("absent" when gone).
        std::string SnapshotMapEntry( const std::string &tx_path )
        {
            auto entry = db_->GetRaw( crdt::GlobalDB::Buffer{}.put( tx_path ) );
            return entry.has_value() ? std::string( entry.value().toString() ) : std::string( "absent" );
        }

        //
        // Pruner helpers (the tx_pruner_test recipes; LEG S3).
        //

        /// Constructs a Light TxPruner with the suite's window and dry_run=false.
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

        /// Drives the manager into the tick gate's READY state (the friend seam).
        void ForceManagerReady()
        {
            TransactionManagerPendingLifecycleTestAccess::ChangeState( *tm_,
                                                                       sgns::TransactionManager::State::READY );
            ASSERT_EQ( tm_->GetState(), sgns::TransactionManager::State::READY );
        }

        /// Runs cycles until one is NOT gate-skipped, delivering its stats.
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
                        return true;
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

        /// Reads the journal state for a CID string (the gate's read path).
        sgns::crdt::ColdUploadState JournalStateOf( const std::string &cid_string )
        {
            auto cid = sgns::CID::fromString( cid_string );
            EXPECT_FALSE( cid.has_error() );
            return db_->GetColdUploadState( cid.value() );
        }

        //
        // The cold-only-block flow (PHASE P / PHASE B / single build).
        //

        /// A cold-only block: the CID + raw stored bytes of a REAL
        /// published delta block, captured from the throwaway publisher.
        struct ColdBlockSeed
        {
            sgns::CID            cid;
            std::string          cid_string;
            std::vector<uint8_t> bytes;
        };

        /// PHASE P: a full throwaway stack publishes @p count unique seed
        /// values through the REAL topic Put path; each delta block's CID
        /// and raw stored bytes (blockKeyOf - the vendored block-key
        /// contract) are captured, then the stack tears down. The fixture
        /// node NEVER sees these blocks hot - no hot-store surgery needed.
        void PublishColdBlocks( size_t count, std::vector<ColdBlockSeed> &out_seeds )
        {
            BuildStack( /*cold_enabled=*/true, /*drain_interval_ms=*/50, /*inject_hermetic_clients=*/true );
            ASSERT_NE( db_, nullptr );
            for ( size_t i = 0; i < count; ++i )
            {
                const std::string key = "/seed/evidence/publisher-" + std::to_string( i );
                crdt::GlobalDB::Buffer value;
                value.put( "cold-only publisher payload " + std::to_string( i ) );
                auto put_result = db_->Put( { key }, value, { kSeedTopic } );
                ASSERT_TRUE( put_result.has_value() );
                const sgns::CID cid = put_result.value();
                auto           bytes = db_->GetDataStore()->get( blockKeyOf( cid ) );
                ASSERT_TRUE( bytes.has_value() ) << "publisher block bytes unreadable";
                out_seeds.push_back( { cid, cid.toString().value(), bytes.value().toVector() } );
            }
            TeardownStack();
        }

        /// PHASE B: seed a (fresh) coldstore dir with the captured blocks
        /// via direct LocalBlockStore::Put - the same library surface the
        /// Phase-8 spill path writes through. Legal only while NO GlobalDB
        /// holds the dir (called before the fixture stack is built).
        void SeedColdstore( const std::string &coldstore_dir, const std::vector<ColdBlockSeed> &seeds )
        {
            auto opened = sgns::blockstore::LocalBlockStore::Open(
                sgns::blockstore::LocalBlockStoreOptions{ coldstore_dir } );
            ASSERT_TRUE( opened.has_value() ) << "coldstore seed open failed at " << coldstore_dir;
            auto local = std::shared_ptr<sgns::blockstore::LocalBlockStore>( std::move( opened.value() ) );
            for ( const auto &seed : seeds )
            {
                auto put = local->Put( seed.cid,
                                       gsl::span<const uint8_t>( seed.bytes.data(), seed.bytes.size() ) );
                ASSERT_TRUE( put.has_value() ) << "coldstore seed Put failed for " << seed.cid_string;
            }
        }

        /// The full flow: @p count cold-only blocks exist in the fixture
        /// node's coldstore; the fixture stack takes the HELD-journal
        /// posture (faulted hermetic fakes - the seeded journal records
        /// never advance; the LOCAL tier still serves the bytes, which is
        /// all the cold chain's first tier needs).
        void BuildFixtureWithColdBlocks( size_t count )
        {
            ASSERT_EQ( db_, nullptr ) << "BuildFixtureWithColdBlocks expects no live stack";
            std::vector<ColdBlockSeed> seeds;
            PublishColdBlocks( count, seeds );
            ASSERT_EQ( seeds.size(), count );
            const std::string hot_dir = FreshDir( "hot" );
            SeedColdstore( hot_dir + "/coldstore", seeds );
            BuildStackOnDir( /*cold_enabled=*/true,
                             /*drain_interval_ms=*/60000,
                             hot_dir,
                             /*inject_hermetic_clients=*/false );
            cold_seeds_ = std::move( seeds );
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
        std::string                                                keypair_path_;
        std::vector<ColdBlockSeed>                                 cold_seeds_;

        std::shared_ptr<sgns::GeniusAccount>      account_;
        std::shared_ptr<sgns::Blockchain>         blockchain_;
        std::shared_ptr<sgns::TransactionManager> tm_;
    };

    //
    // The SECONDARY fixture: the re-warm worker gate (the responder-side
    // third surface) on the cold_storage_fallback_test kit - a directly
    // constructed ColdReadService over a seeded LocalBlockStore +
    // InMemory S3 fake + InMemory hot store, with the evidence predicate
    // installed through the production SetPruneEvidenceLookup seam.
    //
    class ColdEvidenceRewarmTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            auto opened_local = sgns::blockstore::LocalBlockStore::Open(
                sgns::blockstore::LocalBlockStoreOptions{ FreshDir( "rewarm" ) } );
            ASSERT_TRUE( opened_local.has_value() );
            local_ = std::shared_ptr<sgns::blockstore::LocalBlockStore>( std::move( opened_local.value() ) );

            fake_ = std::make_shared<sgns::blockstore::InMemoryS3Client>();
            auto opened_remote = sgns::blockstore::S3BlockStore::Open( fake_ );
            ASSERT_TRUE( opened_remote.has_value() );
            remote_ = std::move( opened_remote.value() );

            cold_store_mutex_   = std::make_shared<std::mutex>();
            remote_store_mutex_ = std::make_shared<std::mutex>();
            inner_              = std::make_shared<sgns::ipfs_lite::ipfs::InMemoryDatastore>();
            decorator_          = std::make_shared<sgns::crdt::BlockStoreBackedIpfsDatastore>(
                inner_, local_, cold_store_mutex_, sgns::base::createLogger( "ColdEvidenceRewarm" ) );
            service_ = std::make_shared<sgns::crdt::ColdReadService>(
                local_, remote_, cold_store_mutex_, remote_store_mutex_, decorator_ );
        }

        void TearDown() override
        {
            if ( service_ )
            {
                service_->Stop();
            }
        }

        std::shared_ptr<sgns::blockstore::LocalBlockStore>               local_;
        std::shared_ptr<sgns::blockstore::InMemoryS3Client>              fake_;
        std::shared_ptr<sgns::blockstore::S3BlockStore>                  remote_;
        std::shared_ptr<std::mutex>                                      cold_store_mutex_;
        std::shared_ptr<std::mutex>                                      remote_store_mutex_;
        std::shared_ptr<sgns::ipfs_lite::ipfs::InMemoryDatastore>        inner_;
        std::shared_ptr<sgns::crdt::BlockStoreBackedIpfsDatastore>       decorator_;
        std::shared_ptr<sgns::crdt::ColdReadService>                     service_;
    };
} // namespace

/**
 * LEG S1 - SC1: evidence refuses a cold-bytes-present CID on every read
 * surface, with zero resurrection and per-CID discrimination.
 */
TEST_F( ColdEvidenceMatrixTest, Serve_EvidenceRefusalBeatsColdBytes )
{
    BuildFixtureWithColdBlocks( /*count=*/2 );
    ASSERT_NE( db_, nullptr );
    ASSERT_EQ( cold_seeds_.size(), 2u );

    const sgns::CID pruned_cid = cold_seeds_[0].cid;
    const sgns::CID sibling_cid = cold_seeds_[1].cid;

    // A well-formed /prune/blk row for the target CID - seeded through the
    // D-C1 home (also the prefix-parity pin: this write goes through
    // PruneEligibility's key builder while the production consult reads
    // through globaldb.cpp's file-local mirror; a drift on either side
    // flips this leg from refusal to serve and fails it loudly).
    ASSERT_TRUE( sgns::PruneEligibility::WritePrunedBlockEntryForTest(
        *db_, cold_seeds_[0].cid_string, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        /*generation=*/42 )
                    .has_value() );

    auto syncer = syncerOf( *db_ );

    // Pre-state: the block is cold-only (the hot store never held it).
    auto hot_before = syncer->HasStoredBlock( pruned_cid );
    ASSERT_TRUE( hot_before.has_value() );
    EXPECT_FALSE( hot_before.value() );

    // getNode refuses with the DISTINCT terminal value (SERVE-04/D-07 -
    // node-local terminality: no peer fall-through, no cold Get).
    auto refused = syncer->getNode( pruned_cid );
    ASSERT_TRUE( refused.has_error() );
    EXPECT_EQ( refused.error(), sgns::crdt::GraphsyncDAGSyncer::Error::HISTORY_PRUNED_LOCALLY );

    // GetNodeWithoutRequest refuses IDENTICALLY (one consult site covers
    // both read surfaces).
    auto refused_no_request = syncer->GetNodeWithoutRequest( pruned_cid );
    ASSERT_TRUE( refused_no_request.has_error() );
    EXPECT_EQ( refused_no_request.error(), sgns::crdt::GraphsyncDAGSyncer::Error::HISTORY_PRUNED_LOCALLY );

    // No resurrection: the hot store still does not hold the block (no
    // Rehydrate ever wrote the cold bytes hot).
    auto hot_after = syncer->HasStoredBlock( pruned_cid );
    ASSERT_TRUE( hot_after.has_value() );
    EXPECT_FALSE( hot_after.value() );

    // The consult is a stateless point lookup over an immutable marker
    // row: a second identical read refuses identically (PROBE-LIFT
    // concurrency backstop: interrupted/repeat reads never serve).
    auto again = syncer->getNode( pruned_cid );
    ASSERT_TRUE( again.has_error() );
    EXPECT_EQ( again.error(), sgns::crdt::GraphsyncDAGSyncer::Error::HISTORY_PRUNED_LOCALLY );
    EXPECT_FALSE( syncer->HasStoredBlock( pruned_cid ).value() );

    // Per-CID evidence, not a stack-wide outage: the no-evidence sibling
    // still serves through the unchanged cold chain (the SC2 control on
    // the same fixture; the classes never merge - PROBE-LIFT).
    auto served = syncer->getNode( sibling_cid );
    ASSERT_TRUE( served.has_value() ) << served.error().message();
    EXPECT_EQ( served.value()->getCID(), sibling_cid );
}

/**
 * LEG S2 - SC2: no-evidence cold bytes serve + rehydrate (evict = DETACH).
 */
TEST_F( ColdEvidenceMatrixTest, Serve_NoEvidenceColdBytesServeAndRehydrate )
{
    BuildFixtureWithColdBlocks( /*count=*/1 );
    ASSERT_NE( db_, nullptr );
    const sgns::CID cid = cold_seeds_[0].cid;

    auto syncer = syncerOf( *db_ );

    auto hot_before = syncer->HasStoredBlock( cid );
    ASSERT_TRUE( hot_before.has_value() );
    EXPECT_FALSE( hot_before.value() ) << "precondition: the cold-only posture";

    // The NoEvidence branch preserves the v1.1 serve body verbatim.
    auto served = syncer->getNode( cid );
    ASSERT_TRUE( served.has_value() ) << served.error().message();
    EXPECT_EQ( served.value()->getCID(), cid );

    // Rehydrate happened: the block is hot now.
    auto hot_after = syncer->HasStoredBlock( cid );
    ASSERT_TRUE( hot_after.has_value() );
    EXPECT_TRUE( hot_after.value() );
}

/**
 * LEG S3 - full-stack evidence production (D-01/D-02 end-to-end): one
 * gated TxPruner erase stages BOTH marker rows in ONE commit, the block
 * record carries the tx-hash back-reference and the RunCycle generation,
 * the cold bytes survive, and the erased CID subsequently refuses.
 */
TEST_F( ColdEvidenceMatrixTest, Evidence_TxPrunerEraseStagesBothMarkers )
{
    BuildStack( /*cold_enabled=*/true, /*drain_interval_ms=*/50, /*inject_hermetic_clients=*/true );
    ASSERT_NE( db_, nullptr );

    // The Gate_DurableAdmitsAndErases seeding shape: an ancient spender,
    // one eligible, one unspent sibling.
    auto spender  = MakeTransfer( 1, 0 );
    auto eligible = MakeTransfer( 2, 1 );
    auto unspent  = MakeTransfer( 3, 2 );

    SeedTransaction( spender );
    PutSeedValue( "/seed/evidence/s3-driver-1", "d1" );
    SeedTransaction( eligible );
    SeedTransaction( unspent );

    TrackOutputs( *eligible );
    TrackOutputs( *unspent ); // deliberately never consumed
    ASSERT_TRUE( ConsumeOutput( *eligible, 0, *spender ) );
    PutSeedValue( "/seed/evidence/s3-driver-2", "d2" );

    const std::string cid_string = SnapshotMapEntry( sgns::TransactionManager::GetTransactionPath( *eligible ) );
    ASSERT_NE( cid_string, "absent" );
    auto cid = sgns::CID::fromString( cid_string );
    ASSERT_TRUE( cid.has_value() );

    // REAL transitions to Durable on the injected fakes (LEG S3's
    // deterministic drive - the WaitAllDurable shape).
    ASSERT_TRUE( PollUntil( [&] { return JournalStateOf( cid_string ) == crdt::ColdUploadState::Durable; },
                            std::chrono::milliseconds( 20000 ) ) )
        << "eligible's block never reached Durable (cid " << cid_string << ")";

    auto pruner = MakePruner( 10, kSmallSettleSeconds );
    ASSERT_NE( pruner, nullptr );

    sgns::PruneCycleStats stats;
    RunSettledCycle( *pruner, stats );
    EXPECT_EQ( stats.erased, 1u );
    EXPECT_EQ( stats.errors, 0u );

    auto syncer = syncerOf( *db_ );

    // D-01: BOTH marker rows exist after the one commit.
    auto tx_lookup = sgns::PruneEligibility::LookupPrunedEntry( *db_, eligible->GetHash() );
    ASSERT_TRUE( tx_lookup.has_value() );
    EXPECT_EQ( tx_lookup.value().first, sgns::PrunedLookup::PRUNED );

    auto blk_lookup = sgns::PruneEligibility::LookupPrunedBlockEntry( *db_, cid_string );
    ASSERT_TRUE( blk_lookup.has_value() );
    EXPECT_EQ( blk_lookup.value().first, sgns::PrunedBlockLookup::PRUNED );
    ASSERT_TRUE( blk_lookup.value().second.has_value() );
    // D-02: the back-reference record - the erasing /prune/tx hash plus
    // the erased-at generation threaded from RunCycle (D-C4).
    EXPECT_EQ( blk_lookup.value().second->pruning_tx_hash(), eligible->GetHash() );
    EXPECT_TRUE( blk_lookup.value().second->has_erased_at_generation() );
    EXPECT_EQ( blk_lookup.value().second->erased_at_generation(), kTestGeneration );

    // The hot bytes are GONE (the erase removed them)...
    auto hot = syncer->HasStoredBlock( cid.value() );
    ASSERT_TRUE( hot.has_value() );
    EXPECT_FALSE( hot.value() );
    // ...while the cold evidence SURVIVES: the journal still reads Durable
    // and HasBlock's cold-dir term answers TRUE - which is exactly the
    // D-04 posture (HasBlock stays TRUE for an erased CID; dedup-correct).
    EXPECT_EQ( JournalStateOf( cid_string ), crdt::ColdUploadState::Durable );
    auto has_block = syncer->HasBlock( cid.value() );
    ASSERT_TRUE( has_block.has_value() );
    EXPECT_TRUE( has_block.value() );

    // The tracer's end-to-end proof: evidence built by the PRODUCTION
    // path, read by the PRODUCTION consult - getNode now refuses with the
    // distinct terminal value.
    auto refused = syncer->getNode( cid.value() );
    ASSERT_TRUE( refused.has_error() );
    EXPECT_EQ( refused.error(), sgns::crdt::GraphsyncDAGSyncer::Error::HISTORY_PRUNED_LOCALLY );

    // Negative control: the unspent sibling survives untouched.
    EXPECT_TRUE( db_->Get( { sgns::TransactionManager::GetTransactionPath( *unspent ) } ).has_value() );
}

/**
 * LEG S4 - malformed evidence fails closed (Pitfall 7 / T-17-0502): a
 * garbage value AND a zero-length value both hard-error - never a serve,
 * never a silent NoEvidence.
 */
TEST_F( ColdEvidenceMatrixTest, Evidence_MalformedMarkerFailsClosed )
{
    BuildFixtureWithColdBlocks( /*count=*/2 );
    ASSERT_NE( db_, nullptr );
    auto syncer = syncerOf( *db_ );

    // Garbage (non-parseable) bytes for the first CID - WITH cold bytes
    // present, the strongest shape (bytes exist and are still refused).
    const std::string garbage_key = std::string( sgns::PruneEligibility::PRUNE_BLOCK_MARKER_PREFIX )
                                  + cold_seeds_[0].cid_string;
    ASSERT_TRUE( db_->PutRaw( crdt::GlobalDB::Buffer{}.put( garbage_key ),
                              crdt::GlobalDB::Buffer( std::vector<uint8_t>{ 0xFF, 0xFE, 0x01 } ) )
                      .has_value() );

    // A ZERO-LENGTH value for the second CID (the probe-lifted empty
    // edge): fail-closed Malformed, not NoEvidence.
    const std::string empty_key = std::string( sgns::PruneEligibility::PRUNE_BLOCK_MARKER_PREFIX )
                                + cold_seeds_[1].cid_string;
    ASSERT_TRUE( db_->PutRaw( crdt::GlobalDB::Buffer{}.put( empty_key ), crdt::GlobalDB::Buffer{} ).has_value() );

    for ( const auto &seed : cold_seeds_ )
    {
        auto read = syncer->getNode( seed.cid );
        ASSERT_TRUE( read.has_error() ) << "malformed evidence served for " << seed.cid_string;
        EXPECT_EQ( read.error(), sgns::crdt::GraphsyncDAGSyncer::Error::PRUNE_EVIDENCE_CORRUPT );

        auto read_no_request = syncer->GetNodeWithoutRequest( seed.cid );
        ASSERT_TRUE( read_no_request.has_error() );
        EXPECT_EQ( read_no_request.error(), sgns::crdt::GraphsyncDAGSyncer::Error::PRUNE_EVIDENCE_CORRUPT );

        // Never served: the hot store never received the bytes.
        auto hot = syncer->HasStoredBlock( seed.cid );
        ASSERT_TRUE( hot.has_value() );
        EXPECT_FALSE( hot.value() );
    }
}

/**
 * LEG S5 - the disabled-build bar (Phase-7): a cold-DISABLED GlobalDB
 * carrying a well-formed marker row answers the PRE-PHASE not-found shape
 * - the null hook is never consulted, so neither refusal value can ever
 * surface (byte-identical reads).
 */
TEST_F( ColdEvidenceMatrixTest, Evidence_ColdDisabledNeverConsultsEvidence )
{
    BuildStack( /*cold_enabled=*/false, /*drain_interval_ms=*/50 );
    ASSERT_NE( db_, nullptr );
    ASSERT_FALSE( db_->HasColdStorage() );

    // Evidence seeded onto the raw surface of the cold-DISABLED node: the
    // row exists, but with a null hook the consult never runs.
    const auto        unseen        = makeUnseenCid( std::vector<uint8_t>( 32, 0x5E ) );
    const std::string unseen_string = unseen.toString().value();
    ASSERT_TRUE(
        sgns::PruneEligibility::WritePrunedBlockEntryForTest( *db_, unseen_string, "cold-off-pruner", 1 ).has_value() );

    auto syncer = syncerOf( *db_ );
    auto read   = syncer->getNode( unseen );
    ASSERT_TRUE( read.has_error() );
    const auto err = read.error();
    EXPECT_NE( err, sgns::crdt::GraphsyncDAGSyncer::Error::HISTORY_PRUNED_LOCALLY )
        << "disabled build surfaced the pruned refusal - the consult ran";
    EXPECT_NE( err, sgns::crdt::GraphsyncDAGSyncer::Error::PRUNE_EVIDENCE_CORRUPT )
        << "disabled build surfaced the corrupt refusal - the consult ran";
    EXPECT_TRUE( err == sgns::crdt::GraphsyncDAGSyncer::Error::ROUTE_NOT_FOUND
                 || err == sgns::crdt::GraphsyncDAGSyncer::Error::CID_NOT_FOUND )
        << "unexpected pre-phase error shape: " << err.message();
}

/**
 * The responder-side third surface (SERVE-01): the re-warm worker's
 * evidence gate. A Pruned CID's re-warm is SKIPPED by the worker consult
 * (no Get, no Rehydrate - erased bytes are never written hot by any
 * surface), while the NoEvidence control on the SAME worker re-warms
 * normally - proving the gate discriminates rather than being dead.
 */
TEST_F( ColdEvidenceRewarmTest, Rewarm_PrunedEvidenceSkipsRewarmWorker )
{
    const std::string pruned_payload  = "re-warm refusal payload";
    const std::string control_payload = "re-warm control payload";
    const auto        pruned_cid_lib  = MakeCid( pruned_payload );
    const auto        control_cid_lib = MakeCid( control_payload );
    const sgns::CID   pruned_cid( pruned_cid_lib );
    const sgns::CID   control_cid( control_cid_lib );

    // Cold bytes for BOTH CIDs in the local tier.
    ASSERT_TRUE( local_->Put( pruned_cid_lib, SpanOf( pruned_payload ) ).has_value() );
    ASSERT_TRUE( local_->Put( control_cid_lib, SpanOf( control_payload ) ).has_value() );

    // The table-driven predicate (TEST-01 secondary): Pruned for the
    // target, NoEvidence for everything else - installed through the
    // PRODUCTION SetPruneEvidenceLookup seam (the same surface GlobalDB
    // uses at the OpenColdStorageStack wiring point).
    service_->SetPruneEvidenceLookup( [pruned_cid]( const sgns::CID &c ) {
        return c == pruned_cid ? sgns::crdt::PruneEvidenceClass::Pruned
                               : sgns::crdt::PruneEvidenceClass::NoEvidence;
    } );

    // The syncer-level consult proof on the same predicate (the enum
    // mapping the full-stack legs assert through GlobalDB's lambda).
    auto syncer  = std::make_shared<sgns::crdt::GraphsyncDAGSyncer>( decorator_, nullptr, nullptr, service_ );
    auto refused = syncer->getNode( pruned_cid );
    ASSERT_TRUE( refused.has_error() );
    EXPECT_EQ( refused.error(), sgns::crdt::GraphsyncDAGSyncer::Error::HISTORY_PRUNED_LOCALLY );

    service_->Start();

    // The gate: enqueue BOTH; the control must complete (the worker is
    // alive and discriminating)...
    EXPECT_TRUE( service_->EnqueueRewarm( pruned_cid ) );
    EXPECT_TRUE( service_->EnqueueRewarm( control_cid ) );

    const auto control_hot = [&] { return inner_->get( control_cid ).has_value(); };
    ASSERT_TRUE( PollUntil( control_hot, std::chrono::milliseconds( 8000 ) ) )
        << "control re-warm must complete (the gate discriminates, it is not dead)";

    // ...then wait past the re-warm budget and assert the pruned CID
    // NEVER landed hot (the worker consult skipped it - SERVE-01).
    std::this_thread::sleep_for( sgns::crdt::ColdReadService::kRewarmBudget
                                 + std::chrono::milliseconds( 250 ) );
    EXPECT_FALSE( inner_->get( pruned_cid ).has_value() )
        << "the re-warm worker resurrected erased bytes (SERVE-01 violation)";
}
