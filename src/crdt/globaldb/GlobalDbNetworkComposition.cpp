#include "crdt/globaldb/GlobalDbNetworkComposition.hpp"

#include <chrono>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

#include <ipfs_lite/ipfs/graphsync/impl/local_requests.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_pubsub/gossip_pubsub.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
#include <rapidjson/document.h>

#include "crdt/crdt_options.hpp"
#include "crdt/globaldb/globaldb.hpp"
#include "crdt/globaldb/keypair_file_storage.hpp"

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns::crdt, GlobalDbNetworkComposition::Error, e )
{
    using Error = sgns::crdt::GlobalDbNetworkComposition::Error;
    switch ( e )
    {
        case Error::INVALID_CONFIG:
            return "Invalid GlobalDB network composition configuration";
        case Error::NETWORK_CONFIG_IO:
            return "Unable to read network configuration";
        case Error::NETWORK_CONFIG_PARSE:
            return "Invalid network configuration";
        case Error::KEYPAIR_LOAD_FAILED:
            return "Unable to load the GlobalDB transport keypair";
        case Error::PUBSUB_START_FAILED:
            return "Unable to start GlobalDB PubSub transport";
        case Error::GLOBALDB_CREATE_FAILED:
            return "Unable to create GlobalDB";
        case Error::TOPIC_CONFIGURATION_FAILED:
            return "Unable to configure GlobalDB topic";
    }
    return "Unknown GlobalDB network composition error";
}

namespace sgns::crdt
{
    outcome::result<GlobalDbNetworkComposition::NetworkConfig> GlobalDbNetworkComposition::LoadNetworkConfig(
        const std::string  &path,
        const base::Logger &logger )
    {
        std::ifstream input( path );
        if ( !input.good() )
        {
            return outcome::failure( Error::NETWORK_CONFIG_IO );
        }

        std::stringstream contents;
        contents << input.rdbuf();

        rapidjson::Document document;
        document.Parse( contents.str().c_str() );
        if ( document.HasParseError() || !document.IsObject() )
        {
            return outcome::failure( Error::NETWORK_CONFIG_PARSE );
        }

        NetworkConfig result;
        if ( document.HasMember( "pubsub_port" ) && document["pubsub_port"].IsString() )
        {
            try
            {
                const auto parsed = std::stoul( document["pubsub_port"].GetString() );
                if ( parsed > std::numeric_limits<uint16_t>::max() )
                {
                    return outcome::failure( Error::NETWORK_CONFIG_PARSE );
                }
                result.pubsub_port = static_cast<uint16_t>( parsed );
            }
            catch ( const std::exception & )
            {
                return outcome::failure( Error::NETWORK_CONFIG_PARSE );
            }
        }

        if ( document.HasMember( "pubsub_bind_address" ) )
        {
            if ( !document["pubsub_bind_address"].IsString() )
            {
                return outcome::failure( Error::NETWORK_CONFIG_PARSE );
            }
            result.pubsub_bind_address = document["pubsub_bind_address"].GetString();
        }

        if ( document.HasMember( "bootstrap_addresses" ) )
        {
            if ( !document["bootstrap_addresses"].IsArray() )
            {
                return outcome::failure( Error::NETWORK_CONFIG_PARSE );
            }
            for ( const auto &address : document["bootstrap_addresses"].GetArray() )
            {
                if ( !address.IsString() )
                {
                    return outcome::failure( Error::NETWORK_CONFIG_PARSE );
                }
                result.bootstrap_addresses.emplace_back( address.GetString() );
            }
        }

        if ( document.HasMember( "high_water" ) )
        {
            if ( !document["high_water"].IsInt() )
            {
                return outcome::failure( Error::NETWORK_CONFIG_PARSE );
            }
            result.high_water = document["high_water"].GetInt();
        }
        if ( document.HasMember( "low_water" ) )
        {
            if ( !document["low_water"].IsInt() )
            {
                return outcome::failure( Error::NETWORK_CONFIG_PARSE );
            }
            result.low_water = document["low_water"].GetInt();
        }

        if ( result.pubsub_bind_address.empty() || result.low_water < 0 || result.high_water <= 0 ||
             result.low_water > result.high_water )
        {
            if ( logger )
            {
                logger->error( "Invalid GlobalDB PubSub bind address or connection watermarks in {}", path );
            }
            return outcome::failure( Error::NETWORK_CONFIG_PARSE );
        }

        return outcome::success( std::move( result ) );
    }

    outcome::result<std::shared_ptr<GlobalDbNetworkComposition>> GlobalDbNetworkComposition::Create( Config config )
    {
        if ( config.network_config_path.empty() || config.database_path.empty() || config.listen_topic.empty() ||
             config.broadcast_topic.empty() )
        {
            return outcome::failure( Error::INVALID_CONFIG );
        }
        if ( !config.logger )
        {
            config.logger = base::createLogger( "GlobalDbNetworkComposition" );
        }

        BOOST_OUTCOME_TRY( auto network_config, LoadNetworkConfig( config.network_config_path, config.logger ) );
        return std::shared_ptr<GlobalDbNetworkComposition>(
            new GlobalDbNetworkComposition( std::move( config ), std::move( network_config ) ) );
    }

    GlobalDbNetworkComposition::GlobalDbNetworkComposition( Config config, NetworkConfig network_config ) :
        config_( std::move( config ) ), network_config_( std::move( network_config ) )
    {
    }

    GlobalDbNetworkComposition::~GlobalDbNetworkComposition()
    {
        Stop();
    }

    outcome::result<void> GlobalDbNetworkComposition::Start()
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        if ( started_ )
        {
            return outcome::success();
        }

        auto io         = std::make_shared<boost::asio::io_context>();
        auto work_guard = std::make_unique<WorkGuard>( io->get_executor() );
        auto scheduler  = std::make_shared<libp2p::basic::SchedulerImpl>(
            std::make_shared<libp2p::basic::AsioSchedulerBackend>( io ),
            libp2p::basic::Scheduler::Config{ std::chrono::milliseconds( 100 ) } );

        auto keypair_result = KeyPairFileStorage( config_.database_path + "/pubsub" ).GetKeyPair();
        if ( keypair_result.has_error() )
        {
            return outcome::failure( Error::KEYPAIR_LOAD_FAILED );
        }

        libp2p::protocol::gossip::Config pubsub_config;
        pubsub_config.echo_forward_mode       = false;
        pubsub_config.sign_messages           = false;
        pubsub_config.seen_cache_limit        = 10;
        pubsub_config.heartbeat_interval_msec = std::chrono::milliseconds{ 500 };
        pubsub_config.rw_timeout_msec         = std::chrono::seconds{ 30 };

        auto pubsub = std::make_shared<ipfs_pubsub::GossipPubSub>( std::move( keypair_result.value() ), pubsub_config );
        auto pubsub_start = pubsub->Start( network_config_.pubsub_port,
                                           network_config_.bootstrap_addresses,
                                           network_config_.pubsub_bind_address,
                                           {} );
        if ( auto start_error = pubsub_start.get(); start_error )
        {
            config_.logger->error( "PubSub failed to start on {}:{}: {}",
                                   network_config_.pubsub_bind_address,
                                   network_config_.pubsub_port,
                                   start_error.message() );
            pubsub->Stop();
            return outcome::failure( Error::PUBSUB_START_FAILED );
        }

        pubsub->GetHost()->getConnectionManagerConfig().high_water = network_config_.high_water;
        pubsub->GetHost()->getConnectionManagerConfig().low_water  = network_config_.low_water;

        auto graphsync_network = std::make_shared<ipfs_lite::ipfs::graphsync::Network>( pubsub->GetHost(), scheduler );
        auto request_id_generator = std::make_shared<ipfs_lite::ipfs::graphsync::RequestIdGenerator>();
        auto crdt_options         = CrdtOptions::DefaultOptions();
        crdt_options->logger      = config_.logger;

        auto db_result = GlobalDB::New( io,
                                        config_.database_path,
                                        pubsub,
                                        crdt_options,
                                        graphsync_network,
                                        scheduler,
                                        request_id_generator );
        if ( db_result.has_error() )
        {
            config_.logger->error( "GlobalDB creation failed: {}", db_result.error().message() );
            pubsub->Stop();
            return outcome::failure( Error::GLOBALDB_CREATE_FAILED );
        }

        auto global_db = std::move( db_result.value() );
        global_db->AddListenTopic( config_.listen_topic );
        auto add_broadcast_result = global_db->AddBroadcastTopic( config_.broadcast_topic );
        if ( add_broadcast_result.has_error() )
        {
            config_.logger->error( "GlobalDB broadcast topic configuration failed: {}",
                                   add_broadcast_result.error().message() );
            global_db->ShutdownNow();
            global_db.reset();
            pubsub->Stop();
            return outcome::failure( Error::TOPIC_CONFIGURATION_FAILED );
        }

        global_db->Start();

        io_                   = std::move( io );
        work_guard_           = std::move( work_guard );
        pubsub_               = std::move( pubsub );
        scheduler_            = std::move( scheduler );
        graphsync_network_    = std::move( graphsync_network );
        request_id_generator_ = std::move( request_id_generator );
        db_                   = std::move( global_db );
        io_thread_            = std::thread( [io_context = io_]() { io_context->run(); } );
        started_              = true;
        return outcome::success();
    }

    void GlobalDbNetworkComposition::Stop()
    {
        std::shared_ptr<boost::asio::io_context>                        io;
        std::unique_ptr<WorkGuard>                                      work_guard;
        std::shared_ptr<ipfs_pubsub::GossipPubSub>                      pubsub;
        std::shared_ptr<libp2p::basic::Scheduler>                       scheduler;
        std::shared_ptr<ipfs_lite::ipfs::graphsync::Network>            graphsync_network;
        std::shared_ptr<ipfs_lite::ipfs::graphsync::RequestIdGenerator> request_id_generator;
        std::shared_ptr<GlobalDB>                                       global_db;
        std::thread                                                     io_thread;

        {
            std::lock_guard<std::mutex> lock( mutex_ );
            global_db            = std::move( db_ );
            work_guard           = std::move( work_guard_ );
            io                   = std::move( io_ );
            graphsync_network    = std::move( graphsync_network_ );
            request_id_generator = std::move( request_id_generator_ );
            scheduler            = std::move( scheduler_ );
            pubsub               = std::move( pubsub_ );
            io_thread            = std::move( io_thread_ );
            started_             = false;
        }

        if ( global_db )
        {
            global_db->ShutdownNow();
        }
        global_db.reset();

        if ( work_guard )
        {
            work_guard->reset();
        }
        if ( io )
        {
            io->stop();
        }
        if ( io_thread.joinable() )
        {
            io_thread.join();
        }

        graphsync_network.reset();
        request_id_generator.reset();
        scheduler.reset();

        if ( pubsub )
        {
            pubsub->Stop();
        }
        pubsub.reset();
        work_guard.reset();
        io.reset();
    }

    std::shared_ptr<GlobalDB> GlobalDbNetworkComposition::db() const
    {
        std::lock_guard<std::mutex> lock( mutex_ );
        return db_;
    }
} // namespace sgns::crdt
