#include "application/impl/validating_node_application.hpp"
#include "integration/AppStateManagerFactory.hpp"
#include "integration/ConfigurationStorageFactory.hpp"
#include "integration/KeyStorageFactory.hpp"
#include "integration/SystemClockFactory.hpp"
#include "integration/BufferStorageFactory.hpp"
#include "integration/HasherFactory.hpp"
#include "integration/BlockHeaderRepositoryFactory.hpp"
#include "integration/VRFProviderFactory.hpp"
#include "integration/ProductionLotteryFactory.hpp"
#include "integration/StorageChangesTrackerFactory.hpp"
#include "integration/TrieStorageBackendFactory.hpp"
#include "integration/TrieSerializerFactory.hpp"
#include "integration/BlockStorageFactory.hpp"
#include "integration/TrieStorageFactory.hpp"
#include "integration/BlockTreeFactory.hpp"
#include "integration/ExtrinsicObserverFactory.hpp"
#include "integration/AuthorApiFactory.hpp"
#include "integration/TranscationPoolFactory.hpp"
#include "integration/PoolModeratorFactory.hpp"
#include "integration/ExtrinsicGossiperFactory.hpp"
#include "integration/ProductionFactory.hpp"
#include "integration/BlockExecutorFactory.hpp"
#include "integration/ProductionConfigurationFactory.hpp"
#include "integration/ProductionSynchronizerFactory.hpp"
#include "integration/OwnPeerInfoFactory.hpp"
#include "integration/BlockValidatorFactory.hpp"
#include "integration/SR25519ProviderFactory.hpp"
#include "integration/EpochStorageFactory.hpp"
#include "integration/AuthorityUpdateObserverFactory.hpp"
#include "integration/ProposerFactory.hpp"
#include "integration/BlockBuilderManager.hpp"
#include "integration/SR25519KeypairFactory.hpp"
#include "integration/FinalityFactory.hpp"
#include "integration/EnvironmentFactory.hpp"
#include "integration/ED25519ProviderFactory.hpp"
#include "integration/ED25519KeyPairFactory.hpp"
#include "integration/SteadyClockFactory.hpp"
#include "integration/AuthorityManagerFactory.hpp"
#include "integration/RouterFactory.hpp"
#include "integration/SyncProtocolObserverFactory.hpp"
#include "integration/ApiServiceFactory.hpp"
#include "integration/RpcThreadPoolFactory.hpp"
#include "integration/RpcContextFactory.hpp"
#include "integration/ListenerFactory.hpp"
#include "integration/JRpcServerFactory.hpp"
#include "integration/JRpcProcessorFactory.hpp"
#include "integration/ChainApiFactory.hpp"
#include "integration/StateApiFactory.hpp"
#include "integration/SystemApiFactory.hpp"

#include "storage/trie/supergenius_trie/supergenius_trie_factory_impl.hpp"
#include "storage/trie/serialization/supergenius_codec.hpp"

#include "verification/production/impl/production_impl.hpp"
#include "verification/finality/impl/finality_impl.hpp"
#include "network/impl/router_libp2p.hpp"
#include <boost/filesystem.hpp>
#include <libp2p/injector/host_injector.hpp>
#include "clock/impl/clock_impl.hpp"
#include "singleton/CComponentFactory.hpp"

namespace sgns::application
{

    ValidatingNodeApplication::ValidatingNodeApplication( const std::shared_ptr<AppConfiguration> &app_config ) :
        logger_( base::createLogger( "Application" ) )
    {
        spdlog::set_level( app_config->verbosity() );

        // genesis launch if database does not exist
        production_execution_strategy_ = boost::filesystem::exists( app_config->rocksdb_path() ) ? Production::ExecutionStrategy::SYNC_FIRST :
                                                                                                   Production::ExecutionStrategy::GENESIS;

        auto component_factory = SINGLETONINSTANCE( CComponentFactory );
        component_factory->Register( sgns::RpcContextFactory{}.create(), "RpcContext", boost::none );

        io_context_ = std::make_shared<boost::asio::io_context>();
        component_factory->Register( AppStateManagerFactory::create(), "AppStateManager", boost::none );
        component_factory->Register( ConfigurationStorageFactory::create( app_config->genesis_path() ), "ConfigurationStorage", boost::none );
        component_factory->Register( KeyStorageFactory::create( app_config->keystore_path() ), "KeyStorage", boost::none );
        component_factory->Register( SystemClockFactory::create(), "SystemClock", boost::none );

        component_factory->Register( BufferStorageFactory::create( "rocksdb", app_config->rocksdb_path() ), "BufferStorage",
                                     boost::make_optional( std::string( "rocksdb" ) ) );
        component_factory->Register( HasherFactory::create(), "Hasher", boost::none );
        component_factory->Register( VRFProviderFactory::create(), "VRFProvider", boost::none );
        component_factory->Register( ProductionLotteryFactory::create(), "ProductionLottery", boost::none );
        component_factory->Register( BlockHeaderRepositoryFactory::create( "rocksdb" ), "BlockHeaderRepository", boost::none );
        component_factory->Register( std::make_shared<sgns::storage::trie::SuperGeniusTrieFactoryImpl>(), "SuperGeniusTrieFactory", boost::none );
        component_factory->Register( std::make_shared<sgns::storage::trie::SuperGeniusCodec>(), "Codec", boost::none );
        component_factory->Register( StorageChangesTrackerFactory::create(), "ChangesTracker", boost::none );
        component_factory->Register( TrieStorageBackendFactory::create(), "TrieStorageBackend", boost::none );
        component_factory->Register( TrieSerializerFactory::create(), "TrieSerializer", boost::none );
        component_factory->Register( TrieStorageFactory::create(), "TrieStorage", boost::none );
        component_factory->Register( sgns::BlockStorageFactory{}.create(), "BlockStorage", boost::none );
        component_factory->Register( ExtrinsicGossiperFactory::create(), "ExtrinsicGossiper", boost::none );
        component_factory->Register( ExtrinsicGossiperFactory::create(), "Gossiper", boost::none );
        component_factory->Register( PoolModeratorFactory::create(), "PoolModerator", boost::none );
        component_factory->Register( sgns::BlockBuilderManager{}.createFactory(), "BlockBuilderFactory", boost::none );
        component_factory->Register( TranscationPoolFactory::create(), "TransactionPool", boost::none );
        component_factory->Register( ProposerFactory::create(), "Proposer", boost::none );
        component_factory->Register( AuthorApiFactory::create(), "AuthorApi", boost::none );
        component_factory->Register( ExtrinsicObserverFactory::create(), "ExtrinsicObserver", boost::none );
        component_factory->Register( BlockTreeFactory::create(), "BlockTree", boost::none );
        component_factory->Register( ProductionConfigurationFactory::create(), "ProductionConfiguration", boost::none );
        component_factory->Register( AuthorityUpdateObserverFactory::create(), "AuthorityUpdateObserver", boost::none );
        component_factory->Register( EpochStorageFactory::create(), "EpochStorage", boost::none );
        component_factory->Register( SR25519ProviderFactory::create(), "SR25519Provider", boost::none );
        component_factory->Register( BlockValidatorFactory::create(), "BlockValidator", boost::none );
        component_factory->Register( OwnPeerInfoFactory::create( app_config->p2p_port() ), "OwnPeerInfo", boost::none );
        component_factory->Register( ProductionSynchronizerFactory::create(), "ProductionSynchronizer", boost::none );
        component_factory->Register( BlockExecutorFactory::create(), "BlockExecutor", boost::none );
        component_factory->Register( ExtrinsicGossiperFactory::create(), "ProductionGossiper", boost::none );
        component_factory->Register( sgns::SR25519KeypairFactory{}.create(), "SR25519Keypair", boost::none );
        component_factory->Register( ProductionFactory::create( *io_context_ ), "Production", boost::none );
        component_factory->Register( ( component_factory->GetComponent( "Production", boost::none ) ).value(), "ProductionObserver", boost::none );
        component_factory->Register( sgns::EnvironmentFactory{}.create(), "Environment", boost::none );
        component_factory->Register( sgns::ED25519ProviderFactory{}.create(), "ED25519Provider", boost::none );
        component_factory->Register( sgns::ED25519KeyPairFactory{}.create(), "ED25519Keypair", boost::none );
        component_factory->Register( sgns::SteadyClockFactory{}.create(), "SteadyClock", boost::none );
        component_factory->Register( sgns::AuthorityManagerFactory{}.create(), "AuthorityManager", boost::none );
        component_factory->Register( sgns::FinalityFactory{}.create( io_context_ ), "Finality", boost::none );
        component_factory->Register( sgns::FinalityFactory{}.create( io_context_ ), "RoundObserver", boost::none );
        component_factory->Register( sgns::SyncProtocolObserverFactory{}.create(), "SyncProtocolObserver", boost::none );
        component_factory->Register( sgns::RouterFactory{}.create(), "Router", boost::none );

        component_factory->Register( sgns::RpcThreadPoolFactory{}.create(), "RpcThreadPool", boost::none );
        component_factory->Register( sgns::ListenerFactory{}.create( "ws", app_config->rpc_ws_endpoint() ), "Listener",
                                     boost::make_optional( std::string( "ws" ) ) );
        component_factory->Register( sgns::ListenerFactory{}.create( "http", app_config->rpc_http_endpoint() ), "Listener",
                                     boost::make_optional( std::string( "http" ) ) );
        component_factory->Register( sgns::JRpcServerFactory{}.create(), "JRpcServer", boost::none );
        component_factory->Register( sgns::JRpcProcessorFactory{}.create( "Author" ), "JRpcProcessor",
                                     boost::make_optional( std::string( "Author" ) ) );
        component_factory->Register( sgns::ChainApiFactory{}.create(), "ChainApi", boost::none );
        component_factory->Register( sgns::JRpcProcessorFactory{}.create( "Chain" ), "JRpcProcessor",
                                     boost::make_optional( std::string( "Chain" ) ) );
        component_factory->Register( sgns::StateApiFactory{}.create(), "StateApi", boost::none );
        component_factory->Register( sgns::JRpcProcessorFactory{}.create( "State" ), "JRpcProcessor",
                                     boost::make_optional( std::string( "State" ) ) );
        component_factory->Register( sgns::SystemApiFactory{}.create(), "SystemApi", boost::none );
        component_factory->Register( sgns::JRpcProcessorFactory{}.create( "System" ), "JRpcProcessor",
                                     boost::make_optional( std::string( "System" ) ) );
        component_factory->Register( sgns::ApiServiceFactory{}.create(), "ApiService", boost::none );

        auto result = component_factory->GetComponent( "AppStateManager", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "AppStateManager not registered " );
        }
        app_state_manager_ = std::dynamic_pointer_cast<sgns::application::AppStateManager>( result.value() );

        result = component_factory->GetComponent( "ConfigurationStorage", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "ConfigurationStorage not registered " );
        }
        config_storage_ = std::dynamic_pointer_cast<sgns::application::ConfigurationStorage>( result.value() );

        result = component_factory->GetComponent( "KeyStorage", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "KeyStorage not registered " );
        }
        key_storage_ = std::dynamic_pointer_cast<sgns::application::KeyStorage>( result.value() );

        result = component_factory->GetComponent( "SystemClock", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "SystemClock not registered " );
        }
        clock_ = std::dynamic_pointer_cast<sgns::clock::SystemClock>( result.value() );

        result = component_factory->GetComponent( "Production", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Production not registered " );
        }
        production_ = std::dynamic_pointer_cast<sgns::verification::Production>( result.value() );

        result = component_factory->GetComponent( "Finality", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Finality not registered " );
        }
        finality_ = std::dynamic_pointer_cast<sgns::verification::finality::Finality>( result.value() );

        result = component_factory->GetComponent( "Router", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "Router not registered " );
        }
        router_ = std::dynamic_pointer_cast<sgns::network::Router>( result.value() );
        result  = component_factory->GetComponent( "ApiService", boost::none );
        if ( !result )
        {
            throw std::runtime_error( "ApiService not registered " );
        }
        jrpc_api_service_ = std::dynamic_pointer_cast<sgns::api::ApiService>( result.value() );
    }

    void ValidatingNodeApplication::run()
    {
        logger_->info( "Start as {} with PID {}", typeid( *this ).name(), getpid() );

        production_->setExecutionStrategy( production_execution_strategy_ );

        app_state_manager_->atLaunch(
            [this]
            {
                // execute listeners
                io_context_->post(
                    [this]
                    {
                        auto component_factory = SINGLETONINSTANCE( CComponentFactory );
                        auto result            = component_factory->GetComponent( "OwnPeerInfo", boost::none );
                        if ( !result )
                        {
                            throw std::runtime_error( "OwnPeerInfo not registered " );
                        }
                        auto current_peer_info = std::dynamic_pointer_cast<sgns::network::OwnPeerInfo>( result.value() );

                        auto  p2p_injector = libp2p::injector::makeHostInjector<BOOST_DI_CFG>();
                        auto &host         = p2p_injector.template create<libp2p::Host &>();
                        for ( const auto &ma : current_peer_info->addresses )
                        {
                            auto listen = host.listen( ma );
                            if ( !listen )
                            {
                                logger_->error( "Cannot listen address {}. Error: {}", ma.getStringAddress(), listen.error().message() );
                                std::exit( 1 );
                            }
                        }
                        this->router_->init();
                    } );
                return true;
            } );

        app_state_manager_->atLaunch(
            [ctx{ io_context_ }]
            {
                std::thread asio_runner( [ctx{ ctx }] { ctx->run(); } );
                asio_runner.detach();
                return true;
            } );

        app_state_manager_->atShutdown( [ctx{ io_context_ }] { ctx->stop(); } );

        app_state_manager_->run();
    }

} // namespace sgns::application
