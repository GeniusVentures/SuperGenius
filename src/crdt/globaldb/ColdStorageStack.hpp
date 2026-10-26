/**
 * @file       ColdStorageStack.hpp
 * @brief      Gated cold-storage stack factory + lifecycle bridges
 *             (SPILL-01, v1.1 Phase 8).
 *
 * ALWAYS COMPILED - both SGNS_COLD_STORAGE build configurations link this
 * TU set - and deliberately free of every sgns-blockstore type: the real
 * implementation lives in ColdStorageStack.cpp (compiled only under
 * SGNS_COLD_STORAGE) and the stub in ColdStorageStackStub.cpp (compiled
 * when OFF). Default builds must stay pristine: no library include, no
 * library symbol, no new code path (the Phase-7 byte-identical bar).
 *
 * globaldb.cpp reaches the stack ONLY through OpenColdStorageStack and the
 * four null-safe lifecycle bridges below - never through ColdStorageManager
 * methods directly - so the ungated translation units never see the
 * manager's library-typed interface.
 */

#ifndef SUPERGENIUS_CRDT_COLDSTORAGESTACK_HPP
#define SUPERGENIUS_CRDT_COLDSTORAGESTACK_HPP

#include <memory>
#include <mutex>
#include <string>

#include <ipfs_lite/ipfs/datastore.hpp>
#include <outcome/outcome.hpp>

#include "globaldb.hpp"

namespace sgns::crdt
{
    /**
     * @brief      Drainer watermark state over the pending-upload record
     *             count (D-02/D-08, v1.1 Phase 8).
     */
    enum class ColdStorageWatermark
    {
        None,     ///< below the warn threshold
        Warn,     ///< at/above warn_pending, below critical_pending
        Critical, ///< at/above critical_pending
    };

    /**
     * @brief      Operator-visible cold-storage status triple (D-08
     *             minimum): journal depth, watermark state, drainer health.
     *
     * Backed by drainer-maintained atomics - reading it never scans the
     * journal and never blocks the write path.
     */
    struct ColdStorageStatus
    {
        size_t              pending_upload_count{ 0 };                    ///< pending-upload record count
        ColdStorageWatermark watermark{ ColdStorageWatermark::None };     ///< current watermark
        bool                drainer_healthy{ false };                     ///< false while a failure streak is held
    };

    // Defined in ColdStorageManager.hpp (gated TU); reachable here only
    // through the bridges below.
    class ColdStorageManager;

    // Defined in ColdReadService.hpp (gated TU, v1.1 Phase 9); reachable
    // here only through the bundle fields below.
    class ColdReadService;

    // Defined in crdt/cold_read_fallback.hpp (ungated interface, v1.1
    // Phase 9) - the DAGSyncer-side hook type. No sgns-blockstore type
    // crosses this header.
    class ColdReadFallback;

    /**
     * @brief      Everything GlobalDB owns of the cold-storage stack
     *             (v1.1 Phase 8; extended in Phase 9).
     *
     * data_store is the BlockStoreBackedIpfsDatastore decorator handed to
     * GraphsyncDAGSyncer in place of the inner RocksdbDatastore; manager is
     * null until Phase 8 plan 08-02 fills it (and stays null whenever cold
     * storage is disabled); cold_store_mutex is the ONE mutex shared by the
     * publisher (decorator set) and the drainer thread honoring the
     * library's single-threaded-use contract.
     *
     * Phase 9 (READ-01) additions: remote_store_mutex is the ONE mutex
     * serializing every S3BlockStore access (the cold-read chain's Gets
     * vs the drainer's remote Puts - MinioS3Client has no
     * concurrent-call contract); cold_read_service is the chain service
     * (creation-side up-cast - globaldb.cpp never needs the complete
     * gated type); cold_read_hook is the SAME object up-cast to the
     * ungated interface for GraphsyncDAGSyncer injection. Both stay null
     * on OFF builds and disabled configs.
     */
    struct ColdStorageBundle
    {
        std::shared_ptr<ipfs_lite::ipfs::IpfsDatastore> data_store;
        std::shared_ptr<ColdStorageManager>             manager;
        std::shared_ptr<std::mutex>                     cold_store_mutex;
        std::shared_ptr<std::mutex>                     remote_store_mutex;
        std::shared_ptr<ColdReadService>                cold_read_service;
        std::shared_ptr<ColdReadFallback>               cold_read_hook;
    };

    /// Factory outcome: the bundle on success, a human-readable failure
    /// message on error (never a credential value - redaction by omission).
    using ColdStorageStackResult = outcome::result<ColdStorageBundle, std::string>;

    /**
     * @brief       Open the cold-storage stack around an inner datastore.
     *
     * Real implementation (SGNS_COLD_STORAGE=ON): derives the cold-store
     * directory as the sibling directory "coldstore" under the database
     * path (NEVER inside the backups dir - M3 D-09 journal-rebuildable
     * posture), opens the LocalBlockStore journal, constructs the
     * production S3 client via OpenMinioS3Client (eager validation, zero
     * network I/O), opens the S3BlockStore, and wraps the inner datastore
     * in the journal-spill decorator.
     * @param[in]   options the enabled cold-storage configuration
     * @param[in]   database_path_absolute absolute database path (cold
     *              store derives as its "coldstore" sibling)
     * @param[in]   inner the freshly constructed RocksdbDatastore
     * @return      the bundle (manager null in plan 08-01), or a failure
     *              message
     */
    ColdStorageStackResult OpenColdStorageStack( const GlobalDB::ColdStorageOptions &options,
                                                 const std::string                   &database_path_absolute,
                                                 std::shared_ptr<ipfs_lite::ipfs::IpfsDatastore> inner );

    /**
     * @brief      Start the drainer thread (null-safe no-op when null).
     * @param[in]  manager the bundle's manager (may be null)
     */
    void StartColdStorageManager( const std::shared_ptr<ColdStorageManager> &manager );

    /**
     * @brief      Stop the drainer thread with bounded grace then join
     *             (null-safe no-op when null; D-11).
     * @param[in]  manager the bundle's manager (may be null)
     */
    void StopColdStorageManager( const std::shared_ptr<ColdStorageManager> &manager );

    /**
     * @brief      Read the D-08 status triple from the manager's atomics.
     * @param[in]  manager the bundle's manager (may be null)
     * @return     live status, or the default triple when null
     */
    ColdStorageStatus GetColdStorageManagerStatus( const std::shared_ptr<ColdStorageManager> &manager );

    /**
     * @brief      Enqueue an ops-triggered drain (flag + notify only - the
     *             drain body NEVER runs on the caller's thread; SC2).
     * @param[in]  manager the bundle's manager (may be null)
     */
    void RequestColdStorageDrain( const std::shared_ptr<ColdStorageManager> &manager );

    /**
     * @brief      Start the re-warm worker thread (null-safe no-op when
     *             null; READ-02, v1.1 Phase 9).
     * @param[in]  service the bundle's cold-read service (may be null)
     */
    void StartColdReadService( const std::shared_ptr<ColdReadService> &service );

    /**
     * @brief      Stop the re-warm worker with bounded grace then join
     *             (null-safe no-op when null; Phase-8 D-11 shape).
     * @param[in]  service the bundle's cold-read service (may be null)
     */
    void StopColdReadService( const std::shared_ptr<ColdReadService> &service );

} // namespace sgns::crdt

#endif // SUPERGENIUS_CRDT_COLDSTORAGESTACK_HPP
