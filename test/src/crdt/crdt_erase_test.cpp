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

// MSVC: the vendored ipfs-lite rocksdb headers pulled in below include
// <windows.h>, which by default also includes winsock.h (v1); boost.asio
// headers included later require winsock2 and fail with
// "WinSock.h has already been included". WIN32_LEAN_AND_MEAN excludes
// winsock.h from windows.h (NOMINMAX is already set project-wide).
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include <gtest/gtest.h>
#include <testutil/literals.hpp>
#include <testutil/outcome.hpp>
#include <testutil/remove_all.hpp>
#include <testutil/wait_condition.hpp>

#include <ipfs_lite/ipfs/impl/datastore_rocksdb.hpp>
#include <ipfs_lite/rocksdb/rocksdb.hpp>
#include <libp2p/multi/multihash.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <storage/rocksdb/rocksdb.hpp>

#include "crdt/graphsync_dagsyncer.hpp"
#include "crdt/crdt_datastore.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/crdt_set.hpp"
#include "crdt/hierarchical_key.hpp"
#include "crdt_custom_dagsyncer.hpp"
#include "crdt_mirror_broadcaster.hpp"
#include "outcome/outcome.hpp"

namespace sgns::crdt
{
    using libp2p::multi::HashType;
    using libp2p::multi::Multihash;
    using sgns::base::Buffer;
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

    /// One temp RocksDB addressed by ALL THREE erase owners at once: the real
    /// CrdtSet (crdt_set_test.cpp assembly over the SuperGNUS facade), the
    /// real RocksdbDatastore behind a never-started GraphsyncDAGSyncer, and
    /// the raw map-key surface of the SuperGNUS facade itself. One committed
    /// batch must remove all three levels; a destroyed batch none of them.
    class EraseAtomicityTest : public ::testing::Test
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

            // Same one-DB-two-facades wiring as EraseBlockStagingTest
            // (globaldb.cpp:322-329): the vendored ipfs-lite datastore shares
            // the SAME underlying RocksDB as the SuperGNUS facade.
            auto ipfs_db_result = ipfs_lite::rocksdb::create( db->getDB() );
            ASSERT_TRUE( ipfs_db_result );
            ipfsDb    = ipfs_db_result.value();
            ipfsStore = std::make_shared<ipfs_lite::ipfs::RocksdbDatastore>( ipfsDb );
            dagSyncer = std::make_shared<GraphsyncDAGSyncer>( ipfsStore, nullptr, nullptr );

            // The CRDT-entry owner over the SAME facade: all three owners
            // address ONE database, so one batch commit moves all of them.
            crdtSet = std::make_shared<CrdtSet>( db, HierarchicalKey( "/namespace" ) );
        }

        /// Raw count of facade keys starting with aPrefix (bare prefix, so it
        /// catches anything the erase could have missed).
        size_t countKeysWithPrefix( const std::string &prefix )
        {
            Buffer prefixBuffer;
            prefixBuffer.put( prefix );
            auto queryResult = db->query( prefixBuffer );
            EXPECT_TRUE( queryResult );
            return queryResult.value().size();
        }

        bool facadeContains( const std::string &key )
        {
            Buffer keyBuffer;
            keyBuffer.put( key );
            return db->contains( keyBuffer );
        }

        const std::string databasePath = "supergenius_crdt_erase_test_atomicity";

        std::shared_ptr<rocksdb>                           db;        ///< SuperGNUS facade (owns the shared DB)
        std::shared_ptr<ipfs_lite::rocksdb>                ipfsDb;    ///< vendored facade, same DB
        std::shared_ptr<ipfs_lite::ipfs::RocksdbDatastore> ipfsStore; ///< vendored block-bytes datastore
        std::shared_ptr<GraphsyncDAGSyncer>                dagSyncer; ///< never-started staging surface
        std::shared_ptr<CrdtSet>                           crdtSet;   ///< CRDT-entry owner, same DB
    };

    TEST_F( EraseAtomicityTest, OneBatchErasesAllThreeLevels )
    {
        const auto        cid         = makeSha256Cid( "4123456789ABCDEF0123456789ABCDEF"_unhex );
        const auto        sibling     = makeSha256Cid( "5123456789ABCDEF0123456789ABCDEF"_unhex );
        const std::string key         = "atomic_target";
        const std::string sibling_key = "atomic_sibling";
        const std::string map_key     = "/tx/map/atomic_target";

        // Seed CRDT element+value for both keys through the public set path
        // (delta + PutElems: the exact write shape a merged delta performs,
        // populating the /s/ elems family plus value and priority).
        auto target_delta = crdtSet->CreateDeltaToAdd( key, std::string( "TARGET_VALUE" ) );
        ASSERT_TRUE( target_delta );
        std::vector<CrdtSet::Element> target_elements( target_delta.value()->elements().begin(),
                                                       target_delta.value()->elements().end() );
        EXPECT_OUTCOME_TRUE_1( crdtSet->PutElems( target_elements, "ID_ATOMIC", 21 ) );
        auto sibling_delta = crdtSet->CreateDeltaToAdd( sibling_key, std::string( "SIBLING_VALUE" ) );
        ASSERT_TRUE( sibling_delta );
        std::vector<CrdtSet::Element> sibling_elements( sibling_delta.value()->elements().begin(),
                                                        sibling_delta.value()->elements().end() );
        EXPECT_OUTCOME_TRUE_1( crdtSet->PutElems( sibling_elements, "ID_SIBLING", 22 ) );

        // Seed block bytes for both CIDs and seal the target.
        ipfs_lite::Buffer target_bytes;
        target_bytes.put( std::string( "BLOCK_BYTES_TARGET" ) );
        ipfs_lite::Buffer sibling_bytes;
        sibling_bytes.put( std::string( "BLOCK_BYTES_SIBLING" ) );
        EXPECT_OUTCOME_TRUE_1( ipfsStore->set( cid, target_bytes ) );
        EXPECT_OUTCOME_TRUE_1( ipfsStore->set( sibling, sibling_bytes ) );
        EXPECT_OUTCOME_TRUE_1( dagSyncer->markResolved( cid ) );

        // Seed the raw map-key value via the wrapper's put (the same surface
        // EraseEntry's third staging step removes from).
        Buffer map_value;
        map_value.put( std::string( "CID_STRING_MAP_VALUE" ) );
        EXPECT_OUTCOME_TRUE_1( db->put( Buffer{}.put( map_key ), map_value ) );

        // Present-before asserts on every level, plus sibling snapshots.
        EXPECT_EQ( 1, countKeysWithPrefix( crdtSet->ElemsPrefix( key ).GetKey() ) );
        EXPECT_TRUE( facadeContains( crdtSet->ValueKey( key ).GetKey() ) );
        EXPECT_TRUE( facadeContains( crdtSet->PriorityKey( key ).GetKey() ) );
        EXPECT_OUTCOME_TRUE( value_before, crdtSet->GetElement( key ) );
        EXPECT_OUTCOME_TRUE( sibling_value_before, crdtSet->GetElement( sibling_key ) );
        EXPECT_OUTCOME_TRUE( block_before, ipfsStore->get( cid ) );
        EXPECT_OUTCOME_TRUE( sibling_block_before, ipfsStore->get( sibling ) );
        EXPECT_TRUE( facadeContains( map_key ) );
        EXPECT_TRUE( rawSealKeyPresent( *ipfsDb, cid ) );

        // One caller-owned batch, three owner stagings, one commit.
        auto batch = db->batch();
        EXPECT_OUTCOME_TRUE_1( crdtSet->StageEraseElement( batch, key ) );
        EXPECT_OUTCOME_TRUE_1( dagSyncer->StageBlockRemoval( cid, batch ) );
        EXPECT_OUTCOME_TRUE_1( batch->remove( Buffer{}.put( map_key ) ) );
        EXPECT_OUTCOME_TRUE_1( batch->commit() );

        // Absent-after: CRDT entries, block bytes, map key.
        EXPECT_EQ( 0, countKeysWithPrefix( crdtSet->ElemsPrefix( key ).GetKey() ) );
        EXPECT_FALSE( facadeContains( crdtSet->ValueKey( key ).GetKey() ) );
        EXPECT_FALSE( facadeContains( crdtSet->PriorityKey( key ).GetKey() ) );
        EXPECT_OUTCOME_FALSE_1( crdtSet->GetElement( key ) );
        auto get_result = ipfsStore->get( cid );
        EXPECT_TRUE( get_result.has_error() );
        EXPECT_TRUE( get_result.error() == ipfs_lite::ipfs::IpfsDatastoreError::NOT_FOUND );
        EXPECT_FALSE( facadeContains( map_key ) );

        // The seal marker survives and the CID stays resolved (GATE-03).
        EXPECT_TRUE( rawSealKeyPresent( *ipfsDb, cid ) );
        EXPECT_OUTCOME_EQ( dagSyncer->isResolved( cid ), true );

        // Negative control (D-C6): untouched sibling is byte-identical on both
        // other levels.
        EXPECT_OUTCOME_TRUE( sibling_after, crdtSet->GetElement( sibling_key ) );
        EXPECT_EQ( sibling_value_before.toString(), sibling_after.toString() );
        EXPECT_OUTCOME_TRUE( sibling_block_after, ipfsStore->get( sibling ) );
        EXPECT_EQ( sibling_block_before.toHex(), sibling_block_after.toHex() );
    }

    TEST_F( EraseAtomicityTest, InterruptedBatchIsAllOrNothing )
    {
        const auto        cid     = makeSha256Cid( "6123456789ABCDEF0123456789ABCDEF"_unhex );
        const std::string key     = "atomic_target";
        const std::string map_key = "/tx/map/atomic_target";

        auto target_delta = crdtSet->CreateDeltaToAdd( key, std::string( "TARGET_VALUE" ) );
        ASSERT_TRUE( target_delta );
        std::vector<CrdtSet::Element> target_elements( target_delta.value()->elements().begin(),
                                                       target_delta.value()->elements().end() );
        EXPECT_OUTCOME_TRUE_1( crdtSet->PutElems( target_elements, "ID_ATOMIC", 21 ) );
        ipfs_lite::Buffer target_bytes;
        target_bytes.put( std::string( "BLOCK_BYTES_TARGET" ) );
        EXPECT_OUTCOME_TRUE_1( ipfsStore->set( cid, target_bytes ) );
        EXPECT_OUTCOME_TRUE_1( dagSyncer->markResolved( cid ) );
        Buffer map_value;
        map_value.put( std::string( "CID_STRING_MAP_VALUE" ) );
        EXPECT_OUTCOME_TRUE_1( db->put( Buffer{}.put( map_key ), map_value ) );

        EXPECT_OUTCOME_TRUE( value_before, crdtSet->GetElement( key ) );
        EXPECT_OUTCOME_TRUE( block_before, ipfsStore->get( cid ) );

        {
            auto batch = db->batch();
            EXPECT_OUTCOME_TRUE_1( crdtSet->StageEraseElement( batch, key ) );
            EXPECT_OUTCOME_TRUE_1( dagSyncer->StageBlockRemoval( cid, batch ) );
            EXPECT_OUTCOME_TRUE_1( batch->remove( Buffer{}.put( map_key ) ) );
            // The batch is deliberately destroyed WITHOUT commit: every
            // staged remove must die with it (SC-2 substrate level). Never
            // value-without-block, never block-without-map-entry.
        }

        // In-process: every level is still present, byte-identical.
        EXPECT_OUTCOME_TRUE( value_after, crdtSet->GetElement( key ) );
        EXPECT_EQ( value_before.toString(), value_after.toString() );
        EXPECT_OUTCOME_TRUE( block_after, ipfsStore->get( cid ) );
        EXPECT_EQ( block_before.toHex(), block_after.toHex() );
        EXPECT_TRUE( facadeContains( map_key ) );
        EXPECT_TRUE( rawSealKeyPresent( *ipfsDb, cid ) );

        // Reopen pass: fresh handles over the same path observe the same
        // three facts -- nothing was ever durably written.
        crdtSet.reset();
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
        auto reopened_set   = std::make_shared<CrdtSet>( reopened_db, HierarchicalKey( "/namespace" ) );

        EXPECT_OUTCOME_TRUE( value_reopened, reopened_set->GetElement( key ) );
        EXPECT_EQ( value_before.toString(), value_reopened.toString() );
        EXPECT_OUTCOME_TRUE( block_reopened, reopened_store->get( cid ) );
        EXPECT_EQ( block_before.toHex(), block_reopened.toHex() );
        Buffer reopened_map_key;
        reopened_map_key.put( map_key );
        EXPECT_TRUE( reopened_db->contains( reopened_map_key ) );
        EXPECT_TRUE( rawSealKeyPresent( *ipfs_reopen_result.value(), cid ) );
    }

    /// Proves the erase guards' INPUTS on real stores: the pending/active
    /// root-job query is true while a root job is genuinely stalled inside
    /// the DAG worker and false once it completes, and the LRU membership
    /// signal the third guard reads flips with InitCIDBlock (D-C5).
    class EraseGuardTest : public ::testing::Test
    {
    protected:
        /// One real-store loopback node (crdt_datastore_test.cpp assembly)
        /// over its own fresh temp RocksDB; the DAG syncer shares the
        /// caller-provided block store, which models the remote peer's
        /// blocks (CustomDagSyncer contract).
        struct RealStoreNode
        {
            std::shared_ptr<rocksdb>               db;
            std::shared_ptr<CustomDagSyncer>       dagSyncer;
            std::shared_ptr<CRDTMirrorBroadcaster> broadcaster;
            std::shared_ptr<CrdtDatastore>         datastore;
        };

        static RealStoreNode makeNode( const std::string                                     &path,
                                       const std::shared_ptr<ipfs_lite::ipfs::IpfsDatastore> &blocks )
        {
            test::removeAllWithRetry( path );
            rocksdb::Options options;
            options.create_if_missing = true; // intentionally
            auto db_result            = rocksdb::create( path, options );
            EXPECT_TRUE( db_result );

            RealStoreNode node;
            node.db          = db_result.value();
            node.dagSyncer   = std::make_shared<CustomDagSyncer>( blocks );
            node.broadcaster = std::make_shared<CRDTMirrorBroadcaster>();
            node.datastore   = CrdtDatastore::New( node.db,
                                                 HierarchicalKey( "/namespace" ),
                                                 node.dagSyncer,
                                                 node.broadcaster,
                                                 CrdtOptions::DefaultOptions() );
            return node;
        }

        /// Drains the node: releases any armed stall FIRST so a worker parked
        /// in the stall gate cannot deadlock Close(), then closes the
        /// datastore and detaches the mirror.
        static void closeNode( RealStoreNode &node )
        {
            if ( node.dagSyncer )
            {
                node.dagSyncer->ReleaseRootFetchStall();
            }
            if ( node.datastore )
            {
                node.datastore->Close();
            }
            if ( node.broadcaster )
            {
                node.broadcaster->SetMirrorCounterPart( nullptr );
            }
            node.datastore.reset();
            node.dagSyncer.reset();
            node.broadcaster.reset();
            node.db.reset();
        }
    };

    TEST_F( EraseGuardTest, PendingOrActiveGuardTracksRootJob )
    {
        const std::string sender_path   = "supergenius_crdt_erase_test_guard_sender";
        const std::string receiver_path = "supergenius_crdt_erase_test_guard_receiver";
        const std::string blocks_path   = "supergenius_crdt_erase_test_guard_blocks";

        // Shared real block store for both loopback peers.
        rocksdb::Options blocks_options;
        blocks_options.create_if_missing = true; // intentionally
        auto blocks_db_result            = rocksdb::create( blocks_path, blocks_options );
        ASSERT_TRUE( blocks_db_result );
        auto blocks_ipfs_result = ipfs_lite::rocksdb::create( blocks_db_result.value()->getDB() );
        ASSERT_TRUE( blocks_ipfs_result );
        auto blocks = std::make_shared<ipfs_lite::ipfs::RocksdbDatastore>( blocks_ipfs_result.value() );

        auto sender   = makeNode( sender_path, blocks );
        auto receiver = makeNode( receiver_path, blocks );
        sender.broadcaster->SetMirrorCounterPart( receiver.broadcaster );
        receiver.broadcaster->SetMirrorCounterPart( sender.broadcaster );
        // Subscribe the receiver so final-head traversal also fetches
        // ancestors whose announcements were coalesced (loopback recipe).
        receiver.datastore->AddTopicName( "topic" );
        receiver.datastore->Start();
        sender.datastore->Start();

        // Arm the stall BEFORE publishing: the receiver's DAG worker will
        // park inside CustomDagSyncer::getNode -- CreateRootJob's
        // fetch-a-root entry -- with the root job recorded as active.
        receiver.dagSyncer->ArmRootFetchStall();

        // One element published through the public put path: the sender
        // broadcasts the new head, and the receiver enqueues an EXTERNAL root
        // job for it (HandleCIDBroadcast -> EnqueueRootCID). A self-created
        // job never enters the pending/active queues, so the guard is proven
        // on the receiver -- the node whose worker genuinely processes the
        // root.
        Buffer value;
        value.put( std::string( "GUARD_VALUE" ) );
        auto put_result = sender.datastore->PutKey( HierarchicalKey( "/guard/key" ), value, { "topic" } );
        ASSERT_TRUE( put_result );
        const auto root_cid = put_result.value();

        // The new head is exactly the put CID (heads-list cross-check).
        {
            auto heads_result = sender.datastore->GetHeadList();
            ASSERT_TRUE( heads_result );
            bool found = false;
            for ( const auto &[topic, topic_heads] : heads_result.value().first )
            {
                found = found || topic_heads.count( root_cid ) > 0;
            }
            EXPECT_TRUE( found );
        }

        // True while the root job is stalled in the DAG worker.
        sgns::test::assertWaitForCondition( [&] { return receiver.datastore->IsRootCIDPendingOrActive( root_cid ); },
                                            std::chrono::seconds( 10 ),
                                            "stalled root job never reported pending/active" );
        EXPECT_TRUE( receiver.datastore->IsRootCIDPendingOrActive( root_cid ) );

        // Release the stall: the root job runs to completion...
        receiver.dagSyncer->ReleaseRootFetchStall();
        sgns::test::assertWaitForCondition(
            [&]
            {
                auto status = receiver.datastore->GetJobStatus( root_cid );
                return status.has_value() && status.value() == CrdtDatastore::JobStatus::COMPLETED;
            },
            std::chrono::seconds( 10 ),
            "released root job never completed" );

        // ...and the guard is false again. Job completion and the
        // pending/active bookkeeping clear are separate steps (loaded CI
        // runners regularly land in the window between them) — wait for the
        // guard itself to flip, do not assert on it immediately.
        sgns::test::assertWaitForCondition( [&] { return !receiver.datastore->IsRootCIDPendingOrActive( root_cid ); },
                                            std::chrono::seconds( 10 ),
                                            "completed root job still reported pending/active" );

        // The job genuinely processed the delta, not just drained a queue.
        EXPECT_OUTCOME_EQ( receiver.datastore->HasKey( HierarchicalKey( "/guard/key" ) ), true );

        closeNode( receiver );
        closeNode( sender );
    }

    TEST_F( EraseGuardTest, LRUCacheMembershipIsGuardInput )
    {
        const std::string path = "supergenius_crdt_erase_test_guard_lru";
        test::removeAllWithRetry( path );

        rocksdb::Options options;
        options.create_if_missing = true; // intentionally
        auto db_result            = rocksdb::create( path, options );
        ASSERT_TRUE( db_result );
        auto ipfs_result = ipfs_lite::rocksdb::create( db_result.value()->getDB() );
        ASSERT_TRUE( ipfs_result );
        auto store      = std::make_shared<ipfs_lite::ipfs::RocksdbDatastore>( ipfs_result.value() );
        auto dag_syncer = std::make_shared<GraphsyncDAGSyncer>( store, nullptr, nullptr );

        const auto cid = makeSha256Cid( "7123456789ABCDEF0123456789ABCDEF"_unhex );

        // Guard-input mechanics (D-C5): membership flips exactly with
        // InitCIDBlock -- the signal EraseEntry's third guard reads.
        EXPECT_FALSE( dag_syncer->IsCIDInCache( cid ) );
        dag_syncer->InitCIDBlock( cid );
        EXPECT_TRUE( dag_syncer->IsCIDInCache( cid ) );
    }

    namespace
    {
        /// Gate input root: the production tree EraseSurfaceGateTest scans.
        std::filesystem::path gateSourceRoot()
        {
#ifdef SUPERGNUS_ERASE_GATE_SRC
            return std::filesystem::path( SUPERGNUS_ERASE_GATE_SRC );
#else
            ADD_FAILURE() << "SUPERGNUS_ERASE_GATE_SRC is not defined -- the source-scan gate "
                             "cannot run (fix test/src/crdt/CMakeLists.txt)";
            return {};
#endif
        }

        /// Recursive walker over .hpp/.cpp files under @p dir.
        std::vector<std::filesystem::path> collectSourceFiles( const std::filesystem::path &dir )
        {
            std::vector<std::filesystem::path> files;
            std::error_code                    ec;
            for ( auto it = std::filesystem::recursive_directory_iterator( dir, ec );
                  it != std::filesystem::recursive_directory_iterator();
                  it.increment( ec ) )
            {
                if ( ec )
                {
                    ADD_FAILURE() << "directory walk failed under " << dir << ": " << ec.message();
                    ec.clear();
                    continue;
                }
                if ( !it->is_regular_file( ec ) )
                {
                    continue;
                }
                const auto ext = it->path().extension().string();
                if ( ext == ".hpp" || ext == ".cpp" )
                {
                    files.push_back( it->path() );
                }
            }
            return files;
        }

        std::string readWholeFile( const std::filesystem::path &path )
        {
            std::ifstream      in( path, std::ios::binary );
            std::ostringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }

        std::string trimCopy( const std::string &line )
        {
            const auto first = line.find_first_not_of( " \t\r" );
            if ( first == std::string::npos )
            {
                return {};
            }
            const auto last = line.find_last_not_of( " \t\r" );
            return line.substr( first, last - first + 1 );
        }

        /// Extracts the function body of the @p qualified_name definition
        /// (e.g. "GlobalDB::EraseEntry") from @p file_text: the line-scan
        /// runs from the definition line to the closing brace at the
        /// definition's own indentation (Allman layout). Fails the test
        /// loudly when the definition is missing -- a renamed function must
        /// break the gate, never bypass it.
        std::string extractFunctionBody( const std::string &file_text, const std::string &qualified_name )
        {
            std::istringstream       in( file_text );
            std::vector<std::string> lines;
            std::string              line;
            while ( std::getline( in, line ) )
            {
                lines.push_back( line );
            }

            const std::string definition_marker = qualified_name + "(";
            for ( size_t i = 0; i < lines.size(); ++i )
            {
                if ( lines[i].find( definition_marker ) == std::string::npos )
                {
                    continue;
                }
                // Opening brace on its own line at the definition's
                // indentation (repository Allman layout).
                size_t open_line  = i;
                bool   found_open = false;
                for ( ; open_line < lines.size() && open_line < i + 12; ++open_line )
                {
                    if ( trimCopy( lines[open_line] ) == "{" )
                    {
                        found_open = true;
                        break;
                    }
                }
                if ( !found_open )
                {
                    ADD_FAILURE() << "no Allman opening brace found after " << qualified_name << " definition";
                    return {};
                }
                for ( size_t j = open_line + 1; j < lines.size() && j < open_line + 600; ++j )
                {
                    if ( trimCopy( lines[j] ) == "}" )
                    {
                        std::string body;
                        for ( size_t k = i; k <= j; ++k )
                        {
                            body += lines[k];
                            body += '\n';
                        }
                        return body;
                    }
                }
                ADD_FAILURE() << "no closing brace found for " << qualified_name << " body";
                return {};
            }

            ADD_FAILURE() << "definition of " << qualified_name << " not found -- the gate must be updated on rename";
            return {};
        }
    } // namespace

    /// CI-enforced bans over the production tree (project rule: gates, not
    /// promises). The scan runs at test runtime against the source root the
    /// build wired via SUPERGNUS_ERASE_GATE_SRC, so a tampered or missing
    /// tree fails the gate instead of silently passing.
    class EraseSurfaceGateTest : public ::testing::Test
    {
    protected:
        /// The base tombstone-erase tokens banned from every new erase
        /// surface: each publishes or reverts network-wide state and must
        /// never join the local physical-erase path.
        static const std::vector<std::string> &baseBannedTokens()
        {
            static const std::vector<std::string> tokens = { "DeleteKey",
                                                             "DeleteTransaction",
                                                             "removeNode",
                                                             "DeleteCIDBlock" };
            return tokens;
        }
    };

    TEST_F( EraseSurfaceGateTest, SealKeyHasNoProductionHome )
    {
        const auto root = gateSourceRoot();
        ASSERT_FALSE( root.empty() );
        for ( const auto &file : collectSourceFiles( root ) )
        {
            // The seal suffix literal's one home is the vendored
            // RocksdbDatastore under thirdparty/ -- outside src/. Production
            // code must never construct the key (GATE-03).
            EXPECT_EQ( readWholeFile( file ).find( "\"sealed\"" ), std::string::npos )
                << "quoted seal-suffix literal must stay in thirdparty/ only: " << file;
        }
    }

    TEST_F( EraseSurfaceGateTest, RemoveNodeStaysDormant )
    {
        const auto root = gateSourceRoot();
        ASSERT_FALSE( root.empty() );
        const std::vector<std::string> owner_suffixes = { "crdt/graphsync_dagsyncer.hpp",
                                                          "crdt/impl/graphsync_dagsyncer.cpp" };
        for ( const auto &file : collectSourceFiles( root ) )
        {
            const auto generic  = file.generic_string();
            const auto is_owner = std::any_of(
                owner_suffixes.begin(),
                owner_suffixes.end(),
                [&]( const std::string &s )
                {
                    return generic.size() >= s.size() && generic.compare( generic.size() - s.size(), s.size(), s ) == 0;
                } );
            if ( is_owner )
            {
                continue;
            }
            std::istringstream in( readWholeFile( file ) );
            std::string        line;
            for ( size_t lineno = 1; std::getline( in, line ); ++lineno )
            {
                // The node-removal chain cannot join an external WriteBatch;
                // its only callers are its own declaration and definition.
                EXPECT_EQ( line.find( "removeNode(" ), std::string::npos )
                    << "removeNode call outside its owner files: " << file << ":" << lineno;
            }
        }
    }

    TEST_F( EraseSurfaceGateTest, EraseSurfaceContainsNoTombstones )
    {
        const auto root = gateSourceRoot();
        ASSERT_FALSE( root.empty() );

        struct FunctionTarget
        {
            std::string              file;
            std::string              qualified_name;
            std::vector<std::string> extra_banned;
        };

        const std::vector<FunctionTarget> targets = {
            { "crdt/globaldb/globaldb.cpp", "GlobalDB::EraseEntry", {} },
            { "transaction/TransactionManager.cpp",
              "TransactionManager::EraseTransactionEntry",
              { "RevertTransaction", "RollBackPeerConfirmedNonce" } },
            { "transaction/TransactionManager.cpp",
              "TransactionManager::RemoveProcessedTransactionEntry",
              { "RevertTransaction", "RollBackPeerConfirmedNonce" } },
            { "crdt/impl/crdt_set.cpp", "CrdtSet::StageEraseElement", {} },
            { "crdt/impl/graphsync_dagsyncer.cpp", "GraphsyncDAGSyncer::StageBlockRemoval", {} },
        };

        for ( const auto &target : targets )
        {
            const auto file_path = root / target.file;
            ASSERT_TRUE( std::filesystem::exists( file_path ) ) << "tracked erase-surface file missing: " << file_path;
            const auto body = extractFunctionBody( readWholeFile( file_path ), target.qualified_name );
            ASSERT_FALSE( body.empty() );

            auto banned = baseBannedTokens();
            banned.insert( banned.end(), target.extra_banned.begin(), target.extra_banned.end() );
            for ( const auto &token : banned )
            {
                EXPECT_EQ( body.find( token ), std::string::npos )
                    << "banned token '" << token << "' inside " << target.qualified_name;
            }
        }
    }
} // namespace sgns::crdt
