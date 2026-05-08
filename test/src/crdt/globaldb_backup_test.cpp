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

                auto globaldb_logger = sgns::base::createLogger( "GlobalDB" );
                auto rocksdb_logger  = sgns::base::createLogger( "rocksdb" );
                globaldb_logger->set_level( spdlog::level::trace );
                rocksdb_logger->set_level( spdlog::level::trace );
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

    for ( int i = 0; i < 6; ++i )
    {
        {
            GlobalDbTestNode node( root, backup_options );
            node.PutValue( "/backup/retention", "v" + std::to_string( i ) );

            const size_t expected_max = static_cast<size_t>( std::min( i + 1, static_cast<int>( backup_options.keep_count ) ) );
            ASSERT_TRUE( WaitFor( [&]() { return GetBackupCount( backup_dir ) >= expected_max; } ) );
        }
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

    const std::string key = "/backup/restore";
    const std::string original_value = "value-before-corruption";
    const std::string db_path = root + "/CommonKey";
    const std::string backup_dir = db_path + "/backups";

    {
        GlobalDbTestNode node( root, backup_options );
        node.PutValue( key, original_value );
        ASSERT_TRUE( WaitFor( [&]() { return GetBackupCount( backup_dir ) >= 1; } ) );
    }

    CorruptDatabase( db_path );

    {
        GlobalDbTestNode node( root, backup_options );
        ASSERT_TRUE( WaitFor( [&]() { return node.GetValue( key ) == original_value; } ) );
        EXPECT_EQ( node.GetValue( key ), original_value );
    }

}