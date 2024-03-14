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
#include "TransactionManager.hpp"
#include "account/TransferTransaction.hpp"

#include <ipfs_lite/ipfs/graphsync/graphsync.hpp>
#include "account/AccountManager.hpp"

std::vector<std::string> wallet_addr{ "0x4E8794BE4831C45D0699865028C8BE23D608C19C1E24371E3089614A50514262",
                                      "0x06DDC80283462181C02917CC3E99C7BC4BDB2856E19A392300A62DBA6262212C" };
std::vector<std::string> pubsub_addr{ "/ip4/127.0.1.1/tcp/40002/p2p/12D3KooWNoW68GUPmjrW4uRE4W3f9weGneNNB6z45ZJ6MEFTPmvq",
                                      "/ip4/127.0.1.1/tcp/40001/p2p/12D3KooWF6D2AtDY8JsYAw5SPUH5b6PWtj2LCDs4r847LH7LG1Tj" };
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

int main( int argc, char *argv[] )
{
    auto logging_system = std::make_shared<soralog::LoggingSystem>( std::make_shared<soralog::ConfiguratorFromYAML>(
        // Original LibP2P logging config
        std::make_shared<libp2p::log::Configurator>(),
        // Additional logging config for application
        logger_config ) );
    logging_system->configure();

    libp2p::log::setLoggingSystem( logging_system );
    libp2p::log::setLevelOfGroup( "account_handling_test", soralog::Level::ERROR_ );

    auto loggerGlobalDB = sgns::base::createLogger( "GlobalDB" );
    loggerGlobalDB->set_level( spdlog::level::trace );

    auto loggerDAGSyncer = sgns::base::createLogger( "GraphsyncDAGSyncer" );
    loggerDAGSyncer->set_level( spdlog::level::trace );

    auto loggerBroadcaster = sgns::base::createLogger( "PubSubBroadcasterExt" );
    loggerBroadcaster->set_level( spdlog::level::trace );

    //Inputs
    std::string   own_wallet_address;
    size_t        serviceindex = std::strtoul( argv[1], nullptr, 10 );
    std::uint32_t balance      = 0;
    if ( argc == 3 )
    {
        balance = std::stoull( argv[2] );
    }
    own_wallet_address = wallet_addr[serviceindex];
    auto maybe_account       = sgns::AccountManager{}.CreateAccount( own_wallet_address, balance );

    const std::string processingGridChannel = "GRID_CHANNEL_ID";

    auto account = std::make_shared<sgns::GeniusAccount>(maybe_account.value());

    auto pubsubKeyPath = ( boost::format( "CRDT.Datastore.TEST.%d/pubs_processor" ) % serviceindex ).str();
    auto pubs          = std::make_shared<sgns::ipfs_pubsub::GossipPubSub>( sgns::crdt::KeyPairFileStorage( pubsubKeyPath ).GetKeyPair().value() );

    //Make Host Pubsubs
    std::vector<std::string> receivedMessages;

    //Start Pubsubs, add peers of other addresses. We'll probably use DHT Discovery bootstrapping in the future.
    pubs->Start( 40001 + serviceindex, { pubsub_addr[serviceindex] } );

    const size_t maximalNodesCount = 1;

    //Asio Context
    auto io = std::make_shared<boost::asio::io_context>();

    //Add to GlobalDB
    auto globalDB =
        std::make_shared<sgns::crdt::GlobalDB>( io, ( boost::format( "CRDT.Datastore.TEST.%d" ) % serviceindex ).str(), 40010 + serviceindex,
                                                std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>( pubs, "CRDT.Datastore.TEST.Channel" ) );

    auto crdtOptions = sgns::crdt::CrdtOptions::DefaultOptions();
    globalDB->Init( crdtOptions );

    sgns::TransactionManager transaction_manager( globalDB, io, account );
    transaction_manager.Start();

    size_t other_addr = 1;
    if (serviceindex)
    {
        other_addr = 0;
    }
    auto transfer_transaction = std::make_shared<sgns::TransferTransaction>(10,uint256_t{wallet_addr[other_addr]});
    transaction_manager.EnqueueTransaction(transfer_transaction);

    //Run ASIO
    std::thread iothread( [io]() { io->run(); } );
    // Gracefully shutdown on signal
    boost::asio::signal_set signals( *pubs->GetAsioContext(), SIGINT, SIGTERM );
    signals.async_wait(
        [&pubs, &io]( const boost::system::error_code &, int )
        {
            pubs->Stop();
            io->stop();
        } );

    pubs->Wait();
    iothread.join();
}