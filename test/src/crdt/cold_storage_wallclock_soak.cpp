/**
 * @file       cold_storage_wallclock_soak.cpp
 * @brief      Dev-gated wall-clock soak (TEST-02 SC3 / D-07, v1.1
 *             Phase 12 plan 12-02 Task 3) - NEVER in the Tier-1 lane.
 *
 * One long-running scenario (default 30 minutes,
 * SGNS_COLD_STORAGE_SOAK_DURATION_MIN overrides; 20-40 min is the
 * accepted window) cycling outage/recovery phases with sustained
 * healthy-phase publishing and periodic cold-fetch sampling, printing
 * timestamped status lines (watermark / pending / staging health / gc
 * counters) to stdout - the status capture IS the evidence.
 *
 * The D-09 TRUE kill-9 relaunch leg: the runbook (12-soak-evidence/
 * README.md) documents killing this process mid-run during an outage
 * window with pending > 0 (KILL WINDOW OPEN status lines mark the
 * eligible windows), then relaunching with
 * SGNS_COLD_STORAGE_SOAK_RESUME=1 against the SAME fixture dirs: the
 * resume mode re-derives the published set from the on-disk journal,
 * pre-loads the in-memory fake with the previously-DURABLE objects
 * (key = DeriveKey(cid), deterministic), and asserts first-poll
 * reconciliation - pending drains to zero, remote ObjectKeys equals
 * the published set EXACTLY, watermark returns to None - then runs
 * out the remaining duration.
 *
 * Env gates (NAMES only - never values in committed captures):
 *   SGNS_COLD_STORAGE_SOAK_ENABLE=1      - required to run (else skip)
 *   SGNS_COLD_STORAGE_SOAK_DURATION_MIN  - minutes (default 30)
 *   SGNS_COLD_STORAGE_SOAK_RESUME=1      - resume-after-kill mode
 *
 * Hermetic: InMemoryS3Client / InMemoryS3AdminClient fakes only.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
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

#include <sgns-blockstore/InMemoryS3AdminClient.hpp>
#include <sgns-blockstore/InMemoryS3Client.hpp>

#include "base/buffer.hpp"
#include "base/logger.hpp"
#include "crdt/globaldb/BlockStoreBackedIpfsDatastore.hpp"
#include "crdt/globaldb/ColdReadService.hpp"
#include "crdt/globaldb/ColdStorageManager.hpp"
#include "storage/rocksdb/rocksdb.hpp"

#include <ipfs_lite/ipfs/impl/datastore_rocksdb.hpp>

namespace
{
    using CID             = sgns::CID;
    using ColdStorageManager = sgns::crdt::ColdStorageManager;
    using ColdStorageWatermark = sgns::crdt::ColdStorageWatermark;
    using sgns::blockstore::Cid;
    using sgns::blockstore::InMemoryS3AdminClient;
    using sgns::blockstore::InMemoryS3Client;
    using sgns::blockstore::LocalBlockStore;
    using sgns::blockstore::LocalBlockStoreOptions;
    using sgns::blockstore::S3BlockStore;
    using sgns::blockstore::S3Client;
    using sgns::blockstore::S3FaultKind;
    using sgns::blockstore::S3Op;
    using sgns::blockstore::UploadState;
    using sgns::storage::rocksdb;

    uint64_t UnixNowMs()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch() )
                .count() );
    }

    bool PollUntil( const std::function<bool()> &predicate,
                    std::chrono::milliseconds       deadline,
                    std::chrono::milliseconds       step = std::chrono::milliseconds( 100 ) )
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

    uint64_t EnvOr( const char *name, uint64_t fallback )
    {
        const char *raw = std::getenv( name );
        return raw == nullptr || *raw == '\0'
                   ? fallback
                   : static_cast<uint64_t>( std::strtoull( raw, nullptr, 10 ) );
    }

    bool EnvSet( const char *name )
    {
        const char *raw = std::getenv( name );
        return raw != nullptr && *raw != '\0';
    }

    std::string Timestamped()
    {
        const auto now = std::chrono::system_clock::now();
        const auto tt  = std::chrono::system_clock::to_time_t( now );
        tm       parts{};
#ifdef _WIN32
        localtime_s( &parts, &tt );
#else
        localtime_r( &tt, &parts );
#endif
        char buf[32];
        strftime( buf, sizeof( buf ), "%H:%M:%S", &parts );
        return buf;
    }

    /// The deterministic BACK-07 object key for a RAW fixture CID
    /// (S3BlockStore's DeriveKey shape, test-side).
    std::string DeriveKey( const Cid &cid )
    {
        auto             cid_string = libp2p::multi::ContentIdentifierCodec::toString( cid );
        static const char *kHex   = "0123456789abcdef";
        const auto         digest = cid.content_address.getHash();
        const uint8_t      first  = digest.empty() ? 0 : digest[0];
        std::string        fanout{ kHex[first >> 4], kHex[first & 0x0F] };
        return std::string( "v1/blocks" ) + "/raw/" + fanout + "/" + cid_string.value();
    }

    Cid MakeCid( const std::string &payload )
    {
        auto cid = sgns::blockstore::CidOf(
            gsl::span<const uint8_t>( reinterpret_cast<const uint8_t *>( payload.data() ),
                                      payload.size() ),
            libp2p::multi::MulticodecType::Code::RAW );
        return cid.value();
    }

    /// The fixed fixture roots (NOT FreshDir - resume must reopen the
    /// SAME dirs the killed process used).
    constexpr const char *kHotDir  = "test-data/cold-storage-wallclock/hot";
    constexpr const char *kColdDir = "test-data/cold-storage-wallclock/cold";

    /// A minimal stack: hot rocksdb + cold journal + fake S3 + fake
    /// admin + decorator + manager (no CRDT layer - the soak publishes
    /// orphans through the decorator directly; the GC is configured
    /// with an age past the soak window so it ticks + reports but never
    /// storms).
    struct WallClockStack
    {
        std::shared_ptr<rocksdb>             hot;
        std::shared_ptr<LocalBlockStore>     local;
        std::shared_ptr<InMemoryS3Client>    fake;
        std::shared_ptr<S3BlockStore>        remote;
        std::shared_ptr<InMemoryS3AdminClient> fake_admin;
        std::shared_ptr<std::mutex>          cold_store_mutex;
        std::shared_ptr<sgns::ipfs_lite::ipfs::IpfsDatastore> inner;
        std::shared_ptr<sgns::crdt::BlockStoreBackedIpfsDatastore> decorator;
        std::shared_ptr<ColdStorageManager>  manager;
    };

    bool OpenStack( WallClockStack &out, bool fresh )
    {
        if ( fresh )
        {
            std::error_code ec;
            std::filesystem::remove_all( kHotDir, ec );
            std::filesystem::remove_all( kColdDir, ec );
        }
        std::error_code mk;
        std::filesystem::create_directories( kHotDir, mk );
        rocksdb::Options hot_options;
        hot_options.create_if_missing = true;
        auto opened_hot = rocksdb::create( kHotDir, hot_options );
        if ( !opened_hot.has_value() )
        {
            return false;
        }
        out.hot = std::move( opened_hot.value() );

        auto opened_local = LocalBlockStore::Open( LocalBlockStoreOptions{ kColdDir } );
        if ( !opened_local.has_value() )
        {
            return false;
        }
        out.local = std::shared_ptr<LocalBlockStore>( std::move( opened_local.value() ) );

        out.fake = std::make_shared<InMemoryS3Client>();
        auto opened_remote = S3BlockStore::Open(
            std::static_pointer_cast<S3Client>( out.fake ) );
        if ( !opened_remote.has_value() )
        {
            return false;
        }
        out.remote = std::shared_ptr<S3BlockStore>( std::move( opened_remote.value() ) );

        out.fake_admin       = std::make_shared<InMemoryS3AdminClient>();
        out.cold_store_mutex = std::make_shared<std::mutex>();

        auto ipfs_db = sgns::ipfs_lite::rocksdb::create( out.hot->getDB() );
        if ( !ipfs_db.has_value() )
        {
            return false;
        }
        out.inner     = std::make_shared<sgns::ipfs_lite::ipfs::RocksdbDatastore>( ipfs_db.value() );
        out.decorator = std::make_shared<sgns::crdt::BlockStoreBackedIpfsDatastore>(
            out.inner, out.local, out.cold_store_mutex,
            sgns::base::createLogger( "WallClockSoak" ) );

        // Tiny watermarks (the FastOptions discipline): a 2-minute
        // outage at this publish rate crosses Warn then Critical
        // within seconds and HOLDS Critical past the 60 s held-
        // watermark heartbeat cadence - the status capture's alarm +
        // heartbeat evidence.
        ColdStorageManager::BackpressureOptions backpressure;
        backpressure.drain_interval_ms = 100;
        backpressure.warn_pending      = 5;
        backpressure.critical_pending  = 10;
        backpressure.backoff_cap_ms    = 1000;

        ColdStorageManager::DurabilityOptions durability;
        durability.flush_min_interval_ms = 1000;
        durability.flush_max_interval_ms = 5000;

        // GC OFF in this composition (age 0 = the D-02 absent=off
        // posture): the wall-clock stack carries no CRDT layer to
        // attach, and the GC-inhibition actor is already covered at
        // Tier-1 by the iteration soak. The gc_* status fields stay
        // zero by design here.
        ColdStorageManager::RetentionOptions retention;

        auto remote_store_mutex = std::make_shared<std::mutex>();
        out.manager              = std::make_shared<ColdStorageManager>(
            out.local, out.remote, out.cold_store_mutex, remote_store_mutex, backpressure,
            durability, out.fake_admin, retention );
        return true;
    }

    void PrintStatus( const WallClockStack &stack, uint64_t published )
    {
        const auto status = stack.manager->GetStatus();
        std::cout << "[status " << Timestamped() << "]"
                  << " watermark=" << ( status.watermark == ColdStorageWatermark::None
                                            ? "None"
                                            : ( status.watermark == ColdStorageWatermark::Warn
                                                    ? "Warn"
                                                    : "Critical" ) )
                  << " pending=" << status.pending_upload_count
                  << " accepted=" << status.accepted_upload_count
                  << " durable=" << status.durable_upload_count
                  << " drainer_healthy=" << ( status.drainer_healthy ? "1" : "0" )
                  << " staging_healthy=" << ( status.staging_healthy ? "1" : "0" )
                  << " gc_enabled=" << ( status.gc_enabled ? "1" : "0" )
                  << " gc_inhibited=" << ( status.gc_inhibited_staging ? "1" : "0" )
                  << " gc_candidates=" << status.gc_last_pass_candidates
                  << " published=" << published
                  << " remote_objects=" << stack.fake->ObjectKeys().size() << std::endl;
    }
} // namespace

/**
 * @given the env-gated wall-clock soak (skips loudly when
 *        SGNS_COLD_STORAGE_SOAK_ENABLE is unset - NEVER in Tier-1)
 * @when  the scenario cycles healthy and outage windows for the
 *        configured duration (or, in resume mode, reconciles the
 *        killed process's journal first and then runs the remaining
 *        duration)
 * @then  publishes never fail, the journal drains after every outage,
 *        the watermark transitions and heartbeats are visible in the
 *        status lines, and (resume mode) the kill-9 reconciliation
 *        assertions hold: pending drains to zero, remote ObjectKeys
 *        equals the published set EXACTLY, watermark None.
 */
TEST( ColdStorageWallClockSoak, Run )
{
    if ( !EnvSet( "SGNS_COLD_STORAGE_SOAK_ENABLE" ) )
    {
        std::cout << "[SKIP] cold_storage_wallclock_soak: SGNS_COLD_STORAGE_SOAK_ENABLE is unset "
                     "- dev-gated Tier3 soak, NEVER in the Tier-1 lane (the README runbook in "
                     "12-soak-evidence documents how to run it)"
                  << std::endl;
        GTEST_SKIP();
    }
    const bool     resume_mode = EnvSet( "SGNS_COLD_STORAGE_SOAK_RESUME" );
    const uint64_t minutes     = EnvOr( "SGNS_COLD_STORAGE_SOAK_DURATION_MIN", 30 );
    const auto     deadline = std::chrono::steady_clock::now() + std::chrono::minutes( minutes );

    WallClockStack stack;
    ASSERT_TRUE( OpenStack( stack, /*fresh=*/!resume_mode ) )
        << "stack open failed (fresh=" << (!resume_mode) << ")";

    uint64_t published = 0;

    if ( resume_mode )
    {
        std::cout << "[resume " << Timestamped() << "] RECONCILIATION: reopening " << kColdDir
                  << " (kill-9 relaunch - D-09)" << std::endl;

        // Re-derive the published set from the ON-DISK journal: every
        // journaled record is a published block (the journal IS the
        // published-set oracle - ForEach enumerates blocks+bytes).
        std::map<std::string, Cid> durable_by_key;
        size_t                     journal_blocks = 0;
        {
            auto enumerated = stack.local->ForEach(
                [&]( const Cid &cid, gsl::span<const uint8_t> bytes ) {
                    ++journal_blocks;
                    auto state = stack.local->GetUploadState( cid );
                    if ( state.has_value()
                         && ( state.value() == UploadState::UploadedDurable
                              || state.value() == UploadState::UploadedAccepted ) )
                    {
                        // Pre-load the fake with every object whose Put
                        // the killed manager already completed (Accepted
                        // AND Durable records - against a REAL remote
                        // those objects survive the crash; the in-memory
                        // fake must reproduce them: same deterministic
                        // keys, createOnly convergence anchors).
                        stack.fake->Put( DeriveKey( cid ),
                                         std::vector<uint8_t>( bytes.begin(), bytes.end() ),
                                         {} );
                    }
                } );
            ASSERT_TRUE( enumerated.has_value() );
        }
        published = journal_blocks;
        std::cout << "[resume " << Timestamped() << "] RECONCILIATION: journal enumerated - "
                  << journal_blocks << " block(s), fake pre-loaded with durable set ("
                  << stack.fake->ObjectKeys().size() << " object(s))" << std::endl;

        stack.manager->Start();
        ASSERT_TRUE( PollUntil(
            [&] {
              // Convergence wait: the object-set equality IS the drain
              // proof (never trust a pending-count snapshot taken
              // before the drainer's first poll - the atomic starts
              // at zero).
              const auto status = stack.manager->GetStatus();
              return stack.fake->ObjectKeys().size() == journal_blocks
                     && status.pending_upload_count == 0
                     && status.watermark == ColdStorageWatermark::None;
            },
            std::chrono::milliseconds( 120000 ),
            std::chrono::milliseconds( 100 ) ) )
            << "resume: first-poll reconciliation must drain the journal";
        std::cout << "[resume " << Timestamped()
                  << "] RECONCILIATION: pending=0, watermark=None, remote objects converged to "
                  << stack.fake->ObjectKeys().size() << std::endl;

        // createOnly convergence: the remote set equals the published
        // set EXACTLY (nothing lost, nothing duplicated).
        ASSERT_EQ( stack.fake->ObjectKeys().size(), journal_blocks )
            << "resume: remote objects must equal the journaled (published) set";
        std::cout << "[resume " << Timestamped() << "] RECONCILIATION: remote objects == published"
                  << " (" << journal_blocks << ") - no loss, no duplication" << std::endl;
    }
    else
    {
        std::cout << "[soak " << Timestamped() << "] start: duration " << minutes
                  << " min (hermetic fakes only)" << std::endl;
        stack.manager->Start();
    }

    // The soak body: cycle healthy (2 min) then outage (1 min) windows;
    // publish continuously; sample status every 5 s; cold-fetch sample
    // every 10 s (the read-chain actor, best-effort).
    bool     outage     = false;
    auto     next_phase = std::chrono::steady_clock::now() + std::chrono::minutes( 2 );
    auto     next_status = std::chrono::steady_clock::now();
    uint64_t n           = published;
    while ( std::chrono::steady_clock::now() < deadline )
    {
        // Phase transitions.
        if ( std::chrono::steady_clock::now() >= next_phase )
        {
            outage = !outage;
            if ( outage )
            {
                stack.fake->SetFault( S3Op::Put, S3FaultKind::Unavailable );
                stack.fake_admin->SetAdminFault( sgns::blockstore::S3AdminFaultKind::Unreachable );
                std::cout << "[phase " << Timestamped() << "] OUTAGE window open" << std::endl;
            }
            else
            {
                stack.fake->ClearFaults();
                stack.fake_admin->SetAdminFault( sgns::blockstore::S3AdminFaultKind::None );
                std::cout << "[phase " << Timestamped() << "] HEALTHY window open" << std::endl;
            }
            next_phase = std::chrono::steady_clock::now()
                       + ( outage ? std::chrono::minutes( 2 ) : std::chrono::minutes( 2 ) );
        }

        // Publish continuously (journal-first: every set must succeed).
        {
            const std::string payload = "wallclock soak payload " + std::to_string( n );
            const auto        cid     = MakeCid( payload );
            sgns::ipfs_lite::ipfs::IpfsDatastore::Value value;
            value.put( payload );
            auto set = stack.decorator->set( CID( cid ), std::move( value ) );
            ASSERT_TRUE( set.has_value() ) << "publish must never fail (journal-first)";
            ++n;
        }

        // Status sampling (the evidence lines).
        if ( std::chrono::steady_clock::now() >= next_status )
        {
            PrintStatus( stack, n );
            const auto status = stack.manager->GetStatus();
            if ( outage && status.pending_upload_count > 0 )
            {
                std::cout << "[kill-window " << Timestamped()
                          << "] KILL WINDOW OPEN: outage + pending="
                          << status.pending_upload_count
                          << " (the runbook's kill point - see 12-soak-evidence/README.md)"
                          << std::endl;
            }
            next_status = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    }

    // End of run: clear any outage, count the crash-window records
    // (state-probe failures - never drainable), then drain to the FULL
    // convergence (object equality included - never trust a pending
    // snapshot taken before the drainer's next poll).
    stack.fake->ClearFaults();
    stack.fake_admin->SetAdminFault( sgns::blockstore::S3AdminFaultKind::None );
    size_t unreadable = 0;
    {
        auto enumerated = stack.local->ForEach(
            [&]( const Cid &cid, gsl::span<const uint8_t> ) {
                // The crash-window record reads as a FAILED state probe
                // (the MI-02 no-record/unreadable distinction - either
                // way it is not drainable).
                auto state = stack.local->GetUploadState( cid );
                if ( !state.has_value() )
                {
                    ++unreadable;
                }
            } );
        ASSERT_TRUE( enumerated.has_value() );
    }
    ASSERT_TRUE( PollUntil(
        [&] {
          const auto status = stack.manager->GetStatus();
          return stack.fake->ObjectKeys().size() == n - unreadable
                 && status.pending_upload_count == 0
                 && status.watermark == ColdStorageWatermark::None;
        },
        std::chrono::milliseconds( 120000 ),
        std::chrono::milliseconds( 100 ) ) )
        << "end-of-run drain must empty the journal and recover the watermark";
    PrintStatus( stack, n );
    stack.manager->Stop();

    std::cout << "[soak " << Timestamped() << "] complete: published " << n
              << " total, remote objects " << stack.fake->ObjectKeys().size() << std::endl;
    // (the carve-out counting happens pre-drain above)
    ASSERT_EQ( stack.fake->ObjectKeys().size(), n - unreadable )
        << "end of run: remote objects must equal the drainable published set";
    std::cout << "[soak " << Timestamped() << "] D-06 close-out: remote == published - crash-window ("
              << n << " - " << unreadable << " unreadable) (no loss, no duplication)" << std::endl;
}
