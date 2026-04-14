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

#include <rocksdb/db.h>

#include <libp2p/multi/multiaddress.hpp>
#include <libp2p/host/host.hpp>
#include <libp2p/injector/host_injector.hpp>
#include <libp2p/basic/scheduler/asio_scheduler_backend.hpp>
#include <libp2p/basic/scheduler/scheduler_impl.hpp>
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

    namespace
    {
        // -----------------------------------------------------------------------
        // NullBroadcaster — no-op Broadcaster for migration-source (read-only) mode.
        // -----------------------------------------------------------------------
        class NullBroadcaster : public Broadcaster
        {
        public:
            outcome::result<void> Broadcast( const base::Buffer &,
                                             std::string,
                                             boost::optional<libp2p::peer::PeerInfo> ) override
            {
                return outcome::success();
            }

            outcome::result<base::Buffer> Next() override
            {
                return outcome::failure( boost::system::error_code{} );
            }

            bool HasTopic( const std::string & ) override
            {
                return false;
            }
        };

        // -----------------------------------------------------------------------
        // NullDagSyncer — no-op DAGSyncer for migration-source (read-only) mode.
        // -----------------------------------------------------------------------
        class NullDagSyncer : public DAGSyncer
        {
            using IPLDNode = ipfs_lite::ipld::IPLDNode;
            using Leaf     = ipfs_lite::ipfs::merkledag::Leaf;

        public:
            outcome::result<bool> HasBlock( const CID & ) const override
            {
                return false;
            }

            outcome::result<void> addNode( std::shared_ptr<const IPLDNode> ) override
            {
                return outcome::success();
            }

            outcome::result<std::shared_ptr<IPLDNode>> getNode( const CID & ) const override
            {
                return outcome::failure( boost::system::error_code{} );
            }

            outcome::result<void> removeNode( const CID & ) override
            {
                return outcome::success();
            }

            outcome::result<size_t> select( gsl::span<const uint8_t>,
                                            gsl::span<const uint8_t>,
                                            std::function<bool( std::shared_ptr<const IPLDNode> )> ) const override
            {
                return 0;
            }

            outcome::result<std::shared_ptr<Leaf>> fetchGraph( const CID & ) const override
            {
                return outcome::failure( boost::system::error_code{} );
            }

            outcome::result<std::shared_ptr<Leaf>> fetchGraphOnDepth( const CID &, uint64_t ) const override
            {
                return outcome::failure( boost::system::error_code{} );
            }

            outcome::result<std::shared_ptr<IPLDNode>> GetNodeWithoutRequest( const CID & ) const override
            {
                return outcome::failure( boost::system::error_code{} );
            }

            std::pair<LinkInfoSet, LinkInfoSet> TraverseCIDsLinks( IPLDNode &,
                                                                   std::string,
                                                                   LinkInfoSet ) const override
            {
                return {};
            }

            outcome::result<void> markResolved( const CID & ) override
            {
                return outcome::success();
            }

            outcome::result<bool> isResolved( const CID & ) const override
            {
                return false;
            }

            void InitCIDBlock( const CID & ) override {}

            bool IsCIDInCache( const CID & ) const override
            {
                return false;
            }

            outcome::result<void> DeleteCIDBlock( const CID & ) override
            {
                return outcome::success();
            }

            void Stop() override {}
        };

    } // anonymous namespace

    outcome::result<std::shared_ptr<GlobalDB>> GlobalDB::New(
        std::shared_ptr<boost::asio::io_context>                              context,
        std::string                                                           databasePath,
        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>                      pubsub,
        std::shared_ptr<CrdtOptions>                                          crdtOptions,
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network>            graphsyncnetwork,
        std::shared_ptr<libp2p::basic::Scheduler>                          scheduler,
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

    outcome::result<std::shared_ptr<GlobalDB>> GlobalDB::NewMigrationSource(
        std::shared_ptr<boost::asio::io_context> context,
        std::string                              databasePath,
        std::shared_ptr<RocksDB>                 datastore )
    {
        if ( !context )
        {
            return outcome::failure( Error::INVALID_PARAMETERS );
        }
        // pubsub is not needed for a migration-source DB; pass nullptr
        auto new_instance = std::shared_ptr<GlobalDB>(
            new GlobalDB( std::move( context ), std::move( databasePath ), nullptr ) );

        BOOST_OUTCOME_TRYV2( auto &&, new_instance->InitMigrationSource( std::move( datastore ) ) );
        return new_instance;
    }

    outcome::result<void> GlobalDB::InitMigrationSource( std::shared_ptr<RocksDB> datastore )
    {
        std::shared_ptr<RocksDB> dataStore = std::move( datastore );
        if ( dataStore == nullptr )
        {
            auto databasePathAbsolute = boost::filesystem::absolute( m_databasePath ).string();

            m_logger->info( "Opening migration-source database " + databasePathAbsolute );
            RocksDB::Options options;
            options.create_if_missing = false; // legacy DB must already exist
            try
            {
                auto dataStoreResult = RocksDB::create( databasePathAbsolute, options );
                if ( !dataStoreResult.has_value() )
                {
                    m_logger->error( "Unable to open migration-source database: {}",
                                     dataStoreResult.error().message() );
                    return outcome::failure( boost::system::error_code{} );
                }
                dataStore = std::move( dataStoreResult.value() );
            }
            catch ( std::exception &e )
            {
                m_logger->error( "Unable to open migration-source database: {}", e.what() );
                return Error::ROCKSDB_IO;
            }
        }
        m_datastore = std::move( dataStore );

        auto ipfsDBResult = IpfsRocksDb::create( m_datastore->getDB() );
        if ( ipfsDBResult.has_error() )
        {
            m_logger->error( "Unable to create IPFS datastore for migration-source database" );
            return Error::IPFS_DB_NOT_CREATED;
        }

        auto nullDagSyncer  = std::make_shared<NullDagSyncer>();
        auto nullBroadcaster = std::make_shared<NullBroadcaster>();

        m_crdtDatastore = CrdtDatastore::New( m_datastore,
                                              HierarchicalKey( "crdt" ),
                                              nullDagSyncer,
                                              nullBroadcaster,
                                              CrdtOptions::DefaultOptions() );
        if ( m_crdtDatastore == nullptr )
        {
            m_logger->error( "Unable to create CRDT datastore for migration-source database" );
            return Error::CRDT_DATASTORE_NOT_CREATED;
        }

        return outcome::success();
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
        if ( m_broadcaster )
        {
            m_broadcaster->Stop();
        }
        if ( m_crdtDatastore )
        {
            m_crdtDatastore->Close();
        }
    }

    outcome::result<void> GlobalDB::Init(
        std::shared_ptr<CrdtOptions>                                          crdtOptions,
        std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network>            graphsyncnetwork,
        std::shared_ptr<libp2p::basic::Scheduler>                             scheduler,
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
                auto dataStoreResult = RocksDB::create( databasePathAbsolute, options );

                // If database open fails with corruption, try to repair it
                if ( !dataStoreResult.has_value() )
                {
                    std::string errorMsg = dataStoreResult.error().message();
                    if ( errorMsg.find( "corruption" ) != std::string::npos ||
                         errorMsg.find( "Corruption" ) != std::string::npos )
                    {
                        m_logger->warn( "Database corruption detected, attempting repair: {}", databasePathAbsolute );
                        auto repairStatus = ::ROCKSDB_NAMESPACE::RepairDB( databasePathAbsolute, options );
                        if ( repairStatus.ok() )
                        {
                            m_logger->info( "Database repair successful, retrying open" );
                            dataStoreResult = RocksDB::create( databasePathAbsolute, options );
                        }
                        else
                        {
                            m_logger->error( "Database repair failed: {}", repairStatus.ToString() );
                        }
                    }
                }

                if ( dataStoreResult.has_value() )
                {
                    dataStore = std::move( dataStoreResult.value() );
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
            m_broadcaster->SetIncomingBroadcastEnabled( incomingBroadcastEnabled_.load() );
            m_broadcaster->Start();
        }
    }

    outcome::result<CID> GlobalDB::Put( const HierarchicalKey &key, const Buffer &value, std::set<std::string> topics )
    {
        if ( !started_ )
        {
            m_logger->error( "{}: GlobalDB Not Started", __func__ );
            return outcome::failure( Error::GLOBALDB_NOT_STARTED );
        }

        return m_crdtDatastore->PutKey( key, value, std::move( topics ) );
    }

    outcome::result<CID> GlobalDB::Put( const std::vector<DataPair> &data_vector, std::set<std::string> topics )
    {
        if ( !started_ )
        {
            m_logger->error( "{}: GlobalDB Not Started", __func__ );
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

    outcome::result<CID> GlobalDB::Remove( const HierarchicalKey &key, const std::set<std::string> &topics )
    {
        if ( !started_ )
        {
            m_logger->error( "{}: GlobalDB Not Started", __func__ );
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
        m_crdtDatastore->AddTopicName( topicName );
    }

    bool GlobalDB::RegisterElementFilter( const std::string &pattern, GlobalDBFilterCallback filter )
    {
        return m_crdtDatastore->RegisterElementFilter( pattern, std::move( filter ) );
    }

    bool GlobalDB::RegisterNewElementCallback( const std::string &pattern, GlobalDBNewElementCallback callback )
    {
        return m_crdtDatastore->RegisterNewElementCallback( pattern, std::move( callback ) );
    }

    bool GlobalDB::RegisterDeletedElementCallback( const std::string &pattern, GlobalDBDeletedElementCallback callback )
    {
        return m_crdtDatastore->RegisterDeletedElementCallback( pattern, std::move( callback ) );
    }

    std::shared_ptr<GlobalDB::RocksDB> GlobalDB::GetDataStore()
    {
        return m_datastore;
    }

    outcome::result<GlobalDB::CRDTHeadListResult> GlobalDB::GetCRDTHeadList()
    {
        return m_crdtDatastore->GetHeadList();
    }

    outcome::result<uint64_t> GlobalDB::GetCRDTHeadHeight( const CID &aCid, const std::string &topic )
    {
        return m_crdtDatastore->GetHeadHeight( aCid, topic );
    }

    outcome::result<void> GlobalDB::CRDTHeadRemove( const CID &aCid, const std::string &topic )
    {
        return m_crdtDatastore->RemoveHead( aCid, topic );
    }

    outcome::result<void> GlobalDB::CRDTHeadAdd( const CID &aCid, const std::string &topic, uint64_t priority )
    {
        return m_crdtDatastore->AddHead( aCid, topic, priority );
    }

    std::shared_ptr<sgns::crdt::PubSubBroadcasterExt> GlobalDB::GetBroadcaster()
    {
        return m_broadcaster;
    }

    outcome::result<crdt::CrdtDatastore::JobStatus> GlobalDB::GetCIDJobStatus( const CID &cid ) const
    {
        if ( !m_crdtDatastore )
        {
            return outcome::failure( Error::CRDT_DATASTORE_NOT_CREATED );
        }
        return m_crdtDatastore->GetJobStatus( cid );
    }

    outcome::result<void> GlobalDB::RequestHeadBroadcast( const std::set<std::string> &topics )
    {
        if ( !m_crdtDatastore )
        {
            m_logger->error( "RequestHeadBroadcast: CRDT datastore not initialized" );
            return outcome::failure( Error::CRDT_DATASTORE_NOT_CREATED );
        }

        if ( !started_.load() )
        {
            m_logger->error( "RequestHeadBroadcast: GlobalDB not started" );
            return outcome::failure( Error::GLOBALDB_NOT_STARTED );
        }

        m_logger->debug( "RequestHeadBroadcast: Forwarding request for {} topics", topics.size() );
        return m_crdtDatastore->BroadcastHeadsForTopics( topics );
    }

    void GlobalDB::SetBroadcastEnabled( bool enabled )
    {
        if ( !m_crdtDatastore )
        {
            m_logger->warn( "SetBroadcastEnabled: CRDT datastore not initialized" );
            return;
        }

        m_crdtDatastore->SetBroadcastEnabled( enabled );
        m_logger->info( "SetBroadcastEnabled: {}", enabled ? "enabled" : "disabled" );
    }

    void GlobalDB::SetIncomingBroadcastEnabled( bool enabled )
    {
        incomingBroadcastEnabled_.store( enabled );

        if ( m_broadcaster )
        {
            m_broadcaster->SetIncomingBroadcastEnabled( enabled );
        }

        m_logger->info( "SetIncomingBroadcastEnabled: {}", enabled ? "enabled" : "disabled" );
    }

    outcome::result<std::set<std::string>> GlobalDB::GetMonitoredTopics() const
    {
        if ( !m_crdtDatastore )
        {
            m_logger->error( "{}: CRDT datastore not initialized", __func__ );
            return outcome::failure( Error::CRDT_DATASTORE_NOT_CREATED );
        }
        m_logger->debug( "{}: Forwarding request", __func__ );
        return m_crdtDatastore->GetTopicNames();
    }
}
