#ifndef SUPERGENIUS_CRDT_GLOBALDB_HPP
#define SUPERGENIUS_CRDT_GLOBALDB_HPP

#include <mutex>
#include <unordered_set>

#include <boost/asio/io_context.hpp>
#include <boost/filesystem/path.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/graphsync_impl.hpp>
#include <ipfs_lite/ipfs/graphsync/impl/local_requests.hpp>
#include <ipfs_pubsub/gossip_pubsub_topic.hpp>
#include <libp2p/protocol/autonat/autonat.hpp>
#include <libp2p/protocol/holepunch/holepunch_client.hpp>
#include <libp2p/protocol/holepunch/holepunch_server.hpp>
#include <libp2p/protocol/identify/identify.hpp>

#include "crdt/atomic_transaction.hpp"
#include "crdt/crdt_datastore.hpp"
#include "crdt/crdt_options.hpp"
#include "outcome/outcome.hpp"
#include "pubsub_broadcaster_ext.hpp"

namespace sgns::crdt
{
    class GlobalDB : public std::enable_shared_from_this<GlobalDB>
    {
    public:
        struct BackupOptions
        {
            bool     enabled{ false };
            uint32_t interval_minutes{ 15 };
            uint32_t keep_count{ 12 };
            bool     auto_restore_on_repair_failure{ true };
        };

        using Buffer             = base::Buffer;
        using QueryResult        = CrdtDatastore::QueryResult;
        using RocksDB            = storage::rocksdb;
        using CRDTHeadListResult = CrdtHeads::CRDTListResult;

        /**
         * @brief       Factory method to create a GlobalDB instance
         * @param[in]   context The io context used to run its inner methods
         * @param[in]   databasePath Local system's path where data will be stored, not used if datastore is not nullptr
         * @param[in]   pubsub The pubsub instance used to communicate
         * @param[in]   crdtOptions CRDT options
         * @param[in]   graphsyncnetwork The graphsync networks used
         * @param[in]   scheduler libp2p scheduler
         * @param[in]   generator The request ID generator from graphsync
         * @param[in]   datastore datastore to be used. If not defined, created using databasePath
         * @param[in]   backup_options configuration for automatic backups of the CRDT data
         * @return      Instance of the GlobalDB initialized or Error
         */
        static outcome::result<std::shared_ptr<GlobalDB>> New(
            std::shared_ptr<boost::asio::io_context>                              context,
            std::string                                                           databasePath,
            std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>                      pubsub,
            std::shared_ptr<CrdtOptions>                                          crdtOptions,
            std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network>            graphsyncnetwork,
            std::shared_ptr<libp2p::basic::Scheduler>                             scheduler,
            std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
            std::shared_ptr<RocksDB>                                              datastore = nullptr,
            BackupOptions                                                         backup_options = BackupOptions{ false, 15, 12, true } );

        /**
         * @brief      Destructor or GlobalDB
         */
        ~GlobalDB();

        /// Pair of key and value to be stored in CRDT
        using DataPair = std::pair<HierarchicalKey, Buffer>;
        /// CRDT Filter callback type
        using GlobalDBFilterCallback         = CrdtDatastore::CRDTElementFilterCallback;
        using GlobalDBNewElementCallback     = CrdtDatastore::CRDTNewElementCallback;
        using GlobalDBDeletedElementCallback = CrdtDatastore::CRDTDeletedElementCallback;

        /**
         * @enum        Error
         * @brief       Enumeration of error codes used in the proof classes.
         */
        enum class Error : uint8_t
        {
            ROCKSDB_IO = 0,                 ///< RocksDB wasn't opened
            IPFS_DB_NOT_CREATED,            ///< IPFS datastore not created
            DAG_SYNCHER_NOT_LISTENING,      ///< DAG Syncher listen error
            CRDT_DATASTORE_NOT_CREATED,     ///< CRDT DataStore not created
            PUBSUB_BROADCASTER_NOT_CREATED, ///< CRDT DataStore not created
            INVALID_PARAMETERS,             ///< Invalid parameters
            GLOBALDB_NOT_STARTED,           ///< Start wasn't called
        };

        /**
         * @brief Puts key-value pair to the CRDT store, optionally specifying a broadcast topic.
         * @param[in] key The hierarchical key where the value should be stored.
         * @param[in] value The value to store.
         * @param[in] topics Topics to publish to.
         * @return outcome::success on success, or outcome::failure otherwise.
         */
        outcome::result<CID> Put( const HierarchicalKey                 &key,
                                  const Buffer                          &value,
                                  const std::unordered_set<std::string> &topics );

        /**
         * @brief       Writes a batch of CRDT data all at once
         * @param[in]   data_vector A set of crdt to be written in a single transaction
         * @param[in]   topics Topics to publish to.
         * @return      outcome::failure on error or success otherwise
         */
        outcome::result<CID> Put( const std::vector<DataPair>           &data_vector,
                                  const std::unordered_set<std::string> &topics );

        /** Gets a value that corresponds to specified key.
        * @param key - value key
        * @return value as a Buffer
        */
        outcome::result<Buffer> Get( const HierarchicalKey &key );

        /** Removes value for a given key.
        * @param key to remove from storage
        * @param topics Topics to publish to
        * @return outcome::failure on error or success otherwise
        */
        outcome::result<CID> Remove( const HierarchicalKey &key, const std::unordered_set<std::string> &topics );

        /** Queries CRDT key-value pairs by prefix. If the prefix is empty returns all elements that were not tombstoned
        * @param keyPrefix - keys prefix to match. An empty prefix matches any key.
        * @return list of key-value pairs matches prefix
        */
        outcome::result<QueryResult> QueryKeyValues( std::string_view keyPrefix );

        /**
         * @brief       Queries with a middle part that can be a wildcard, negated string or normal string
         * @param[in]   prefix_base: The base prefix to query
         * @param[in]   middle_part: Either a string (normal query), '*' or !string
         * @param[in]   remainder_prefix: The remainder part of the query prefix
         * @return      A list of key value pairs
         */
        outcome::result<QueryResult> QueryKeyValues( const std::string &prefix_base,
                                                     const std::string &middle_part,
                                                     const std::string &remainder_prefix );

        /** Converts a unique key part to a string representation
        * @param key - binary key to convert
        * @return string represenation of a unique key part
        */
        outcome::result<std::string> KeyToString( const Buffer &key ) const;

        /** Create a transaction object
        * @return new transaction
        */
        std::shared_ptr<AtomicTransaction> BeginTransaction();

        outcome::result<void> AddBroadcastTopic( const std::string &topicName );
        void                  AddTopicName( const std::string &topicName );
        void                  AddListenTopic( const std::string &topicName );

        void PrintDataStore();

        std::shared_ptr<RocksDB>                          GetDataStore();
        std::shared_ptr<sgns::crdt::PubSubBroadcasterExt> GetBroadcaster();
        std::shared_ptr<CRDTWorkJournal>                  GetWorkJournal() const;

        /** Registers a filter callback for elements matching a pattern.
         * @param pattern The pattern to match elements against.
         * @param filter The callback to invoke for matching elements.
         * @return true if the filter was successfully registered, false otherwise.
         */
        bool RegisterElementFilter( const std::string &pattern, GlobalDBFilterCallback filter );

        /** Registers a callback for new elements matching a pattern.
         * @param pattern The pattern to match new elements against.
         * @param callback The callback to invoke for matching new elements.
         * @return true if the callback was successfully registered, false otherwise.
         */
        bool RegisterNewElementCallback( const std::string &pattern, GlobalDBNewElementCallback callback );

        /** Registers a callback for deleted elements matching a pattern.
         * @param pattern The pattern to match deleted elements against.
         * @param callback The callback to invoke for matching deleted elements.
         * @return true if the callback was successfully registered, false otherwise.
         */
        bool RegisterDeletedElementCallback( const std::string &pattern, GlobalDBDeletedElementCallback callback );

        /** Unregisters the filter callback for a pattern.
         * @param pattern The pattern to unregister the filter for.
         */
        void UnregisterElementFilter( const std::string &pattern );

        /**
         * @brief Unregisters the new element callback for a pattern.
         * @param pattern The pattern to unregister the new element callback for.
         */
        
        void UnregisterNewElementCallback( const std::string &pattern );
        /**
         * @brief Unregisters the deleted element callback for a pattern.
         * @param pattern The pattern to unregister the deleted element callback for.
         */
        void UnregisterDeletedElementCallback( const std::string &pattern );

        /**
         * @brief Starts the GlobalDB instance.
         */
        void Start();

        /**
         * @brief Immediately quiesce and shut down CRDT intake and workers.
         * Safe to call multiple times.
         */
        void ShutdownNow();

        /**
         * @brief Starts receiving CIDs.
         */
        void StartCIDReceiving();

        /**
         * @brief Starts CIC synchronization.
         */
        void StartCICSync();

        /**
         * @brief Starts rebroadcasting heads.
         */
        void StartRebroadcastHeads();

        outcome::result<CRDTHeadListResult> GetCRDTHeadList();

        outcome::result<uint64_t> GetCRDTHeadHeight( const CID &aCid, const std::string &topic );
        outcome::result<void>     CRDTHeadRemove( const CID &aCid, const std::string &topic );
        outcome::result<void>     CRDTHeadAdd( const CID &aCid, const std::string &topic, uint64_t priority );
        outcome::result<crdt::CrdtDatastore::JobStatus> GetCIDJobStatus( const CID &cid ) const;

        /**
         * @brief       Request head broadcast for specified topics
         * @param[in]   topics Vector of topic names to broadcast heads for
         * @return      outcome::success on success, or outcome::failure on error
         */
        outcome::result<void> RequestHeadBroadcast( const std::set<std::string> &topics );

        /**
         * @brief       Get the topics that are being listened to
         * @return      A set of the monitored topic names
         */
        outcome::result<std::unordered_set<std::string>> GetMonitoredTopics() const;

        std::shared_ptr<crdt::CrdtDatastore> GetCRDTDataStore();

        outcome::result<std::vector<std::pair<std::string, base::Buffer>>> GetCIDContent(
            const std::string &cid_string );

    private:
        /**
         * @brief       Constructs a new Global D B object
         * @param[in]   context the io context used to run inner methods
         * @param[in]   databasePath the local path where the files are gonna be stored
         * @param[in]   pubsub the pubsub instance used to communicate
         */
        GlobalDB( std::shared_ptr<boost::asio::io_context>         context,
                  std::string                                      databasePath,
                  std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub> pubsub );

        /**
         * @brief       Initializes the GlobalDB instance by creating the CRDT datastore and broadcaster
         * @param[in]   crdtOptions CRDT options
         * @param[in]   graphsyncnetwork The graphsync network used for DAG sync
         * @param[in]   scheduler libp2p scheduler
         * @param[in]   generator The request ID generator from graphsync
         * @param[in]   datastore datastore to be used. If not defined, created using databasePath
         * @return      outcome::success on success, or outcome::failure on error
         */
        outcome::result<void> Init( std::shared_ptr<CrdtOptions>                               crdtOptions,
                                    std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::Network> graphsyncnetwork,
                                    std::shared_ptr<libp2p::basic::Scheduler>                  scheduler,
                                    std::shared_ptr<sgns::ipfs_lite::ipfs::graphsync::RequestIdGenerator> generator,
                                    std::shared_ptr<RocksDB> datastore = nullptr );

        void scheduleBootstrap( std::shared_ptr<boost::asio::io_context> io_context,
                                std::shared_ptr<libp2p::Host>            host );

        std::shared_ptr<boost::asio::io_context> m_context;
        std::string                              m_databasePath;
        int                                      m_dagSyncPort;
        std::string                              m_graphSyncAddrs;

        std::shared_ptr<sgns::ipfs_pubsub::GossipPubSub>  m_pubsub;
        std::shared_ptr<sgns::crdt::PubSubBroadcasterExt> m_broadcaster;
        std::shared_ptr<RocksDB>                          m_datastore;
        std::atomic_bool                                  started_;
        bool                                              cid_sync_started_;
        bool                                              cid_receiving_started_;
        bool                                              head_broadcasting_started_;
        BackupOptions                                     backup_options_{};
        std::string                                       backup_directory_;
        std::atomic_bool                                  stop_backup_thread_{ false };
        std::atomic_bool                                  shutdown_started_{ false };
        std::thread                                       backup_thread_;
        std::mutex                                        backup_wait_mutex_;
        std::condition_variable                           backup_wait_cv_;

        //std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT> dht_;
        //std::shared_ptr<libp2p::protocol::Identify> identify_;
        //std::shared_ptr<libp2p::protocol::IdentifyMessageProcessor> identifymsgproc_;
        //std::shared_ptr<libp2p::protocol::HolepunchClient> holepunch_;
        //std::shared_ptr<libp2p::protocol::HolepunchClientMsgProc> holepunchmsgproc_;

        int obsAddrRetries = 0;

        std::shared_ptr<CrdtDatastore> m_crdtDatastore;
        mutable std::mutex             lifecycle_mutex_; ///< Guards service pointers during shutdown.

        std::shared_ptr<CrdtDatastore> ActiveCRDTDataStore() const;
        std::shared_ptr<PubSubBroadcasterExt> ActiveBroadcaster() const;

        /** @brief Resolves the backup directory path based on the database path. */
        std::string ResolveBackupDirectory( const std::string &databasePathAbsolute ) const;
        /** @brief Creates a backup immediately. */
        void        CreateBackupNow();
        /** @brief Starts the backup loop in a separate thread. */
        void        StartBackupLoop();
        /** @brief Stops the backup loop and waits for the thread to finish. */
        void        StopBackupLoop();

        sgns::base::Logger m_logger = sgns::base::createLogger( "GlobalDB" );
    };
}

/**
 * @brief       Macro for declaring error handling in the GlobalDB class.
 */
OUTCOME_HPP_DECLARE_ERROR_2( sgns::crdt, GlobalDB::Error );

#endif // SUPERGENIUS_CRDT_GLOBALDB_HPP
