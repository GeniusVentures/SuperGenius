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

#include "storage/trie/supergenius_trie/supergenius_trie_factory_impl.hpp"
#include "storage/trie/serialization/supergenius_codec.hpp"

#include "verification/production/impl/production_impl.hpp"
#include "verification/finality/impl/finality_impl.hpp"
#include "network/impl/router_libp2p.hpp"
#include <boost/filesystem.hpp>
#include <libp2p/injector/host_injector.hpp>
#include "clock/impl/clock_impl.hpp"
#include "integration/CComponentFactory.hpp"

namespace sgns::application
{

    ValidatingNodeApplication::ValidatingNodeApplication( const std::shared_ptr<AppConfiguration> &app_config ) :
        logger_( base::createLogger( "Application" ) )
    {
        spdlog::set_level( app_config->verbosity() );

        // genesis launch if database does not exist
        production_execution_strategy_ = boost::filesystem::exists( app_config->rocksdb_path() ) ? Production::ExecutionStrategy::SYNC_FIRST :
                                                                                                   Production::ExecutionStrategy::GENESIS;
        auto component_factory         = SINGLETONINSTANCE( CComponentFactory );

        component_factory->Register( std::make_shared<sgns::application::AppStateManagerImpl>(), "AppStateManager", boost::none );
        component_factory->Register( ConfigurationStorageFactory::create( app_config->genesis_path() ), "ConfigurationStorage", boost::none );
        component_factory->Register( KeyStorageFactory::create( app_config->keystore_path() ), "KeyStorage", boost::none );
        component_factory->Register( SystemClockFactory::create(), "SystemClock", boost::none );

        component_factory->Register( BufferStorageFactory::create( "rocksdb", app_config->rocksdb_path() ), "BufferStorage", boost::make_optional(std::string("rocksdb")) );
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
        component_factory->Register( BlockStorageFactory::create(), "BlockStorage", boost::none );
        component_factory->Register( BlockTreeFactory::create(), "BlockTree", boost::none );

        auto result = component_factory->GetComponent( "AppStateManager", boost::none );
        if ( result )
        {
            app_state_manager_ = std::dynamic_pointer_cast<sgns::application::AppStateManager>( result.value() );
        }
        result = component_factory->GetComponent( "ConfigurationStorage", boost::none );
        if ( result )
        {
            config_storage_ = std::dynamic_pointer_cast<sgns::application::ConfigurationStorage>( result.value() );
        }
        result = component_factory->GetComponent( "KeyStorage", boost::none );
        if ( result )
        {
            key_storage_ = std::dynamic_pointer_cast<sgns::application::KeyStorage>( result.value() );
        }
        result = component_factory->GetComponent( "SystemClock", boost::none );
        if ( result )
        {
            clock_ = std::dynamic_pointer_cast<sgns::clock::SystemClock>( result.value() );
        }
        //production_        = std::make_shared<verification::ProductionImpl>();
        //finality_          = std::make_shared<verification::finality::FinalityImpl>();
        //router_            = std::make_shared<network::RouterLibp2p>();

        //jrpc_api_service_ = std::make_shared<api::ApiService>();
        io_context_ = std::make_shared<boost::asio::io_context>();
    }

    void ValidatingNodeApplication::run()
    {
        logger_->info( "Start as {} with PID {}", typeid( *this ).name(), getpid() );

        //production_->setExecutionStrategy( production_execution_strategy_ );

        app_state_manager_->atLaunch(
            [this]
            {
                // execute listeners

                io_context_->post(
                    [this]
                    {
                        //di::bind<network::OwnPeerInfo>.to( [p2p_port{ app_config->p2p_port() }]( const auto &injector )
                        //                                   { return get_peer_info( injector, p2p_port ); } ),
                        //auto p2p_injector = libp2p::injector::makeHostInjector<BOOST_DI_CFG>();
                        //auto                &key_marshaller = p2p_injector.template create<libp2p::crypto::marshaller::KeyMarshaller &>();
                        //libp2p::peer::PeerId peer_id  = libp2p::peer::PeerId::fromPublicKey( key_marshaller.marshal( public_key ).value() ).value();
                        //auto                 p2p_info = std::shared_ptr<network::OwnPeerInfo>();
                        //const network::OwnPeerInfo &current_peer_info();
                        //libp2p::Host &host = p2p_injector.template create<libp2p::Host &>();
                        //for ( const auto &ma : current_peer_info.addresses )
                        //{
                        //    auto listen = host.listen( ma );
                        //    if ( !listen )
                        //    {
                        //        logger_->error( "Cannot listen address {}. Error: {}", ma.getStringAddress(), listen.error().message() );
                        //        std::exit( 1 );
                        //    }
                        //}
                        //this->router_->init();
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
