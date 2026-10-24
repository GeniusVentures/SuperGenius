/**
 * @file       BlockStoreBackedIpfsDatastore.hpp
 * @brief      Journal-spill IpfsDatastore decorator (SPILL-01, v1.1
 *             Phase 8).
 *
 * GATED TU HEADER: may include sgns-blockstore headers ONLY because the
 * TUs and tests that include it are themselves compiled under
 * SGNS_COLD_STORAGE - globaldb.hpp, globaldb.cpp, and ColdStorageStack.hpp
 * must NEVER include this header (header-hygiene gate).
 *
 * Wraps the inner RocksdbDatastore at the single IpfsDatastore construction
 * seam inside GlobalDB::New: every set() journals the block bytes plus the
 * PendingUpload record into the LocalBlockStore as ONE atomic sync
 * RocksDB WriteBatch BEFORE the inner hot write (journal-first, D-04), with
 * zero remote I/O on the dagMutex_-held write path. remove() stays hot-only
 * (eviction is not CRDT deletion); seal/is_sealed/get/contains are pure
 * forwards (seal markers are TEXT keys in the hot store's namespace).
 */

#ifndef SUPERGENIUS_CRDT_BLOCKSTOREBACKEDIPFSDATASTORE_HPP
#define SUPERGENIUS_CRDT_BLOCKSTOREBACKEDIPFSDATASTORE_HPP

#include <chrono>
#include <memory>
#include <mutex>

#include <ipfs_lite/ipfs/datastore.hpp>

#include <sgns-blockstore/LocalBlockStore.hpp>

#include "base/logger.hpp"

namespace sgns::crdt
{
    class BlockStoreBackedIpfsDatastore final : public ipfs_lite::ipfs::IpfsDatastore
    {
    public:
        /**
         * @brief       Construct the decorator around an inner datastore.
         * @param[in]   inner the hot store (RocksdbDatastore at the seam)
         * @param[in]   local_block_store the cold-store journal (the
         *              decorator holds shared ownership with the drainer)
         * @param[in]   cold_store_mutex the ONE mutex shared with the
         *              drainer thread (library single-threaded-use
         *              contract); never null
         * @param[in]   logger named logger for the (rate-limited) journal
         *              failure lines
         */
        BlockStoreBackedIpfsDatastore( std::shared_ptr<ipfs_lite::ipfs::IpfsDatastore> inner,
                                       std::shared_ptr<sgns::blockstore::LocalBlockStore> local_block_store,
                                       std::shared_ptr<std::mutex>                     cold_store_mutex,
                                       sgns::base::Logger                              logger );

        ~BlockStoreBackedIpfsDatastore() override = default;

        IPFS::outcome::result<bool> contains( const CID &key ) const override;
        IPFS::outcome::result<void> set( const CID &key, Value value ) override;
        IPFS::outcome::result<void> seal( const CID &key ) override;
        IPFS::outcome::result<bool> is_sealed( const CID &key ) const override;
        IPFS::outcome::result<Value> get( const CID &key ) const override;
        IPFS::outcome::result<void> remove( const CID &key ) override;

    private:
        /**
         * @brief   Time-based rate limiter for journal-failure logging: at
         *          most one line per kJournalFailureLogInterval, counting
         *          the suppressed lines so the next line stays informative.
         * @return  true when a line may be emitted now
         */
        bool ShouldLogJournalFailure();

        /// Minimum spacing between journal-failure log lines (hot path is
        /// never blocked by logging; a failing disk must not spam).
        static constexpr std::chrono::seconds kJournalFailureLogInterval{ 1 };

        std::shared_ptr<ipfs_lite::ipfs::IpfsDatastore>     inner_;
        std::shared_ptr<sgns::blockstore::LocalBlockStore>  local_block_store_;
        std::shared_ptr<std::mutex>                         cold_store_mutex_;
        sgns::base::Logger                                  logger_;
        std::chrono::steady_clock::time_point               last_journal_failure_log_{};
        size_t                                              suppressed_journal_failures_{ 0 };
    };

} // namespace sgns::crdt

#endif // SUPERGENIUS_CRDT_BLOCKSTOREBACKEDIPFSDATASTORE_HPP
