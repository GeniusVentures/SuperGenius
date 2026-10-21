#ifndef SUPERGENIUS_CRDT_GLOBALDB_HPP
#define SUPERGENIUS_CRDT_GLOBALDB_HPP

#include <functional>
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
    class GraphsyncDAGSyncer;

    class GlobalDB
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
            BackupOptions backup_options = BackupOptions{ false, 15, 12, true } );

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
            POST_COMMIT_ERASE_ANOMALY,      ///< Erase already committed; post-commit seal re-check failed
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

        /** Writes an authoritative immutable record that converges by SHA-256 content ordering. */
        outcome::result<CID> PutConvergentImmutable( const HierarchicalKey                 &key,
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

        /**
         * @brief       Puts a key-value pair directly into local storage, bypassing DAG broadcast.
         *              See CrdtDatastore::PutKeyLocal for the full rationale/contract.
         * @param[in]   key The hierarchical key where the value should be stored.
         * @param[in]   value The value to store.
         * @param[in]   id Provenance/tie-break identifier for the local write.
         * @return      outcome::success on success, or outcome::failure otherwise.
         */
        outcome::result<void> PutLocal( const HierarchicalKey &key, const Buffer &value, const std::string &id );

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
        void                  AddTopicName( std::string topicName );
        void                  AddListenTopic( std::string topicName );

        void PrintDataStore();

        std::shared_ptr<RocksDB>                          GetDataStore();

        /**
         * @brief Reads a node-local key straight from RocksDB, bypassing the CRDT layer.
         * @param[in] key Raw key, not a HierarchicalKey; nothing here is replicated.
         * @return The stored value, or `operation_canceled` once shutdown released the store.
         */
        outcome::result<Buffer> GetRaw( const Buffer &key ) const;

        /**
         * @brief Writes a node-local key straight to RocksDB, bypassing the CRDT layer.
         * @param[in] key Raw key, not a HierarchicalKey; nothing here is replicated.
         * @param[in] value Value to store.
         * @return Failure on I/O error, or `operation_canceled` once shutdown released the store.
         */
        outcome::result<void> PutRaw( const Buffer &key, const Buffer &value );

        /**
         * @brief Prefix-scans node-local keys straight from RocksDB, bypassing the CRDT layer.
         * @param[in] key_prefix Raw key prefix.
         * @return Matching key/value pairs, or `operation_canceled` once shutdown released the store.
         */
        outcome::result<QueryResult> QueryRaw( const Buffer &key_prefix ) const;

        /**
         * @brief Generic node-local physical erase: removes, in ONE atomic
         *        RocksDB WriteBatch, the CRDT set entries for @p key (via the
         *        CrdtSet owner), the raw block bytes for @p cid (via the
         *        DAGSyncer owner), and the node-local raw bookkeeping entry at
         *        @p key itself.
         *
         * This is the ONLY entry to physical erasure. Guards run inside the
         * API and reject fail-closed (Error::INVALID_PARAMETERS) BEFORE any
         * staging: a CID that is the head of ANY topic, the root of a
         * pending/active sync job, LRU-cached, or unresolved is not erased.
         *
         * Compose critical section (D-01): the ENTIRE compose -- guard
         * evaluation, staging, the single commit, and the post-commit seal
         * re-assert -- runs inside one owner-vended CrdtSet critical section
         * (CrdtDatastore::MakeComposeLock), the same discipline PutElems
         * already uses across build+commit. The former CHECK-THEN-ACT window
         * (WR-02 -- a concurrent PutElems re-creating the erased entries
         * between the under-lock staging and an outside-the-lock commit) is
         * CLOSED by construction: every CrdtSet-mediated write takes the same
         * recursive mutex, so a concurrent PutElems can never interleave
         * between a guard check and the commit. The hold is scoped to this
         * single per-tx compose (acquired as the first act, released after
         * the commit and the post-commit seal re-assert) and is NEVER held
         * across evaluations or multiple composes -- D-01 bounded holds, as
         * documented on CrdtSet::MakeComposeLock. The seal marker of @p cid
         * is never addressed and is re-asserted after the commit -- a
         * vanished seal fails loudly with Error::POST_COMMIT_ERASE_ANOMALY
         * (a COMMITTED erase -- see @return).
         *
         * Fires NO CRDT hooks and publishes NO tombstones: this is a local
         * prune operation, not a CRDT-logical delete (the network-wide
         * tombstone the CRDT delete path publishes must never fire here).
         *
         * @p key addresses both the CRDT element key and the raw map entry;
         * the caller (TransactionManager::EraseTransactionEntry / the future
         * TxPruner) builds domain keys -- this facade stays generic.
         *
         * @param[in] key CRDT element key == raw bookkeeping key, built by the
         *            domain owner; never concatenated here.
         * @param[in] cid CID whose raw block bytes are erased alongside the
         *            entries for @p key.
         * @return outcome::success on success, or outcome::failure on a guard
         *         rejection, a staging error, or a commit error.
         *         Error::POST_COMMIT_ERASE_ANOMALY reports a COMMITTED erase:
         *         the batch landed, then the post-commit seal re-check
         *         failed. For that value the data IS erased -- callers must
         *         not treat it as "nothing was erased" (guard rejections,
         *         Error::INVALID_PARAMETERS, erase nothing).
         */
        outcome::result<void> EraseEntry( const std::string &key, const CID &cid );

        /**
         * @brief Test-only interruption seam for EraseEntry's compose window.
         * @param hook invoked after each staging step (1 = after CRDT-entry
         *        staging, 2 = after block staging, 3 = after raw-map staging);
         *        a @c true return aborts the erase WITHOUT committing, so the
         *        dying batch proves all-or-nothing. Pass nullptr to clear.
         * @note No production caller may invoke this method.
         */
        static void SetEraseInterruptHookForTest( std::function<bool( int step )> hook );

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

        /** Read a key's per-key CRDT priority through the CRDT datastore
        * (read-only). Contract mirrors CrdtDatastore::GetPriority /
        * CrdtSet::GetPriority: returns the stored priority minus 1, and a key
        * with NO priority record reads as success with value 0 (the NOT_FOUND
        * case is tolerated) -- 0 never implies existence; callers must
        * establish key existence separately.
        * @param key key string
        * @return priority of the key or outcome::failure on error
        */
        outcome::result<uint64_t> GetKeyPriority( const std::string &key ) const;

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

        outcome::result<std::vector<std::pair<std::string, base::Buffer>>> GetLocalDeltaKeyValues(
            const std::string &cid_string );

        /** Enumerate the keys a delta block carries -- elements AND
        * tombstones -- keys-only, through the CRDT datastore (read-only,
        * parse-at-call-time, strictly local: never fetches from the network,
        * unknown CID fails). Elements-only precedent GetLocalDeltaKeyValues
        * is left untouched.
        * @param cid_string CID string of the delta block
        * @return carried keys (element keys then tombstone keys) or outcome::failure
        */
        outcome::result<std::vector<std::string>> GetLocalDeltaKeys( const std::string &cid_string ) const;

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
        /// Concrete DAGSyncer (never the base interface) so EraseEntry can
        /// stage through GraphsyncDAGSyncer::StageBlockRemoval -- test doubles
        /// keep their own implementations untouched.
        std::shared_ptr<GraphsyncDAGSyncer>               m_dagSyncer = nullptr;
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

        /// Test-only EraseEntry interruption hook; see
        /// SetEraseInterruptHookForTest. Inert (nullptr) in production.
        static std::function<bool( int step )> erase_interrupt_hook_for_test_;

        std::shared_ptr<CrdtDatastore> m_crdtDatastore;
        mutable std::mutex             lifecycle_mutex_; ///< Guards service pointers during shutdown.

        std::shared_ptr<RocksDB>       ActiveDataStore() const;

        std::shared_ptr<CrdtDatastore>        ActiveCRDTDataStore() const;
        std::shared_ptr<PubSubBroadcasterExt> ActiveBroadcaster() const;

        /** @brief Resolves the backup directory path based on the database path. */
        std::string ResolveBackupDirectory( const std::string &databasePathAbsolute ) const;
        /** @brief Creates a backup immediately. */
        void CreateBackupNow();
        /** @brief Starts the backup loop in a separate thread. */
        void StartBackupLoop();
        /** @brief Stops the backup loop and waits for the thread to finish. */
        void StopBackupLoop();

        sgns::base::Logger m_logger = sgns::base::createLogger( "GlobalDB" );
    };
}

/**
 * @brief       Macro for declaring error handling in the GlobalDB class.
 */
OUTCOME_HPP_DECLARE_ERROR_2( sgns::crdt, GlobalDB::Error );

#endif // SUPERGENIUS_CRDT_GLOBALDB_HPP
