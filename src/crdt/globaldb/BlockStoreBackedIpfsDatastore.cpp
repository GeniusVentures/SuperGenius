/**
 * @file       BlockStoreBackedIpfsDatastore.cpp
 * @brief      Journal-spill decorator implementation (SPILL-01, v1.1
 *             Phase 8).
 *
 * GATED TU: compiled only under SGNS_COLD_STORAGE.
 *
 * set() contract (D-04 journal-first): (1) under the shared cold-store
 * mutex, journal the block bytes + PendingUpload record into the
 * LocalBlockStore as one atomic sync WriteBatch - local RocksDB I/O ONLY,
 * never remote; (2) only then perform the inner hot write with unchanged
 * semantics. A journal failure (disk full, IO error, hostile out-of-set
 * multihash) fails the Put loudly - the pair is mandatory when cold storage
 * is enabled; best-effort-continue was rejected (silent cold gaps).
 */

#include "BlockStoreBackedIpfsDatastore.hpp"

#include <ipfs_lite/ipfs/ipfs_datastore_error.hpp>

namespace sgns::crdt
{
    using IpfsDatastoreError = sgns::ipfs_lite::ipfs::IpfsDatastoreError;

    BlockStoreBackedIpfsDatastore::BlockStoreBackedIpfsDatastore(
        std::shared_ptr<ipfs_lite::ipfs::IpfsDatastore>    inner,
        std::shared_ptr<sgns::blockstore::LocalBlockStore> local_block_store,
        std::shared_ptr<std::mutex>                        cold_store_mutex,
        sgns::base::Logger                                 logger )
        : inner_( std::move( inner ) ),
          local_block_store_( std::move( local_block_store ) ),
          cold_store_mutex_( std::move( cold_store_mutex ) ),
          logger_( std::move( logger ) )
    {
        BOOST_ASSERT_MSG( inner_ != nullptr, "inner datastore is nullptr" );
        BOOST_ASSERT_MSG( local_block_store_ != nullptr, "local block store is nullptr" );
        BOOST_ASSERT_MSG( cold_store_mutex_ != nullptr, "cold store mutex is nullptr" );
    }

    bool BlockStoreBackedIpfsDatastore::ShouldLogJournalFailure()
    {
        const auto now = std::chrono::steady_clock::now();
        if ( last_journal_failure_log_.time_since_epoch().count() != 0
             && ( now - last_journal_failure_log_ ) < kJournalFailureLogInterval )
        {
            ++suppressed_journal_failures_;
            return false;
        }
        last_journal_failure_log_ = now;
        return true;
    }

    IPFS::outcome::result<void> BlockStoreBackedIpfsDatastore::set( const CID &key, Value value )
    {
        // 1. Journal FIRST: one atomic sync WriteBatch (block bytes +
        //    PendingUpload record). Local disk only - zero remote I/O ever
        //    executes on this dagMutex_-held path. The shared mutex honors
        //    the library's single-threaded-use contract against the drainer.
        try
        {
            std::lock_guard<std::mutex> lock( *cold_store_mutex_ );
            auto journaled = local_block_store_->Put(
                key, gsl::span<const uint8_t>( value.data(), value.size() ) );
            if ( !journaled.has_value() )
            {
                // D-04: local journal-write failure fails the publish
                // loudly. Rate-limited line carrying the BlockStatus code
                // only - no per-block CID stringification on this hot path
                // and never a credential value (redaction by omission).
                if ( ShouldLogJournalFailure() )
                {
                    if ( suppressed_journal_failures_ > 0 )
                    {
                        logger_->error(
                            "cold-storage journal write failed (code {}), {} more suppressed",
                            static_cast<int>( journaled.error().code ),
                            suppressed_journal_failures_ );
                        suppressed_journal_failures_ = 0;
                    }
                    else
                    {
                        logger_->error( "cold-storage journal write failed (code {})",
                                        static_cast<int>( journaled.error().code ) );
                    }
                }
                return IPFS::outcome::failure( IpfsDatastoreError::UNKNOWN );
            }
        }
        catch ( const std::invalid_argument &e )
        {
            // Out-of-set multihash CIDs throw at library entry
            // (programmer-error taxonomy, BlockStore.hpp) - a hostile or
            // unusual CID must NEVER terminate the node through the throw.
            if ( ShouldLogJournalFailure() )
            {
                logger_->error( "cold-storage journal rejected CID: {}", e.what() );
            }
            return IPFS::outcome::failure( IpfsDatastoreError::UNKNOWN );
        }
        // 2. Hot write (inner, unchanged semantics).
        return inner_->set( key, std::move( value ) );
    }

    IPFS::outcome::result<bool> BlockStoreBackedIpfsDatastore::contains( const CID &key ) const
    {
        return inner_->contains( key );
    }

    IPFS::outcome::result<void> BlockStoreBackedIpfsDatastore::seal( const CID &key )
    {
        // Seal markers are TEXT keys in the hot store's namespace - the
        // cold store never sees them.
        return inner_->seal( key );
    }

    IPFS::outcome::result<bool> BlockStoreBackedIpfsDatastore::is_sealed( const CID &key ) const
    {
        return inner_->is_sealed( key );
    }

    IPFS::outcome::result<BlockStoreBackedIpfsDatastore::Value> BlockStoreBackedIpfsDatastore::get(
        const CID &key ) const
    {
        // READ-01 (v1.1 Phase 9): the cold chain lives in GraphsyncDAGSyncer
        // (FetchNodeFromColdStorage - OUTSIDE dagMutex_ and off the
        // single-thread responder, 09-RESEARCH §4.1); ColdServeDagService is
        // the serving-side sibling of that same read-path discipline. get()
        // stays a pure forward by design - the publish mutex and the
        // responder both reach it.
        return inner_->get( key );
    }

    IPFS::outcome::result<void> BlockStoreBackedIpfsDatastore::remove( const CID &key )
    {
        // Eviction is not CRDT deletion: the cold-store block bytes AND the
        // journal record survive local eviction untouched.
        return inner_->remove( key );
    }

} // namespace sgns::crdt
