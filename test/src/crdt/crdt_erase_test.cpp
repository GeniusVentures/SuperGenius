/**
 * @file       crdt_erase_test.cpp
 * @brief      Block-storage erase staging tests (GraphsyncDAGSyncer::StageBlockRemoval).
 *
 * Wires the real ipfs-lite RocksdbDatastore behind a never-started
 * GraphsyncDAGSyncer on a fresh temp database shared by both RocksDB facades
 * (the one-DB-two-facades production shape, globaldb.cpp:322-329) and proves
 * block-key staging, seal-marker retention, interruption safety, and sibling
 * integrity (D-C6 erase fixture; PRUNE-02 atomicity / GATE-03 seal).
 */

#include <gtest/gtest.h>
#include <testutil/literals.hpp>
#include <testutil/outcome.hpp>
#include <testutil/remove_all.hpp>

#include <ipfs_lite/ipfs/impl/datastore_rocksdb.hpp>
#include <ipfs_lite/rocksdb/rocksdb.hpp>
#include <libp2p/multi/multihash.hpp>

#include <memory>
#include <string>
#include <vector>

#include <storage/rocksdb/rocksdb.hpp>

#include "crdt/graphsync_dagsyncer.hpp"
#include "outcome/outcome.hpp"

namespace sgns::crdt
{
    using libp2p::multi::HashType;
    using libp2p::multi::Multihash;
    using sgns::storage::rocksdb;

    namespace
    {
        CID makeSha256Cid( const std::vector<uint8_t> &digest )
        {
            return CID( CID::Version::V1,
                        CID::Multicodec::SHA2_256,
                        Multihash::create( HashType::sha256, digest ).value() );
        }

        /// Raw seal-key presence check. The seal marker key is cid.toString()
        /// plus the four-char suffix built by its ONE production home, the
        /// vendored RocksdbDatastore (thirdparty/ipfs-lite-cpp/ipfs_lite/ipfs/
        /// impl/datastore_rocksdb.cpp:48-56). Production code never constructs
        /// it; this test builds it ONLY to assert the erase left it in place
        /// (GATE-03: the traversal terminator must survive block erasure).
        bool rawSealKeyPresent( const ipfs_lite::rocksdb &aDb, const CID &aCid )
        {
            auto              cid_string = aCid.toString().value();
            ipfs_lite::Buffer seal_key;
            seal_key.put( cid_string + "sealed" );
            return aDb.contains( seal_key );
        }
    } // namespace

    class EraseBlockStagingTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            test::removeAllWithRetry( databasePath );

            rocksdb::Options options;
            options.create_if_missing = true; // intentionally
            auto data_store_result    = rocksdb::create( databasePath, options );
            ASSERT_TRUE( data_store_result );
            db = data_store_result.value();

            // Mirror the production wiring (globaldb.cpp:322-329): the vendored
            // ipfs-lite datastore shares the SAME underlying RocksDB as the
            // SuperGNUS facade, so one caller-owned batch commits to both.
            auto ipfs_db_result = ipfs_lite::rocksdb::create( db->getDB() );
            ASSERT_TRUE( ipfs_db_result );
            ipfsDb    = ipfs_db_result.value();
            ipfsStore = std::make_shared<ipfs_lite::ipfs::RocksdbDatastore>( ipfsDb );

            // Never-started instance: null graphsync/host is inert for
            // markResolved / isResolved / StageBlockRemoval (StopSync is
            // null-guarded).
            dagSyncer = std::make_shared<GraphsyncDAGSyncer>( ipfsStore, nullptr, nullptr );
        }

        const std::string databasePath = "supergenius_crdt_erase_test_block_staging";

        std::shared_ptr<rocksdb>                           db;        ///< SuperGNUS facade (owns the shared DB)
        std::shared_ptr<ipfs_lite::rocksdb>                ipfsDb;    ///< vendored facade, same DB
        std::shared_ptr<ipfs_lite::ipfs::RocksdbDatastore> ipfsStore; ///< vendored block-bytes datastore
        std::shared_ptr<GraphsyncDAGSyncer>                dagSyncer; ///< never-started staging surface
    };

    TEST_F( EraseBlockStagingTest, StagedRemovalDeletesBlockKeyOnly )
    {
        const auto cid     = makeSha256Cid( "0123456789ABCDEF0123456789ABCDEF"_unhex );
        const auto sibling = makeSha256Cid( "1123456789ABCDEF0123456789ABCDEF"_unhex );

        // Seed block bytes through the vendored datastore (the production
        // block-bytes path) and write the REAL seal via markResolved.
        ipfs_lite::Buffer target_bytes;
        target_bytes.put( std::string( "BLOCK_BYTES_TARGET" ) );
        ipfs_lite::Buffer sibling_bytes;
        sibling_bytes.put( std::string( "BLOCK_BYTES_SIBLING" ) );
        EXPECT_OUTCOME_TRUE_1( ipfsStore->set( cid, target_bytes ) );
        EXPECT_OUTCOME_TRUE_1( ipfsStore->set( sibling, sibling_bytes ) );
        EXPECT_OUTCOME_TRUE_1( dagSyncer->markResolved( cid ) );

        // Present-before asserts.
        EXPECT_OUTCOME_TRUE( sibling_hex_before, ipfsStore->get( sibling ) );
        EXPECT_OUTCOME_EQ( dagSyncer->isResolved( cid ), true );
        EXPECT_TRUE( rawSealKeyPresent( *ipfsDb, cid ) );

        // Stage into a caller-owned batch, then commit it ourselves: the
        // caller owns the single commit.
        auto batch = db->batch();
        EXPECT_OUTCOME_TRUE_1( dagSyncer->StageBlockRemoval( cid, batch ) );
        EXPECT_OUTCOME_TRUE_1( batch->commit() );

        // Absent-after: the raw block-bytes key is gone (vendored get returns
        // the NOT_FOUND error), while the seal survives and the CID stays
        // resolved.
        auto get_result = ipfsStore->get( cid );
        EXPECT_TRUE( get_result.has_error() );
        EXPECT_TRUE( get_result.error() == ipfs_lite::ipfs::IpfsDatastoreError::NOT_FOUND );
        EXPECT_OUTCOME_EQ( dagSyncer->isResolved( cid ), true );
        EXPECT_TRUE( rawSealKeyPresent( *ipfsDb, cid ) );

        // The untouched sibling's block bytes are byte-identical.
        EXPECT_OUTCOME_TRUE( sibling_after, ipfsStore->get( sibling ) );
        EXPECT_EQ( sibling_hex_before.toHex(), sibling_after.toHex() );
    }

    TEST_F( EraseBlockStagingTest, InterruptedBatchLeavesEverythingPresent )
    {
        const auto cid = makeSha256Cid( "2123456789ABCDEF0123456789ABCDEF"_unhex );

        ipfs_lite::Buffer target_bytes;
        target_bytes.put( std::string( "BLOCK_BYTES_TARGET" ) );
        EXPECT_OUTCOME_TRUE_1( ipfsStore->set( cid, target_bytes ) );
        EXPECT_OUTCOME_TRUE_1( dagSyncer->markResolved( cid ) );
        EXPECT_OUTCOME_TRUE( block_hex_before, ipfsStore->get( cid ) );

        {
            auto batch = db->batch();
            EXPECT_OUTCOME_TRUE_1( dagSyncer->StageBlockRemoval( cid, batch ) );
            // The batch is deliberately destroyed without commit: the staged
            // remove must die with it (all-or-nothing at the substrate level).
        }

        // Block bytes and seal marker are byte-identical to before staging.
        EXPECT_OUTCOME_TRUE( block_after, ipfsStore->get( cid ) );
        EXPECT_EQ( block_hex_before.toHex(), block_after.toHex() );
        EXPECT_TRUE( rawSealKeyPresent( *ipfsDb, cid ) );

        // Reopen the same database path with fresh handles and re-observe the
        // same two facts (nothing was ever durably written).
        dagSyncer.reset();
        ipfsStore.reset();
        ipfsDb.reset();
        db.reset();

        rocksdb::Options reopen_options;
        reopen_options.create_if_missing = true; // intentionally
        auto reopen_result               = rocksdb::create( databasePath, reopen_options );
        ASSERT_TRUE( reopen_result );
        auto reopened_db = reopen_result.value();

        auto ipfs_reopen_result = ipfs_lite::rocksdb::create( reopened_db->getDB() );
        ASSERT_TRUE( ipfs_reopen_result );
        auto reopened_store = std::make_shared<ipfs_lite::ipfs::RocksdbDatastore>( ipfs_reopen_result.value() );

        EXPECT_OUTCOME_TRUE( block_reopened, reopened_store->get( cid ) );
        EXPECT_EQ( block_hex_before.toHex(), block_reopened.toHex() );
        EXPECT_TRUE( rawSealKeyPresent( *ipfs_reopen_result.value(), cid ) );
    }

    TEST_F( EraseBlockStagingTest, NullBatchIsRejected )
    {
        const auto cid = makeSha256Cid( "3123456789ABCDEF0123456789ABCDEF"_unhex );

        ipfs_lite::Buffer target_bytes;
        target_bytes.put( std::string( "BLOCK_BYTES_TARGET" ) );
        EXPECT_OUTCOME_TRUE_1( ipfsStore->set( cid, target_bytes ) );

        EXPECT_OUTCOME_FALSE_1( dagSyncer->StageBlockRemoval( cid, nullptr ) );

        // Nothing was staged or committed: the block bytes are still readable.
        EXPECT_OUTCOME_TRUE( block_after, ipfsStore->get( cid ) );
        EXPECT_EQ( target_bytes.toHex(), block_after.toHex() );
    }
} // namespace sgns::crdt
