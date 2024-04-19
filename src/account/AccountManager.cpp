/**
 * @file       AccountManager.cpp
 * @brief      
 * @date       2024-04-18
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include "account/AccountManager.hpp"
#include "processing/processing_imagesplit.hpp"

namespace sgns
{
    AccountManager::AccountManager( const std::string &priv_key_data ) :
        account_( std::make_shared<GeniusAccount>( uint256_t{ priv_key_data }, 0, 0 ) ), //
        io_( std::make_shared<boost::asio::io_context>() )                               //
    {
        const std::string logger_config( R"(
                    # ----------------
                    sinks:
                    - name: console
                        type: console
                        color: true
                    groups:
                    - name: SuperGeniusDemo
                        sink: console
                        level: info
                        children:
                        - name: libp2p
                        - name: Gossip
                    # ----------------
                    )" );
        auto              logging_system = std::make_shared<soralog::LoggingSystem>( std::make_shared<soralog::ConfiguratorFromYAML>(
            // Original LibP2P logging config
            std::make_shared<libp2p::log::Configurator>(),
            // Additional logging config for application
            logger_config ) );
        logging_system->configure();

        libp2p::log::setLoggingSystem( logging_system );
        libp2p::log::setLevelOfGroup( "SuperGNUSNode", soralog::Level::ERROR_ );

        auto loggerGlobalDB = sgns::base::createLogger( "GlobalDB" );
        loggerGlobalDB->set_level( spdlog::level::debug );

        auto loggerDAGSyncer = sgns::base::createLogger( "GraphsyncDAGSyncer" );
        loggerDAGSyncer->set_level( spdlog::level::debug );

        //auto loggerBroadcaster = sgns::base::createLogger( "PubSubBroadcasterExt" );
        //loggerBroadcaster->set_level( spdlog::level::debug );

        auto pubsubKeyPath = ( boost::format( "SuperGNUSNode.TestNet.%s/pubs_processor" ) % priv_key_data ).str();

        pubsub_ = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( sgns::crdt::KeyPairFileStorage( pubsubKeyPath ).GetKeyPair().value() );
        pubsub_->Start( 40001, { pubsub_->GetLocalAddress() } );

        io_ = std::make_shared<boost::asio::io_context>();

        globaldb_ =
            std::make_shared<sgns::crdt::GlobalDB>( io_, ( boost::format( "SuperGNUSNode.TestNet.%s" ) % priv_key_data ).str(), 40010,
                                                    std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>( pubsub_, "SGNUS.TestNet.Channel" ) );

        globaldb_->Init( sgns::crdt::CrdtOptions::DefaultOptions() );

        sgns::base::Buffer root_hash;
        root_hash.put( std::vector<uint8_t>( 32ul, 1 ) );
        hasher_ = std::make_shared<sgns::crypto::HasherImpl>();

        header_repo_ = std::make_shared<sgns::blockchain::KeyValueBlockHeaderRepository>(
            globaldb_, hasher_, ( boost::format( std::string( db_path_ ) ) % TEST_NET ).str() );
        auto maybe_block_storage = sgns::blockchain::KeyValueBlockStorage::create( root_hash, globaldb_, hasher_, header_repo_, []( auto & ) {} );

        if ( !maybe_block_storage )
        {
            std::cout << "Error initializing blockchain" << std::endl;
            throw std::runtime_error( "Error initializing blockchain" );
        }
        block_storage_       = std::move( maybe_block_storage.value() );
        transaction_manager_ = std::make_shared<TransactionManager>( globaldb_, io_, account_, block_storage_ );
        transaction_manager_->Start();

        io_thread = std::thread( [this]() { io_->run(); } );
    }
    AccountManager::AccountManager() : account_( std::make_shared<GeniusAccount>( 0, 0, 0 ) ) //

    {
    }
    AccountManager::~AccountManager()
    {
        if ( signals_ )
        {
            signals_->cancel(); // Cancel any pending signals
        }
        if ( io_ )
        {
            io_->stop(); // Stop our io_context
        }
        if ( pubsub_ )
        {
            pubsub_->Stop(); // Stop activities of OtherClass
        }
        if ( io_thread.joinable() )
        {
            io_thread.join();
        }
        std::cout << "deleted_it " << std::endl;
    }

    boost::optional<GeniusAccount> AccountManager::CreateAccount( const std::string &priv_key_data, const uint64_t &initial_amount )
    {
        uint256_t value_in_num( priv_key_data );
        return GeniusAccount( value_in_num, initial_amount, 0 );
    }

    void AccountManager::ProcessImage( const std::string &image_path, uint16_t funds )
    {
        transaction_manager_->TransferFunds( uint256_t{ funds }, uint256_t{ "0x100" } );
    }
    void AccountManager::MintTokens( uint64_t amount )
    {
        transaction_manager_->MintFunds( amount );
    }
}