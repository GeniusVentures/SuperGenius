#include "globaldb.hpp"
#include "pubsub_broadcaster.hpp"
#include "pubsub_broadcaster_ext.hpp"
#include "keypair_file_storage.hpp"

#include "crdt/crdt_datastore.hpp"
#include "crdt/graphsync_dagsyncer.hpp"
#include "crdt/atomic_transaction.hpp"

#include <ipfs_lite/ipfs/merkledag/impl/merkledag_service_impl.hpp>
#include <ipfs_lite/ipfs/impl/datastore_rocksdb.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/graphsync_impl.hpp>

#include <libp2p/multi/multiaddress.hpp>
#include <libp2p/host/host.hpp>
#include <libp2p/injector/host_injector.hpp>
#include <libp2p/protocol/common/asio/asio_scheduler.hpp>
#include <libp2p/common/literals.hpp>
#include <libp2p/injector/kademlia_injector.hpp>
#include <boost/di/extension/scopes/shared.hpp>
#include <boost/format.hpp>

#if defined( _WIN32 )
#include <winsock2.h>
#include <iphlpapi.h>
#pragma comment( lib, "iphlpapi.lib" )
#pragma comment( lib, "ws2_32.lib" )
#else
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

OUTCOME_CPP_DEFINE_CATEGORY_3( sgns::crdt, GlobalDB::Error, e )
{
    using ProofError = sgns::crdt::GlobalDB::Error;
    switch ( e )
    {
        case ProofError::ROCKSDB_IO:
            return "RocksDB I/O error";
        case ProofError::IPFS_DB_NOT_CREATED:
            return "IPFS Database creation error";
        case ProofError::DAG_SYNCHER_NOT_LISTENING:
            return "DAG Syncher listen error";
        case ProofError::CRDT_DATASTORE_NOT_CREATED:
            return "CRDT DataStore creation error";
    }
    return "Unknown error";
}

namespace sgns::crdt
{
    using RocksDB            = storage::rocksdb;
    using CrdtOptions        = crdt::CrdtOptions;
    using CrdtDatastore      = crdt::CrdtDatastore;
    using HierarchicalKey    = crdt::HierarchicalKey;
    using GraphsyncDAGSyncer = crdt::GraphsyncDAGSyncer;
    using RocksdbDatastore   = ipfs_lite::ipfs::RocksdbDatastore;
    using IpfsRocksDb        = ipfs_lite::rocksdb;
    using GossipPubSub       = ipfs_pubsub::GossipPubSub;
    using GraphsyncImpl      = ipfs_lite::ipfs::graphsync::GraphsyncImpl;
    using GossipPubSubTopic  = ipfs_pubsub::GossipPubSubTopic;

    GlobalDB::GlobalDB( std::shared_ptr<boost::asio::io_context>         context,
                        std::string                                      databasePath,
                        int                                              dagSyncPort,
                        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub,
                        std::string                                      gsaddresses ) :
        m_context( std::move( context ) ),
        m_databasePath( std::move( databasePath ) ),
        m_dagSyncPort( dagSyncPort ),
        m_graphSyncAddrs( std::move( gsaddresses ) ),
        m_pubsub( std::move( pubsub ) )
    {
    }

    GlobalDB::GlobalDB( std::shared_ptr<boost::asio::io_context>              context,
                        std::string                                           databasePath,
                        int                                                   dagSyncPort,
                        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSubTopic> broadcastChannel,
                        std::string                                           gsaddresses ) :
        m_context( std::move( context ) ),
        m_databasePath( std::move( databasePath ) ),
        m_dagSyncPort( dagSyncPort ),
        m_graphSyncAddrs( std::move( gsaddresses ) ),
        m_broadcastChannel( std::move( broadcastChannel ) )
    {
    }


    GlobalDB::~GlobalDB()
    {
        if (m_crdtDatastore)
        {
            m_crdtDatastore->Close();
        }
        m_crdtDatastore    = nullptr;
        m_broadcastChannel = nullptr;
        m_context          = nullptr;
    }

std::string GetLocalIP( boost::asio::io_context &io )
    {
#if defined( _WIN32 )
        // Windows implementation using GetAdaptersAddresses
        ULONG                 bufferSize       = 15000;
        IP_ADAPTER_ADDRESSES *adapterAddresses = (IP_ADAPTER_ADDRESSES *)malloc( bufferSize );
        if ( GetAdaptersAddresses( AF_INET, 0, nullptr, adapterAddresses, &bufferSize ) == ERROR_BUFFER_OVERFLOW )
        {
            free( adapterAddresses );
            adapterAddresses = (IP_ADAPTER_ADDRESSES *)malloc( bufferSize );
        }
        std::string addr = "127.0.0.1"; // Default to localhost
        if ( GetAdaptersAddresses( AF_INET, 0, nullptr, adapterAddresses, &bufferSize ) == NO_ERROR )
        {
            for ( IP_ADAPTER_ADDRESSES *adapter = adapterAddresses; adapter; adapter = adapter->Next )
            {
                if ( adapter->OperStatus == IfOperStatusUp && adapter->IfType != IF_TYPE_SOFTWARE_LOOPBACK )
                {
                    for ( IP_ADAPTER_UNICAST_ADDRESS *unicast = adapter->FirstUnicastAddress; unicast;
                          unicast                             = unicast->Next )
                    {
                        SOCKADDR *addrStruct = unicast->Address.lpSockaddr;
                        if ( addrStruct->sa_family == AF_INET )
                        { // For IPv4
                            char buffer[INET_ADDRSTRLEN];
                            inet_ntop( AF_INET,
                                       &( ( (struct sockaddr_in *)addrStruct )->sin_addr ),
                                       buffer,
                                       INET_ADDRSTRLEN );

                            // Skip APIPA/link-local addresses (169.254.x.x)
                            if ( strncmp( buffer, "169.254.", 8 ) == 0 )
                            {
                                continue;
                            }

                            addr = buffer;
                            break;
                        }
                    }
                }
                if ( addr != "127.0.0.1" )
                {
                    break; // Stop if we found a non-loopback IP
                }
            }
        }
        free( adapterAddresses );
        return addr;
#else
        // Unix-like implementation using getifaddrs
        struct ifaddrs *ifaddr, *ifa;
        int             family;
        std::string     addr = "127.0.0.1"; // Default to localhost
        if ( getifaddrs( &ifaddr ) == -1 )
        {
            perror( "getifaddrs" );
            return addr;
        }
        for ( ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next )
        {
            if ( ifa->ifa_addr == nullptr )
            {
                continue;
            }
            family = ifa->ifa_addr->sa_family;
            // We only want IPv4 addresses
            if ( family == AF_INET && !( ifa->ifa_flags & IFF_LOOPBACK ) )
            {
                char host[NI_MAXHOST];
                int  s = getnameinfo( ifa->ifa_addr,
                                     sizeof( struct sockaddr_in ),
                                     host,
                                     NI_MAXHOST,
                                     nullptr,
                                     0,
                                     NI_NUMERICHOST );
                if ( s == 0 )
                {
                    // Skip APIPA/link-local addresses (169.254.x.x)
                    if ( strncmp( host, "169.254.", 8 ) == 0 )
                    {
                        continue;
                    }

                    addr = host;
                    break;
                }
            }
        }
        freeifaddrs( ifaddr );
        return addr;
#endif
    }

    outcome::result<void> GlobalDB::Init( std::shared_ptr<CrdtOptions> crdtOptions )
    {
        std::shared_ptr<RocksDB> dataStore            = nullptr;
        auto                     databasePathAbsolute = boost::filesystem::absolute( m_databasePath ).string();

        // Create new database
        m_logger->info( "Opening database " + databasePathAbsolute );
        RocksDB::Options options;
        options.create_if_missing = true; // intentionally
        try
        {
            if ( auto dataStoreResult = RocksDB::create( databasePathAbsolute, options ); dataStoreResult.has_value() )
            {
                dataStore = dataStoreResult.value();
            }
            else
            {
                m_logger->error( "Unable to open database: " + std::string( dataStoreResult.error().message() ) );
                return outcome::failure( boost::system::error_code{} );
            }
        }
        catch ( std::exception &e )
        {
            m_logger->error( "Unable to open database: " + std::string( e.what() ) );
            return Error::ROCKSDB_IO;
        }

        boost::filesystem::path keyPath = databasePathAbsolute + "/key";
        KeyPairFileStorage      keyPairStorage( keyPath );
        auto                    keyPair = keyPairStorage.GetKeyPair();
        //Kademlia Config
        libp2p::protocol::kademlia::Config kademlia_config;
        kademlia_config.randomWalk.enabled  = true;
        kademlia_config.randomWalk.interval = std::chrono::seconds( 300 );
        kademlia_config.requestConcurency   = 3;
        kademlia_config.maxProvidersPerKey  = 300;
        // injector creates and ties dependent objects
        auto injector = libp2p::injector::makeHostInjector<boost::di::extension::shared_config>(
            boost::di::bind<boost::asio::io_context>.to( m_context )[boost::di::override],
            boost::di::bind<libp2p::crypto::KeyPair>.to( keyPair.value() )[boost::di::override] );

        // create asio context
        auto io = injector.create<std::shared_ptr<boost::asio::io_context>>();

        // Create new DAGSyncer
        IpfsRocksDb::Options rdbOptions;
        rdbOptions.create_if_missing = true; // intentionally
        auto ipfsDBResult            = IpfsRocksDb::create( dataStore->getDB() );
        if ( ipfsDBResult.has_error() )
        {
            m_logger->error( "Unable to create database for IPFS datastore" );
            return Error::IPFS_DB_NOT_CREATED;
        }

        auto ipfsDataStore = std::make_shared<RocksdbDatastore>( ipfsDBResult.value() );
        auto scheduler = std::make_shared<libp2p::protocol::AsioScheduler>( m_context,
                                                                            libp2p::protocol::SchedulerConfig{} );

        std::shared_ptr<libp2p::Host> host;
        if ( m_broadcastChannel )
        {
            host = m_broadcastChannel->GetPubsub()->GetHost();
        }
        else if ( m_pubsub )
        {
            host = m_pubsub->GetHost();
        }
        else
        {
            m_logger->error( "Neither broadcast channel nor pubsub is initialized." );
            return outcome::failure( Error::DAG_SYNCHER_NOT_LISTENING );
        }

        auto graphsync = std::make_shared<GraphsyncImpl>( host, std::move( scheduler ) );
        auto dagSyncer = std::make_shared<GraphsyncDAGSyncer>( ipfsDataStore,
                                                               graphsync,
                                                               host );

        auto dagSyncerHost = injector.create<std::shared_ptr<libp2p::Host>>();

        //Make a DHT
        //auto kademlia =
        //    injector
        //    .create<std::shared_ptr<libp2p::protocol::kademlia::Kademlia>>();
        //dht_ = std::make_shared<sgns::ipfs_lite::ipfs::dht::IpfsDHT>(kademlia, bootstrapAddresses_, m_context);
        ////Make Holepunch Client
        //holepunchmsgproc_ = std::make_shared<libp2p::protocol::HolepunchClientMsgProc>(*dagSyncerHost, dagSyncerHost->getNetwork().getConnectionManager());
        //holepunch_ = std::make_shared<libp2p::protocol::HolepunchClient>(*dagSyncerHost, holepunchmsgproc_, dagSyncerHost->getBus());
        //holepunch_->start();
        ////Make Identify
        //identifymsgproc_ = std::make_shared<libp2p::protocol::IdentifyMessageProcessor>(
        //    *dagSyncerHost, dagSyncerHost->getNetwork().getConnectionManager(), *injector.create<std::shared_ptr<libp2p::peer::IdentityManager>>(), injector.create<std::shared_ptr<libp2p::crypto::marshaller::KeyMarshaller>>());
        //identify_ = std::make_shared<libp2p::protocol::Identify>(*dagSyncerHost, identifymsgproc_, dagSyncerHost->getBus(), injector.create<std::shared_ptr<libp2p::transport::Upgrader>>(), [self{ shared_from_this() }]() {
        //    });
        //identify_->start();

        //If we used upnp we should have an address list, if not just get local ip
        std::string localaddress;
        std::string wanaddress;
        if ( m_graphSyncAddrs.empty() )
        {
            localaddress = ( boost::format( "/ip4/%s/tcp/%d/p2p/%s" ) % GetLocalIP( *io ) % m_dagSyncPort %
                             dagSyncerHost->getId().toBase58() )
                               .str();
        }
        else
        {
            //use the first address, which should be the lan address for listening
            localaddress = ( boost::format( "/ip4/%s/tcp/%d/p2p/%s" ) % m_graphSyncAddrs % m_dagSyncPort %
                             dagSyncerHost->getId().toBase58() )
                               .str();
            //wanaddress = (boost::format("/ip4/%s/tcp/%d/p2p/%s") % m_graphSyncAddrs[1] % m_dagSyncPort % dagSyncerHost->getId().toBase58()).str();
        }
        //m_broadcastChannel->GetPubsub()->GetHost()->getObservedAddresses()
        //auto listen_to = libp2p::multi::Multiaddress::create(
        //    (boost::format("/ip4/192.168.46.18/tcp/%d/ipfs/%s") % m_dagSyncPort % dagSyncerHost->getId().toBase58()).str()).value();
        auto listen_to = libp2p::multi::Multiaddress::create( localaddress ).value();
        m_logger->debug( listen_to.getStringAddress() );

        auto scheduler = std::make_shared<libp2p::protocol::AsioScheduler>( io, libp2p::protocol::SchedulerConfig{} );
        auto graphsync = std::make_shared<GraphsyncImpl>( dagSyncerHost, std::move( scheduler ) );
        auto dagSyncer = std::make_shared<GraphsyncDAGSyncer>( ipfsDataStore, graphsync, dagSyncerHost );

        // Start DagSyner listener
        auto listenResult = dagSyncer->Listen( listen_to );
        if ( listenResult.has_failure() )
        {
            m_logger->warn( "DAG syncer failed to listen " + std::string( listen_to.getStringAddress() ) );
            // @todo Check if the error is not fatal
            return Error::DAG_SYNCHER_NOT_LISTENING;
        }

        //dht_->Start();
        //dht_->bootstrap();
        //scheduleBootstrap(io, dagSyncerHost);
        // Create pubsub broadcaster
        std::shared_ptr<PubSubBroadcasterExt>                              broadcaster;
        std::vector<std::shared_ptr<sgns::ipfs_pubsub::GossipPubSubTopic>> topics;
        if ( !m_broadcastTopicNames.empty() )
        {
            for ( const auto &topicName : m_broadcastTopicNames )
            {
                topics.push_back( std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>( m_pubsub, topicName ) );
            }
        }
        else if ( m_broadcastChannel != nullptr )
        {
            topics.push_back( m_broadcastChannel );
        }
        // Otherwise, topics remains empty.

        m_broadcaster   = PubSubBroadcasterExt::New( topics, dagSyncer, listen_to );
        m_crdtDatastore = CrdtDatastore::New( dataStore,
                                              HierarchicalKey( "crdt" ),
                                              dagSyncer,
                                              m_broadcaster,
                                              crdtOptions );
        if ( m_crdtDatastore == nullptr )
        {
            m_logger->error( "Unable to create CRDT datastore" );
            return Error::CRDT_DATASTORE_NOT_CREATED;
        }

        m_broadcaster->SetCrdtDataStore( m_crdtDatastore );

        // have to set the dataStore before starting the broadcasting
        m_broadcaster->Start();

        // TODO: bootstrapping
        //m_logger->info("Bootstrapping...");
        //bstr, _ : = multiaddr.NewMultiaddr("/ip4/94.130.135.167/tcp/33123/ipfs/12D3KooWFta2AE7oiK1ioqjVAKajUJauZWfeM7R413K7ARtHRDAu");
        //inf, _ : = peer.AddrInfoFromP2pAddr(bstr);
        //list: = append(ipfslite.DefaultBootstrapPeers(), *inf);
        //ipfs.Bootstrap(list);
        //h.ConnManager().TagPeer(inf.ID, "keep", 100);

        //if (daemonMode && echoProtocol)
        //{
        //  // set a handler for Echo protocol
        //  libp2p::protocol::Echo echo{ libp2p::protocol::EchoConfig{} };
        //  host->setProtocolHandler(
        //    echo.getProtocolId(),
        //    [&echo](std::shared_ptr<libp2p::connection::Stream> received_stream) {
        //      echo.handle(std::move(received_stream));
        //    });
        //}
        return outcome::success();
    }

    //void GlobalDB::scheduleBootstrap(std::shared_ptr<boost::asio::io_context> io_context, std::shared_ptr<libp2p::Host> host)
    //{
    //    auto timer = std::make_shared<boost::asio::steady_timer>(*io_context);
    //    timer->expires_after(std::chrono::seconds(30));
    //
    //    timer->async_wait([self{ shared_from_this() }, timer, io_context, host](const boost::system::error_code& ec) {
    //        if (!ec && self->obsAddrRetries < 3) {
    //            if (host->getObservedAddressesReal().size() <= 0)
    //            {
    //                self->dht_->bootstrap();
    //                ++self->obsAddrRetries;
    //                self->scheduleBootstrap(io_context, host);
    //            }
    //        }
    //        });
    //}

    outcome::result<void> GlobalDB::Put( const HierarchicalKey     &key,
                                         const Buffer              &value,
                                         std::optional<std::string> topic )
    {
        if ( !m_crdtDatastore )
        {
            m_logger->error( "CRDT datastore is not initialized yet" );
            return outcome::failure( Error::CRDT_DATASTORE_NOT_CREATED );
        }

        return m_crdtDatastore->PutKey( key, value, topic );
    }

    outcome::result<void> GlobalDB::Put( const std::vector<DataPair> &data_vector )
    {
        if ( !m_crdtDatastore )
        {
            m_logger->error( "CRDT datastore is not initialized yet" );
            return outcome::failure( Error::CRDT_DATASTORE_NOT_CREATED );
        }
        AtomicTransaction batch( m_crdtDatastore );

        for ( auto &data : data_vector )
        {
            BOOST_OUTCOME_TRYV2( auto &&, batch.Put( std::get<0>( data ), std::get<1>( data ) ) );
        }

        return batch.Commit();
    }
    
    outcome::result<GlobalDB::Buffer> GlobalDB::Get( const HierarchicalKey &key )
    {
        if ( !m_crdtDatastore )
        {
            m_logger->error( "CRDT datastore is not initialized yet" );
            return outcome::failure( boost::system::error_code{} );
        }

        return m_crdtDatastore->GetKey( key );
    }

    outcome::result<void> GlobalDB::Remove( const HierarchicalKey &key )
    {
        if ( !m_crdtDatastore )
        {
            m_logger->error( "CRDT datastore is not initialized yet" );
            return outcome::failure( boost::system::error_code{} );
        }

        return m_crdtDatastore->DeleteKey( key );
    }

    outcome::result<GlobalDB::QueryResult> GlobalDB::QueryKeyValues( const std::string &keyPrefix )
    {
        if ( !m_crdtDatastore )
        {
            m_logger->error( "CRDT datastore is not initialized yet" );
            return outcome::failure( boost::system::error_code{} );
        }

        return m_crdtDatastore->QueryKeyValues( keyPrefix );
    }

    outcome::result<std::string> GlobalDB::KeyToString( const Buffer &key ) const
    {
        // @todo cache the prefix and suffix
        auto keysPrefix = m_crdtDatastore->GetKeysPrefix();
        if ( !keysPrefix.has_value() )
        {
            return outcome::failure( boost::system::error_code{} );
        }
        auto valueSuffix = m_crdtDatastore->GetValueSuffix();
        if ( !valueSuffix.has_value() )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        auto sKey = std::string( key.toString() );

        size_t prefixPos = ( !keysPrefix.value().empty() ) ? sKey.find( keysPrefix.value(), 0 ) : 0;
        if ( prefixPos != 0 )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        size_t keyPos    = keysPrefix.value().size();
        auto   suffixPos = ( !valueSuffix.value().empty() ) ? sKey.rfind( valueSuffix.value(), std::string::npos )
                                                            : sKey.size();
        if ( ( suffixPos == std::string::npos ) || ( suffixPos < keyPos ) )
        {
            return outcome::failure( boost::system::error_code{} );
        }

        return sKey.substr( keyPos, suffixPos - keyPos );
    }

    void GlobalDB::PrintDataStore()
    {
        m_crdtDatastore->PrintDataStore();
    }

    std::shared_ptr<AtomicTransaction> GlobalDB::BeginTransaction()
    {
        return std::make_shared<AtomicTransaction>( m_crdtDatastore );
    }

    void GlobalDB::AddBroadcastTopic( const std::string &topicName )
    {
        if ( !m_pubsub )
        {
            m_logger->error( "PubSub instance is not available to create new topic." );
            return;
        }
        auto newTopic = std::make_shared<sgns::ipfs_pubsub::GossipPubSubTopic>( m_pubsub, topicName );
        if ( m_broadcaster )
        {
            m_broadcaster->AddTopic( newTopic );
            m_logger->info( "New broadcast topic added: " + topicName );
        }
        else
        {
            m_logger->error( "Broadcaster is not initialized." );
        }
    }

}
