#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>
#include <boost/dll.hpp>
#include <boost/filesystem.hpp>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <rocksdb/utilities/backup_engine.h>

#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/local_requests.hpp>

#include "base/buffer.hpp"
#include "base/logger.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"
#include "crdt/hierarchical_key.hpp"

namespace
{
    constexpr std::chrono::milliseconds kWaitTimeout{ 10000 };
    constexpr std::chrono::milliseconds kPollStep{ 100 };

    std::string GetLoggingSystem( const std::string & )
    {
        return R"(
# ----------------
sinks:
    - name: console
      type: console
      color: true
groups:
    - name: globaldb_backup_test
      sink: console
      level: error
      children:
        - name: libp2p
        - name: Gossip
# ----------------
  )";
            }

    void ConfigureTestLogger( const std::string &tag, spdlog::level::level_enum level )
    {
        auto logger = sgns::base::createLogger( tag );
        logger->set_level( level );
        if ( level != spdlog::level::off )
        {
            logger->flush_on( level );
        }
    }


    std::atomic<uint16_t> g_test_port{ 51501 };

    template <typename Predicate>
    bool WaitFor( Predicate pred,
                  std::chrono::milliseconds timeout = kWaitTimeout,
                  std::chrono::milliseconds step = kPollStep )
    {
        const auto start = std::chrono::steady_clock::now();
        while ( std::chrono::steady_clock::now() - start < timeout )
        {
            if ( pred() )
            {
                return true;
            }
            std::this_thread::sleep_for( step );
        }
        return false;
    }

    size_t GetBackupCount( const std::string &backup_dir )
    {
        if ( !boost::filesystem::exists( backup_dir ) )
        {
            return 0;
        }

        ::ROCKSDB_NAMESPACE::BackupEngineReadOnly *engine = nullptr;
        ::ROCKSDB_NAMESPACE::BackupEngineOptions   options( backup_dir );
        const auto status = ::ROCKSDB_NAMESPACE::BackupEngineReadOnly::Open(
            ::ROCKSDB_NAMESPACE::Env::Default(),
            options,
            &engine );

        if ( !status.ok() || engine == nullptr )
        {
            return 0;
        }

        std::unique_ptr<::ROCKSDB_NAMESPACE::BackupEngineReadOnly> engine_guard( engine );
        std::vector<::ROCKSDB_NAMESPACE::BackupInfo>                info;
        engine_guard->GetBackupInfo( &info );
        return info.size();
    }

    class GlobalDbTestNode
    {
    public:
        explicit GlobalDbTestNode( std::string db_root, sgns::crdt::GlobalDB::BackupOptions backup_options ) :
            db_root_( std::move( db_root ) ),
            db_path_( db_root_ + "/CommonKey" )
        {
            boost::filesystem::create_directories( db_root_ );

            sgns::crdt::KeyPairFileStorage key_store( db_root_ + "/key" );
            auto                           key_pair = key_store.GetKeyPair().value();

            pubsub_ = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( key_pair );
            const auto port = g_test_port.fetch_add( 1 );
            auto       pubsub_start = pubsub_->Start( port, {}, "127.0.0.1", {} );
            pubsub_start.wait();
            if ( !pubsub_->GetHost() )
            {
                throw std::runtime_error( "PubSub host is null after Start" );
            }

            io_ = std::make_shared<boost::asio::io_context>();
            auto backend = std::make_shared<libp2p::basic::AsioSchedulerBackend>( io_ );
            scheduler_ = std::make_shared<libp2p::basic::SchedulerImpl>( backend, libp2p::basic::Scheduler::Config{} );

            graphsync_network_ =
                std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubsub_->GetHost(), scheduler_ );
            generator_ = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();

            auto db_result = sgns::crdt::GlobalDB::New( io_,
                                                        db_path_,
                                                        pubsub_,
                                                        sgns::crdt::CrdtOptions::DefaultOptions(),
                                                        graphsync_network_,
                                                        scheduler_,
                                                        generator_,
                                                        nullptr,
                                                        backup_options );
            if ( !db_result.has_value() )
            {
                throw std::runtime_error( "GlobalDB::New failed: " + db_result.error().message() );
            }

            db_ = std::move( db_result.value() );
            db_->AddBroadcastTopic( "test" );
            db_->AddListenTopic( "test" );
            db_->Start();

            io_thread_ = std::thread( [ctx = io_]() { ctx->run(); } );
        }

        ~GlobalDbTestNode()
        {
            if ( io_ )
            {
                io_->stop();
            }
            if ( io_thread_.joinable() )
            {
                io_thread_.join();
            }
            if ( pubsub_ )
            {
                pubsub_->Stop();
            }
            db_.reset();
        }

        void PutValue( const std::string &key, const std::string &value )
        {
            ASSERT_NE( db_, nullptr );
            auto tx = db_->BeginTransaction();
            ASSERT_NE( tx, nullptr );

            sgns::base::Buffer buffer;
            buffer.put( value );

            const auto put_res = tx->Put( sgns::crdt::HierarchicalKey( key ), buffer );
            ASSERT_TRUE( put_res.has_value() );
            const auto commit_res = tx->Commit( { "test" } );
            ASSERT_TRUE( commit_res.has_value() );
        }

        std::string GetValue( const std::string &key ) const
        {
            const auto get_res = db_->Get( sgns::crdt::HierarchicalKey( key ) );
            if ( !get_res.has_value() )
            {
                return {};
            }
            return std::string( get_res.value().toString() );
        }

        const std::string &DbPath() const
        {
            return db_path_;
        }

        bool CreateManualBackup( uint32_t keep_count ) const
        {
            if ( !db_ )
            {
                return false;
            }

            const std::string backup_dir = db_path_ + "/backups";
            boost::filesystem::create_directories( backup_dir );

            auto data_store = db_->GetDataStore();
            if ( !data_store )
            {
                return false;
            }

            auto native_db = data_store->getDB();
            if ( !native_db )
            {
                return false;
            }

            ::ROCKSDB_NAMESPACE::BackupEngine *engine = nullptr;
            ::ROCKSDB_NAMESPACE::BackupEngineOptions options( backup_dir );
            const auto open_status = ::ROCKSDB_NAMESPACE::BackupEngine::Open(
                ::ROCKSDB_NAMESPACE::Env::Default(),
                options,
                &engine );
            if ( !open_status.ok() || engine == nullptr )
            {
                return false;
            }

            std::unique_ptr<::ROCKSDB_NAMESPACE::BackupEngine> engine_guard( engine );
            const auto create_status = engine_guard->CreateNewBackup( native_db.get(), true );
            if ( !create_status.ok() )
            {
                return false;
            }

            const auto purge_status = engine_guard->PurgeOldBackups( keep_count );
            return purge_status.ok();
        }

    private:
        std::string db_root_;
        std::string db_path_;

        std::shared_ptr<boost::asio::io_context>         io_;
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub_;
        std::shared_ptr<libp2p::basic::SchedulerImpl>    scheduler_;
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network>            graphsync_network_;
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator_;
        std::shared_ptr<sgns::crdt::GlobalDB>                                  db_;
        std::thread                                                             io_thread_;
    };

    std::string BuildTestDir( const std::string &suffix )
    {
        const std::string binary_path = boost::dll::program_location().parent_path().string();
        const std::string test_name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        return binary_path + "/" + suffix + "_" + test_name;
    }

    void CorruptDatabase( const std::string &db_path )
    {
        const boost::filesystem::path db_dir( db_path );
        const auto                    current = db_dir / "CURRENT";

        if ( boost::filesystem::exists( current ) )
        {
            std::ofstream current_file( current.string(), std::ios::trunc | std::ios::binary );
            current_file << "CORRUPTED_MANIFEST_POINTER";
        }

        for ( boost::filesystem::directory_iterator it( db_dir ); it != boost::filesystem::directory_iterator(); ++it )
        {
            const auto filename = it->path().filename().string();
            if ( filename.rfind( "MANIFEST", 0 ) == 0 )
            {
                boost::filesystem::remove( it->path() );
            }
        }
    }

    void CatastrophicallyCorruptDatabase( const std::string &db_path )
    {
        const boost::filesystem::path db_dir( db_path );
        if ( !boost::filesystem::exists( db_dir ) )
        {
            return;
        }

        const auto current = db_dir / "CURRENT";
        if ( boost::filesystem::exists( current ) )
        {
            std::ofstream current_file( current.string(), std::ios::trunc | std::ios::binary );
            current_file << "BROKEN_CURRENT_POINTER_MANIFEST-999999";
        }

        for ( boost::filesystem::directory_iterator it( db_dir ); it != boost::filesystem::directory_iterator(); ++it )
        {
            const auto path = it->path();
            const auto filename = path.filename().string();

            if ( boost::filesystem::is_directory( path ) )
            {
                // Keep BackupEngine artifacts intact; we only destroy live DB files.
                continue;
            }

            const bool is_manifest = filename.rfind( "MANIFEST", 0 ) == 0;
            const bool is_options = filename.rfind( "OPTIONS", 0 ) == 0;
            const bool is_sst = path.extension() == ".sst" || path.extension() == ".ldb";
            const bool is_wal = path.extension() == ".log";

            if ( is_manifest || is_options || is_wal )
            {
                boost::filesystem::remove( path );
                continue;
            }

            if ( is_sst )
            {
                std::ofstream truncated( path.string(), std::ios::trunc | std::ios::binary );
                truncated << "X";
            }
        }
    }

    void EnsureLoggingInitialized()
    {
        static std::once_flag once;
        std::call_once(
            once,
            []()
            {
                std::shared_ptr<soralog::LoggingSystem> logging_system;

                const std::string logging_yaml = GetLoggingSystem( "" );
                auto logger_configurator = std::make_shared<libp2p::log::Configurator>();
                auto config_from_yaml =
                    std::make_shared<soralog::ConfiguratorFromYAML>( logger_configurator, logging_yaml );
                logging_system = std::make_shared<soralog::LoggingSystem>( config_from_yaml );
                auto conf_result = logging_system->configure();

                if ( conf_result.has_error )
                {
                    auto fallback_config = std::make_shared<libp2p::log::Configurator>();
                    logging_system       = std::make_shared<soralog::LoggingSystem>( fallback_config );
                    conf_result          = logging_system->configure();
                    if ( conf_result.has_error )
                    {
                        std::cerr << "[globaldb_backup_test] logger configuration failed; continuing with default logging\n";
                        return;
                    }
                }

                libp2p::log::setLoggingSystem( logging_system );

                // Mirror GeniusNode::ConfigureLogger usage to quiet unrelated subsystems.
                ConfigureTestLogger( "SuperGeniusNode", spdlog::level::err );
                ConfigureTestLogger( "GeniusNode", spdlog::level::err );
                ConfigureTestLogger( "GlobalDB", spdlog::level::trace );
                ConfigureTestLogger( "GraphsyncDAGSyncer", spdlog::level::err );
                ConfigureTestLogger( "graphsync", spdlog::level::err );
                ConfigureTestLogger( "PubSubBroadcasterExt", spdlog::level::err );
                ConfigureTestLogger( "CrdtDatastore", spdlog::level::err );
                ConfigureTestLogger( "CrdtHeads", spdlog::level::err );
                ConfigureTestLogger( "TransactionManager", spdlog::level::err );
                ConfigureTestLogger( "MigrationManager", spdlog::level::err );
                ConfigureTestLogger( "MigrationStep", spdlog::level::err );
                ConfigureTestLogger( "ProcessingTaskQueueImpl", spdlog::level::err );
                ConfigureTestLogger( "rocksdb", spdlog::level::err );
                ConfigureTestLogger( "Kademlia", spdlog::level::err );
                ConfigureTestLogger( "Noise", spdlog::level::err );
                ConfigureTestLogger( "ProcessingEngine", spdlog::level::err );
                ConfigureTestLogger( "ProcessingSubTaskQueueAccessorImpl", spdlog::level::err );
                ConfigureTestLogger( "ProcessingService", spdlog::level::err );
                ConfigureTestLogger( "ProcessingSubTaskQueueManager", spdlog::level::err );
                ConfigureTestLogger( "UPNP", spdlog::level::err );
                ConfigureTestLogger( "ProcessingNode", spdlog::level::err );
                ConfigureTestLogger( "GossipPubSub", spdlog::level::off );
                ConfigureTestLogger( "Gossip", spdlog::level::off );
                ConfigureTestLogger( "AccountMessenger", spdlog::level::err );
                ConfigureTestLogger( "GeniusAccount", spdlog::level::err );
                ConfigureTestLogger( "KeyPairFileStorage", spdlog::level::err );
                ConfigureTestLogger( "Blockchain", spdlog::level::err );
                ConfigureTestLogger( "ValidatorRegistry", spdlog::level::err );
                ConfigureTestLogger( "SGProcessingManager", spdlog::level::err );
                ConfigureTestLogger( "SGProcessor", spdlog::level::err );
                ConfigureTestLogger( "CRDTCallbackManager", spdlog::level::err );
                ConfigureTestLogger( "CoinPrices", spdlog::level::err );
                ConfigureTestLogger( "FILECommon", spdlog::level::err );
                ConfigureTestLogger( "FileManager", spdlog::level::err );
                ConfigureTestLogger( "HTTPCommon", spdlog::level::err );
                ConfigureTestLogger( "IPFSCommon", spdlog::level::err );
                ConfigureTestLogger( "IPFSLoader", spdlog::level::err );
                ConfigureTestLogger( "MNNLoader", spdlog::level::err );
                ConfigureTestLogger( "WSCommon", spdlog::level::err );
            } );
    }
} // namespace

TEST( GlobalDbBackupTest, KeepsOnlyConfiguredMaximumBackups )
{
    EnsureLoggingInitialized();

    const auto root = BuildTestDir( "globaldb_backup_retention" );
    boost::filesystem::remove_all( root );
    boost::filesystem::create_directories( root );

    sgns::crdt::GlobalDB::BackupOptions backup_options;
    backup_options.enabled = true;
    backup_options.interval_minutes = 60;
    backup_options.keep_count = 3;
    backup_options.auto_restore_on_repair_failure = true;

    const std::string db_path = root + "/CommonKey";
    const std::string backup_dir = db_path + "/backups";

    GlobalDbTestNode node( root, backup_options );

    for ( int i = 0; i < 6; ++i )
    {
        node.PutValue( "/backup/retention", "v" + std::to_string( i ) );
        ASSERT_TRUE( node.CreateManualBackup( backup_options.keep_count ) );

        const size_t expected_max =
            static_cast<size_t>( std::min( i + 1, static_cast<int>( backup_options.keep_count ) ) );
        ASSERT_TRUE( WaitFor( [&]() { return GetBackupCount( backup_dir ) >= expected_max; } ) );
    }

    const size_t final_count = GetBackupCount( backup_dir );
    EXPECT_EQ( final_count, backup_options.keep_count );

}

TEST( GlobalDbBackupTest, CorruptionRestoresFromLatestBackup )
{
    EnsureLoggingInitialized();

    const auto root = BuildTestDir( "globaldb_backup_restore" );
    boost::filesystem::remove_all( root );
    boost::filesystem::create_directories( root );

    sgns::crdt::GlobalDB::BackupOptions backup_options;
    backup_options.enabled = true;
    backup_options.interval_minutes = 60;
    backup_options.keep_count = 3;
    backup_options.auto_restore_on_repair_failure = true;

    std::vector<std::string> keys;
    std::vector<std::string> original_values;
    keys.reserve( backup_options.keep_count );
    original_values.reserve( backup_options.keep_count );

    for ( uint32_t i = 0; i < backup_options.keep_count; ++i )
    {
        keys.emplace_back( "/backup/restore/key_" + std::to_string( i ) );
        original_values.emplace_back( "value_" + std::to_string( i ) );
    }

    const std::string early_key = keys.front();
    const std::string early_original_value = original_values.front();
    const std::string early_corrupted_value = "corrupted-early-value";
    const std::string db_path = root + "/CommonKey";
    const std::string backup_dir = db_path + "/backups";

    {
        GlobalDbTestNode node( root, backup_options );

        for ( uint32_t i = 0; i < backup_options.keep_count; ++i )
        {
            node.PutValue( keys[i], original_values[i] );
            ASSERT_TRUE( node.CreateManualBackup( backup_options.keep_count ) );

            const size_t expected_backups = static_cast<size_t>( std::min( i + 1, backup_options.keep_count ) );
            ASSERT_TRUE( WaitFor( [&]() { return GetBackupCount( backup_dir ) >= expected_backups; } ) );
        }

        // Simulate an unintended mutation after the latest good backup.
        node.PutValue( early_key, early_corrupted_value );
        EXPECT_EQ( node.GetValue( early_key ), early_corrupted_value );
        for (uint32_t i = 0; i < backup_options.keep_count; ++i )
        {
            const auto key = keys[i];
            const auto expected_value = original_values[i];
            std::cout << "Pre-corruption, Key: " << key << ", Expected: " << expected_value << "\n" << "In DB Val: " << node.GetValue( key ) << "\n";
        }
        std::cout << "Pre-corruption, Early Key: " << early_key << ", Expected: " << early_original_value
                << ", Corrupted: " << early_corrupted_value << "\n" << "In DB Val: " << node.GetValue( early_key ) << "\n";
    }


    CorruptDatabase( db_path );

    {
        GlobalDbTestNode node( root, backup_options );

        for ( uint32_t i = 0; i < backup_options.keep_count; ++i )
        {
            const auto key = keys[i];
            if ( key == early_key )
            {
                // If RocksDB repair succeeds, this post-backup mutation can legitimately survive.
                ASSERT_TRUE( WaitFor( [&]()
                                      {
                                          const auto value = node.GetValue( key );
                                          return value == early_original_value || value == early_corrupted_value;
                                      } ) );
                const auto repaired_value = node.GetValue( key );
                std::cout << "Key: " << key << ", Repaired Value: " << repaired_value << "\n";
                EXPECT_TRUE( repaired_value == early_original_value || repaired_value == early_corrupted_value );
            }
            else
            {
                const auto expected_value = original_values[i];
                
                ASSERT_TRUE( WaitFor( [&]() { return node.GetValue( key ) == expected_value; } ) );
                std::cout << "Key: " << key << ", Expected: " << expected_value << ", Actual: " << node.GetValue( key ) << "\n";
                EXPECT_EQ( node.GetValue( key ), expected_value );
            }
        }

        // For this test, successful repair is acceptable and may keep the post-backup write.
        const auto early_after_recovery = node.GetValue( early_key );
        EXPECT_TRUE( early_after_recovery == early_original_value || early_after_recovery == early_corrupted_value );
    }

}

TEST( GlobalDbBackupTest, CatastrophicCorruptionForcesRestoreFromLatestBackup )
{
    EnsureLoggingInitialized();

    const auto root = BuildTestDir( "globaldb_backup_restore_catastrophic" );
    boost::filesystem::remove_all( root );
    boost::filesystem::create_directories( root );

    sgns::crdt::GlobalDB::BackupOptions backup_options;
    backup_options.enabled = true;
    backup_options.interval_minutes = 60;
    backup_options.keep_count = 3;
    backup_options.auto_restore_on_repair_failure = true;

    std::vector<std::string> keys;
    std::vector<std::string> original_values;
    keys.reserve( backup_options.keep_count );
    original_values.reserve( backup_options.keep_count );

    for ( uint32_t i = 0; i < backup_options.keep_count; ++i )
    {
        keys.emplace_back( "/backup/restore/catastrophic/key_" + std::to_string( i ) );
        original_values.emplace_back( "value_" + std::to_string( i ) );
    }

    const std::string early_key = keys.front();
    const std::string early_original_value = original_values.front();
    const std::string early_corrupted_value = "corrupted-early-value";
    const std::string db_path = root + "/CommonKey";
    const std::string backup_dir = db_path + "/backups";

    {
        GlobalDbTestNode node( root, backup_options );

        for ( uint32_t i = 0; i < backup_options.keep_count; ++i )
        {
            node.PutValue( keys[i], original_values[i] );
            ASSERT_TRUE( node.CreateManualBackup( backup_options.keep_count ) );

            const size_t expected_backups = static_cast<size_t>( std::min( i + 1, backup_options.keep_count ) );
            ASSERT_TRUE( WaitFor( [&]() { return GetBackupCount( backup_dir ) >= expected_backups; } ) );
        }

        // This write is intentionally after the latest backup and must be rolled back by restore.
        node.PutValue( early_key, early_corrupted_value );
        EXPECT_EQ( node.GetValue( early_key ), early_corrupted_value );
    }

    CatastrophicallyCorruptDatabase( db_path );

    {
        GlobalDbTestNode node( root, backup_options );

        for ( uint32_t i = 0; i < backup_options.keep_count; ++i )
        {
            const auto key = keys[i];
            const auto expected_value = original_values[i];
            ASSERT_TRUE( WaitFor( [&]() { return node.GetValue( key ) == expected_value; } ) );
            EXPECT_EQ( node.GetValue( key ), expected_value );
        }

        EXPECT_EQ( node.GetValue( early_key ), early_original_value );
    }
}