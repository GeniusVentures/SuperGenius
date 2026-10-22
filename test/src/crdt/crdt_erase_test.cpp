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

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
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

        /**
         * Full-body variant of @ref extractFunctionBody for the
         * source-ORDER pins (18-07): the trim-based original stops at the
         * first NESTED closing brace that sits alone on its line, so a
         * position comparison between the top and the bottom of a long
         * body (mode gate vs erase path) is impossible on its prefix. This
         * variant counts braces from the definition's opening brace until
         * the depth returns to zero. The scanned bodies' string literals
         * carry only balanced '{}' format placeholders, so character
         * counting is sound here; a rename still fails loudly.
         */
        std::string extractFullFunctionBody( const std::string &file_text, const std::string &qualified_name )
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
                size_t open_line  = i;
                bool   found_open = false;
                for ( ; open_line < lines.size() && open_line < i + 12; ++open_line )
                {
                    if ( lines[open_line].find( '{' ) != std::string::npos )
                    {
                        found_open = true;
                        break;
                    }
                }
                if ( !found_open )
                {
                    ADD_FAILURE() << "no opening brace found after " << qualified_name << " definition";
                    return {};
                }
                int depth = 0;
                for ( size_t j = open_line; j < lines.size() && j < open_line + 1200; ++j )
                {
                    depth += static_cast<int>( std::count( lines[j].begin(), lines[j].end(), '{' ) );
                    depth -= static_cast<int>( std::count( lines[j].begin(), lines[j].end(), '}' ) );
                    if ( depth <= 0 )
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
                ADD_FAILURE() << "no balanced closing brace found for " << qualified_name << " body";
                return {};
            }

            ADD_FAILURE() << "definition of " << qualified_name << " not found -- the gate must be updated on rename";
            return {};
        }

        /// True when @p file's generic path ends with @p suffix.
        bool endsWithSuffix( const std::filesystem::path &file, const std::string &suffix )
        {
            const auto generic = file.generic_string();
            return generic.size() >= suffix.size() &&
                   generic.compare( generic.size() - suffix.size(), suffix.size(), suffix ) == 0;
        }

        /**
         * Root-relative paths of every file under @p root whose text contains
         * @p token, excluding the @p owner_files (the token's sanctioned
         * declaration/definition homes). THE sanctioned-caller-set primitive:
         * callers assert the RESULT against an exact expected list (Phase 18
         * / Pattern 6 -- production erase/marker callers are exactly the
         * sanctioned set), never merely that it is small or nonempty.
         */
        std::set<std::string> filesContainingToken( const std::filesystem::path              &root,
                                                    const std::vector<std::filesystem::path> &files,
                                                    const std::string                        &token,
                                                    const std::vector<std::string>           &owner_files )
        {
            std::set<std::string> hits;
            for ( const auto &file : files )
            {
                if ( std::any_of( owner_files.begin(),
                                  owner_files.end(),
                                  [&]( const std::string &suffix ) { return endsWithSuffix( file, suffix ); } ) )
                {
                    continue;
                }
                if ( readWholeFile( file ).find( token ) != std::string::npos )
                {
                    std::error_code ec;
                    const auto      relative = std::filesystem::relative( file, root, ec );
                    if ( !ec )
                    {
                        hits.insert( relative.generic_string() );
                    }
                }
            }
            return hits;
        }
    } // namespace

    /// CI-enforced bans over the production tree (project rule: gates, not
    /// promises). The scan runs at test runtime against the source root the
    /// build wired via SUPERGNUS_ERASE_GATE_SRC, so a tampered or missing
    /// tree fails the gate instead of silently passing.
    ///
    /// Phase-18 invariant (Pattern 6): production erase/marker callers ARE
    /// legitimately nonzero now (TxPruner is the first production eraser), so
    /// the 16/17 "zero production caller" absence-proofs are obsolete. The
    /// destructive surface is instead ENUMERATION-PINNED: the tracked-body
    /// bans below pin what each mutation surface may contain (zero PutRaw in
    /// the erase path -- the marker stages via batch puts), and the
    /// sanctioned-caller greps pin EXACTLY who may call into the surface.
    class EraseSurfaceGateTest : public ::testing::Test
    {
    protected:
        /// The base tokens banned from every tracked erase-surface body:
        /// the tombstone erasers each publish or revert network-wide state
        /// and must never join the local physical-erase path, and PutRaw is
        /// banned from the erase path entirely (T-17-0406's count shifted to
        /// exactly zero: the /prune/tx marker stages via the erase
        /// WriteBatch's batch->put, never a raw direct write).
        static const std::vector<std::string> &baseBannedTokens()
        {
            static const std::vector<std::string> tokens = { "DeleteKey",
                                                             "DeleteTransaction",
                                                             "removeNode",
                                                             "DeleteCIDBlock",
                                                             "PutRaw" };
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
              "TransactionManager::EraseTransactionEntryWithMarker",
              { "RevertTransaction", "RollBackPeerConfirmedNonce" } },
            { "transaction/TransactionManager.cpp",
              "TransactionManager::EraseTransactionEntryPruned",
              { "RevertTransaction", "RollBackPeerConfirmedNonce" } },
            { "transaction/TransactionManager.cpp",
              "TransactionManager::RemoveProcessedTransactionEntry",
              { "RevertTransaction", "RollBackPeerConfirmedNonce" } },
            { "crdt/impl/crdt_set.cpp", "CrdtSet::StageEraseElement", {} },
            { "crdt/impl/graphsync_dagsyncer.cpp", "GraphsyncDAGSyncer::StageBlockRemoval", {} },
            // Phase-18 mutation surfaces:
            { "crdt/impl/crdt_set.cpp",
              "CrdtSet::MakeComposeLock",
              // The owner-vended critical section ONLY vends the guard:
              // staging/raw writes inside the lock helper would bypass the
              // compose discipline it exists to enforce.
              { "StageEraseElement", "StageBlockRemoval" } },
            { "transaction/PruneEligibility.cpp",
              "PruneEligibility::BuildPruneMarkerEntry",
              // The marker buffers are CONSTRUCTED here and staged by the
              // caller into the erase batch. The direct writer
              // (WritePrunedEntryForTest) is pinned TEST-ONLY by the
              // sanctioned-caller test below, not by this body scan (its
              // name legitimately appears in this body's twin-ship comment).
              {} },
            { "transaction/TxPruner.cpp", "TxPruner::RunCycle", {} },
            { "base/DevMintPolicy.hpp",
              "DevMintBurnlessPermitted",
              // The policy predicate reads ONLY version::GetNetworkID() --
              // never a transaction or proposal field (proposer claims are
              // attacker-chosen; Pitfall 5).
              { "GetChainId", "GetSrcAddress", "GetNonce", "GetHash", "GetUTXOParameters" } },
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

    /**
     * The Phase-18 sanctioned-caller sets (Pattern 6): the 16/17
     * "zero production erase caller" absence-proofs are obsolete BY DESIGN
     * this phase -- TxPruner is the first production eraser -- so the gates
     * now pin the caller files as EXACT lists. A stray production
     * erase/marker caller (any new file reaching into the destructive
     * surface) grows a set and fails the gate; deleting a sanctioned caller
     * shrinks it and fails just as loudly.
     */
    TEST_F( EraseSurfaceGateTest, SanctionedCallerSetsAreExact )
    {
        const auto root = gateSourceRoot();
        ASSERT_FALSE( root.empty() );
        const auto files = collectSourceFiles( root );

        // Named locals for both sides of every EXPECT_EQ: a braced set
        // inline at the macro call would split the invocation on its commas.
        const std::vector<std::string> tm_owner    = { "transaction/TransactionManager.hpp",
                                                       "transaction/TransactionManager.cpp" };
        const std::vector<std::string> prune_owner = { "transaction/PruneEligibility.hpp",
                                                       "transaction/PruneEligibility.cpp" };
        const std::vector<std::string> gdb_owner   = { "crdt/globaldb/globaldb.hpp", "crdt/globaldb/globaldb.cpp" };
        const std::set<std::string>    empty_set   = {};
        const std::set<std::string>    pruned_set  = { "transaction/TxPruner.cpp", "transaction/TxPruner.hpp" };
        const std::set<std::string>    tm_set      = { "transaction/TransactionManager.cpp",
                                                       "transaction/TransactionManager.hpp" };
        // WR-02 (review fix b0fad55b6) moved marker construction into
        // TransactionManager::EraseTransactionEntryWithMarker; PruneEligibility
        // no longer references EraseEntry, so the sanctioned compose set is
        // the 3 files below (was 4 with PruneEligibility.hpp).
        const std::set<std::string> compose_set = { "transaction/PruneEligibility.cpp",
                                                    "transaction/TransactionManager.cpp",
                                                    "transaction/TransactionManager.hpp" };

        // The pruned-erase driver: its only production caller is the TxPruner
        // cycle (decl docs + the ONE RunCycle call site); tests live outside
        // src/ (the gate's scan root).
        const auto pruned_callers = filesContainingToken( root, files, "EraseTransactionEntryPruned", tm_owner );
        EXPECT_EQ( pruned_set, pruned_callers )
            << "EraseTransactionEntryPruned callers must be exactly the TxPruner service";

        // The marker pair's one builder: staged ONLY by TransactionManager's
        // pruned driver (decl docs + the BuildPruneMarkerEntry call).
        const auto marker_callers = filesContainingToken( root, files, "BuildPruneMarkerEntry", prune_owner );
        EXPECT_EQ( tm_set, marker_callers ) << "BuildPruneMarkerEntry callers must be exactly the pruned-erase driver";

        // The guarded compose itself: outside its GlobalDB owner files, the
        // only production presence is PruneEligibility's contract comments
        // and TransactionManager's ONE composition (WithMarker) -- together
        // with the marker-pair pin above, exactly the pruned driver passes
        // the marker into EraseEntry.
        const auto erase_entry_callers = filesContainingToken( root, files, "EraseEntry", gdb_owner );
        EXPECT_EQ( compose_set, erase_entry_callers )
            << "EraseEntry callers outside GlobalDB must be exactly the TransactionManager compose + contract docs";

        // The test-only direct marker writer still has ZERO production
        // callers (the production twin stages into the erase batch instead).
        const auto test_writer_callers = filesContainingToken( root, files, "WritePrunedEntryForTest", prune_owner );
        EXPECT_EQ( empty_set, test_writer_callers ) << "WritePrunedEntryForTest must have zero production callers";

        // DeleteKey/DeleteTransaction stay absent from the pruning surface:
        // the tombstone erasers are banned from every tracked erase-path
        // body by EraseSurfaceContainsNoTombstones (the base token set) --
        // nothing in the Phase-18 surface may reach them.
    }

    /**
     * The dry-run guarantee (17-06's manual inventory, now a permanent
     * gate): RunPruneDryRunCycle's body carries NO erase, marker, staging,
     * or raw-write token and never touches the TxPruner service -- the
     * dry-run arm observes and logs, it cannot erase.
     */
    TEST_F( EraseSurfaceGateTest, DryRunCycleBodyStaysTokenClean )
    {
        const auto root = gateSourceRoot();
        ASSERT_FALSE( root.empty() );
        const auto file_path = root / "account/GeniusNode.cpp";
        ASSERT_TRUE( std::filesystem::exists( file_path ) ) << "GeniusNode.cpp missing from the scan root";
        const auto body = extractFullFunctionBody( readWholeFile( file_path ), "GeniusNode::RunPruneDryRunCycle" );
        ASSERT_FALSE( body.empty() );

        auto banned = baseBannedTokens();
        // tx_pruner_ (the service member) rather than the type name: the
        // body's comments legitimately REFERENCE TxPruner, but any use of
        // the service would go through the member.
        const std::vector<std::string> erase_tokens = { "EraseEntry",
                                                        "EraseTransactionEntry",
                                                        "BuildPruneMarkerEntry",
                                                        "StageEraseElement",
                                                        "StageBlockRemoval",
                                                        "MakeComposeLock",
                                                        "RemoveProcessedTransactionEntry",
                                                        "tx_pruner_" };
        banned.insert( banned.end(), erase_tokens.begin(), erase_tokens.end() );
        for ( const auto &token : banned )
        {
            EXPECT_EQ( body.find( token ), std::string::npos )
                << "erase-path token '" << token << "' inside the dry-run cycle body";
        }
    }

    /**
     * The real-cycle pin (T-18-0404): RunPruneCycle's erase-bearing region
     * is gated by its OWN dry_run mode check -- the FIRST occurrence of the
     * mode gate precedes the first occurrence of the erase path (the
     * TxPruner service invocation) in source order, so the real arm can
     * never erase in dry-run mode.
     */
    TEST_F( EraseSurfaceGateTest, RealCycleIsPinnedToRealModeBeforeAnyErase )
    {
        const auto root = gateSourceRoot();
        ASSERT_FALSE( root.empty() );
        const auto file_path = root / "account/GeniusNode.cpp";
        ASSERT_TRUE( std::filesystem::exists( file_path ) ) << "GeniusNode.cpp missing from the scan root";
        const auto body = extractFullFunctionBody( readWholeFile( file_path ), "GeniusNode::RunPruneCycle" );
        ASSERT_FALSE( body.empty() );

        const auto mode_gate  = body.find( "pruning_config_.dry_run" );
        const auto erase_path = body.find( "tx_pruner_->RunCycle(" );
        ASSERT_NE( mode_gate, std::string::npos ) << "RunPruneCycle lost its dry_run mode gate";
        ASSERT_NE( erase_path, std::string::npos ) << "RunPruneCycle lost its TxPruner cycle invocation";
        EXPECT_LT( mode_gate, erase_path ) << "the dry_run mode gate must precede the erase path in source order";
    }

    /**
     * The tick-gate conjunct pin (checker W4, gates-not-promises): a cycle
     * may erase ONLY past TxPruner's tick gate, and the gate must carry ALL
     * THREE conjuncts -- State::READY, an empty missing-tx set
     * (HasMissingTxHashes), and the settle-window check (settle_seconds).
     * RunCycle's body must invoke the gate (TickGateSettled) BEFORE the
     * first erase token, and the gate helper's body must contain each
     * conjunct token -- deleting any one conjunct, or the gate call, fails
     * this gate loudly.
     */
    TEST_F( EraseSurfaceGateTest, TxPrunerTickGateConjunctsPrecedeErases )
    {
        const auto root = gateSourceRoot();
        ASSERT_FALSE( root.empty() );
        const auto txpruner_path = root / "transaction/TxPruner.cpp";
        ASSERT_TRUE( std::filesystem::exists( txpruner_path ) ) << "TxPruner.cpp missing from the scan root";
        const auto txpruner_text = readWholeFile( txpruner_path );

        // (1) RunCycle: the gate invocation precedes the erase path. The
        // erase driver call is mandatory (RunCycle is the only erase path);
        // any staging token that ever appears must also sit behind the gate.
        const auto cycle_body = extractFullFunctionBody( txpruner_text, "TxPruner::RunCycle" );
        ASSERT_FALSE( cycle_body.empty() );
        const auto gate_call = cycle_body.find( "TickGateSettled(" );
        ASSERT_NE( gate_call, std::string::npos ) << "RunCycle lost its tick-gate invocation";
        const auto erase_pos = cycle_body.find( "EraseTransactionEntryPruned" );
        ASSERT_NE( erase_pos, std::string::npos ) << "RunCycle lost its erase path";
        EXPECT_LT( gate_call, erase_pos ) << "the tick gate must precede the erase path in RunCycle";
        for ( const auto *staging_token : { "StageEraseElement", "StageBlockRemoval" } )
        {
            const auto staging_pos = cycle_body.find( staging_token );
            if ( staging_pos != std::string::npos )
            {
                EXPECT_LT( gate_call, staging_pos ) << "the tick gate must precede " << staging_token << " in RunCycle";
            }
        }

        // (2) The gate helper itself: ALL THREE conjuncts present.
        const auto gate_body = extractFullFunctionBody( txpruner_text, "TxPruner::TickGateSettled" );
        ASSERT_FALSE( gate_body.empty() );
        for ( const auto *conjunct : { "State::READY", "HasMissingTxHashes", "settle_seconds" } )
        {
            EXPECT_NE( gate_body.find( conjunct ), std::string::npos )
                << "tick-gate conjunct '" << conjunct << "' deleted from TickGateSettled";
        }
    }
} // namespace sgns::crdt
