/**
 * @file       cold_gc_walk_gap_exempt_test.cpp
 * @brief      Hermetic Tier-1 RED-FIRST counter-test: the GC walk-gap
 *             evidence exemption (v1.2 Phase 18 plan 18-01; POLICY-01;
 *             D-01/D-02/D-03/D-04).
 *
 * RED-FIRST DECLARATION (D-01, 18-RESEARCH Pitfall 8): LEG-1 (the LIVE
 * poisoned-GC-triangle state: /prune/blk evidence PRUNED + hot bytes
 * gone + cold bytes STILL PRESENT - Pitfall 1) and LEG-2 (the v2
 * fault-injected state: the cold object additionally deleted from BOTH
 * tiers via the fake's test-side deletion) are RED against the
 * UNMODIFIED walk BY DESIGN until plan 18-01 Task 2 lands the gap-site
 * consult: GetNodeWithoutRequest refuses an evidenced CID
 * (HISTORY_PRUNED_LOCALLY outranks bytes), WalkReachable sees a
 * node-miss, and the pass aborts (gc: pass-aborted-walk-gap) - GC
 * liveness broken from the first prune cycle onward. The fail-closed
 * CONTROL legs (NoEvidence / Malformed) are GREEN today and must STAY
 * green: the exemption opens ONLY on the Pruned verdict of the ONE
 * Phase 15 predicate (D-02 - GraphsyncDAGSyncer::LookupPruneEvidence
 * through the gc_dag_syncer_ handle the walk already holds; never a
 * GC-local /prune/blk read, never error-code matching, never age).
 *
 * D-03 BOTH-LEGS POSTURE NOTE: this file is the deterministic Tier-1
 * hermetic leg; the dev-gated live leg on a real MinIO stack lives in
 * Phase 18 plan 18-03's cold_downgrade_live_smoke_test.cpp (Phase 16
 * D-11 hybrid discipline) - NOT here.
 *
 * D-04: the exemption is counted per exempted CID, reset per pass, and
 * surfaced through GetStatus (gc_last_pass_exempted) plus the per-pass
 * "gc: pass complete" INFO line - never-healthy-by-omission (Phase 14
 * D-03 / Phase 16 D-10 precedent). The count assertions land with the
 * Task-2 consult; this RED commit pins the walk-completion observable
 * (gc_last_pass_candidates) each leg drives.
 *
 * Fixture composition: the cold_storage_gc_test.cpp hermetic GC family
 * (real hot RocksDB + LocalBlockStore cold dir + InMemoryS3Client /
 * InMemoryS3AdminClient + real ColdReadService / GraphsyncDAGSyncer
 * with the evidence lookup installed + real CrdtDatastore +
 * AttachGcDependencies + direct GcPassOnce drive, gc_interval_ms = 0
 * dormant cadence). Evidence becomes consultable to the walk WITHOUT
 * hand-built keys: the fixture installs a lookup on the REAL
 * ColdReadService via SetPruneEvidenceLookup whose verdicts derive
 * exclusively from PrunedBlockRecord bytes stored at keys produced by
 * the sanctioned PruneEligibility builder (BuildPruneBlockMarkerKey),
 * mirroring the production lambda shape (globaldb.cpp: native raw Get
 * + PrunedBlockRecord parse, fail-closed Malformed - Pitfall 7). The
 * malformed control corrupts ONLY the VALUE at a sanctioned-builder
 * key. Zero string-formatted /prune/* keys anywhere in this file
 * (testing trap 1 / D-C1).
 *
 * The walk-completion canary: every leg seeds one backdated past-
 * retention Durable ORPHAN (never reachable, never a walk root). A
 * COMPLETED pass necessarily nominates it (gc_last_pass_candidates ==
 * 1); an ABORTED pass necessarily does not (== 0). That observable is
 * the strongest pre-consult pass-outcome surface and stays meaningful
 * after the consult lands.
 *
 * Include discipline: every library result is guarded with has_value()
 * BEFORE .value()/.error(). CID landmine: sgns::CID has NO implicit
 * ctor from the blockstore Cid - wrap explicitly (CID cid_key( cid ));
 * the reverse direction slices implicitly (Cid lib( sgns_cid )). Waits
 * only via PollUntil bounded predicates; fresh fixture dir per case
 * (distinct paths - Windows LOCK contention, Pitfall 9).
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
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
    // IPFS resolves only inside namespace sgns - alias it for this TU
    // (the decorator's set()/get() surface is IpfsDatastore-typed).
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
    using sgns::blockstore::S3BlockStore;
    using sgns::blockstore::UploadState;
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
    /// stays stable on loaded Windows CI-equivalent machines.
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

    /// Retention for driven passes: retention threshold well above the
    /// Durable-wait jitter (young chain blocks stay provably young), a
    /// backdate margin well beyond it (the orphan canary is provably old),
    /// and gc_interval_ms = 0 so the background GC thread stays dormant
    /// while tests drive GcPassOnce() directly. EXPLICIT zero grace so a
    /// second driven pass processes the canary's due mark deterministically
    /// (the repeat-pass leg).
    ColdStorageManager::RetentionOptions RetentionFastGrace( uint64_t retention_age_ms = 5000 )
    {
        ColdStorageManager::RetentionOptions retention;
        retention.retention_age_ms = retention_age_ms;
        retention.dry_run          = false;
        retention.gc_interval_ms   = 0; // 0 -> kDefaultGcIntervalMs (dormant)
        retention.grace_ms         = 0; // EXPLICIT - never normalized
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

    // SHORT relative path under the build tree (Windows MAX_PATH; the
    // upload_drainer_test FreshDir/NextUniqueDir discipline).
    std::string NextUniqueDir( const char *lane )
    {
        static std::atomic<uint64_t> next{ 0 };
        return std::string( "test-data/cold-gc-gap/" ) + lane + "-case-"
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

    /// The BACK-07 object key for a fixture CID (S3BlockStore's private
    /// DeriveKey replicated test-side for the LEG-2 deletion - the fake can
    /// delete; production adapters cannot). Codec-aware: the CRDT delta
    /// chain nodes carry DAG_PB while orphan payloads are RAW.
    std::string DeriveTestKey( const Cid &cid )
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

    /**
     * @brief      The hermetic GC walk-gap exemption fixture (the
     *             cold_storage_gc_test.cpp composition).
     *
     * The walk under test is the REAL WalkReachable inside a REAL
     * ColdStorageManager, driven directly through GcPassOnce. The
     * evidence predicate is consultable through the REAL ColdReadService
     * (SetPruneEvidenceLookup, D-02 altitude): the installed lookup reads
     * PrunedBlockRecord rows seeded at sanctioned-builder keys from the
     * shared hot RocksDB - the production lambda shape (native raw get +
     * parse + fail-closed Malformed), never a hand-built key.
     */
    class ColdGcWalkGapExemptTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            // The REAL hot CRDT RocksDB store (heads + set rows land here
            // through the CrdtDatastore). storage::rocksdb::create does NOT
            // create parent directories - create them explicitly.
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

            fake_ = std::make_shared<InMemoryS3Client>();
            auto opened_remote = S3BlockStore::Open( fake_ );
            ASSERT_TRUE( opened_remote.has_value() );
            remote_ = std::shared_ptr<S3BlockStore>( std::move( opened_remote.value() ) );

            fake_admin_       = std::make_shared<InMemoryS3AdminClient>();
            cold_store_mutex_ = std::make_shared<std::mutex>();

            // The DAG layer (the production shared-DB shape): ONE
            // underlying RocksDB, wrapped TWICE - sgns::storage::rocksdb
            // for the CrdtDatastore, and ipfs_lite::rocksdb over the SAME
            // DB handle for the RocksdbDatastore the decorator wraps.
            auto ipfs_db = sgns::ipfs_lite::rocksdb::create( real_hot_->getDB() );
            ASSERT_TRUE( ipfs_db.has_value() );
            inner_     = std::make_shared<sgns::ipfs_lite::ipfs::RocksdbDatastore>( ipfs_db.value() );
            decorator_ = std::make_shared<BlockStoreBackedIpfsDatastore>(
                inner_, shared_local_, cold_store_mutex_, sgns::base::createLogger( "ColdGcGap" ) );

            // The REAL cold-read chain as the DAGSyncer hook - the cold
            // dir term, the disposed-marker term, and the direct-read cold
            // chain all live here (the re-warm worker stays unstarted).
            remote_store_mutex_ = std::make_shared<std::mutex>();
            cold_read_service_  = std::make_shared<ColdReadService>(
                shared_local_, remote_, cold_store_mutex_, remote_store_mutex_, decorator_ );

            // EVIDENCE INSTALL (D-02 altitude): the ONE predicate the walk
            // (and the cold serve path) consults. Verdicts derive
            // exclusively from PrunedBlockRecord rows at sanctioned-builder
            // keys (BuildPruneBlockMarkerKey) - the production lambda shape
            // (globaldb.cpp: native raw read + parse + fail-closed
            // Malformed, Pitfall 7). No row = NoEvidence (D-03); an
            // unreadable or malformed row = Malformed, NEVER exemption.
            cold_read_service_->SetPruneEvidenceLookup(
                [this]( const CID &cid ) -> PruneEvidenceClass
                {
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
                            // D-03: no row IS the NoEvidence answer - cold
                            // bytes are restorable history (SC2).
                            return PruneEvidenceClass::NoEvidence;
                        }
                        // Fail-closed (Pitfall 7): unreadable evidence is
                        // NEVER treated as absent evidence.
                        return PruneEvidenceClass::Malformed;
                    }
                    SGTransaction::PrunedBlockRecord record;
                    // The explicit empty check mirrors LookupPrunedBlockEntry:
                    // a zero-length /prune/blk row is corruption - Malformed,
                    // never NoEvidence (Pitfall 7).
                    if ( marker.value().empty()
                         || !record.ParseFromArray( marker.value().data(),
                                                    static_cast<int>( marker.value().size() ) ) )
                    {
                        return PruneEvidenceClass::Malformed;
                    }
                    return PruneEvidenceClass::Pruned;
                } );

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
                RetentionFastGrace( 5000 ) );
            manager_->AttachGcDependencies( crdt_.get(), dagsyncer_.get(), real_hot_ );
            return manager_;
        }

        /// Publish one element through the REAL CRDT Put path: the delta
        /// node lands in the DAG through the decorator (journaled = the
        /// spill shape) and the heads mutate (the walk universe).
        CID PublishElement( const std::string &key, const std::string &value )
        {
            auto delta = CrdtDatastore::CreateDeltaToAdd( key, value );
            EXPECT_TRUE( delta.has_value() );
            auto published = crdt_->Publish( delta.value(), { "topic" } );
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

        /// Simulate the TxPruner erase's HOT half for one CID (the
        /// poisoned-triangle setup): delete the block's hot binary key
        /// (the RunHotCleanup key shape - cid.toBytes()) and drop the LRU
        /// copy (the A6 invalidation posture). Cold bytes are NOT touched.
        void RemoveHotCopy( const CID &cid )
        {
            auto encoded = cid.toBytes();
            ASSERT_TRUE( encoded.has_value() );
            Buffer binary_key;
            binary_key.put( encoded.value() );
            auto removed = real_hot_->remove( binary_key );
            ASSERT_TRUE( removed.has_value() );
            (void)dagsyncer_->DeleteCIDBlock( cid ); // LRU invalidation (A6)
        }

        /// Seed PRUNED evidence for one CID at a sanctioned-builder key:
        /// a real serialized PrunedBlockRecord (the
        /// WritePrunedBlockEntryForTest payload shape). When @p corrupt is
        /// set, the VALUE bytes are an unparseable varint run instead -
        /// ONLY the value is corrupted, never the key (the malformed
        /// control's discipline).
        void SeedPruneEvidence( const CID &cid, bool corrupt = false )
        {
            const auto cid_string = cid.toString();
            ASSERT_TRUE( cid_string.has_value() );
            const std::string key_string
                = sgns::PruneEligibility::BuildPruneBlockMarkerKey( cid_string.value() );
            Buffer marker_key;
            marker_key.put( key_string );
            Buffer marker_value;
            if ( corrupt )
            {
                // Five never-terminating varint continuation bytes: a
                // guaranteed PrunedBlockRecord parse failure (the
                // PRUNE_EVIDENCE_CORRUPT fail-closed arm's input).
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
            ASSERT_TRUE( written.has_value() );
        }

        /// The LEG-2 test-side deletion: remove the cold object from BOTH
        /// tiers - the fake's conformance-only Delete at the derived
        /// object key, plus the local cold copy through the library's own
        /// unconditional EvictLocal (bytes + journal record, no disposed
        /// marker). The v2 fault-injected state (D-03).
        void DeleteColdBothTiers( const Cid &cid )
        {
            auto deleted = fake_->Delete( DeriveTestKey( cid ) );
            ASSERT_TRUE( deleted.has_value() );
            auto evicted = shared_local_->EvictLocal( cid );
            ASSERT_TRUE( evicted.has_value() );
        }

        // Declaration order == reverse destruction order: manager_ dies
        // first (its threads join against still-live stores), then the
        // CRDT layer, then the DAG layer, then the stores.
        std::string                             hot_dir_;
        std::shared_ptr<rocksdb>                real_hot_;
        std::string                             shared_local_dir_;
        std::shared_ptr<LocalBlockStore>        shared_local_;
        std::shared_ptr<InMemoryS3Client>       fake_;
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
// LEG-1 (RED until Task 2): the LIVE poisoned-GC-triangle state.
// ---------------------------------------------------------------------------

/**
 * @given a published two-block delta chain (head d2 -> d1, both Durable
 *        and young) plus a backdated past-retention Durable orphan canary;
 *        the child d1 carries /prune/blk evidence PRUNED and its HOT bytes
 *        are removed while its cold bytes are STILL PRESENT in BOTH cold
 *        tiers (the live poisoned-GC-triangle state, 18-RESEARCH Pitfall 1)
 * @when  a driven GC pass runs, then runs AGAIN on the unchanged state
 * @then  the walk does NOT abort - the evidenced gap is an expected-absent
 *        leaf, the canary is nominated (walk-completion observable), and
 *        the verdict is IDENTICAL across both passes (the idempotency
 *        edge: the consult is a pure read - POLICY-01)
 *
 * RED SIGNATURE (pre-consult): GetNodeWithoutRequest refuses the evidenced
 * d1 (HISTORY_PRUNED_LOCALLY outranks the still-present cold bytes), the
 * walk aborts with gc: pass-aborted-walk-gap, gc_last_pass_candidates
 * stays 0 in BOTH passes - GC liveness broken from the first prune cycle
 * onward (exactly the state the exemption defuses).
 */
TEST_F( ColdGcWalkGapExemptTest, Leg1PoisonedTriangleWalkCompletesViaExemption )
{
    const CID d1 = PublishElement( "gap/leg1/key1", "value1" );
    const CID d2 = PublishElement( "gap/leg1/key2", "value2" );
    const Cid canary = SetOrphan( "leg1 canary payload" );

    auto manager = MakeManager();
    manager->Start();
    WaitAllDurable( { Cid( d1 ), Cid( d2 ), canary } );

    // The poisoned triangle: evidence + hot gone + cold present.
    RemoveHotCopy( d1 );
    SeedPruneEvidence( d1 );
    Backdate( canary, 60000 ); // provably old vs the 5000 ms threshold

    const auto pass1 = manager->GcPassOnce();
    ASSERT_EQ( pass1.size(), 1u )
        << "RED pre-consult: the walk aborted at the evidenced gap "
           "(pass-aborted-walk-gap) and never nominated the canary; GREEN "
           "post-consult: the gap is an expected-absent leaf, the pass "
           "completes, the canary is nominated";
    EXPECT_TRUE( pass1[0] == canary );
    EXPECT_EQ( manager->GetStatus().gc_last_pass_candidates, 1u );
    // D-04 (Task 2): the exemption is counted - one evidenced gap seen
    // this pass, never-healthy-by-omission.
    EXPECT_GT( manager->GetStatus().gc_last_pass_exempted, 0u );
    const uint64_t exempted_pass1 = manager->GetStatus().gc_last_pass_exempted;

    // Repeat-pass (POLICY-01 idempotency): the consult is a pure read -
    // an unchanged evidenced graph yields the same verdict per CID and a
    // stable pass outcome across two consecutive passes.
    const auto pass2 = manager->GcPassOnce();
    ASSERT_EQ( pass2.size(), 1u ) << "the repeat pass must reach the same verdict";
    EXPECT_EQ( pass1.size(), pass2.size() );
    EXPECT_EQ( manager->GetStatus().gc_last_pass_candidates, 1u );
    EXPECT_EQ( manager->GetStatus().gc_last_pass_exempted, exempted_pass1 )
        << "the repeat pass must report an IDENTICAL walk-exempted count "
           "on the unchanged evidenced graph (per-CID counting, per-pass "
           "reset)";
}

// ---------------------------------------------------------------------------
// LEG-2 (RED until Task 2): the v2 fault-injected deleted-cold state.
// ---------------------------------------------------------------------------

/**
 * @given LEG-1's state with the cold object ADDITIONALLY deleted from BOTH
 *        cold tiers (the fake's test-side Delete at the derived object key
 *        + the local EvictLocal - the v2 collection fault injection, D-03)
 * @when  a driven GC pass runs
 * @then  the walk STILL completes via the exemption (the consult never
 *        needs the bytes - the evidence row alone is the verdict), the
 *        canary is nominated, and the exempted outcome is observable
 *
 * RED SIGNATURE (pre-consult): identical to LEG-1 - the walk aborts with
 * gc: pass-aborted-walk-gap, gc_last_pass_candidates stays 0. This leg
 * pins the state any future v2 collection capability produces (deleted
 * cold + surviving evidence) so v2 can never ship without the exemption
 * (SC1's no-collection-without-exemption gate).
 */
TEST_F( ColdGcWalkGapExemptTest, Leg2DeletedColdWalkCompletesViaExemption )
{
    const CID d1 = PublishElement( "gap/leg2/key1", "value1" );
    const CID d2 = PublishElement( "gap/leg2/key2", "value2" );
    const Cid canary = SetOrphan( "leg2 canary payload" );

    auto manager = MakeManager();
    manager->Start();
    WaitAllDurable( { Cid( d1 ), Cid( d2 ), canary } );

    // LEG-1's state, then the v2 fault injection: cold gone from both
    // tiers. Evidence survives (the erased-node terminal record).
    RemoveHotCopy( d1 );
    SeedPruneEvidence( d1 );
    DeleteColdBothTiers( Cid( d1 ) );
    Backdate( canary, 60000 );

    const auto candidates = manager->GcPassOnce();
    ASSERT_EQ( candidates.size(), 1u )
        << "RED pre-consult: the walk aborted at the evidenced deleted-cold "
           "gap; GREEN post-consult: the evidence row alone carries the "
           "expected-absent verdict, the pass completes";
    EXPECT_TRUE( candidates[0] == canary );
    EXPECT_EQ( manager->GetStatus().gc_last_pass_candidates, 1u );
    // D-04 (Task 2): the deleted-cold exemption is counted identically.
    EXPECT_GT( manager->GetStatus().gc_last_pass_exempted, 0u );
}

// ---------------------------------------------------------------------------
// CONTROL legs (GREEN today, must STAY green): fail-closed preserved.
// ---------------------------------------------------------------------------

/**
 * @given a genuinely missing head-reachable link: d1's hot bytes removed
 *        AND its cold object deleted from both tiers AND NO evidence row
 *        (missing from every tier - the real walk gap)
 * @when  a driven GC pass runs
 * @then  the pass ABORTS (pass-aborted-walk-gap, under-eviction) and the
 *        canary is NOT nominated - today's fail-closed behavior, which the
 *        exemption must preserve exactly (the consult opens ONLY on the
 *        Pruned verdict; NoEvidence is never exemption)
 */
TEST_F( ColdGcWalkGapExemptTest, ControlNoEvidenceGapStillAborts )
{
    const CID d1 = PublishElement( "gap/noev/key1", "value1" );
    const CID d2 = PublishElement( "gap/noev/key2", "value2" );
    const Cid canary = SetOrphan( "noevidence canary payload" );

    auto manager = MakeManager();
    manager->Start();
    WaitAllDurable( { Cid( d1 ), Cid( d2 ), canary } );

    // No evidence row anywhere: a REAL gap (missing from every tier).
    RemoveHotCopy( d1 );
    DeleteColdBothTiers( Cid( d1 ) );
    Backdate( canary, 60000 );

    const auto candidates = manager->GcPassOnce();
    EXPECT_TRUE( candidates.empty() )
        << "an unevidenced gap must abort the pass (under-eviction) - the "
           "exemption never opens on NoEvidence";
    EXPECT_EQ( manager->GetStatus().gc_last_pass_candidates, 0u );
    EXPECT_EQ( manager->GetStatus().gc_last_pass_exempted, 0u )
        << "NoEvidence must never be counted as exemption";
}

/**
 * @given d1's hot bytes removed, its cold bytes still present, and a
 *        CORRUPT VALUE at the sanctioned-builder /prune/blk key for d1
 *        (only the value is corrupted - never a hand-built key)
 * @when  a driven GC pass runs
 * @then  the pass ABORTS - unreadable evidence is never exemption
 *        (PRUNE_EVIDENCE_CORRUPT discipline, Pitfall 7); the Malformed arm
 *        keeps today's abort exactly
 */
TEST_F( ColdGcWalkGapExemptTest, ControlMalformedEvidenceStillAborts )
{
    const CID d1 = PublishElement( "gap/mal/key1", "value1" );
    const CID d2 = PublishElement( "gap/mal/key2", "value2" );
    const Cid canary = SetOrphan( "malformed canary payload" );

    auto manager = MakeManager();
    manager->Start();
    WaitAllDurable( { Cid( d1 ), Cid( d2 ), canary } );

    // Corrupt value at the sanctioned key: the fail-closed arm's input.
    RemoveHotCopy( d1 );
    SeedPruneEvidence( d1, /*corrupt=*/true );
    Backdate( canary, 60000 );

    const auto candidates = manager->GcPassOnce();
    EXPECT_TRUE( candidates.empty() )
        << "a malformed evidence row must abort the pass - fail closed, "
           "never exempted (Pitfall 7)";
    EXPECT_EQ( manager->GetStatus().gc_last_pass_candidates, 0u );
    EXPECT_EQ( manager->GetStatus().gc_last_pass_exempted, 0u )
        << "Malformed must never be counted as exemption";
}

/**
 * @given a clean graph with NO gaps at all (a published young two-block
 *        chain, nothing removed, nothing evidenced, nothing old)
 * @when  a driven GC pass runs
 * @then  the pass completes with zero candidates and the walk-exempted
 *        observable reads EXACTLY 0 through GetStatus - the counter is
 *        never-healthy-by-omission in BOTH directions (D-04: a zero
 *        baseline proves the surface is live, not defaulted)
 */
TEST_F( ColdGcWalkGapExemptTest, NoEvidenceBaselinePassReportsZeroExempted )
{
    (void)PublishElement( "gap/base/key1", "value1" );
    (void)PublishElement( "gap/base/key2", "value2" );

    auto manager = MakeManager();
    manager->Start();

    const auto candidates = manager->GcPassOnce();
    EXPECT_TRUE( candidates.empty() ) << "a young clean graph nominates nothing";
    EXPECT_EQ( manager->GetStatus().gc_last_pass_candidates, 0u );
    EXPECT_EQ( manager->GetStatus().gc_last_pass_exempted, 0u )
        << "a pass with zero evidenced gaps must report walk-exempted 0 - "
           "the operator surface is live both ways";
}
