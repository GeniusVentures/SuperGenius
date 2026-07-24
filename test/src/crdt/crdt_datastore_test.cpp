#include "crdt/crdt_datastore.hpp"
#include "crdt/atomic_transaction.hpp"
#include <gtest/gtest.h>
#include <storage/rocksdb/rocksdb.hpp>
#include "outcome/outcome.hpp"
#include <testutil/outcome.hpp>
#include <testutil/literals.hpp>
#include "testutil/wait_condition.hpp"

#include <boost/filesystem.hpp>
#include <boost/algorithm/hex.hpp>
#include <libp2p/multi/multihash.hpp>
#include <ipfs_lite/ipfs/impl/in_memory_datastore.hpp>
#include <queue>
#include <array>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <future>
#include "crdt/proto/bcast.pb.h"
#include "crdt_mirror_broadcaster.hpp"
#include "crdt_custom_dagsyncer.hpp"
#include "testutil/remove_all.hpp"

namespace sgns::crdt
{
    using base::Buffer;
    using ipfs_lite::ipfs::InMemoryDatastore;
    using ipfs_lite::ipfs::IpfsDatastore;
    using ipfs_lite::ipld::IPLDNode;
    using storage::rocksdb;
    using CrdtBuffer = CrdtDatastore::Buffer;
    using Delta      = CrdtDatastore::Delta;
    using Element    = CrdtDatastore::Element;
    using libp2p::multi::HashType;
    using libp2p::multi::Multihash;

    namespace fs = boost::filesystem;

    class CrdtDatastoreTest : public ::testing::Test
    {
    public:
        // Remove leftover database if any
        const std::string databasePath = "supergenius_crdt_datastore_test";

        CrdtDatastoreTest() {}

        void SetUp() override
        {
            test::removeAllWithRetry( databasePath );
            // Create new database
            rocksdb::Options options;
            options.create_if_missing = true; // intentionally
            auto result               = rocksdb::create( databasePath, options );
            if ( !result )
            {
                throw std::invalid_argument( result.error().message() );
            }
            db_ = std::move( result.value() );

            // Create new DAGSyncer
            ipfsDataStore_ = std::make_shared<InMemoryDatastore>();
            dagSyncer_     = std::make_shared<CustomDagSyncer>( ipfsDataStore_ );

            // Create new Broadcaster
            broadcaster_ = std::make_shared<CRDTMirrorBroadcaster>();

            // Define test values
            const std::string strNamespace = "/namespace";
            namespaceKey_                  = HierarchicalKey( strNamespace );

            // Create crdtDatastore
            crdtDatastore_       = CrdtDatastore::New( db_,
                                                 namespaceKey_,
                                                 dagSyncer_,
                                                 broadcaster_,
                                                 CrdtOptions::DefaultOptions() );
            auto loggerDataStore = sgns::base::createLogger( "CrdtDatastore", "" );
            crdtDatastore_->Start();
            loggerDataStore->set_level( spdlog::level::debug );
        }

        static std::pair<std::shared_ptr<CrdtDatastore>, std::shared_ptr<CRDTMirrorBroadcaster>>
        CreateLoopBackCRDTInstance( const std::string                        &base_path,
                                    const std::shared_ptr<InMemoryDatastore> &ipfsDataStore )
        {
            // Create new database
            test::removeAllWithRetry( base_path );
            rocksdb::Options options;
            options.create_if_missing = true; // intentionally
            auto result               = rocksdb::create( base_path, options );
            if ( !result )
            {
                throw std::invalid_argument( result.error().message() );
            }
            auto db = std::move( result.value() );

            // Create new DAGSyncer
            auto dagSyncer = std::make_shared<CustomDagSyncer>( ipfsDataStore );

            // Create new Broadcaster
            auto broadcaster = std::make_shared<CRDTMirrorBroadcaster>();

            // Define test values
            const std::string strNamespace = "/namespace";
            auto              namespaceKey = HierarchicalKey( strNamespace );

            // Create crdtDatastore
            return std::make_pair(
                CrdtDatastore::New( db, namespaceKey, dagSyncer, broadcaster, CrdtOptions::DefaultOptions() ),
                broadcaster );
        }

        void CloseAndResetCRDT( std::shared_ptr<CrdtDatastore>         &crdt,
                                std::shared_ptr<CRDTMirrorBroadcaster> &broadcaster )
        {
            if ( broadcaster )
            {
                broadcaster->SetMirrorCounterPart( nullptr );
                broadcaster.reset();
            }
            if ( crdt )
            {
                crdt->Close();
                crdt.reset();
            }
        }

        void TearDown() override
        {
            if ( crdtDatastore_ )
            {
                crdtDatastore_->Close();
                crdtDatastore_ = nullptr;
            }

            // Clean up any additional datastores
            db_          = nullptr;
            dagSyncer_   = nullptr;
            broadcaster_ = nullptr;
        }

        // Helper to create a test delta
        std::shared_ptr<Delta> CreateTestDelta( const std::string &key,
                                                const std::string &value,
                                                uint64_t           priority = 1 )
        {
            auto delta   = std::make_shared<Delta>();
            auto element = delta->add_elements();
            element->set_key( key );
            element->set_value( value );
            delta->set_priority( priority );
            return delta;
        }

        std::shared_ptr<rocksdb>               db_;
        std::shared_ptr<CustomDagSyncer>       dagSyncer_;
        std::shared_ptr<CRDTMirrorBroadcaster> broadcaster_;
        std::shared_ptr<CrdtDatastore>         crdtDatastore_ = nullptr;
        std::shared_ptr<InMemoryDatastore>     ipfsDataStore_;
        HierarchicalKey                        namespaceKey_;
    };

    TEST_F( CrdtDatastoreTest, TestKeyFunctions )
    {
        auto       newKey = HierarchicalKey( "NewKey" );
        CrdtBuffer buffer;
        buffer.put( "Data" );

        EXPECT_OUTCOME_EQ( crdtDatastore_->HasKey( newKey ), false );
        EXPECT_OUTCOME_TRUE_1( crdtDatastore_->PutKey( newKey, buffer, { "topic" } ) );
        EXPECT_OUTCOME_EQ( crdtDatastore_->HasKey( newKey ), true );
        EXPECT_OUTCOME_TRUE( valueBuffer, crdtDatastore_->GetKey( newKey ) );
        EXPECT_TRUE( buffer.toString() == valueBuffer.toString() );
        EXPECT_OUTCOME_TRUE_1( crdtDatastore_->DeleteKey( newKey, { "topic" } ) );
        EXPECT_OUTCOME_EQ( crdtDatastore_->HasKey( newKey ), false );
    }

    TEST_F( CrdtDatastoreTest, TestDeleteCreatesDifferentCIDAndHidesFromQuery )
    {
        auto       key = HierarchicalKey( "claimable/task_x" );
        CrdtBuffer value;
        value.put( "task_x" );

        EXPECT_OUTCOME_TRUE( createCid, crdtDatastore_->PutKey( key, value, { "topic" } ) );
        EXPECT_OUTCOME_TRUE( createCidStr, createCid.toString() );
        EXPECT_OUTCOME_EQ( crdtDatastore_->HasKey( key ), true );

        EXPECT_OUTCOME_TRUE( queryBeforeDelete, crdtDatastore_->QueryKeyValues( key.GetKey() ) );
        EXPECT_FALSE( queryBeforeDelete.empty() );

        EXPECT_OUTCOME_TRUE( deleteCid, crdtDatastore_->DeleteKey( key, { "topic" } ) );
        EXPECT_OUTCOME_TRUE( deleteCidStr, deleteCid.toString() );

        // Creation and deletion are distinct deltas and must have distinct CIDs.
        EXPECT_NE( createCidStr, deleteCidStr );
        EXPECT_OUTCOME_EQ( crdtDatastore_->HasKey( key ), false );

        // Query view should also hide tombstoned keys.
        EXPECT_OUTCOME_TRUE( queryAfterDelete, crdtDatastore_->QueryKeyValues( key.GetKey() ) );
        EXPECT_TRUE( queryAfterDelete.empty() );
    }

    TEST_F( CrdtDatastoreTest, TestDeltaFunctions )
    {
        auto       newKey1 = HierarchicalKey( "NewKey1" );
        CrdtBuffer buffer1;
        buffer1.put( "Data1" );

        auto       newKey2 = HierarchicalKey( "NewKey2" );
        CrdtBuffer buffer2;
        buffer2.put( "Data2" );

        auto       newKey3 = HierarchicalKey( "NewKey3" );
        CrdtBuffer buffer3;
        buffer3.put( "Data3" );

        AtomicTransaction transaction = AtomicTransaction( crdtDatastore_ );
        EXPECT_OUTCOME_TRUE_1( transaction.Put( newKey1, buffer1 ) );
        EXPECT_OUTCOME_TRUE_1( transaction.Put( newKey2, buffer2 ) );
        EXPECT_OUTCOME_TRUE_1( transaction.Put( newKey3, buffer3 ) );
        // this won't work as part of the same atomic transaction, because the Remove looks for the existing key
        // to create the delta, and since it's queued in the atomic transaction, it doesn't find key2
        EXPECT_OUTCOME_TRUE_1( transaction.Commit( { "topic" } ) );
        EXPECT_OUTCOME_EQ( crdtDatastore_->HasKey( newKey1 ), true );
        AtomicTransaction transactionRemoveKey2 = AtomicTransaction( crdtDatastore_ );
        EXPECT_OUTCOME_TRUE_1( transactionRemoveKey2.Remove( newKey2 ) );
        EXPECT_OUTCOME_TRUE_1( transactionRemoveKey2.Commit( { "topic" } ) );
        EXPECT_OUTCOME_EQ( crdtDatastore_->HasKey( newKey1 ), true );
        EXPECT_OUTCOME_EQ( crdtDatastore_->HasKey( newKey2 ), false );
        EXPECT_OUTCOME_EQ( crdtDatastore_->HasKey( newKey3 ), true );

        auto       newKey4 = HierarchicalKey( "NewKey4" );
        CrdtBuffer buffer4;
        buffer4.put( "Data4" );

        auto       newKey5 = HierarchicalKey( "NewKey5" );
        CrdtBuffer buffer5;
        buffer5.put( "Data5" );

        auto delta1      = std::make_shared<Delta>();
        auto newElement1 = delta1->add_elements();
        newElement1->set_key( newKey4.GetKey() );
        newElement1->set_value( std::string( buffer4.toString() ) );
        auto newElement2 = delta1->add_tombstones();
        newElement2->set_key( newKey5.GetKey() );
        newElement2->set_value( std::string( buffer5.toString() ) );
        delta1->set_priority( 1 );

        auto delta2      = std::make_shared<Delta>();
        auto newElement3 = delta2->add_elements();
        newElement3->set_key( newKey3.GetKey() );
        newElement3->set_value( std::string( buffer3.toString() ) );
        delta2->set_priority( 2 );

        auto mergedDelta = CrdtDatastore::DeltaMerge( delta1, delta2 );
        ASSERT_TRUE( mergedDelta != nullptr );
        EXPECT_TRUE( mergedDelta->priority() == 2 );

        auto t = mergedDelta->tombstones();
        ASSERT_TRUE( t.size() == 1 );
        EXPECT_TRUE( t[0].key() == newKey5.GetKey() );
        EXPECT_TRUE( t[0].value() == std::string( buffer5.toString() ) );

        auto e = mergedDelta->elements();
        ASSERT_TRUE( e.size() == 2 );

        EXPECT_OUTCOME_TRUE_1( crdtDatastore_->Publish( mergedDelta, { "topic" } ) );
        EXPECT_OUTCOME_EQ( crdtDatastore_->HasKey( newKey3 ), true );
        EXPECT_OUTCOME_EQ( crdtDatastore_->HasKey( newKey4 ), true );
        EXPECT_OUTCOME_EQ( crdtDatastore_->HasKey( newKey5 ), false );
    }

    TEST_F( CrdtDatastoreTest, FilterCallbackOneInvalid )
    {
        // Create a filter that rejects Deltas containing a specific key
        const std::string rejectedKey = "RejectMe";
        const std::string acceptedKey = "AcceptMe";

        // Track filter calls
        std::atomic<int> filter_called_count{ 0 };
        std::promise<void> filters_complete;
        auto filters_complete_future = filters_complete.get_future();

        auto filter_func = [&]( const Element &element ) -> std::optional<std::vector<Element>>
        {
            if ( filter_called_count.fetch_add( 1 ) + 1 == 4 )
            {
                filters_complete.set_value();
            }

            // Check if any element has the rejected key

            if ( element.value() == rejectedKey )
            {
                Element tombstone = element;
                return std::vector<Element>{ tombstone }; // Reject this delta
            }
            return std::nullopt; // Accept this delta
        };

        auto crdt_pair = CreateLoopBackCRDTInstance( databasePath + "aux1", ipfsDataStore_ );

        auto second_crdt        = crdt_pair.first;
        auto second_broadcaster = crdt_pair.second;
        broadcaster_->SetMirrorCounterPart( second_broadcaster );
        second_broadcaster->SetMirrorCounterPart( broadcaster_ );

        second_crdt->RegisterElementFilter( "Key.*", filter_func );
        second_crdt->Start();

        std::shared_ptr<Delta> delta    = std::make_shared<Delta>();
        auto                   element1 = delta->add_elements();
        element1->set_key( "Key1" );
        element1->set_value( acceptedKey );
        auto element2 = delta->add_elements();
        element2->set_key( "Key2" );
        element2->set_value( rejectedKey );
        auto element3 = delta->add_elements();
        element3->set_key( "Key3" );
        element3->set_value( acceptedKey );
        auto element4 = delta->add_elements();
        element4->set_key( "Key4" );
        element4->set_value( acceptedKey );

        delta->set_priority( 1 );

        ASSERT_OUTCOME_SUCCESS( final_cid, crdtDatastore_->Publish( delta, { "topic" } ) );

        filters_complete_future.wait();
        while ( true )
        {
            auto head_height = second_crdt->GetHeadHeight( final_cid, "topic" );
            if ( head_height.has_value() && head_height.value() > 0 )
            {
                break;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }

        EXPECT_OUTCOME_EQ( second_crdt->HasKey( { "Key1" } ), true );
        EXPECT_OUTCOME_EQ( crdtDatastore_->HasKey( { "Key2" } ), true );
        EXPECT_OUTCOME_EQ( second_crdt->HasKey( { "Key2" } ), false );
        EXPECT_OUTCOME_EQ( second_crdt->HasKey( { "Key3" } ), true );
        EXPECT_OUTCOME_EQ( second_crdt->HasKey( { "Key4" } ), true );

        // Verify filter was called
        EXPECT_GE( filter_called_count, 1 );
        CloseAndResetCRDT( second_crdt, second_broadcaster );
    }

    TEST_F( CrdtDatastoreTest, FilterCallbackOneValid )
    {
        // Create a filter that rejects Deltas containing a specific key
        const std::string rejectedKey = "RejectMe";
        const std::string acceptedKey = "AcceptMe";

        // Track filter calls
        std::atomic<int> filter_called_count{ 0 };
        std::promise<void> filters_complete;
        auto filters_complete_future = filters_complete.get_future();

        auto filter_func = [&]( const Element &element ) -> std::optional<std::vector<Element>>
        {
            if ( filter_called_count.fetch_add( 1 ) + 1 == 4 )
            {
                filters_complete.set_value();
            }

            // Check if any element has the rejected key

            if ( element.value() == rejectedKey )
            {
                Element tombstone = element;
                return std::vector<Element>{ tombstone }; // Reject this delta
            }
            return std::nullopt; // Accept this delta
        };

        auto crdt_pair = CreateLoopBackCRDTInstance( databasePath + "aux2", ipfsDataStore_ );

        auto second_crdt        = crdt_pair.first;
        auto second_broadcaster = crdt_pair.second;
        broadcaster_->SetMirrorCounterPart( second_broadcaster );
        second_broadcaster->SetMirrorCounterPart( broadcaster_ );

        second_crdt->RegisterElementFilter( "Key.*", filter_func );
        second_crdt->Start();

        std::shared_ptr<Delta> delta    = std::make_shared<Delta>();
        auto                   element1 = delta->add_elements();
        element1->set_key( "Key1" );
        element1->set_value( rejectedKey );
        auto element2 = delta->add_elements();
        element2->set_key( "Key2" );
        element2->set_value( rejectedKey );
        auto element3 = delta->add_elements();
        element3->set_key( "Key3" );
        element3->set_value( rejectedKey );
        auto element4 = delta->add_elements();
        element4->set_key( "Key4" );
        element4->set_value( acceptedKey );

        delta->set_priority( 1 );

        ASSERT_OUTCOME_SUCCESS( final_cid, crdtDatastore_->Publish( delta, { "topic" } ) );

        filters_complete_future.wait();
        while ( true )
        {
            auto head_height = second_crdt->GetHeadHeight( final_cid, "topic" );
            if ( head_height.has_value() && head_height.value() > 0 )
            {
                break;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }

        EXPECT_OUTCOME_EQ( second_crdt->HasKey( { "Key1" } ), false );
        EXPECT_OUTCOME_EQ( second_crdt->HasKey( { "Key2" } ), false );
        EXPECT_OUTCOME_EQ( second_crdt->HasKey( { "Key3" } ), false );
        EXPECT_OUTCOME_EQ( second_crdt->HasKey( { "Key4" } ), true );

        // Verify filter was called
        EXPECT_GE( filter_called_count, 1 );
        CloseAndResetCRDT( second_crdt, second_broadcaster );
    }

    TEST_F( CrdtDatastoreTest, FilterCallbackMultipleDeltas )
    {
        // Create a filter that rejects Deltas containing a specific key
        const std::string rejectedKey = "RejectMe";
        const std::string acceptedKey = "AcceptMe";

        // Track filter calls
        std::atomic<int> filter_called_count{ 0 };
        std::promise<void> filters_complete;
        auto filters_complete_future = filters_complete.get_future();

        auto filter_func = [&]( const Element &element ) -> std::optional<std::vector<Element>>
        {
            if ( filter_called_count.fetch_add( 1 ) + 1 == 4 )
            {
                filters_complete.set_value();
            }

            // Check if any element has the rejected key

            if ( element.value() == rejectedKey )
            {
                Element tombstone = element;
                return std::vector<Element>{ tombstone }; // Reject this delta
            }
            return std::nullopt; // Accept this delta
        };

        auto crdt_pair = CreateLoopBackCRDTInstance( databasePath + "aux3", ipfsDataStore_ );

        auto second_crdt        = crdt_pair.first;
        auto second_broadcaster = crdt_pair.second;

        broadcaster_->SetMirrorCounterPart( second_broadcaster );
        second_broadcaster->SetMirrorCounterPart( broadcaster_ );

        second_crdt->RegisterElementFilter( "Key.*", filter_func );
        second_crdt->Start();

        std::shared_ptr<Delta> delta1   = std::make_shared<Delta>();
        auto                   element1 = delta1->add_elements();
        element1->set_key( "Key1" );
        element1->set_value( acceptedKey );
        std::shared_ptr<Delta> delta2   = std::make_shared<Delta>();
        auto                   element2 = delta2->add_elements();
        element2->set_key( "Key2" );
        element2->set_value( rejectedKey );
        std::shared_ptr<Delta> delta3   = std::make_shared<Delta>();
        auto                   element3 = delta3->add_elements();
        element3->set_key( "Key3" );
        element3->set_value( acceptedKey );
        std::shared_ptr<Delta> delta4   = std::make_shared<Delta>();
        auto                   element4 = delta4->add_elements();
        element4->set_key( "Key4" );
        element4->set_value( acceptedKey );

        delta1->set_priority( 1 );
        delta2->set_priority( 2 );
        delta3->set_priority( 3 );
        delta4->set_priority( 4 );

        EXPECT_OUTCOME_TRUE_1( crdtDatastore_->Publish( delta1, { "topic" } ) );
        EXPECT_OUTCOME_TRUE_1( crdtDatastore_->Publish( delta2, { "topic" } ) );
        EXPECT_OUTCOME_TRUE_1( crdtDatastore_->Publish( delta3, { "topic" } ) );
        ASSERT_OUTCOME_SUCCESS( final_cid, crdtDatastore_->Publish( delta4, { "topic" } ) );

        filters_complete_future.wait();
        while ( true )
        {
            auto head_height = second_crdt->GetHeadHeight( final_cid, "topic" );
            if ( head_height.has_value() && head_height.value() > 0 )
            {
                break;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }

        EXPECT_OUTCOME_EQ( second_crdt->HasKey( { "Key1" } ), true );
        EXPECT_OUTCOME_EQ( second_crdt->HasKey( { "Key3" } ), true );
        EXPECT_OUTCOME_EQ( second_crdt->HasKey( { "Key4" } ), true );

        // Test with accepted key (should be stored)
        EXPECT_OUTCOME_EQ( crdtDatastore_->HasKey( { "Key2" } ), true );
        EXPECT_OUTCOME_EQ( second_crdt->HasKey( { "Key2" } ), false );

        // Verify filter was called
        EXPECT_GE( filter_called_count, 1 );
        CloseAndResetCRDT( second_crdt, second_broadcaster );
    }

    TEST_F( CrdtDatastoreTest, FilterCallbackMultipleFilters )
    {
        // Create a filter that rejects Deltas containing a specific key
        const std::string rejectedKey = "RejectMe";
        const std::string acceptedKey = "AcceptMe";

        // Track filter calls
        std::atomic<int> filter_called_count{ 0 };
        std::promise<void> filters_complete;
        auto filters_complete_future = filters_complete.get_future();
        auto record_filter_call = [&]()
        {
            if ( filter_called_count.fetch_add( 1 ) + 1 == 4 )
            {
                filters_complete.set_value();
            }
        };

        auto crdt_pair = CreateLoopBackCRDTInstance( databasePath + "aux4", ipfsDataStore_ );

        auto second_crdt        = crdt_pair.first;
        auto second_broadcaster = crdt_pair.second;

        //This Filter always accepts all values
        second_crdt->RegisterElementFilter( "Key.*",
                                            [&]( const Element &element ) -> std::optional<std::vector<Element>>
                                            {
                                                record_filter_call();

                                                // Check if any element has the rejected key
                                                return std::nullopt; // Accept this delta
                                            } );

        //This Filter checks the "RejectMe"
        second_crdt->RegisterElementFilter( "OtherKey.*",
                                            [&]( const Element &element ) -> std::optional<std::vector<Element>>
                                            {
                                                record_filter_call();

                                                if ( element.value() == rejectedKey )
                                                {
                                                    Element tombstone = element;
                                                    return std::vector<Element>{ tombstone }; // Reject this delta
                                                }
                                                return std::nullopt; // Accept this delta
                                            } );
        second_crdt->Start();

        broadcaster_->SetMirrorCounterPart( second_broadcaster );
        second_broadcaster->SetMirrorCounterPart( broadcaster_ );

        std::shared_ptr<Delta> delta1   = std::make_shared<Delta>();
        auto                   element1 = delta1->add_elements();
        element1->set_key( "Key1" );
        element1->set_value( acceptedKey );
        std::shared_ptr<Delta> delta2   = std::make_shared<Delta>();
        auto                   element2 = delta2->add_elements();
        element2->set_key( "Key2" );
        element2->set_value( rejectedKey );
        std::shared_ptr<Delta> delta4   = std::make_shared<Delta>();
        auto                   element4 = delta4->add_elements();
        element4->set_key( "OtherKey1" );
        element4->set_value( rejectedKey );
        std::shared_ptr<Delta> delta3   = std::make_shared<Delta>();
        auto                   element3 = delta3->add_elements();
        element3->set_key( "OtherKeySomething" );
        element3->set_value( acceptedKey );

        delta1->set_priority( 1 );
        delta2->set_priority( 2 );
        delta3->set_priority( 3 );
        delta4->set_priority( 4 );

        EXPECT_OUTCOME_TRUE_1( crdtDatastore_->Publish( delta1, { "topic" } ) );
        EXPECT_OUTCOME_TRUE_1( crdtDatastore_->Publish( delta2, { "topic" } ) );
        EXPECT_OUTCOME_TRUE_1( crdtDatastore_->Publish( delta3, { "topic" } ) );
        ASSERT_OUTCOME_SUCCESS( final_cid, crdtDatastore_->Publish( delta4, { "topic" } ) );

        filters_complete_future.wait();
        while ( true )
        {
            auto head_height = second_crdt->GetHeadHeight( final_cid, "topic" );
            if ( head_height.has_value() && head_height.value() > 0 )
            {
                break;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
        }

        EXPECT_OUTCOME_EQ( second_crdt->HasKey( { "Key1" } ), true );
        EXPECT_OUTCOME_EQ( second_crdt->HasKey( { "Key2" } ), true );
        EXPECT_OUTCOME_EQ( second_crdt->HasKey( { "OtherKeySomething" } ), true );

        EXPECT_OUTCOME_EQ( crdtDatastore_->HasKey( { "OtherKey1" } ), true );
        EXPECT_OUTCOME_EQ( second_crdt->HasKey( { "OtherKey1" } ), false );

        // Verify filter was called
        EXPECT_GE( filter_called_count, 1 );
        CloseAndResetCRDT( second_crdt, second_broadcaster );
    }

    TEST_F( CrdtDatastoreTest, DeltaFilterRejectRemovesMatchingElementsAndTombstones )
    {
        CRDTDataFilter filter( crdtDatastore_->GetWorkJournal() );
        std::atomic<int> calls{ 0 };
        ASSERT_TRUE( filter.RegisterDeltaFilter(
            "^/?cert/.*$",
            [&calls]( const pb::Delta & )
            {
                ++calls;
                return DeltaFilterResult::Reject();
            } ) );

        Buffer unrelated_value;
        unrelated_value.put( "live" );
        ASSERT_TRUE( crdtDatastore_->PutKey( { "/unrelated/live" }, unrelated_value, { "topic" } ).has_value() );
        ASSERT_TRUE( crdtDatastore_->HasKey( { "/unrelated/live" } ).value() );
        ASSERT_OUTCOME_SUCCESS( removal, crdtDatastore_->CreateDeltaToRemove( "/unrelated/live" ) );

        auto *matching_element = removal->add_elements();
        matching_element->set_key( "/cert/v2/slot/" + std::string( 64, 'a' ) );
        matching_element->set_value( "attack" );
        auto *matching_tombstone = removal->add_tombstones();
        matching_tombstone->set_key( "/cert/v2/tx/" + std::string( 64, 'b' ) );
        matching_tombstone->set_id( "exact-certificate-id" );

        auto result = filter.FilterDelta( *removal );
        EXPECT_EQ( result.decision, DeltaFilterDecision::Reject );
        EXPECT_EQ( calls.load(), 1 );
        ASSERT_EQ( result.delta.elements_size(), 0 );
        ASSERT_EQ( result.delta.tombstones_size(), 1 );
        EXPECT_EQ( result.delta.tombstones( 0 ).key(), "/unrelated/live" );

        ASSERT_TRUE(
            crdtDatastore_->Publish( std::make_shared<Delta>( result.delta ), { "topic" } ).has_value() );
        EXPECT_FALSE( crdtDatastore_->HasKey( { "/unrelated/live" } ).value() );
    }

    TEST_F( CrdtDatastoreTest, DeltaFilterRejectMatchesTombstoneOnlyDelta )
    {
        CRDTDataFilter filter( crdtDatastore_->GetWorkJournal() );
        std::atomic<int> calls{ 0 };
        ASSERT_TRUE( filter.RegisterDeltaFilter(
            "^/?cert/.*$",
            [&calls]( const pb::Delta & )
            {
                ++calls;
                return DeltaFilterResult::Reject();
            } ) );

        Delta delta;
        auto *tombstone = delta.add_tombstones();
        tombstone->set_key( "/cert/v2/slot/" + std::string( 64, 'c' ) );
        tombstone->set_id( "exact-id" );
        auto result = filter.FilterDelta( std::move( delta ) );

        EXPECT_EQ( calls.load(), 1 );
        EXPECT_EQ( result.decision, DeltaFilterDecision::Reject );
        EXPECT_TRUE( result.delta.tombstones().empty() );
    }

    TEST_F( CrdtDatastoreTest, DeltaFilterMixedRejectAndRetryDependencyPreservesRetry )
    {
        CRDTDataFilter filter( crdtDatastore_->GetWorkJournal() );
        std::atomic<int> legacy_filter_calls{ 0 };
        ASSERT_TRUE( filter.RegisterDeltaFilter(
            "^/?reject/.*$",
            []( const pb::Delta & ) { return DeltaFilterResult::Reject(); } ) );
        ASSERT_TRUE( filter.RegisterDeltaFilter(
            "^/?retry/.*$",
            []( const pb::Delta & )
            {
                return DeltaFilterResult::RetryDependency( "dependency-one" );
            } ) );
        ASSERT_TRUE( filter.RegisterElementFilter(
            "^/?unrelated/.*$",
            [&]( const Element & ) -> std::optional<std::vector<Element>>
            {
                ++legacy_filter_calls;
                return std::nullopt;
            } ) );

        Delta mixed;
        mixed.add_elements()->set_key( "/reject/attack" );
        mixed.add_elements()->set_key( "/retry/slot" );
        mixed.add_elements()->set_key( "/unrelated/live" );
        auto result = filter.FilterDelta( mixed );

        ASSERT_EQ( result.decision, DeltaFilterDecision::RetryDependency );
        ASSERT_TRUE( result.dependency_cid.has_value() );
        EXPECT_EQ( *result.dependency_cid, "dependency-one" );
        EXPECT_EQ( result.delta.elements_size(), 2 );
        EXPECT_EQ( result.delta.elements( 0 ).key(), "/retry/slot" );
        EXPECT_EQ( result.delta.elements( 1 ).key(), "/unrelated/live" );
        EXPECT_EQ( legacy_filter_calls.load(), 0 );

        CRDTDataFilter conflicting_filter( crdtDatastore_->GetWorkJournal() );
        ASSERT_TRUE( conflicting_filter.RegisterDeltaFilter(
            "^/?retry/one/.*$",
            []( const pb::Delta & )
            {
                return DeltaFilterResult::RetryDependency( "dependency-one" );
            } ) );
        ASSERT_TRUE( conflicting_filter.RegisterDeltaFilter(
            "^/?retry/two/.*$",
            []( const pb::Delta & )
            {
                return DeltaFilterResult::RetryDependency( "dependency-two" );
            } ) );
        Delta conflicting;
        conflicting.add_elements()->set_key( "/retry/one/slot" );
        conflicting.add_elements()->set_key( "/retry/two/slot" );
        conflicting.add_elements()->set_key( "/unrelated/live" );
        auto conflict_result = conflicting_filter.FilterDelta( conflicting );

        EXPECT_EQ( conflict_result.decision, DeltaFilterDecision::Reject );
        EXPECT_FALSE( conflict_result.dependency_cid.has_value() );
        ASSERT_EQ( conflict_result.delta.elements_size(), 1 );
        EXPECT_EQ( conflict_result.delta.elements( 0 ).key(), "/unrelated/live" );
    }

    TEST_F( CrdtDatastoreTest, MixedRejectAndRetryDependencyParksRetainedNamespace )
    {
        auto crdt_pair = CreateLoopBackCRDTInstance( databasePath + "mixed-dependency", ipfsDataStore_ );
        auto receiver = crdt_pair.first;
        auto receiver_broadcaster = crdt_pair.second;

        std::atomic<int64_t> fake_elapsed_ms{ 0 };
        const auto base_now = std::chrono::steady_clock::now();
        receiver->SetMonotonicClockForTesting(
            [&] { return base_now + std::chrono::milliseconds( fake_elapsed_ms.load() ); } );

        ASSERT_OUTCOME_SUCCESS(
            dependency_root,
            crdtDatastore_->Publish( CreateTestDelta( "/dependency/mixed", "ready-marker" ), { "topic" } ) );
        const auto dependency_cid = dependency_root.toString().value();
        std::atomic<bool> dependency_ready{ false };
        std::atomic<int>  callback_calls{ 0 };
        ASSERT_TRUE( receiver->RegisterDeltaFilter(
            "^/?reject/.*$",
            []( const pb::Delta & ) { return DeltaFilterResult::Reject(); } ) );
        ASSERT_TRUE( receiver->RegisterDeltaFilter(
            "^/?retry/.*$",
            [&]( const pb::Delta & )
            {
                return dependency_ready.load()
                           ? DeltaFilterResult::Approve()
                           : DeltaFilterResult::RetryDependency( dependency_cid );
            } ) );
        ASSERT_TRUE( receiver->RegisterNewElementCallback(
            "^/?(retry|unrelated)/.*$",
            [&]( CRDTCallbackManager::NewDataPair, const std::string & ) { ++callback_calls; } ) );
        receiver->Start();

        broadcaster_->SetMirrorCounterPart( receiver_broadcaster );
        receiver_broadcaster->SetMirrorCounterPart( broadcaster_ );

        auto mixed = std::make_shared<Delta>();
        mixed->add_elements()->set_key( "/reject/attack" );
        mixed->mutable_elements( 0 )->set_value( "attack" );
        mixed->add_elements()->set_key( "/retry/slot" );
        mixed->mutable_elements( 1 )->set_value( "certificate" );
        mixed->add_elements()->set_key( "/unrelated/live" );
        mixed->mutable_elements( 2 )->set_value( "live" );
        ASSERT_OUTCOME_SUCCESS( source_cid, crdtDatastore_->Publish( mixed, { "topic" } ) );

        ASSERT_WAIT_FOR_CONDITION( [&] { return receiver->GetParkedRootCount() == 1; },
                                   std::chrono::milliseconds( 5000 ),
                                   "mixed dependency root was not parked",
                                   nullptr );
        EXPECT_EQ( receiver->GetParkedRootCountForDependency( dependency_root ), 1 );
        EXPECT_FALSE( receiver->HasKey( { "/reject/attack" } ).value() );
        EXPECT_FALSE( receiver->HasKey( { "/retry/slot" } ).value() );
        EXPECT_FALSE( receiver->HasKey( { "/unrelated/live" } ).value() );
        EXPECT_EQ( callback_calls.load(), 0 );

        dependency_ready = true;
        fake_elapsed_ms = 1000;
        receiver->WakeDependencyRetryWorkerForTesting();
        ASSERT_WAIT_FOR_CONDITION(
            [&]
            {
                return receiver->HasKey( { "/retry/slot" } ).value() &&
                       receiver->HasKey( { "/unrelated/live" } ).value();
            },
            std::chrono::milliseconds( 5000 ),
            "mixed dependency retry did not merge",
            nullptr );
        EXPECT_FALSE( receiver->HasKey( { "/reject/attack" } ).value() );
        EXPECT_EQ( receiver->GetParkedRootCount(), 0 );
        EXPECT_EQ( callback_calls.load(), 2 );

        fake_elapsed_ms = 3000;
        receiver->WakeDependencyRetryWorkerForTesting();
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        EXPECT_EQ( callback_calls.load(), 2 );
        CloseAndResetCRDT( receiver, receiver_broadcaster );
    }

    TEST_F( CrdtDatastoreTest, DeltaFilterDependencyRetryRetainsAndEventuallyProcessesSource )
    {
        auto crdt_pair = CreateLoopBackCRDTInstance( databasePath + "dependency", ipfsDataStore_ );
        auto receiver = crdt_pair.first;
        auto receiver_broadcaster = crdt_pair.second;

        std::atomic<int64_t> fake_elapsed_ms{ 0 };
        const auto base_now = std::chrono::steady_clock::now();
        receiver->SetMonotonicClockForTesting(
            [&] { return base_now + std::chrono::milliseconds( fake_elapsed_ms.load() ); } );

        std::atomic<bool> dependency_ready{ false };
        std::atomic<int>  filter_calls{ 0 };
        std::atomic<int>  new_element_calls{ 0 };
        auto dependency_seed = CreateTestDelta( "/dependency/id", "registry" );
        ASSERT_OUTCOME_SUCCESS( dependency_root,
                                crdtDatastore_->Publish( dependency_seed, { "topic" } ) );
        std::string dependency_cid = dependency_root.toString().value();
        ASSERT_TRUE( receiver->RegisterDeltaFilter(
            "^/?retry/.*$",
            [&]( const pb::Delta & )
            {
                ++filter_calls;
                return dependency_ready.load()
                           ? DeltaFilterResult::Approve()
                           : DeltaFilterResult::RetryDependency( dependency_cid );
            } ) );
        ASSERT_TRUE( receiver->RegisterNewElementCallback(
            "^/?retry/.*$",
            [&]( CRDTCallbackManager::NewDataPair, const std::string & ) { ++new_element_calls; } ) );
        receiver->Start();

        broadcaster_->SetMirrorCounterPart( receiver_broadcaster );
        receiver_broadcaster->SetMirrorCounterPart( broadcaster_ );

        auto delta = CreateTestDelta( "/retry/slot", "certificate" );
        ASSERT_OUTCOME_SUCCESS( source_cid, crdtDatastore_->Publish( delta, { "topic" } ) );
        dependency_cid = source_cid.toString().value();

        ASSERT_WAIT_FOR_CONDITION( [&] { return receiver->GetParkedRootCount() == 1; },
                                   std::chrono::milliseconds( 5000 ),
                                   "dependency root was not parked",
                                   nullptr );
        EXPECT_TRUE( receiver->IsCIDRetainedInCacheForTesting( source_cid ) );
        ASSERT_OUTCOME_SUCCESS( status, receiver->GetTrackedJobStatusForTesting( source_cid ) );
        EXPECT_EQ( status, CrdtDatastore::JobStatus::PENDING );
        EXPECT_FALSE( receiver->HasKey( { "/retry/slot" } ).value() );
        EXPECT_EQ( new_element_calls.load(), 0 );

        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        EXPECT_EQ( filter_calls.load(), 1 );

        dependency_ready = true;
        fake_elapsed_ms = 1000;
        receiver->WakeDependencyRetryWorkerForTesting();
        ASSERT_WAIT_FOR_CONDITION( [&] { return receiver->HasKey( { "/retry/slot" } ).value(); },
                                   std::chrono::milliseconds( 5000 ),
                                   "dependency retry did not merge",
                                   nullptr );
        EXPECT_EQ( receiver->GetParkedRootCount(), 0 );
        EXPECT_EQ( new_element_calls.load(), 1 );
        EXPECT_EQ( filter_calls.load(), 2 );

        fake_elapsed_ms = 3000;
        receiver->WakeDependencyRetryWorkerForTesting();
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        EXPECT_EQ( new_element_calls.load(), 1 );
        CloseAndResetCRDT( receiver, receiver_broadcaster );
    }

    TEST_F( CrdtDatastoreTest, DeltaFilterDependencyAttemptLimitAndShutdownDrainParkedRoots )
    {
        auto crdt_pair = CreateLoopBackCRDTInstance( databasePath + "attempt-limit", ipfsDataStore_ );
        auto receiver = crdt_pair.first;
        auto receiver_broadcaster = crdt_pair.second;

        std::atomic<int64_t> fake_elapsed_ms{ 0 };
        const auto base_now = std::chrono::steady_clock::now();
        receiver->SetMonotonicClockForTesting(
            [&] { return base_now + std::chrono::milliseconds( fake_elapsed_ms.load() ); } );

        auto dependency_seed = CreateTestDelta( "/dependency/attempts", "registry" );
        ASSERT_OUTCOME_SUCCESS( dependency_root,
                                crdtDatastore_->Publish( dependency_seed, { "topic" } ) );
        const auto dependency_cid = dependency_root.toString().value();
        std::atomic<int> filter_calls{ 0 };
        ASSERT_TRUE( receiver->RegisterDeltaFilter(
            "^/?bounded/.*$",
            [&]( const pb::Delta & )
            {
                ++filter_calls;
                return DeltaFilterResult::RetryDependency( dependency_cid );
            } ) );
        receiver->Start();
        broadcaster_->SetMirrorCounterPart( receiver_broadcaster );
        receiver_broadcaster->SetMirrorCounterPart( broadcaster_ );

        ASSERT_OUTCOME_SUCCESS(
            source_cid,
            crdtDatastore_->Publish( CreateTestDelta( "/bounded/attempts", "certificate" ), { "topic" } ) );
        ASSERT_WAIT_FOR_CONDITION( [&] { return receiver->GetParkedRootCount() == 1; },
                                   std::chrono::milliseconds( 5000 ),
                                   "attempt-limited root was not parked",
                                   nullptr );

        const std::array<int64_t, 7> retry_deadlines_ms{ 1000, 3000, 7000, 15000, 31000, 61000, 91000 };
        for ( std::size_t retry = 0; retry < retry_deadlines_ms.size(); ++retry )
        {
            fake_elapsed_ms = retry_deadlines_ms[retry];
            receiver->WakeDependencyRetryWorkerForTesting();
            ASSERT_WAIT_FOR_CONDITION(
                [&] { return filter_calls.load() >= static_cast<int>( retry + 2 ); },
                std::chrono::milliseconds( 2000 ),
                "dependency retry did not run at its deadline",
                nullptr );
        }
        ASSERT_WAIT_FOR_CONDITION( [&] { return receiver->GetParkedRootCount() == 0; },
                                   std::chrono::milliseconds( 2000 ),
                                   "eighth stalled evaluation did not evict",
                                   nullptr );
        auto stats = receiver->GetDependencyRetryStatistics();
        EXPECT_EQ( stats.dependency_roots_parked, 1 );
        EXPECT_EQ( stats.dependency_retries, 7 );
        EXPECT_EQ( stats.dependency_evicted_attempts, 1 );
        EXPECT_TRUE( receiver->GetTrackedJobStatusForTesting( source_cid ).has_error() );

        fake_elapsed_ms = 100000;
        ASSERT_OUTCOME_SUCCESS(
            shutdown_cid,
            crdtDatastore_->Publish( CreateTestDelta( "/bounded/shutdown", "certificate" ), { "topic" } ) );
        ASSERT_WAIT_FOR_CONDITION( [&] { return receiver->GetParkedRootCount() == 1; },
                                   std::chrono::milliseconds( 5000 ),
                                   "shutdown root was not parked",
                                   nullptr );
        const auto callbacks_before_shutdown = filter_calls.load();
        receiver->CancelAndCloseNow();
        EXPECT_EQ( receiver->GetParkedRootCount(), 0 );
        EXPECT_TRUE( receiver->GetTrackedJobStatusForTesting( shutdown_cid ).has_error() );
        fake_elapsed_ms = 200000;
        receiver->WakeDependencyRetryWorkerForTesting();
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        EXPECT_EQ( filter_calls.load(), callbacks_before_shutdown );
        CloseAndResetCRDT( receiver, receiver_broadcaster );
    }
}
