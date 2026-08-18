#include "crdt/crdt_datastore.hpp"
#include "crdt_custom_dagsyncer.hpp"
#include "crdt_mirror_broadcaster.hpp"

#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <ipfs_lite/ipfs/impl/in_memory_datastore.hpp>
#include <storage/rocksdb/rocksdb.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

namespace sgns::crdt
{
    namespace
    {
        using ipfs_lite::ipfs::InMemoryDatastore;
        using storage::rocksdb;

        constexpr auto kTimeout = std::chrono::seconds( 10 );

        std::shared_ptr<CrdtDatastore::Delta> MakeDelta( const std::string &key,
                                                         const std::string &value )
        {
            auto delta = std::make_shared<CrdtDatastore::Delta>();
            auto *element = delta->add_elements();
            element->set_key( key );
            element->set_value( value );
            delta->set_priority( 1 );
            return delta;
        }

        std::shared_ptr<rocksdb> MakeDatabase( const std::string &path )
        {
            boost::filesystem::remove_all( path );
            rocksdb::Options options;
            options.create_if_missing = true;
            auto result = rocksdb::create( path, options );
            return result ? std::move( result.value() ) : nullptr;
        }

        int RunFinalOwnerChild()
        {
            const auto suffix = std::to_string( static_cast<long long>( ::getpid() ) );
            const std::string sender_path = "/tmp/supergenius_crdt_last_owner_sender_" + suffix;
            const std::string receiver_path = "/tmp/supergenius_crdt_last_owner_receiver_" + suffix;

            auto sender_db = MakeDatabase( sender_path );
            auto receiver_db = MakeDatabase( receiver_path );
            if ( !sender_db || !receiver_db )
            {
                return 10;
            }

            auto ipfs_store = std::make_shared<InMemoryDatastore>();
            auto sender_syncer = std::make_shared<CustomDagSyncer>( ipfs_store );
            auto receiver_syncer = std::make_shared<CustomDagSyncer>( ipfs_store );
            auto sender_broadcaster = std::make_shared<CRDTMirrorBroadcaster>();
            auto receiver_broadcaster = std::make_shared<CRDTMirrorBroadcaster>();
            sender_broadcaster->SetMirrorCounterPart( receiver_broadcaster );

            const HierarchicalKey namespace_key( "/lifetime" );
            auto sender = CrdtDatastore::New( sender_db,
                                              namespace_key,
                                              sender_syncer,
                                              sender_broadcaster,
                                              CrdtOptions::DefaultOptions() );
            auto receiver = CrdtDatastore::New( receiver_db,
                                                namespace_key,
                                                receiver_syncer,
                                                receiver_broadcaster,
                                                CrdtOptions::DefaultOptions() );
            if ( !sender || !receiver )
            {
                return 11;
            }

            std::weak_ptr<CrdtDatastore> weak_receiver = receiver;
            auto close_completion =
                CrdtDatastoreLifetimeObserver::CloseCompletion( receiver );
            auto destruction =
                CrdtDatastoreLifetimeObserver::DestructionCompletion( receiver );

            std::promise<void> delete_callback_promise;
            auto delete_callback = delete_callback_promise.get_future();
            std::atomic<int> delete_calls{ 0 };
            std::atomic<int> callback_error{ 0 };
            if ( !receiver->RegisterDeletedElementCallback(
                     "^/?lifetime/delete$",
                     [&]( std::string, std::string )
                     {
                         ++delete_calls;
                         const auto snapshot =
                             CrdtDatastoreLifetimeObserver::Snapshot( receiver );
                         if ( snapshot.delete_callback_wrapper_entries == 0 ||
                              snapshot.active_callback_wrappers == 0 ||
                              snapshot.destructor_started )
                         {
                             callback_error = 20;
                         }
                         delete_callback_promise.set_value();
                     } ) )
            {
                return 12;
            }

            std::promise<void> trigger_entered_promise;
            auto trigger_entered = trigger_entered_promise.get_future();
            std::mutex release_mutex;
            std::condition_variable release_cv;
            bool release_trigger = false;
            std::atomic<int> trigger_calls{ 0 };
            std::atomic<int> after_calls{ 0 };
            std::atomic<bool> callback_body_finished{ false };
            std::thread::id callback_thread;

            if ( !receiver->RegisterNewElementCallback(
                     "^/?lifetime/trigger$",
                     [&]( CRDTCallbackManager::NewDataPair, const std::string & )
                     {
                         ++trigger_calls;
                         callback_thread = std::this_thread::get_id();

                         const auto before =
                             CrdtDatastoreLifetimeObserver::Snapshot( receiver );
                         if ( before.put_callback_wrapper_entries == 0 ||
                              before.active_callback_wrappers == 0 ||
                              before.destructor_started )
                         {
                             callback_error = 21;
                         }

                         receiver.reset();
                         if ( weak_receiver.expired() )
                         {
                             callback_error = 22;
                         }
                         {
                             auto assertion_owner = weak_receiver.lock();
                             if ( !assertion_owner )
                             {
                                 callback_error = 23;
                             }
                             else
                             {
                                 const auto during =
                                     CrdtDatastoreLifetimeObserver::Snapshot( assertion_owner );
                                 if ( during.active_callback_wrappers == 0 ||
                                      during.destructor_started )
                                 {
                                     callback_error = 24;
                                 }
                             }
                         }
                         if ( close_completion.wait_for( std::chrono::seconds( 0 ) ) ==
                              std::future_status::ready )
                         {
                             callback_error = 25;
                         }

                         trigger_entered_promise.set_value();
                         std::unique_lock lock( release_mutex );
                         if ( !release_cv.wait_for(
                                  lock, kTimeout, [&] { return release_trigger; } ) )
                         {
                             callback_error = 26;
                         }
                         callback_body_finished = true;
                     } ) ||
                 !receiver->RegisterNewElementCallback(
                     "^/?lifetime/(after|post-close)$",
                     [&]( CRDTCallbackManager::NewDataPair, const std::string & )
                     {
                         ++after_calls;
                     } ) )
            {
                return 13;
            }

            sender->Start();
            receiver->Start();

            auto put_delete = sender->Publish(
                MakeDelta( "/lifetime/delete", "delete-me" ), { "topic" } );
            if ( !put_delete )
            {
                return 14;
            }
            auto delete_result =
                sender->DeleteKey( HierarchicalKey( "/lifetime/delete" ), { "topic" } );
            if ( !delete_result ||
                 delete_callback.wait_for( kTimeout ) != std::future_status::ready )
            {
                return 15;
            }
            if ( delete_calls.load() != 1 || callback_error.load() != 0 )
            {
                return 16;
            }

            auto trigger_result = sender->Publish(
                MakeDelta( "/lifetime/trigger", "release" ), { "topic" } );
            if ( !trigger_result ||
                 trigger_entered.wait_for( kTimeout ) != std::future_status::ready )
            {
                return 17;
            }

            auto after_result = sender->Publish(
                MakeDelta( "/lifetime/after", "must-not-dispatch" ), { "topic" } );
            if ( !after_result )
            {
                return 18;
            }
            {
                std::lock_guard lock( release_mutex );
                release_trigger = true;
            }
            release_cv.notify_all();

            if ( close_completion.wait_for( kTimeout ) != std::future_status::ready )
            {
                return 27;
            }
            if ( !callback_body_finished.load() || trigger_calls.load() != 1 ||
                 callback_error.load() != 0 || !weak_receiver.expired() )
            {
                return 28;
            }

            if ( destruction.destructor.wait_for( kTimeout ) !=
                     std::future_status::ready ||
                 destruction.deletion.wait_for( kTimeout ) !=
                     std::future_status::ready )
            {
                return 29;
            }
            const auto destructor_thread = destruction.destructor.get();
            const auto deletion_thread = destruction.deletion.get();
            if ( destructor_thread == callback_thread ||
                 deletion_thread == callback_thread ||
                 destructor_thread != deletion_thread )
            {
                return 30;
            }

            auto post_close_result = sender->Publish(
                MakeDelta( "/lifetime/post-close", "still-must-not-dispatch" ),
                { "topic" } );
            if ( !post_close_result )
            {
                return 31;
            }
            sender->WakeDependencyRetryWorkerForTesting();
            if ( after_calls.load() != 0 )
            {
                return 32;
            }

            sender->Close();
            sender_broadcaster->SetMirrorCounterPart( nullptr );
            receiver_broadcaster->SetMirrorCounterPart( nullptr );
            receiver_broadcaster.reset();
            receiver_syncer.reset();
            receiver_db.reset();
            std::_Exit( 0 );
        }

        int RunReaperShutdownChild()
        {
            const auto suffix = std::to_string( static_cast<long long>( ::getpid() ) );
            const std::string database_path =
                "/tmp/supergenius_crdt_reaper_shutdown_" + suffix;

            auto database = MakeDatabase( database_path );
            if ( !database )
            {
                return 40;
            }

            auto ipfs_store = std::make_shared<InMemoryDatastore>();
            auto syncer = std::make_shared<CustomDagSyncer>( ipfs_store );
            auto broadcaster = std::make_shared<CRDTMirrorBroadcaster>();
            auto datastore = CrdtDatastore::New( database,
                                                 HierarchicalKey( "/reaper-shutdown" ),
                                                 syncer,
                                                 broadcaster,
                                                 CrdtOptions::DefaultOptions() );
            if ( !datastore )
            {
                return 41;
            }

            const auto destruction =
                CrdtDatastoreLifetimeObserver::DestructionCompletion( datastore );
            datastore->Start();
            datastore->Close();
            datastore.reset();

            CrdtDatastoreLifetimeObserver::ShutdownReaperForTesting();
            if ( destruction.deletion.wait_for( std::chrono::seconds( 0 ) ) !=
                 std::future_status::ready )
            {
                return 42;
            }

            broadcaster.reset();
            syncer.reset();
            ipfs_store.reset();
            database.reset();
            boost::filesystem::remove_all( database_path );
            return 0;
        }
    } // namespace

    TEST( CrdtDatastoreLifetimeTest,
          FinalExternalOwnerReleasedInsideCallbackEventuallyDeletesOnReaper )
    {
        EXPECT_EXIT( std::_Exit( RunFinalOwnerChild() ),
                     ::testing::ExitedWithCode( 0 ),
                     "" );
    }

    TEST( CrdtDatastoreLifetimeTest, ReaperShutdownDrainsQueuedFinalDeletion )
    {
        EXPECT_EXIT( std::exit( RunReaperShutdownChild() ),
                     ::testing::ExitedWithCode( 0 ),
                     "" );
    }
} // namespace sgns::crdt
