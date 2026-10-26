/**
 * @file       ColdReadService.hpp
 * @brief      Cold-read fallback chain + rehydrate service (READ-01,
 *             v1.1 Phase 9 plan 09-01).
 *
 * GATED TU HEADER: compiled (and includable) ONLY under
 * SGNS_COLD_STORAGE - it carries sgns-blockstore types. globaldb.hpp,
 * globaldb.cpp, and ColdStorageStack.hpp reach this class EXCLUSIVELY
 * through the ColdStorageStack bundle (D-10 - no library type crosses
 * the ungated boundary); the DAGSyncer-side interface it implements
 * (ColdReadFallback) lives in the ungated crdt/cold_read_fallback.hpp.
 *
 * Implements the D-01/D-02 read chain over the SAME store handles the
 * Phase-8 stack constructed (LocalBlockStore cold dir, then S3BlockStore
 * remote tier, both CID-verifying internally - verification is never
 * bypassed), the D-04 rehydrate path (bytes only, through the Phase-8
 * decorator set() - SC4), and the D-08/D-09 local-only cold-dir presence
 * check. Thread discipline: every LocalBlockStore access runs under the
 * SHARED cold_store_mutex_ (library single-threaded-use contract against
 * the publisher and the drainer); every S3BlockStore access runs under
 * the SHARED remote_store_mutex_ (the same mutex serializes the
 * drainer's remote Put phase - MinioS3Client has no concurrent-call
 * contract, 09-RESEARCH §6.3).
 */

#ifndef SUPERGENIUS_CRDT_COLDREADSERVICE_HPP
#define SUPERGENIUS_CRDT_COLDREADSERVICE_HPP

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>

#include <ipfs_lite/ipfs/datastore.hpp>

#include <sgns-blockstore/LocalBlockStore.hpp>
#include <sgns-blockstore/S3BlockStore.hpp>

#include "base/logger.hpp"
#include "crdt/cold_read_fallback.hpp"

namespace sgns::crdt
{
    class ColdReadService final : public ColdReadFallback
    {
    public:
        /**
         * @brief       Construct the cold-read chain over the shared
         *              Phase-8 stack handles.
         * @param[in]   local_block_store the cold-dir journal store (the
         *              SAME instance the decorator and drainer use)
         * @param[in]   remote_block_store the S3 tier (the SAME instance
         *              the drainer Puts through)
         * @param[in]   cold_store_mutex the ONE mutex shared with the
         *              publisher and the drainer over the LocalBlockStore
         * @param[in]   remote_store_mutex the ONE mutex serializing all
         *              S3BlockStore access (chain Gets vs drainer Puts;
         *              09-RESEARCH §6.3)
         * @param[in]   rehydrate_target the Phase-8 decorator (set()
         *              journals + writes hot - SC4's dedicated path)
         */
        ColdReadService( std::shared_ptr<sgns::blockstore::LocalBlockStore> local_block_store,
                         std::shared_ptr<sgns::blockstore::S3BlockStore>    remote_block_store,
                         std::shared_ptr<std::mutex>                        cold_store_mutex,
                         std::shared_ptr<std::mutex>                        remote_store_mutex,
                         std::shared_ptr<ipfs_lite::ipfs::IpfsDatastore>    rehydrate_target );

        ~ColdReadService() override = default;

        ColdReadService( const ColdReadService & )            = delete;
        ColdReadService &operator=( const ColdReadService & ) = delete;
        ColdReadService( ColdReadService && )                 = delete;
        ColdReadService &operator=( ColdReadService && )      = delete;

        /** @copydoc ColdReadFallback::Get() */
        ColdReadStatus Get( const CID &cid, sgns::common::Buffer &out ) override;

        /** @copydoc ColdReadFallback::HasLocalCold() */
        bool HasLocalCold( const CID &cid ) override;

        /** @copydoc ColdReadFallback::EnqueueRewarm() */
        bool EnqueueRewarm( const CID &cid ) override;

        /** @copydoc ColdReadFallback::Rehydrate() */
        void Rehydrate( const CID &cid, const sgns::common::Buffer &bytes ) override;

    private:
        /**
         * @brief   Time-based rate limiter for cold-failure logging: at
         *          most one line per kColdFailureLogInterval, counting
         *          suppressed lines so the next line stays informative
         *          (BlockStoreBackedIpfsDatastore discipline).
         * @return  true when a line may be emitted now
         */
        bool ShouldLogColdFailure();

        /// Minimum spacing between cold-failure log lines (the chain runs
        /// on the CRDT DAG worker - a failing s3d must not spam it).
        static constexpr std::chrono::seconds kColdFailureLogInterval{ 1 };

        std::shared_ptr<sgns::blockstore::LocalBlockStore> local_block_store_;
        std::shared_ptr<sgns::blockstore::S3BlockStore>    remote_block_store_;
        std::shared_ptr<std::mutex>                        cold_store_mutex_;
        std::shared_ptr<std::mutex>                        remote_store_mutex_;
        std::shared_ptr<ipfs_lite::ipfs::IpfsDatastore>    rehydrate_target_;

        // Rate-limited failure logging state (D-03/§3.4: cold failures
        // logged + counted, BlockStatus code only - never endpoint or
        // credential values).
        std::chrono::steady_clock::time_point last_cold_failure_log_{};
        size_t                                suppressed_cold_failures_{ 0 };

        // D-03 transport-failure counters (per tier; surfaced by a future
        // status getter alongside the Phase-8 drainer triple).
        std::atomic<size_t> local_transport_failures_{ 0 };
        std::atomic<size_t> remote_transport_failures_{ 0 };

        sgns::base::Logger logger_ = sgns::base::createLogger( "ColdReadService" );
    };

} // namespace sgns::crdt

#endif // SUPERGENIUS_CRDT_COLDREADSERVICE_HPP
