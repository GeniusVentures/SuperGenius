/**
 * @file       AccountHandling.cpp
 * @brief      
 * @date       2024-03-12
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */
#include <math.h>
#include <fstream>
#include <memory>
#include <iostream>
#include <cstdlib>
#include <cstdint>
#include "ipfs_pubsub/gossip_pubsub.hpp"
#include <crdt/globaldb/globaldb.hpp>
#include <crdt/globaldb/keypair_file_storage.hpp>
#include <crdt/globaldb/proto/broadcast.pb.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>
#include <libp2p/multi/content_identifier_codec.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/asio.hpp>
#include "account/TransactionManager.hpp"
#include "blockchain/impl/common.hpp"
#include "blockchain/impl/key_value_block_header_repository.hpp"
#include "blockchain/impl/key_value_block_storage.hpp"
#include "crypto/hasher/hasher_impl.hpp"

#include <ipfs_lite/ipfs/graphsync/graphsync.hpp>
#include "account/GeniusAccount.hpp"

std::vector<std::string> wallet_addr{ "0x4E8794BE4831C45D0699865028C8BE23D608C19C1E24371E3089614A50514262",
                                      "0x06DDC80283462181C02917CC3E99C7BC4BDB2856E19A392300A62DBA6262212C" };

using GossipPubSub = sgns::ipfs_pubsub::GossipPubSub;
const std::string logger_config( R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: account_handling_test
    sink: console
    level: warning
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )" );

std::mutex              keyboard_mutex;
std::condition_variable cv;
std::queue<std::string> events;

void keyboard_input_thread()
{
    std::string line;
    while ( std::getline( std::cin, line ) )
    {
        {
            std::lock_guard<std::mutex> lock( keyboard_mutex );
            events.push( line );
        }
        cv.notify_one();
    }
}
void CreateTransferTransaction( const std::vector<std::string> &args, sgns::TransactionManager &transaction_manager )
{
    if ( args.size() != 3 )
    {
        std::cerr << "Invalid transfer command format.\n";
        return;
    }
    uint64_t amount = std::stoull( args[1] );
    if (! transaction_manager.TransferFunds( uint256_t{ args[1] }, uint256_t{ args[2] } ))
    {
        std::cout << "Insufficient funds.\n";
    }
}
void CreateProcessingTransaction( const std::vector<std::string> &args, sgns::TransactionManager &transaction_manager )
{
    if ( args.size() != 2 )
    {
        std::cerr << "Invalid process command format.\n";
        return;
    }

    //TODO - Create processing transaction
}
void MintTokens( const std::vector<std::string> &args, sgns::TransactionManager &transaction_manager )
{
    if ( args.size() != 2 )
    {
        std::cerr << "Invalid process command format.\n";
        return;
    }
    transaction_manager.MintFunds( std::stoull( args[1] ) );
}
void PrintAccountInfo( const std::vector<std::string> &args, sgns::TransactionManager &transaction_manager )
{
    if ( args.size() != 1 )
    {
        std::cerr << "Invalid info command format.\n";
        return;
    }
    transaction_manager.PrintAccountInfo();

    //TODO - Create processing transaction
}

std::vector<std::string> split_string( const std::string &str )
{
    std::istringstream       iss( str );
    std::vector<std::string> results( ( std::istream_iterator<std::string>( iss ) ), std::istream_iterator<std::string>() );
    return results;
}

void process_events( sgns::TransactionManager &transaction_manager )
{
    std::unique_lock<std::mutex> lock( keyboard_mutex );
    cv.wait( lock, [] { return !events.empty(); } );

    while ( !events.empty() )
    {
        std::string event = events.front();
        events.pop();

        auto arguments = split_string( event );
        if ( arguments.size() == 0 )
        {
            return;
        }
        if ( arguments[0] == "transfer" )
        {
            CreateTransferTransaction( arguments, transaction_manager );
        }
        else if ( arguments[0] == "process" )
        {
            CreateProcessingTransaction( arguments, transaction_manager );
        }
        else if ( arguments[0] == "info" )
        {
            PrintAccountInfo( arguments, transaction_manager );
        }
        else if ( arguments[0] == "mint" )
        {
            MintTokens( arguments, transaction_manager );
        }
        else
        {
            std::cerr << "Unknown command: " << arguments[0] << "\n";
        }
    }
}

int main( int argc, char *argv[] )
{
    std::thread input_thread( keyboard_input_thread );
    //make_terminal_nonblocking();
    auto logging_system = std::make_shared<soralog::LoggingSystem>( std::make_shared<soralog::ConfiguratorFromYAML>(
        // Original LibP2P logging config
        std::make_shared<libp2p::log::Configurator>(),
        // Additional logging config for application
        logger_config ) );
    logging_system->configure();

    libp2p::log::setLoggingSystem( logging_system );
    libp2p::log::setLevelOfGroup( "account_handling_test", soralog::Level::ERROR_ );

    auto loggerGlobalDB = sgns::base::createLogger( "GlobalDB" );
    loggerGlobalDB->set_level( spdlog::level::debug );

    auto loggerDAGSyncer = sgns::base::createLogger( "GraphsyncDAGSyncer" );
    loggerDAGSyncer->set_level( spdlog::level::debug );

    //auto loggerBroadcaster = sgns::base::createLogger( "PubSubBroadcasterExt" );
    //loggerBroadcaster->set_level( spdlog::level::debug );

    //Inputs
    size_t               serviceindex = std::strtoul( argv[1], nullptr, 10 );
    std::string          own_wallet_address( argv[2] );
    std::string          pubs_address( argv[3] );

    const std::string processingGridChannel = "GRID_CHANNEL_ID";

    auto account = std::make_shared<sgns::GeniusAccount>( uint256_t{own_wallet_address},0,0 );

    auto pubsubKeyPath = ( boost::format( "CRDT.Datastore.TEST.%d/pubs_processor" ) % serviceindex ).str();
    auto pubs          = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( sgns::crdt::KeyPairFileStorage( pubsubKeyPath ).GetKeyPair().value() );

    //Make Host Pubsubs
    std::vector<std::string> receivedMessages;

    //Start Pubsubs, add peers of other addresses. We'll probably use DHT Discovery bootstrapping in the future.
    pubs->Start( 40001 + serviceindex, { pubs_address } );

    std::cout << "***This is our pubsub address. Copy and past on other node third argument" << pubs->GetLocalAddress() << std::endl;
    std::cout << "BOOSTRAPPING  " << pubs->GetLocalAddress() << " with " << pubs_address << std::endl;

    //const size_t maximalNodesCount = 1;

    ////Asio Context
    auto io = std::make_shared<boost::asio::io_context>();

    ////Add to GlobalDB
    auto globalDB =
        std::make_shared<sgns::crdt::GlobalDB>( io, ( boost::format( "CRDT.Datastore.TEST.%d" ) % serviceindex ).str(), 40010 + serviceindex,
                                                std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>( pubs, "CRDT.Datastore.TEST.Channel" ) );

    auto crdtOptions = sgns::crdt::CrdtOptions::DefaultOptions();
    globalDB->Init( crdtOptions );

    sgns::base::Buffer root_hash;
    root_hash.put( std::vector<uint8_t>( 32ul, 1 ) );
    auto        hasher_             = std::make_shared<sgns::crypto::HasherImpl>();
    std::string db_path_            = "bc-963/";
    auto        header_repo_        = std::make_shared<sgns::blockchain::KeyValueBlockHeaderRepository>( globalDB, hasher_, db_path_ );
    auto        maybe_block_storage = sgns::blockchain::KeyValueBlockStorage::create( root_hash, globalDB, hasher_, header_repo_, []( auto        &) {} );

    if ( !maybe_block_storage )
    {
        std::cout << "Error initializing blockchain" << std::endl;
        return -1;
    }
    sgns::TransactionManager transaction_manager( globalDB, io, account, hasher_, maybe_block_storage.value() );
    transaction_manager.Start();

    //Run ASIO
    std::thread iothread( [io]() { io->run(); } );
    while ( true )
    {
        process_events( transaction_manager );
    }
    // Gracefully shutdown on signal
    boost::asio::signal_set signals( *pubs->GetAsioContext(), SIGINT, SIGTERM );
    signals.async_wait(
        [&pubs, &io]( const boost::system::error_code &, int )
        {
            pubs->Stop();
            io->stop();
        } );

    pubs->Wait();
    if ( input_thread.joinable() )
    {
        input_thread.join();
    }
    iothread.join();
    return 0;
}
