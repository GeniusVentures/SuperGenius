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
        case ProofError::PUBSUB_BROADCASTER_NOT_CREATED:
            return "Pubsub Broadcaster creation error";
        case ProofError::INVALID_PARAMETERS:
            return "Invalid parameters provided";
        case ProofError::GLOBALDB_NOT_STARTED:
            return "Start method wasn't called";
    }
    return "Unknown error";
}

namespace sgns::crdt
{

    using CrdtOptions        = crdt::CrdtOptions;
    using CrdtDatastore      = crdt::CrdtDatastore;
    using HierarchicalKey    = crdt::HierarchicalKey;
    using GraphsyncDAGSyncer = crdt::GraphsyncDAGSyncer;
    using RocksdbDatastore   = ipfs_lite::ipfs::RocksdbDatastore;
    using IpfsRocksDb        = ipfs_lite::rocksdb;
    using GossipPubSub       = ipfs_pubsub::GossipPubSub;
    using GraphsyncImpl      = ipfs_lite::ipfs::graphsync::GraphsyncImpl;
    using GossipPubSubTopic  = ipfs_pubsub::GossipPubSubTopic;

    outcome::result<std::shared_ptr<GlobalDB>> GlobalDB::New(
        std::shared_ptr<boost::asio::io_context>                              context,
        std::string                                                           databasePath,
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>                      pubsub,
        std::shared_ptr<CrdtOptions>                                          crdtOptions,
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network>            graphsyncnetwork,
        std::shared_ptr<libp2p::protocol::Scheduler>                          scheduler,
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
        std::shared_ptr<RocksDB>                                              datastore )
    {
        if ( ( !context ) || ( !generator ) || ( !pubsub ) || ( !graphsyncnetwork ) )
        {
            return outcome::failure( Error::INVALID_PARAMETERS );
        }
        auto new_instance = std::shared_ptr<GlobalDB>(
            new GlobalDB( std::move( context ), std::move( databasePath ), std::move( pubsub ) ) );

        BOOST_OUTCOME_TRYV2( auto &&,
                             new_instance->Init( std::move( crdtOptions ),
                                                 std::move( graphsyncnetwork ),
                                                 std::move( scheduler ),
                                                 std::move( generator ),
                                                 std::move( datastore ) ) );
        return new_instance;
    }

    GlobalDB::GlobalDB( std::shared_ptr<boost::asio::io_context>         context,
                        std::string                                      databasePath,
                        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub ) :
        m_context( std::move( context ) ),
        m_databasePath( std::move( databasePath ) ),
        m_pubsub( std::move( pubsub ) ),
        started_{ false }
    {
    }

    GlobalDB::~GlobalDB()
    {
        m_logger->debug( "~GlobalDB CALLED" );
        m_broadcaster->Stop();
        m_crdtDatastore->Close();
    }

    outcome::result<void> GlobalDB::Init(
        std::shared_ptr<CrdtOptions>                                          crdtOptions,
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network>            graphsyncnetwork,
        std::shared_ptr<libp2p::protocol::Scheduler>                          scheduler,
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
        std::shared_ptr<RocksDB>                                              datastore )
    {
        std::shared_ptr<RocksDB> dataStore = std::move( datastore );
        if ( dataStore == nullptr )
        {
            auto databasePathAbsolute = boost::filesystem::absolute( m_databasePath ).string();

            // Create new database
            m_logger->info( "Opening database " + databasePathAbsolute );
            RocksDB::Options options;
            options.create_if_missing                    = true; // intentionally
            options.target_file_size_base                = 32 * 1024 * 1024;
            options.max_compaction_bytes                 = 32 * 1024 * 1024;
            options.write_buffer_size                    = 32 * 1024 * 1024;
            options.level0_file_num_compaction_trigger   = 1;
            options.target_file_size_multiplier          = 1;
            options.level_compaction_dynamic_level_bytes = false;
            try
            {
                if ( auto dataStoreResult = RocksDB::create( databasePathAbsolute, options );
                     dataStoreResult.has_value() )
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
        }
        m_datastore = std::move( dataStore );

        IpfsRocksDb::Options rdbOptions;
        rdbOptions.create_if_missing = true; // intentionally
        auto ipfsDBResult            = IpfsRocksDb::create( m_datastore->getDB() );
        if ( ipfsDBResult.has_error() )
        {
            m_logger->error( "Unable to create database for IPFS datastore" );
            return Error::IPFS_DB_NOT_CREATED;
        }

        auto ipfsDataStore = std::make_shared<RocksdbDatastore>( ipfsDBResult.value() );

        if ( !m_pubsub )
        {
            m_logger->error( "pubsub not initialized." );
            return outcome::failure( Error::DAG_SYNCHER_NOT_LISTENING );
        }
        std::shared_ptr<libp2p::Host> host = m_pubsub->GetHost();

        auto graphsync = std::make_shared<GraphsyncImpl>( host,
                                                          std::move( scheduler ),
                                                          graphsyncnetwork,
                                                          generator,
                                                          m_context );
        auto dagSyncer = std::make_shared<GraphsyncDAGSyncer>( ipfsDataStore, graphsync, host );

        // Start DagSyner listener
        auto startResult = dagSyncer->StartSync();
        if ( startResult.has_failure() )
        {
            m_logger->error( "DAG Syncher not listening" );
            return startResult.error();
        }

        m_broadcaster = PubSubBroadcasterExt::New( dagSyncer, m_pubsub );
        if ( m_broadcaster == nullptr )
        {
            m_logger->error( "Unable to create PubSub broadcaster" );
            return Error::PUBSUB_BROADCASTER_NOT_CREATED;
        }
        m_crdtDatastore = CrdtDatastore::New( m_datastore,
                                              HierarchicalKey( "crdt" ),
                                              dagSyncer,
                                              m_broadcaster,
                                              crdtOptions );
        if ( m_crdtDatastore == nullptr )
        {
            m_logger->error( "Unable to create CRDT datastore" );
            return Error::CRDT_DATASTORE_NOT_CREATED;
        }

        return outcome::success();
    }

    void GlobalDB::Start()
    {
        if ( !started_ )
        {
            started_ = true;
            m_crdtDatastore->Start();
            m_broadcaster->Start();
        }
    }

    outcome::result<void> GlobalDB::Put( const HierarchicalKey &key, const Buffer &value, std::set<std::string> topics )
    {
        if ( !started_ )
        {
            m_logger->error( "GlobalDB Not Started" );
            return outcome::failure( Error::GLOBALDB_NOT_STARTED );
        }

        return m_crdtDatastore->PutKey( key, value, std::move( topics ) );
    }

    outcome::result<void> GlobalDB::Put( const std::vector<DataPair> &data_vector, std::set<std::string> topics )
    {
        if ( !started_ )
        {
            m_logger->error( "GlobalDB Not Started" );
            return outcome::failure( Error::GLOBALDB_NOT_STARTED );
        }
        AtomicTransaction batch( m_crdtDatastore );

        for ( auto &data : data_vector )
        {
            BOOST_OUTCOME_TRYV2( auto &&, batch.Put( std::get<0>( data ), std::get<1>( data ) ) );
        }

        return batch.Commit( topics );
    }

    outcome::result<GlobalDB::Buffer> GlobalDB::Get( const HierarchicalKey &key )
    {
        return m_crdtDatastore->GetKey( key );
    }

    outcome::result<void> GlobalDB::Remove( const HierarchicalKey &key, const std::set<std::string> &topics )
    {
        if ( !started_ )
        {
            m_logger->error( "GlobalDB Not Started" );
            return outcome::failure( Error::GLOBALDB_NOT_STARTED );
        }

        return m_crdtDatastore->DeleteKey( key, topics );
    }

    outcome::result<GlobalDB::QueryResult> GlobalDB::QueryKeyValues( const std::string &keyPrefix )
    {
        return m_crdtDatastore->QueryKeyValues( keyPrefix );
    }

    outcome::result<GlobalDB::QueryResult> GlobalDB::QueryKeyValues( const std::string &prefix_base,
                                                                     const std::string &middle_part,
                                                                     const std::string &remainder_prefix )
    {
        if ( !started_ )
        {
            m_logger->error( "GlobalDB Not Started" );
            return outcome::failure( Error::GLOBALDB_NOT_STARTED );
        }

        return m_crdtDatastore->QueryKeyValues( prefix_base, middle_part, remainder_prefix );
    }

    outcome::result<std::string> GlobalDB::KeyToString( const Buffer &key ) const
    {
        // @todo cache the prefix and suffix
        auto keysPrefix  = m_crdtDatastore->GetKeysPrefix();
        auto valueSuffix = m_crdtDatastore->GetValueSuffix();

        auto sKey = std::string( key.toString() );

        if ( auto prefixPos = keysPrefix.empty() ? 0 : sKey.find( keysPrefix, 0 ); prefixPos != 0 )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        size_t keyPos = keysPrefix.size();

        auto suffixPos = valueSuffix.empty() ? sKey.size() : sKey.rfind( valueSuffix, std::string::npos );
        if ( suffixPos == std::string::npos || suffixPos < keyPos )
        {
            return outcome::failure( std::errc::invalid_argument );
        }

        return sKey.substr( keyPos, suffixPos - keyPos );
    }

    void GlobalDB::PrintDataStore()
    {
        m_crdtDatastore->PrintDataStore();
    }

    void GlobalDB::AddTopicName( std::string topicName )
    {
        m_crdtDatastore->AddTopicName( topicName );
    }

    void GlobalDB::SetFullNode( bool full_node )
    {
        m_crdtDatastore->SetFullNode( std::move( full_node ) );
    }

    std::shared_ptr<AtomicTransaction> GlobalDB::BeginTransaction()
    {
        return std::make_shared<AtomicTransaction>( m_crdtDatastore );
    }

    void GlobalDB::AddBroadcastTopic( const std::string &topicName )
    {
        m_broadcaster->AddBroadcastTopic( topicName );
    }

    void GlobalDB::AddListenTopic( const std::string &topicName )
    {
        m_broadcaster->AddListenTopic( topicName );
    }

    bool GlobalDB::RegisterElementFilter( const std::string &pattern, GlobalDBFilterCallback filter )
    {
        return m_crdtDatastore->RegisterElementFilter( pattern, std::move( filter ) );
    }

    std::shared_ptr<GlobalDB::RocksDB> GlobalDB::GetDataStore()
    {
        return m_datastore;
    }

}
