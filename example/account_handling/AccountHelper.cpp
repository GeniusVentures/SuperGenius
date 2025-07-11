/**
 * @file       AccountHelper.cpp
 * @brief      
 * @date       2024-05-15
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <boost/format.hpp>
#include <rapidjson/document.h>
#include "AccountHelper.hpp"
#include <ipfs_lite/ipfs/graphsync/impl/network/network.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/local_requests.hpp>
#include "account/TokenID.hpp"
extern AccountKey2   ACCOUNT_KEY;
extern DevConfig_st2 DEV_CONFIG;

static const std::string logger_config( R"(
            # ----------------
            sinks:
              - name: console
                type: console
                color: true
            groups:
              - name: SuperGeniusDemo
                sink: console
                level: error
                children:
                  - name: libp2p
                  - name: Gossip
            # ----------------
              )" );

namespace sgns
{
    AccountHelper::AccountHelper( const AccountKey2   &priv_key_data,
                                  const DevConfig_st2 &dev_config,
                                  const char          *eth_private_key ) :
        account_( std::make_shared<GeniusAccount>( sgns::TokenID::FromBytes( { 0x00 } ), "", eth_private_key ) ),
        io_( std::make_shared<boost::asio::io_context>() ),
        dev_config_( dev_config )
    {
        logging_system = std::make_shared<soralog::LoggingSystem>( std::make_shared<soralog::ConfiguratorFromYAML>(
            // Original LibP2P logging config
            std::make_shared<libp2p::log::Configurator>(),
            // Additional logging config for application
            logger_config ) );
        logging_system->configure();
        libp2p::log::setLoggingSystem( logging_system );
        libp2p::log::setLevelOfGroup( "SuperGNUSNode", soralog::Level::ERROR_ );

        auto loggerGlobalDB = base::createLogger( "GlobalDB" );
        loggerGlobalDB->set_level( spdlog::level::debug );

        auto loggerDAGSyncer = base::createLogger( "GraphsyncDAGSyncer" );
        loggerDAGSyncer->set_level( spdlog::level::debug );

        auto logkad = sgns::base::createLogger( "Kademlia" );
        logkad->set_level( spdlog::level::trace );

        auto logNoise = sgns::base::createLogger( "Noise" );
        logNoise->set_level( spdlog::level::trace );

        auto pubsubKeyPath = ( boost::format( "SuperGNUSNode.TestNet.%s/pubs_processor" ) % account_->GetAddress() )
                                 .str();

        pubsub_ = std::make_shared<ipfs_pubsub::GossipPubSub>(
            crdt::KeyPairFileStorage( pubsubKeyPath ).GetKeyPair().value() );
        pubsub_->Start( 40001, {} );

        auto scheduler = std::make_shared<libp2p::protocol::AsioScheduler>( io_, libp2p::protocol::SchedulerConfig{} );
        auto graphsyncnetwork = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::Network>( pubsub_->GetHost(),
                                                                                             scheduler );
        auto generator        = std::make_shared<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator>();

        auto globaldc_ret = crdt::GlobalDB::New(
            io_,
            ( boost::format( "SuperGNUSNode.TestNet.%s" ) % account_->GetAddress() ).str(),
            pubsub_,
            crdt::CrdtOptions::DefaultOptions(),
            graphsyncnetwork,
            scheduler,
            generator );

        if ( globaldc_ret.has_error() )
        {
            throw std::runtime_error( globaldc_ret.error().message() );
        }

        globaldb_ = std::move( globaldc_ret.value() );

        globaldb_->AddListenTopic( std::string( PROCESSING_CHANNEL ) );
        globaldb_->AddBroadcastTopic( std::string( PROCESSING_CHANNEL ) );
        globaldb_->Start();

        base::Buffer root_hash;
        root_hash.put( std::vector<uint8_t>( 32ul, 1 ) );
        hasher_ = std::make_shared<crypto::HasherImpl>();

        header_repo_ = std::make_shared<blockchain::KeyValueBlockHeaderRepository>(
            globaldb_,
            hasher_,
            ( boost::format( std::string( db_path_ ) ) % TEST_NET ).str() );
        auto maybe_block_storage = blockchain::KeyValueBlockStorage::create( root_hash,
                                                                             globaldb_,
                                                                             hasher_,
                                                                             header_repo_,
                                                                             []( auto & ) {} );

        if ( !maybe_block_storage )
        {
            std::cout << "Error initializing blockchain" << std::endl;
            throw std::runtime_error( "Error initializing blockchain" );
        }
        block_storage_       = std::move( maybe_block_storage.value() );
        transaction_manager_ = std::make_shared<TransactionManager>( globaldb_, io_, account_, hasher_ );
        transaction_manager_->Start();

        // Encode the string to UTF-8 bytes
        std::string                temp = std::string( PROCESSING_CHANNEL );
        std::vector<unsigned char> inputBytes( temp.begin(), temp.end() );

        // Compute the SHA-256 hash of the input bytes
        std::vector<unsigned char> hash( SHA256_DIGEST_LENGTH );
        SHA256( inputBytes.data(), inputBytes.size(), hash.data() );
        //Provide CID
        libp2p::protocol::kademlia::ContentId key( hash );
        pubsub_->GetDHT()->Start();
        pubsub_->GetDHT()->ProvideCID( key, true );

        auto cidtest = libp2p::multi::ContentIdentifierCodec::decode( key.data );

        auto cidstring = libp2p::multi::ContentIdentifierCodec::toString( cidtest.value() );
        std::cout << "CID Test::" << cidstring.value() << std::endl;

        //Also Find providers
        pubsub_->StartFindingPeers( key );

        io_thread = std::thread( [this]() { io_->run(); } );
    }

    AccountHelper::~AccountHelper()
    {
        if ( io_ )
        {
            io_->stop();
        }
        if ( pubsub_ )
        {
            pubsub_->Stop();
        }
        if ( io_thread.joinable() )
        {
            io_thread.join();
        }
    }

    std::shared_ptr<TransactionManager> AccountHelper::GetManager()
    {
        return transaction_manager_;
    }

}
