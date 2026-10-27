/**
 * @file       cold_storage_gc_test.cpp
 * @brief      Hermetic Tier-1 retention-GC observe-only tests (GC-01
 *             SC1/SC5 slice, v1.1 Phase 11 plan 11-01).
 *
 * Registered ONLY under SGNS_COLD_STORAGE (links
 * sgns-blockstore::sgns-blockstore plus the InMemoryS3Client /
 * InMemoryS3AdminClient fakes via the relative include dir - both fake
 * headers are deliberately not installed). The manager is constructed
 * DIRECTLY over a real LocalBlockStore journal (the durability-suite
 * fixture shape), and the GC dependencies attach to a REAL CrdtDatastore
 * over a real hot RocksDB store with a REAL GraphsyncDAGSyncer whose
 * dagService is the spill decorator - the pass walks the same DAG the
 * CRDT layer published, and eligibility reads the same journal the
 * decorator wrote (D-10 direct-construction precedent).
 *
 * SC1 instrument: the hot-store dependency is a test-local PREFIX SPY
 * (a BufferStorage decorator tallying every operation by key shape).
 * Across a full driven pass the spy must observe ZERO operations under
 * the protected tombstone/heads/current-value/set keyspaces - policy
 * evaluation never enumerates CRDT metadata (11-01 must_have 4).
 *
 * Include discipline: every library result is guarded with has_value()
 * BEFORE .value()/.error() (policy::terminate alias). CID landmine:
 * sgns::CID has NO implicit ctor from the blockstore Cid - wrap
 * explicitly (CID cid_key( cid )); the reverse direction slices
 * implicitly (Cid lib( sgns_cid )).
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
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
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <libp2p/multi/content_identifier_codec.hpp>

#include <sgns-blockstore/Cid.hpp>
#include <sgns-blockstore/LocalBlockStore.hpp>
#include <sgns-blockstore/S3AdminClient.hpp>
#include <sgns-blockstore/S3BlockStore.hpp>

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
    using sgns::blockstore::InMemoryS3AdminClient;
    using sgns::blockstore::InMemoryS3Client;
    using sgns::blockstore::LocalBlockStore;
    using sgns::blockstore::LocalBlockStoreOptions;
    using sgns::blockstore::S3AdminClient;
    using sgns::blockstore::S3AdminFaultKind;
    using sgns::blockstore::S3BlockStore;
    using sgns::blockstore::S3FaultKind;
    using sgns::blockstore::S3Op;
    using sgns::blockstore::SpillEntry;
    using sgns::blockstore::UploadState;
    using sgns::blockstore::UploadStats;
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

    /// Retention options for driven passes: a threshold well above the
    /// Durable-wait jitter (young blocks stay provably young) with a
    /// backdate margin well beyond it (old blocks are provably old), and
    /// the DEFAULT cadence knobs (0 -> 600000/60000) so the background GC
    /// thread stays dormant while tests drive GcPassOnce() directly.
    ColdStorageManager::RetentionOptions RetentionFast( uint64_t retention_age_ms = 5000 )
    {
        ColdStorageManager::RetentionOptions retention;
        retention.retention_age_ms = retention_age_ms;
        retention.dry_run          = false;
        retention.gc_interval_ms   = 0; // 0 -> kDefaultGcIntervalMs (dormant)
        retention.grace_ms         = 0; // 0 -> kDefaultGraceMs
        return retention;
    }

    /// Retention with an EXPLICIT grace (ms-scale) so eviction tests
    /// process due marks on the next driven pass deterministically.
    ColdStorageManager::RetentionOptions RetentionFastGrace( uint64_t retention_age_ms,
                                                             uint32_t grace_ms )
    {
        auto retention      = RetentionFast( retention_age_ms );
        retention.grace_ms = grace_ms; // explicit - never normalized
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
        return std::string( "test-data/cold-storage-gc/" ) + lane + "-case-"
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

    /// The BACK-07 object key for a RAW fixture CID (S3BlockStore's
    /// private DeriveKey replicated test-side for per-key fault scripting).
    std::string DeriveTestKey( const Cid &cid )
    {
        auto         cid_string = libp2p::multi::ContentIdentifierCodec::toString( cid );
        static const char *kHex = "0123456789abcdef";
        const auto         digest = cid.content_address.getHash();
        const uint8_t      first  = digest.empty() ? 0 : digest[0];
        std::string        fanout{ kHex[first >> 4], kHex[first & 0x0F] };
        return std::string( "v1/blocks" ) + "/raw/" + fanout + "/" + cid_string.value();
    }

    /// @brief Test-local in-memory inner IpfsDatastore (the decorator's
    ///        hot tier stand-in - the spill-suite FakeInnerDatastore shape).
    class FakeInnerDatastore final : public IpfsDatastore
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
            auto key_str = key.toString();
            if ( key_str.has_error() )
            {
                return key_str.error();
            }
            sealed_.insert( key_str.value() );
            return ipfs_outcome::success();
        }

        ipfs_outcome::result<bool> is_sealed( const CID &key ) const override
        {
            auto key_str = key.toString();
            if ( key_str.has_error() )
            {
                return key_str.error();
            }
            return sealed_.count( key_str.value() ) > 0;
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
        std::set<std::string>                       sealed_;
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

    /**
     * @brief Test-local S3Client wrapper counting Get calls before
     *        delegating to the in-memory fake - the D-11 zero-S3-traffic
     * probe (the fallback-suite CountingS3Client shape).
     */
    class CountingS3Client final : public sgns::blockstore::S3Client
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

        sgns::blockstore::result<sgns::blockstore::S3PutResult, sgns::blockstore::BlockStatus> Put(
            const std::string &key, const std::vector<uint8_t> &bytes,
            const sgns::blockstore::PutObjectOptions &options = {} ) override
        {
            return inner_->Put( key, bytes, options );
        }

        sgns::blockstore::result<sgns::blockstore::S3Object, sgns::blockstore::BlockStatus> Get(
            const std::string &key ) override
        {
            ++get_calls_;
            return inner_->Get( key );
        }

        sgns::blockstore::result<sgns::blockstore::S3HeadResult, sgns::blockstore::BlockStatus> Head(
            const std::string &key ) override
        {
            return inner_->Head( key );
        }

        sgns::blockstore::result<void, sgns::blockstore::BlockStatus> Delete(
            const std::string &key ) override
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
     *        decorator - the SC1 "provably never" instrument).
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

        /// Every tallied operation regardless of key shape.
        uint64_t TotalOps() const
        {
            std::lock_guard<std::mutex> lock( mutex_ );
            uint64_t                    total = 0;
            for ( const auto &[key, ops] : op_by_key_ )
            {
                (void)key;
                total += ops;
            }
            return total;
        }

        /// WRITE operations (put/remove) whose key CONTAINS the needle -
        /// the dry-run zero-writes probe.
        uint64_t WriteOpsMatching( const std::string &needle ) const
        {
            std::lock_guard<std::mutex> lock( mutex_ );
            uint64_t                    total = 0;
            for ( const auto &[key, ops] : write_op_by_key_ )
            {
                if ( key.find( needle ) != std::string::npos )
                {
                    total += ops;
                }
            }
            return total;
        }

        /// Every tallied WRITE operation regardless of key shape.
        uint64_t TotalWriteOps() const
        {
            std::lock_guard<std::mutex> lock( mutex_ );
            uint64_t                    total = 0;
            for ( const auto &[key, ops] : write_op_by_key_ )
            {
                (void)key;
                total += ops;
            }
            return total;
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

    class ColdStorageGcTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            // The REAL hot CRDT RocksDB store (heads + set rows + tombstones
            // land here through the CrdtDatastore), wrapped in the SC1 spy.
            // storage::rocksdb::create does NOT create parent directories
            // (unlike LocalBlockStore::Open) - create them explicitly (A4).
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

            fake_       = std::make_shared<InMemoryS3Client>();
            counting_   = std::make_shared<CountingS3Client>( fake_ );
            auto opened_remote = S3BlockStore::Open( counting_ );
            ASSERT_TRUE( opened_remote.has_value() );
            remote_ = std::shared_ptr<S3BlockStore>( std::move( opened_remote.value() ) );

            fake_admin_     = std::make_shared<InMemoryS3AdminClient>();
            cold_store_mutex_ = std::make_shared<std::mutex>();

            // The DAG layer (the production shared-DB shape): ONE
            // underlying RocksDB, wrapped TWICE - sgns::storage::rocksdb
            // for the CrdtDatastore + the manager's hot face (the spy),
            // and ipfs_lite::rocksdb over the SAME DB handle for the
            // RocksdbDatastore the decorator wraps (globaldb.cpp:803's
            // IpfsRocksDb::create( m_datastore->getDB() ) shape). Hot
            // block bytes + seal text keys land in the real store the
            // GC's hot cleanup deletes from.
            auto ipfs_db = sgns::ipfs_lite::rocksdb::create( real_hot_->getDB() );
            ASSERT_TRUE( ipfs_db.has_value() );
            inner_     = std::make_shared<sgns::ipfs_lite::ipfs::RocksdbDatastore>( ipfs_db.value() );
            decorator_ = std::make_shared<BlockStoreBackedIpfsDatastore>(
                inner_, shared_local_, cold_store_mutex_, sgns::base::createLogger( "ColdStorageGc" ) );
            // 11-02: the REAL cold-read chain as the DAGSyncer hook - the
            // cold-dir term, the disposed-marker term (D-11), and the
            // direct-read cold chain (D-12) all live here (the re-warm
            // worker stays unstarted - these paths are synchronous).
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

        // Declaration order == reverse destruction order: manager_ dies
        // first (its threads join against still-live stores), then the
        // CRDT layer, then the DAG layer, then the stores.
        std::string                             hot_dir_;
        std::shared_ptr<rocksdb>                real_hot_;
        std::shared_ptr<PrefixSpyHotStore>      spy_;
        std::string                             shared_local_dir_;
        std::shared_ptr<LocalBlockStore>        shared_local_;
        std::shared_ptr<InMemoryS3Client>       fake_;
        std::shared_ptr<CountingS3Client>        counting_;
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
 * @given a published two-block delta chain (head d2 -> d1, both Durable
 *        and backdated past retention), an ORPHAN Durable block backdated
 *        past retention, and an ORPHAN Durable block with a young
 *        timestamp - all seeded through the real CRDT/decorator path
 * @when  a retention-configured GC pass runs (driven directly through
 *        GcPassOnce)
 * @then  the candidate set is EXACTLY the backdated unreachable orphan:
 *        reachability pins the whole delta chain (heads by construction),
 *        age pins the young orphan, and only the intersection of
 *        Durable + old + unreachable qualifies (SC1 tracer); the
 *        prefix-spy observed ZERO hot-store operations under the
 *        protected tombstone/heads/current-value/set keyspaces; the
 *        status getter reports gc_last_pass_candidates == 1.
 */
TEST_F( ColdStorageGcTest, EndToEndRetentionPassIdentifiesExactCandidateSet )
{
    // The delta chain: d2 becomes the head and LINKS to d1 (the
    // CreateDAGNode head-link shape) - both reachable from heads.
    const CID d1 = PublishElement( "gc/key1", "value1" );
    const CID d2 = PublishElement( "gc/key2", "value2" );
    // Orphans: journaled through the decorator, never part of the DAG.
    const Cid orphan_old   = SetOrphan( "orphan old payload" );
    const Cid orphan_young = SetOrphan( "orphan young payload" );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionFast( 5000 ) );
    manager->Start();
    WaitAllDurable( { Cid( d1 ), Cid( d2 ), orphan_old, orphan_young } );

    // Backdate the reachable old chain member AND the unreachable orphan.
    // Only the orphan may qualify.
    Backdate( Cid( d1 ), 60000 );
    Backdate( orphan_old, 60000 );
    // orphan_young keeps its birth timestamp (age ~ 0 << 5000).

    const auto candidates = manager->GcPassOnce();

    ASSERT_EQ( candidates.size(), 1u );
    EXPECT_TRUE( candidates[0] == orphan_old )
        << "the single candidate must be exactly the backdated unreachable Durable CID";

    const auto status = manager->GetStatus();
    EXPECT_EQ( status.gc_last_pass_candidates, 1u );
    EXPECT_GT( status.gc_last_pass_unix_ms, 0u );
    EXPECT_TRUE( status.gc_enabled );
    EXPECT_FALSE( status.gc_inhibited_staging );

    // SC1 keyspace boundary: the GC performed ZERO hot-store operations
    // under the protected tombstone/heads/current-value/set prefixes
    // during policy evaluation. Since 11-02's marking step the pass also
    // POINT-CHECKS the candidate's seal text key (a contains, never a
    // write) and writes the SC5 watermark under /crdt/gc/ - neither
    // touches a protected prefix.
    EXPECT_EQ( spy_->ProtectedPrefixOps(), 0u );
    EXPECT_EQ( spy_->OpsMatching( "/s/" ), 0u );
}

/**
 * @given an orphan Durable block whose spill timestamp sits BELOW the
 *        retention threshold by a safe margin (the boundary's under-side
 *        flank; the gate is age STRICTLY exceeding the threshold, so age
 *        == threshold lands on the same not-eligible branch)
 * @when  a driven GC pass runs
 * @then  the block is NOT a candidate - the tie breaks toward
 *        under-eviction.
 */
TEST_F( ColdStorageGcTest, AgeAtOrBelowThresholdNotEligible )
{
    const Cid orphan = SetOrphan( "boundary young-ish payload" );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionFast( 5000 ) );
    manager->Start();
    WaitAllDurable( { orphan } );

    // 4000 < 5000 with a full second of pass-latency margin: the age at
    // evaluation stays strictly below the threshold.
    Backdate( orphan, 4000 );

    const auto candidates = manager->GcPassOnce();
    EXPECT_TRUE( candidates.empty() );
    EXPECT_EQ( manager->GetStatus().gc_last_pass_candidates, 0u );
}

/**
 * @given an orphan block whose remote Put is faulted forever (its journal
 *        record stays PendingUpload - never drained, never flushed) with
 *        its timestamp backdated far past retention
 * @when  a driven GC pass runs
 * @then  the block is NOT a candidate - eligibility requires the journal
 *        state EXACTLY kEvictionDurabilityGate (UploadedDurable);
 *        anything else (NoRecord/Pending/Accepted/unreadable) is not
 *        eligible (MI-02).
 */
TEST_F( ColdStorageGcTest, DurabilityGateRequiresExactlyDurable )
{
    const Cid orphan = MakeCid( "never drained payload" );

    // Fault the remote Put BEFORE journaling: the drainer can never move
    // the record past PendingUpload.
    fake_->SetFaultForKey( DeriveTestKey( orphan ), S3Op::Put, S3FaultKind::Unavailable );
    IpfsDatastore::Value orphan_value;
    orphan_value.put( "never drained payload" );
    auto set = decorator_->set( CID( orphan ), std::move( orphan_value ) );
    ASSERT_TRUE( set.has_value() );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionFast( 5000 ) );
    manager->Start();

    // The record is stuck Pending by construction; give the drainer room
    // to attempt (and fail) so the state is observed, not assumed.
    ASSERT_TRUE( PollUntil( [&] { return fake_->ObjectKeys().empty(); },
                            std::chrono::milliseconds( 1000 ) ) );
    auto state = manager->GetUploadState( orphan );
    ASSERT_TRUE( state.has_value() );
    EXPECT_EQ( state.value(), UploadState::PendingUpload );

    Backdate( orphan, 60000 );

    const auto candidates = manager->GcPassOnce();
    const bool listed = std::find( candidates.begin(), candidates.end(), orphan ) != candidates.end();
    EXPECT_FALSE( listed )
        << "a PendingUpload record must never be eligible - the gate consults "
           "ColdStorageManager::kEvictionDurabilityGate (UploadedDurable) exactly";
}

/**
 * @given the fake admin scripted with an UNHEALTHY staging predicate
 *        (bufferUsed 900 of bufferLimit 1000 - above the 0.8 D-12
 *        fraction) and a backdated unreachable Durable orphan
 * @when  a driven GC pass runs
 * @then  the tick is skipped: zero candidates and
 *        gc_inhibited_staging reads true in the status getter; after the
 *        stats return healthy the next pass evaluates normally.
 */
TEST_F( ColdStorageGcTest, StagingInhibitionSkipsTick )
{
    const Cid orphan = SetOrphan( "inhibited payload" );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionFast( 5000 ) );
    manager->Start();
    WaitAllDurable( { orphan } );
    Backdate( orphan, 60000 );

    UploadStats unhealthy;
    unhealthy.bufferUsed  = 900;
    unhealthy.bufferLimit = 1000;
    fake_admin_->SetStats( unhealthy );

    const auto inhibited = manager->GcPassOnce();
    EXPECT_TRUE( inhibited.empty() );
    EXPECT_TRUE( manager->GetStatus().gc_inhibited_staging );
    EXPECT_EQ( manager->GetStatus().gc_last_pass_candidates, 0u );

    // Recovery: stats return healthy (bufferLimit 0 = unlimited) and the
    // next pass evaluates normally.
    fake_admin_->SetStats( UploadStats{} );
    const auto recovered = manager->GcPassOnce();
    ASSERT_EQ( recovered.size(), 1u );
    EXPECT_TRUE( recovered[0] == orphan );
    EXPECT_FALSE( manager->GetStatus().gc_inhibited_staging );
}

/**
 * @given an empty journal (no Durable records) and empty heads
 * @when  a driven GC pass runs
 * @then  the pass completes with zero candidates, no error, and the
 *        zero-pass metric recorded.
 */
TEST_F( ColdStorageGcTest, EmptyJournalZeroCandidatePass )
{
    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionFast( 5000 ) );
    manager->Start();

    const auto candidates = manager->GcPassOnce();
    EXPECT_TRUE( candidates.empty() );
    const auto status = manager->GetStatus();
    EXPECT_EQ( status.gc_last_pass_candidates, 0u );
    EXPECT_GT( status.gc_last_pass_unix_ms, 0u );
}

/**
 * @given a retention-configured manager whose GC dependencies were NEVER
 *        attached (the programmer-error posture)
 * @when  Start() runs
 * @then  the GC thread is fail-closed (never spawned): no pass ever
 *        runs, observable as gc_last_pass_unix_ms staying 0 while the
 *        drainer thread runs normally.
 */
TEST_F( ColdStorageGcTest, UnattachedDependenciesFailClosed )
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
        RetentionFast( 5000 ) );
    // Deliberately NO AttachGcDependencies call.

    manager_->Start();
    std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );

    EXPECT_EQ( manager_->GetStatus().gc_last_pass_unix_ms, 0u )
        << "the GC thread must not run without attached dependencies";
}

/**
 * @given a published delta chain whose MID-TIER block carries resolution
 *        state (sealed through the production markResolved path) yet
 *        remains linked from the head, backdated past retention
 * @when  a driven GC pass runs
 * @then  ZERO candidates come from that subtree - the walk traverses
 *        THROUGH resolved blocks (resolution marks processing state, not
 *        reachability; skipping them under-counts reachability, the
 *        wrongful-eviction direction - Pitfall 1 regression).
 */
TEST_F( ColdStorageGcTest, ResolvedOldSubtreeNeverEligible )
{
    const CID d1 = PublishElement( "gc/resolved-1", "value1" );
    const CID d2 = PublishElement( "gc/resolved-2", "value2" );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionFast( 5000 ) );
    manager->Start();
    WaitAllDurable( { Cid( d1 ), Cid( d2 ) } );

    // Resolution state present on the mid-tier block (the production
    // markResolved -> decorator seal path).
    auto sealed = dagsyncer_->markResolved( d1 );
    ASSERT_TRUE( sealed.has_value() );
    auto is_resolved = dagsyncer_->isResolved( d1 );
    ASSERT_TRUE( is_resolved.has_value() );
    EXPECT_TRUE( is_resolved.value() ) << "fixture must actually carry resolution state";

    Backdate( Cid( d1 ), 60000 );
    Backdate( Cid( d2 ), 60000 );

    const auto candidates = manager->GcPassOnce();
    EXPECT_TRUE( candidates.empty() )
        << "resolved-but-reachable blocks are NEVER candidates";
    EXPECT_EQ( spy_->ProtectedPrefixOps(), 0u );
}

/**
 * @given a backdated unreachable Durable orphan (a certain candidate) and
 *        a mid-pass hook that publishes a NEW head block between the
 *        pass-start snapshot and the finalization re-check
 * @when  the driven pass runs
 * @then  the pass ABORTS with zero candidates (heads moved during the
 *        pass - the 2.2 exactness lemma's operational rule; a normal
 *        pass over the same state would report exactly one candidate);
 *        the NEXT pass over the now-stable heads reports the candidate.
 */
TEST_F( ColdStorageGcTest, HeadsChangeDuringPassAborts )
{
    PublishElement( "gc/abort-base", "value" );
    const Cid orphan = SetOrphan( "heads-change abort payload" );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionFast( 5000 ) );
    manager->Start();
    WaitAllDurable( { orphan } );
    Backdate( orphan, 60000 );

    // The mid-pass mutation: a new head published INSIDE the pass window
    // (the SetGcMidPassHookForTesting seam, between walk and re-check).
    manager->SetGcMidPassHookForTesting( [this] {
        (void)PublishElement( "gc/abort-mid-pass", "mid-pass-value" );
    } );

    const auto aborted = manager->GcPassOnce();
    EXPECT_TRUE( aborted.empty() )
        << "heads moved during the pass - zero candidates (under-eviction)";
    EXPECT_EQ( manager->GetStatus().gc_last_pass_candidates, 0u );

    // With heads stable again, the same state yields the candidate.
    manager->SetGcMidPassHookForTesting( nullptr );
    const auto recovered = manager->GcPassOnce();
    ASSERT_EQ( recovered.size(), 1u );
    EXPECT_TRUE( recovered[0] == orphan );
}

/**
 * @given an unreachable Durable orphan whose spill timestamp is ERASED
 *        (spilled_at 0 - the pre-Phase-11 record shape) despite its true
 *        age being far past retention
 * @when  a driven pass runs
 * @then  the record is NOT a candidate regardless of true age: the pass
 *        BACKFILLS the timestamp to now (observable via GetSpilledAt)
 *        and the block waits one full retention period (backfill
 *        fairness - the under-eviction direction).
 */
TEST_F( ColdStorageGcTest, BackfillFairnessWaitsFullPeriod )
{
    const Cid orphan = SetOrphan( "backfill fairness payload" );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionFast( 5000 ) );
    manager->Start();
    WaitAllDurable( { orphan } );

    // Erase the age evidence: spilled_at 0 = absent (the pre-Phase-11
    // shape). NO backdate follows - a backdate would re-write a REAL old
    // stamp and defeat the erase; the true-age-old block must wait on the
    // absent stamp alone.
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        ASSERT_TRUE( shared_local_->SetSpilledAt( orphan, 0 ).has_value() );
    }

    const auto candidates = manager->GcPassOnce();
    EXPECT_TRUE( candidates.empty() )
        << "a record with no age evidence waits one full retention period";

    // The backfill happened: a REAL timestamp exists now.
    std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
    auto backfilled = shared_local_->GetSpilledAt( orphan );
    ASSERT_TRUE( backfilled.has_value() );
    EXPECT_GT( backfilled.value(), 0u );
}

/**
 * @given a two-topic DAG (topic-a chain of two deltas, topic-b one
 *        delta) all Durable
 * @when  driven passes run
 * @then  the SC5 epoch watermark record exists under the PINNED
 *        /crdt/gc/ prefix with per-topic floor/max heights and a
 *        computed-at stamp; a second pass REWRITES it with a fresh
 *        computed-at; the pin holds across passes (the record survives,
 *        contributes no journal candidates, and the spy saw writes ONLY
 *        under /crdt/gc/ - the four protected prefixes stay untouched);
 *        nothing in this plan consumes it for deletion.
 */
TEST_F( ColdStorageGcTest, EpochWatermarkWrittenPerPassAndPinned )
{
    const CID a1 = PublishElement( "gc/wm-a1", "v1", { "topic-a" } );
    const CID a2 = PublishElement( "gc/wm-a2", "v2", { "topic-a" } );
    const CID b1 = PublishElement( "gc/wm-b1", "v1", { "topic-b" } );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionFast( 5000 ) );
    manager->Start();
    WaitAllDurable( { Cid( a1 ), Cid( a2 ), Cid( b1 ) } );

    // Read the record through the UNWRAPPED real face (the spy's inner).
    auto read_watermark = [&]( uint64_t &computed_at ) {
        sgns::storage::Buffer key;
        key.put( std::string( ColdStorageManager::kGcEpochWatermarkKey ) );
        auto value = real_hot_->get( key );
        ASSERT_TRUE( value.has_value() );
        const auto &bytes = value.value().toVector();
        ASSERT_GE( bytes.size(), 12u );
        auto read_u64 = [&bytes]( size_t offset ) {
            uint64_t v = 0;
            for ( unsigned i = 0; i < 8; ++i )
            {
                v |= static_cast<uint64_t>( bytes[offset + i] ) << ( 8 * i );
            }
            return v;
        };
        computed_at = read_u64( 0 );
        const uint32_t topic_count = static_cast<uint32_t>( bytes[8] )
                                   | ( static_cast<uint32_t>( bytes[9] ) << 8 )
                                   | ( static_cast<uint32_t>( bytes[10] ) << 16 )
                                   | ( static_cast<uint32_t>( bytes[11] ) << 24 );
        // Both topics present with sane floor<=max (heights >= 1).
        EXPECT_GE( topic_count, 2u );
        size_t offset = 12;
        size_t topics_seen = 0;
        for ( uint32_t t = 0; t < topic_count && offset + 2 <= bytes.size(); ++t )
        {
            const uint16_t name_len = static_cast<uint16_t>( bytes[offset] )
                                    | ( static_cast<uint16_t>( bytes[offset + 1] ) << 8 );
            offset += 2 + name_len;
            ASSERT_LE( offset + 16, bytes.size() );
            const uint64_t floor_height = read_u64( offset );
            const uint64_t max_height   = read_u64( offset + 8 );
            offset += 16;
            EXPECT_LE( floor_height, max_height );
            EXPECT_GE( max_height, 1u );
            ++topics_seen;
        }
        EXPECT_EQ( topics_seen, topic_count );
    };

    (void)manager->GcPassOnce();
    uint64_t first_computed_at = 0;
    read_watermark( first_computed_at );
    EXPECT_GT( first_computed_at, 0u );

    // Second pass rewrites with a fresh computed-at (freshness proof).
    std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    (void)manager->GcPassOnce();
    uint64_t second_computed_at = 0;
    read_watermark( second_computed_at );
    EXPECT_GT( second_computed_at, first_computed_at );

    // Pin + keyspace boundary: the record survived two passes, every
    // hot-store write the GC made sits under /crdt/gc/, and the four
    // protected prefixes saw ZERO operations.
    EXPECT_GE( spy_->OpsMatching( "/crdt/gc/" ), 2u );
    EXPECT_EQ( spy_->ProtectedPrefixOps(), 0u );
}

/**
 * @given an old Durable orphan and a driven pass, then the same orphan
 *        with its age evidence erased
 * @when  two driven passes run
 * @then  the per-pass metrics track each pass: candidates 1 then 0, and
 *        gc_last_pass_unix_ms advances monotonically.
 */
TEST_F( ColdStorageGcTest, PerPassMetricsTrackEachPass )
{
    const Cid orphan = SetOrphan( "metrics payload" );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionFast( 5000 ) );
    manager->Start();
    WaitAllDurable( { orphan } );
    Backdate( orphan, 60000 );

    const auto first = manager->GcPassOnce();
    ASSERT_EQ( first.size(), 1u );
    const auto status_first = manager->GetStatus();
    EXPECT_EQ( status_first.gc_last_pass_candidates, 1u );
    EXPECT_GT( status_first.gc_last_pass_unix_ms, 0u );

    // Age evidence erased: the next pass backfills and reports zero.
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        ASSERT_TRUE( shared_local_->SetSpilledAt( orphan, 0 ).has_value() );
    }
    std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    const auto second = manager->GcPassOnce();
    EXPECT_TRUE( second.empty() );
    const auto status_second = manager->GetStatus();
    EXPECT_EQ( status_second.gc_last_pass_candidates, 0u );
    EXPECT_GE( status_second.gc_last_pass_unix_ms, status_first.gc_last_pass_unix_ms );
}

// ===========================================================================
// v1.1 Phase 11 plan 11-02: the destructive half (GC-02 / SC2)
// ===========================================================================

/// The hot-store binary key for a CID (the datastore encode shape).
sgns::storage::Buffer HotKeyOf( const CID &cid )
{
    auto bytes = cid.toBytes();
    sgns::storage::Buffer key;
    key.put( bytes.value() );
    return key;
}

/// The hot-store seal text key ("<cid-string>sealed" - datastore_rocksdb
/// :47-56 shape).
sgns::storage::Buffer SealKeyOf( const CID &cid )
{
    auto cid_string = cid.toString();
    sgns::storage::Buffer key;
    key.put( cid_string.value() + "sealed" );
    return key;
}

/// The delta's OWN membership row key: ElemsPrefix(key).ChildString(cid)
/// (crdt_set.cpp:707) - the elems namespace is the SIBLING of the
/// k-namespace under the set root ("<set>/s/k" keys prefix ->
/// "<set>/s/s/<key>/<cid>" rows; GetKeysPrefix carries NO trailing
/// slash).
std::string ElemRowKeyOf( const std::string &keys_prefix,
                          const std::string &elem_key,
                          const std::string &cid_string )
{
    EXPECT_GE( keys_prefix.size(), 1u );
    return keys_prefix.substr( 0, keys_prefix.size() - 1 ) + "s/" + elem_key + "/" + cid_string;
}

/**
 * @given a real delta block that LOST head-reachability (RemoveHead) -
 *        backdated past retention, Durable, sealed (resolution state
 *        present), with its own s-row in the hot store
 * @when  two driven passes run with a fast explicit grace (mark, then
 *        due-processing with heads unchanged)
 * @then  the full D-16 two-half eviction lands as ONE policy unit: HALF 1
 *        removes cold bytes + journal + timestamp and writes the disposed
 *        marker in ONE library batch (IsDisposed true, GetUploadState
 *        absent, GetColdUploadState NoRecord); HALF 2 removes the hot
 *        binary key, the seal text key, and EXACTLY the block's own
 *        s-row via one hot batch + LRU invalidation; the t/k/h keyspaces
 *        and every OTHER s-row see ZERO operations (spy); HasBlock stays
 *        TRUE across the whole eviction moment (hot bytes before, the
 *        disposed-marker term after - D-11); and a DIRECT read still
 *        resolves the CID CID-verified through the S3 tier (D-12 - the
 *        marker is dedup-only, never read absence).
 */
TEST_F( ColdStorageGcTest, EvictionEndToEndTwoHalvesUnitAtomic )
{
    const CID d1 = PublishElement( "gc/evict-e2e", "value" );
    // Detach the head: d1 becomes unreachable (the retention target - a
    // REAL delta with real s-rows and seal state).
    auto detached = crdt_->RemoveHead( d1, "topic" );
    ASSERT_TRUE( detached.has_value() );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionFastGrace( 5000, 1 ) );
    manager->Start();
    WaitAllDurable( { Cid( d1 ) } );
    Backdate( Cid( d1 ), 60000 );

    // Resolution state present (the production markResolved -> seal path).
    ASSERT_TRUE( dagsyncer_->markResolved( d1 ).has_value() );

    const auto keys_prefix = crdt_->GetKeysPrefix();
    const auto d1_string   = d1.toString().value();
    const auto row_key     = ElemRowKeyOf( keys_prefix, "gc/evict-e2e", d1_string );

    // Preconditions: the hot store holds the binary key, the seal text
    // key, and the delta's own s-row.
    ASSERT_TRUE( real_hot_->contains( HotKeyOf( d1 ) ) );
    ASSERT_TRUE( real_hot_->contains( SealKeyOf( d1 ) ) );
    {
        sgns::storage::Buffer row_buf;
        row_buf.put( row_key );
        ASSERT_TRUE( real_hot_->contains( row_buf ) );
    }

    // Pass 1: the candidate is marked, NOTHING is deleted yet
    // (defer-to-next-tick grace).
    const auto pass1 = manager->GcPassOnce();
    ASSERT_EQ( pass1.size(), 1u );
    EXPECT_EQ( manager->GetStatus().gc_evicting_count, 1u );
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto cold_has = shared_local_->Has( Cid( d1 ) );
        ASSERT_TRUE( cold_has.has_value() );
        EXPECT_TRUE( cold_has.value() ) << "grace: cold bytes kept until due";
    }
    EXPECT_TRUE( real_hot_->contains( HotKeyOf( d1 ) ) ) << "grace: hot bytes kept until due";

    // Pass 2 (grace elapsed, heads unchanged): both halves run.
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    (void)manager->GcPassOnce();

    // HALF 1 post-state: cold bytes + journal gone, marker present.
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto cold_has = shared_local_->Has( Cid( d1 ) );
        ASSERT_TRUE( cold_has.has_value() );
        EXPECT_FALSE( cold_has.value() );
        auto state = shared_local_->GetUploadState( Cid( d1 ) );
        EXPECT_FALSE( state.has_value() ) << "journal record removed";
        auto disposed = shared_local_->IsDisposed( Cid( d1 ) );
        ASSERT_TRUE( disposed.has_value() );
        EXPECT_TRUE( disposed.value() );
        auto marker = shared_local_->GetDisposed();
        ASSERT_TRUE( marker.has_value() );
        ASSERT_EQ( marker.value().size(), 1u );
        EXPECT_TRUE( marker.value()[0].first == Cid( d1 ) );
        EXPECT_TRUE( marker.value()[0].second.was_sealed );
        EXPECT_GT( marker.value()[0].second.original_size, 0u );
    }

    // HALF 2 post-state: binary key, seal key, and EXACTLY the own s-row
    // gone; the marker answers HasBlock (D-11 fourth term).
    EXPECT_FALSE( real_hot_->contains( HotKeyOf( d1 ) ) );
    EXPECT_FALSE( real_hot_->contains( SealKeyOf( d1 ) ) );
    {
        sgns::storage::Buffer row_buf;
        row_buf.put( row_key );
        EXPECT_FALSE( real_hot_->contains( row_buf ) );
    }
    auto has_block = dagsyncer_->HasBlock( d1 );
    ASSERT_TRUE( has_block.has_value() );
    EXPECT_TRUE( has_block.value() ) << "HasBlock stays honest via the disposed-marker term";

    // The bridge reports the post-eviction journal truth: NoRecord -
    // asserted BEFORE the direct read below, whose re-learn legitimately
    // re-journals the block (D-10: re-journal => re-spill => re-drain).
    EXPECT_EQ( sgns::crdt::GetColdUploadState( manager, d1 ), sgns::crdt::ColdUploadState::NoRecord );

    // D-12 split: a DIRECT read never treats the marker as absence - the
    // cold chain resolves the disposed CID from the S3 fake, CID-verified.
    auto resolved = dagsyncer_->GetNodeWithoutRequest( d1 );
    ASSERT_TRUE( resolved.has_value() ) << "disposed CIDs stay readable through the cold chain";

    // Metrics + marks drained.
    const auto status = manager->GetStatus();
    EXPECT_EQ( status.gc_last_pass_reclaimed_count, 1u );
    EXPECT_GT( status.gc_last_pass_reclaimed_bytes, 0u );
    EXPECT_EQ( status.gc_evicting_count, 0u );
    EXPECT_EQ( status.gc_disposed_marker_count, 1u );

    // Keyspace boundary across the WHOLE eviction: zero operations under
    // /t/, /h/, /k/ and zero on any s-row other than the block's own.
    EXPECT_EQ( spy_->OpsMatching( "/t/" ), 0u );
    EXPECT_EQ( spy_->OpsMatching( "/h/" ), 0u );
    EXPECT_EQ( spy_->OpsMatching( "/k/" ), 0u );
    EXPECT_LE( spy_->OpsMatching( "/s/" ), 2u ); // get(own row) + remove(own row) at most
}

/**
 * @given an eligible candidate marked on pass 1 with a grace LONGER than
 *        the test window
 * @when  pass 2 runs immediately (grace not elapsed)
 * @then  the mark is NOT processed on pass 2 (defer-to-next-tick grace:
 *        nothing deleted, mark retained).
 */
TEST_F( ColdStorageGcTest, GraceDeferToNextTickNotProcessedEarly )
{
    const CID d1 = PublishElement( "gc/defer-e2e", "value" );
    ASSERT_TRUE( crdt_->RemoveHead( d1, "topic" ).has_value() );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionFastGrace( 5000, 60000 ) );
    manager->Start();
    WaitAllDurable( { Cid( d1 ) } );
    Backdate( Cid( d1 ), 60000 );

    const auto pass1 = manager->GcPassOnce();
    ASSERT_EQ( pass1.size(), 1u );
    EXPECT_EQ( manager->GetStatus().gc_evicting_count, 1u );

    // Immediately-driven second pass: the grace has not elapsed - the
    // mark must survive untouched.
    (void)manager->GcPassOnce();
    EXPECT_EQ( manager->GetStatus().gc_evicting_count, 1u );
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto cold_has = shared_local_->Has( Cid( d1 ) );
        ASSERT_TRUE( cold_has.has_value() );
        EXPECT_TRUE( cold_has.value() );
    }
}

/**
 * @given a marked candidate (grace NOT yet processed)
 * @when  the manager is DESTROYED mid-grace (marks are in-memory, D-15)
 *        and a fresh manager opens over the SAME stores
 * @then  NOTHING was deleted (cold bytes, hot bytes, journal all
 *        present, no marker), and the fresh manager's first pass
 *        re-identifies and re-marks the candidate from scratch.
 */
TEST_F( ColdStorageGcTest, CrashMidGraceDeletesNothingAndReMarks )
{
    const CID d1 = PublishElement( "gc/crash-mid-grace", "value" );
    ASSERT_TRUE( crdt_->RemoveHead( d1, "topic" ).has_value() );

    {
        auto manager = MakeManager( FastOptions(), FastDurability(), RetentionFastGrace( 5000, 60000 ) );
        manager->Start();
        WaitAllDurable( { Cid( d1 ) } );
        Backdate( Cid( d1 ), 60000 );
        ASSERT_EQ( manager->GcPassOnce().size(), 1u );
        EXPECT_EQ( manager->GetStatus().gc_evicting_count, 1u );
        // Destroy WITHOUT processing due marks (Stop is the TearDown
        // order; marks die with the object).
    }

    // The safe post-state: everything still present, no marker.
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto cold_has = shared_local_->Has( Cid( d1 ) );
        ASSERT_TRUE( cold_has.has_value() );
        EXPECT_TRUE( cold_has.value() );
        auto disposed = shared_local_->IsDisposed( Cid( d1 ) );
        ASSERT_TRUE( disposed.has_value() );
        EXPECT_FALSE( disposed.value() );
    }
    EXPECT_TRUE( real_hot_->contains( HotKeyOf( d1 ) ) );

    // The fresh manager re-identifies and re-marks from scratch.
    auto fresh = MakeManager( FastOptions(), FastDurability(), RetentionFastGrace( 5000, 1 ) );
    const auto reidentified = fresh->GcPassOnce();
    ASSERT_EQ( reidentified.size(), 1u );
    EXPECT_EQ( fresh->GetStatus().gc_evicting_count, 1u );
}

/**
 * @given a fully-evictable candidate where HALF 1 is driven DIRECTLY
 *        through the public library primitive (the crash-between-halves
 *        window: the library batch committed, the hot cleanup never ran)
 * @when  a fresh manager runs one pass
 * @then  FinishOrphanedHotCleanups completes HALF 2 exactly (hot binary
 *        key, seal key, and own s-rows gone; marker intact), with NO
 *        re-spill (no new journal record).
 */
TEST_F( ColdStorageGcTest, CrashBetweenHalvesOrphanRescanCompletesHotCleanup )
{
    const CID d1 = PublishElement( "gc/crash-between-halves", "value" );
    ASSERT_TRUE( crdt_->RemoveHead( d1, "topic" ).has_value() );
    ASSERT_TRUE( dagsyncer_->markResolved( d1 ).has_value() );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionFastGrace( 5000, 1 ) );
    manager->Start();
    WaitAllDurable( { Cid( d1 ) } );
    Backdate( Cid( d1 ), 60000 );
    const auto keys_prefix = crdt_->GetKeysPrefix();
    const auto row_key     = ElemRowKeyOf( keys_prefix, "gc/crash-between-halves", d1.toString().value() );
    ASSERT_TRUE( real_hot_->contains( HotKeyOf( d1 ) ) );
    ASSERT_TRUE( real_hot_->contains( SealKeyOf( d1 ) ) );

    // HALF 1 only, driven directly (the public primitive - no seam).
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        sgns::blockstore::DisposedRecord record;
        record.disposed_at_unix_ms = UnixNowMs();
        record.reclaimed_bytes     = 1;
        record.original_size       = 1;
        record.reason              = ColdStorageManager::kEvictionReasonRetention;
        record.was_sealed          = true;
        ASSERT_TRUE( shared_local_->EvictLocalAndMarkDisposed( Cid( d1 ), record ).has_value() );
    }
    // The orphaned window: hot bytes still present behind the marker.
    EXPECT_TRUE( real_hot_->contains( HotKeyOf( d1 ) ) );

    // One pass: the orphan re-scan completes HALF 2.
    (void)manager->GcPassOnce();
    EXPECT_FALSE( real_hot_->contains( HotKeyOf( d1 ) ) );
    EXPECT_FALSE( real_hot_->contains( SealKeyOf( d1 ) ) );
    {
        sgns::storage::Buffer row_buf;
        row_buf.put( row_key );
        EXPECT_FALSE( real_hot_->contains( row_buf ) );
    }
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto disposed = shared_local_->IsDisposed( Cid( d1 ) );
        ASSERT_TRUE( disposed.has_value() );
        EXPECT_TRUE( disposed.value() ) << "marker intact - no re-spill";
        auto state = shared_local_->GetUploadState( Cid( d1 ) );
        EXPECT_FALSE( state.has_value() ) << "no new journal record (nothing re-spilled)";
    }
}

/**
 * @given a marked candidate whose grace will elapse
 * @when  a NEW head block is published before the due tick (heads mutate
 *        relative to the mark's snapshot)
 * @then  the delete-time re-check CANCELS the mark: ALL bytes kept in
 *        BOTH stores, gc_evicting_count returns to 0, the CID returns to
 *        normal evaluation, and the next pass re-marks it.
 */
TEST_F( ColdStorageGcTest, DeleteTimeHeadsChangeCancelsMark )
{
    const CID d1 = PublishElement( "gc/cancel-e2e", "value" );
    ASSERT_TRUE( crdt_->RemoveHead( d1, "topic" ).has_value() );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionFastGrace( 5000, 1 ) );
    manager->Start();
    WaitAllDurable( { Cid( d1 ) } );
    Backdate( Cid( d1 ), 60000 );

    ASSERT_EQ( manager->GcPassOnce().size(), 1u );
    EXPECT_EQ( manager->GetStatus().gc_evicting_count, 1u );

    // Heads mutate BEFORE the due tick (D-14's race realized).
    (void)PublishElement( "gc/cancel-new-head", "value" );
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );

    (void)manager->GcPassOnce(); // due mark processed -> cancelled
    EXPECT_EQ( manager->GetStatus().gc_evicting_count, 0u );
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto cold_has = shared_local_->Has( Cid( d1 ) );
        ASSERT_TRUE( cold_has.has_value() );
        EXPECT_TRUE( cold_has.value() ) << "cancel keeps the cold bytes";
        auto disposed = shared_local_->IsDisposed( Cid( d1 ) );
        ASSERT_TRUE( disposed.has_value() );
        EXPECT_FALSE( disposed.value() );
    }
    EXPECT_TRUE( real_hot_->contains( HotKeyOf( d1 ) ) ) << "cancel keeps the hot bytes";

    // The CID returns to normal: still eligible on the next pass.
    const auto reidentified = manager->GcPassOnce();
    ASSERT_EQ( reidentified.size(), 1u );
    EXPECT_EQ( manager->GetStatus().gc_evicting_count, 1u );
}

/**
 * @given a fully-evictable candidate and dry_run = true
 * @when  two driven passes run (mark, then due-processing)
 * @then  the evaluation surface is IDENTICAL (candidates reported,
 *        evicting count cycles, metrics track) but ZERO writes occur on
 *        ANY key shape (spy write-op totals: only the SC5 watermark
 *        under /crdt/gc/), no library delete (bytes + journal intact),
 *        and no marker.
 */
TEST_F( ColdStorageGcTest, DryRunSuppressesBothHalvesEntirely )
{
    const CID d1 = PublishElement( "gc/dry-run-e2e", "value" );
    ASSERT_TRUE( crdt_->RemoveHead( d1, "topic" ).has_value() );

    auto retention         = RetentionFastGrace( 5000, 1 );
    retention.dry_run      = true;
    auto manager           = MakeManager( FastOptions(), FastDurability(), retention );
    manager->Start();
    WaitAllDurable( { Cid( d1 ) } );
    Backdate( Cid( d1 ), 60000 );

    ASSERT_EQ( manager->GcPassOnce().size(), 1u );
    EXPECT_EQ( manager->GetStatus().gc_evicting_count, 1u );
    EXPECT_TRUE( manager->GetStatus().gc_dry_run );

    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    (void)manager->GcPassOnce(); // due mark processed - suppressed

    // Identical evaluation surface, zero deletes anywhere.
    EXPECT_EQ( manager->GetStatus().gc_last_pass_candidates, 1u );
    EXPECT_EQ( manager->GetStatus().gc_last_pass_reclaimed_count, 0u );
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto cold_has = shared_local_->Has( Cid( d1 ) );
        ASSERT_TRUE( cold_has.has_value() );
        EXPECT_TRUE( cold_has.value() );
        auto state = shared_local_->GetUploadState( Cid( d1 ) );
        ASSERT_TRUE( state.has_value() );
        EXPECT_EQ( state.value(), sgns::blockstore::UploadState::UploadedDurable );
        auto disposed = shared_local_->IsDisposed( Cid( d1 ) );
        ASSERT_TRUE( disposed.has_value() );
        EXPECT_FALSE( disposed.value() );
    }
    EXPECT_TRUE( real_hot_->contains( HotKeyOf( d1 ) ) );

    // The write-shape proof: the ONLY writes across both passes are the
    // SC5 watermark records under the pinned /crdt/gc/ prefix.
    EXPECT_EQ( spy_->TotalWriteOps(), spy_->WriteOpsMatching( "/crdt/gc/" ) );
    EXPECT_EQ( spy_->WriteOpsMatching( "/s/" ), 0u );
    EXPECT_EQ( spy_->WriteOpsMatching( "/t/" ), 0u );
    EXPECT_EQ( spy_->WriteOpsMatching( "/h/" ), 0u );
    EXPECT_EQ( spy_->WriteOpsMatching( "/k/" ), 0u );
}

/**
 * @given a fully-evicted CID whose disposed marker is then TRIMMED (the
 *        D-10 bound realized - dedup state gone)
 * @when  the block is re-delivered through the decorator set (the
 *        pubsub-receive re-entry shape)
 * @then  the CID re-enters the pipeline with a FRESH PendingUpload
 *        journal record (re-journal => re-spill => re-drain, D-10's
 *        re-learn) and the re-delivery stays bytes-only (no seal marker
 *        restored - Phase-9 SC4).
 */
TEST_F( ColdStorageGcTest, TrimmedMarkerRelearnRejournalsOnRedelivery )
{
    const Cid orphan = MakeCid( "relearn payload" );
    {
        sgns::ipfs_lite::ipfs::IpfsDatastore::Value value;
        value.put( "relearn payload" );
        ASSERT_TRUE( decorator_->set( CID( orphan ), std::move( value ) ).has_value() );
    }

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionFastGrace( 5000, 1 ) );
    manager->Start();
    WaitAllDurable( { orphan } );
    Backdate( orphan, 60000 );

    ASSERT_EQ( manager->GcPassOnce().size(), 1u );
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    (void)manager->GcPassOnce(); // evicted

    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto disposed = shared_local_->IsDisposed( orphan );
        ASSERT_TRUE( disposed.has_value() );
        EXPECT_TRUE( disposed.value() );
    }

    // The bound realizes: trim ALL markers (age bound in the future).
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        ASSERT_TRUE( shared_local_->TrimDisposed( 0, UnixNowMs() + 1000 ).has_value() );
        auto disposed = shared_local_->IsDisposed( orphan );
        ASSERT_TRUE( disposed.has_value() );
        EXPECT_FALSE( disposed.value() ) << "marker trimmed - dedup state gone";
    }

    // Re-delivery: the decorator set re-journals (fresh PendingUpload).
    {
        sgns::ipfs_lite::ipfs::IpfsDatastore::Value value;
        value.put( "relearn payload" );
        ASSERT_TRUE( decorator_->set( CID( orphan ), std::move( value ) ).has_value() );
    }
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto state = shared_local_->GetUploadState( orphan );
        ASSERT_TRUE( state.has_value() );
        EXPECT_EQ( state.value(), sgns::blockstore::UploadState::PendingUpload )
            << "re-journal: the re-delivered block re-enters the pipeline";
        auto stamp = shared_local_->GetSpilledAt( orphan );
        ASSERT_TRUE( stamp.has_value() );
        EXPECT_GT( stamp.value(), 0u );
    }
    // Bytes-only re-delivery: no seal marker restored (SC4).
    EXPECT_FALSE( real_hot_->contains( SealKeyOf( CID( orphan ) ) ) );
}

/**
 * @given an otherwise-evictable candidate and the local store faulted
 *        Unavailable (the unreadable-evidence shape)
 * @when  the state is read through the bridge and a pass runs
 * @then  the bridge maps the failed read to the UNREADABLE enum value
 *        (never absence - MI-02), the pass evicts NOTHING, and after the
 *        fault clears the store is intact.
 */
TEST_F( ColdStorageGcTest, UnreadableEvidenceNeverEvicts )
{
    const CID d1 = PublishElement( "gc/unreadable-e2e", "value" );
    ASSERT_TRUE( crdt_->RemoveHead( d1, "topic" ).has_value() );

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionFastGrace( 5000, 1 ) );
    manager->Start();
    WaitAllDurable( { Cid( d1 ) } );
    Backdate( Cid( d1 ), 60000 );

    // Unreadable evidence: the journal read itself fails.
    shared_local_->SetFault( sgns::blockstore::FaultKind::Unavailable );
    EXPECT_EQ( sgns::crdt::GetColdUploadState( manager, d1 ),
               sgns::crdt::ColdUploadState::Unreadable )
        << "an unreadable read maps to Unreadable - never absence";

    const auto candidates = manager->GcPassOnce();
    EXPECT_TRUE( candidates.empty() ) << "no eviction on unreadable evidence";
    EXPECT_EQ( manager->GetStatus().gc_last_pass_reclaimed_count, 0u );

    // Fault clears: everything intact.
    shared_local_->SetFault( sgns::blockstore::FaultKind::None );
    {
        std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
        auto cold_has = shared_local_->Has( Cid( d1 ) );
        ASSERT_TRUE( cold_has.has_value() );
        EXPECT_TRUE( cold_has.value() );
        auto disposed = shared_local_->IsDisposed( Cid( d1 ) );
        ASSERT_TRUE( disposed.has_value() );
        EXPECT_FALSE( disposed.value() );
    }
    EXPECT_TRUE( real_hot_->contains( HotKeyOf( d1 ) ) );
}

/**
 * @given a fully-evicted, still-marked CID (marker present, hot and cold
 *        bytes gone)
 * @when  HasBlock runs (the pubsub re-delivery dedup call shape)
 * @then  the answer is TRUE via the disposed-marker term with ZERO S3
 *        Gets (the counting client's probe) - re-delivery drops at the
 *        dedup point without any network traffic (D-11).
 */
TEST_F( ColdStorageGcTest, RedeliveryDedupDropsAtHasBlockWithZeroS3Gets )
{
    const Cid orphan = MakeCid( "dedup payload" );
    {
        sgns::ipfs_lite::ipfs::IpfsDatastore::Value value;
        value.put( "dedup payload" );
        ASSERT_TRUE( decorator_->set( CID( orphan ), std::move( value ) ).has_value() );
    }

    auto manager = MakeManager( FastOptions(), FastDurability(), RetentionFastGrace( 5000, 1 ) );
    manager->Start();
    WaitAllDurable( { orphan } );
    Backdate( orphan, 60000 );
    ASSERT_EQ( manager->GcPassOnce().size(), 1u );
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    (void)manager->GcPassOnce(); // evicted

    const uint64_t gets_before = counting_->get_calls();

    auto has_block = dagsyncer_->HasBlock( CID( orphan ) );
    ASSERT_TRUE( has_block.has_value() );
    EXPECT_TRUE( has_block.value() ) << "the marker term answers the dedup query";

    EXPECT_EQ( counting_->get_calls(), gets_before )
        << "zero S3 Gets on the dedup path - the marker is a local point lookup";
}
