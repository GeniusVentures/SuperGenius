/**
 * @file       crdt_datastore.hpp
 * @brief      CRDT datastore class source file
 * @date       2025-04-04
 * @author     devcareer0
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef SUPERGENIUS_CRDT_DATASTORE_HPP
#define SUPERGENIUS_CRDT_DATASTORE_HPP

#include <shared_mutex>
#include <future>
#include <chrono>
#include <queue>
#include <unordered_set>
#include <map>
#include <condition_variable>
#include <functional>
#include <optional>
#include <thread>

#include <boost/asio/steady_timer.hpp>
#include <ipfs_lite/ipld/ipld_node.hpp>
#include <primitives/cid/cid.hpp>

#include "base/logger.hpp"
#include "crdt/crdt_set.hpp"
#include "crdt/crdt_heads.hpp"
#include "crdt/broadcaster.hpp"
#include "crdt/dagsyncer.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/crdt_data_filter.hpp"
#include "crdt/crdt_callback_manager.hpp"
#include "crdt/globaldb/crdt_work_journal.hpp"
#include "storage/rocksdb/rocksdb.hpp"

namespace sgns
{
    class Blockchain;
    class ValidatorRegistry;
}

namespace sgns::crdt
{
    class CrdtSet; ///< Forward declaration of CRDT Set class
    class CrdtDatastoreReaper;
    class CrdtDatastoreLifetimeObserver;

    /**
     * @brief       CRDT datastore class based on https://github.com/ipfs/go-ds-crdt
     */
    class CrdtDatastore : public std::enable_shared_from_this<CrdtDatastore>
    {
    public:
        using Buffer      = base::Buffer;
        using Logger      = base::Logger;
        using RocksDB     = storage::rocksdb;
        using QueryResult = RocksDB::QueryResult;
        using Delta       = pb::Delta;
        using Element     = pb::Element;
        using IPLDNode    = ipfs_lite::ipld::IPLDNode;

        using CRDTElementFilterCallback  = CRDTDataFilter::ElementFilterCallback;
        using CRDTDeltaFilterCallback    = CRDTDataFilter::DeltaFilterCallback;
        using CRDTNewElementCallback     = CRDTCallbackManager::NewDataCallback;
        using CRDTDeletedElementCallback = CRDTCallbackManager::DeletedDataCallback;

        enum class JobStatus
        {
            PENDING,
            COMPLETED,
            FAILED
        };

        enum class Error
        {
            INVALID_PARAM = 0,
            FETCH_ROOT_NODE,
            NODE_DESERIALIZATION,
            FETCHING_GRAPH,
            NODE_CREATION,
            GET_NODE,
            INVALID_JOB,
        };

        static constexpr std::size_t              kMaxParkedRoots              = 256;
        static constexpr std::size_t              kMaxParkedRootsPerDependency = 32;
        static constexpr std::chrono::minutes     kParkedRootTtl               = std::chrono::minutes( 10 );
        static constexpr std::size_t              kMaxDependencyStallAttempts  = 8;

        struct DependencyRetryStatistics
        {
            uint64_t dependency_roots_parked      = 0;
            uint64_t dependency_retries           = 0;
            uint64_t dependency_deduplicated      = 0;
            uint64_t dependency_evicted_capacity  = 0;
            uint64_t dependency_evicted_ttl       = 0;
            uint64_t dependency_evicted_attempts  = 0;
        };

        struct ShutdownSnapshot
        {
            std::size_t pending_jobs = 0;
            std::size_t self_queue = 0;
            std::size_t root_queue = 0;
            std::size_t pending_roots = 0;
            std::size_t active_roots = 0;
            std::size_t parked_roots = 0;
            std::size_t parked_dependencies = 0;

            bool empty() const
            {
                return pending_jobs == 0 && self_queue == 0 && root_queue == 0 &&
                       pending_roots == 0 && active_roots == 0 &&
                       parked_roots == 0 && parked_dependencies == 0;
            }
        };

        /**
         * @brief       Factory method to create a shared_ptr to a CrdtDatastore
         * @param[in]   aDatastore The underlying database where CRDT is stored
         * @param[in]   aKey The namespace key on the database where CRDT's variables will be stored
         * @param[in]   aDagSyncer The MerkleDAG syncer to request content of CIDs
         * @param[in]   aBroadcaster The broadcaster to publish CIDs
         * @param[in]   aOptions Options to construct the object
         * @return      A new instance of @ref CrdtDatastore
         */
        static std::shared_ptr<CrdtDatastore> New( std::shared_ptr<RocksDB>     aDatastore,
                                                   const HierarchicalKey       &aKey,
                                                   std::shared_ptr<DAGSyncer>   aDagSyncer,
                                                   std::shared_ptr<Broadcaster> aBroadcaster,
                                                   std::shared_ptr<CrdtOptions> aOptions );

        /**
         * @brief       Starts the datastore threads
         */
        void Start();
        void StartCIDProcessing();
        void StartRebroadcastHeads();
        /**
         * @brief      Destructor of the CRDT datastore
         */
        virtual ~CrdtDatastore();

        /** Static function to merge delta elements and tombstones, use highest priority for the result delta
        * @param aDelta1 Delta to merge
        * @param aDelta2 Delta to merge
        * @return pointer to merged delta
        */
        static std::shared_ptr<Delta> DeltaMerge( const std::shared_ptr<Delta> &aDelta1,
                                                  const std::shared_ptr<Delta> &aDelta2 );

        /** Get the value of an element not tombstoned from the CRDT set by key
        * @param aKey Hierarchical key to get
        * @return value as a Buffer
        */
        outcome::result<Buffer> GetKey( const HierarchicalKey &aKey ) const;

        /** Query CRDT set key-value pairs by prefix, if prefix empty return all elements are not tombstoned
        * @param aPrefix prefix to search, if empty string, return all
        * @return list of key-value pairs matches prefix
        */
        outcome::result<QueryResult> QueryKeyValues( std::string_view aPrefix ) const;

        /**
         * @brief       Queries with a middle part that can be a wildcard, negated string or normal string
         * @param[in]   prefix_base: The base prefix to query
         * @param[in]   middle_part: Either a string (normal query), '*' or !string
         * @param[in]   remainder_prefix: The remainder part of the query prefix
         * @return      A list of key value pairs
         */
        outcome::result<QueryResult> QueryKeyValues( const std::string &prefix_base,
                                                     const std::string &middle_part,
                                                     const std::string &remainder_prefix ) const;

        /** Get key prefix used in set, e.g. /namespace/s/k/
        * @return key prefix
        */
        std::string GetKeysPrefix() const;

        /** Get value suffix used in set, e.g. /v
        * @return value suffix
        */
        static std::string GetValueSuffix();

        /**
         * @brief Stores the given value in the CRDT store
         * @param aKey Hierarchical key to put
         * @param aValue Value to be stored
         * @param topics Topics to publish to
         * @return outcome::success if stored and broadcasted successfully, or outcome::failure otherwise.
         */
        outcome::result<CID> PutKey( const HierarchicalKey                 &aKey,
                                     const Buffer                          &aValue,
                                     const std::unordered_set<std::string> &topics );

        /** HasKey returns whether the `key` is mapped to a `value` in set
        * @param aKey HierarchicalKey to look for in set
        * @return true if key found or false if not found or outcome::failure on error
        */
        outcome::result<bool> HasKey( const HierarchicalKey &aKey ) const;

        /** Delete removes the value for given `key`.
        * @param aKey HierarchicalKey to delete from set
        * @param topics Topics to publish to
        * @return outcome::failure on error or success otherwise
        */
        outcome::result<CID> DeleteKey( const HierarchicalKey &aKey, const std::unordered_set<std::string> &topics );

        /**
         * @brief Publishes a Delta.
         * Creates a DAG node from the given Delta, merges it into the CRDT, and broadcasts the node.
         * @param aDelta Delta to publish
         * @param topics Topics to publish to
         * @return returns outcome::success on success or outcome::failure otherwise
         */
        outcome::result<CID> Publish( const std::shared_ptr<Delta>          &aDelta,
                                      const std::unordered_set<std::string> &topics );

        /** PrintDAG pretty prints the current Merkle-DAG using the given printFunc
        * @return returns outcome::success on success or outcome::failure otherwise
        */
        outcome::result<void> PrintDAG();

        /** DecodeBroadcast decodes CRDT broadcast data
        * @param buff Buffer data to decode
        * @return vector of CIDs or outcome::failure on error
        */
        static outcome::result<std::vector<CID>> DecodeBroadcast( const Buffer &buff );

        /** Returns a new delta-set adding the given key/value.
        * @param key - delta key to add to datastore
        * @param value - delta value to add to datastore
        * @return pointer to new delta or outcome::failure on error
        */
        static outcome::result<std::shared_ptr<Delta>> CreateDeltaToAdd( const std::string &key,
                                                                         const std::string &value );

        /** Returns a new delta-set removing the given keys with prefix /namespace/s/key
        * @param key - delta key to remove from datastore
        * @return pointer to delta or outcome::failure on error
        */
        outcome::result<std::shared_ptr<Delta>> CreateDeltaToRemove( const std::string &key ) const;

        void PrintDataStore();

        /** Close shuts down the CRDT datastore and worker threads. It should not be used afterwards.
        */
        void Close();

        /**
         * @brief Immediately cancels CRDT work and closes all worker threads.
         * Safe to call multiple times.
         */
        void CancelAndCloseNow();

        /**
         * @brief Requests close without waiting for the calling worker.
         *
         * Every call returns the same barrier. The barrier becomes ready only
         * after all workers exit and retained state is drained.
         */
        std::shared_future<void> RequestClose();

        bool RegisterElementFilter( const std::string &pattern, CRDTElementFilterCallback filter );
        bool RegisterDeltaFilter( const std::string &pattern, CRDTDeltaFilterCallback filter );
        bool RegisterNewElementCallback( const std::string &pattern, CRDTNewElementCallback callback );
        bool RegisterDeletedElementCallback( const std::string &pattern, CRDTDeletedElementCallback callback );
        void UnregisterElementFilter( const std::string &pattern );
        void UnregisterDeltaFilter( const std::string &pattern );
        void UnregisterNewElementCallback( const std::string &pattern );
        void UnregisterDeletedElementCallback( const std::string &pattern );
        std::shared_ptr<CRDTWorkJournal> GetWorkJournal() const;

        /**
         * @brief Configure which topic this datastore should filter on.
         *
         * When processing or rebroadcasting Merkle-DAG links, only those whose
         * name exactly matches the topic set via this call will be considered.
         *
         * @param[in] topic
         *   The topic name to use when filtering links. Only links whose
         *   `IPLDLinkImpl::getName()` equals this string will be processed.
         */
        void AddTopicName( std::string topic );

        outcome::result<CrdtHeads::CRDTListResult> GetHeadList();
        outcome::result<void>                      RemoveHead( const CID &aCid, const std::string &topic );
        outcome::result<uint64_t>                  GetHeadHeight( const CID &aCid, const std::string &topic );
        outcome::result<void>      AddHead( const CID &aCid, const std::string &topic, uint64_t priority );
        outcome::result<JobStatus> GetJobStatus( const CID &cid );
        DependencyRetryStatistics  GetDependencyRetryStatistics() const;
        std::size_t                 GetParkedRootCount() const;
        std::size_t                 GetParkedRootCountForDependency( const CID &dependency ) const;
        bool                        IsCIDRetainedInCacheForTesting( const CID &cid ) const;
        outcome::result<bool>       IsCIDResolvedForTesting( const CID &cid ) const;
        outcome::result<JobStatus>  GetTrackedJobStatusForTesting( const CID &cid ) const;
        ShutdownSnapshot            GetShutdownSnapshotForTesting() const;
        std::size_t                 GetPendingHeadCountForTesting() const;
        bool                        AreWorkerFuturesCompleteForTesting();

        /**
         * @brief Overrides the monotonic clock used by dependency retries.
         * Intended for deterministic tests; callers must wake the worker after
         * advancing the supplied clock.
         */
        void SetMonotonicClockForTesting(
            std::function<std::chrono::steady_clock::time_point()> clock );
        void WakeDependencyRetryWorkerForTesting();

        /**
         * @brief       Broadcast heads for the specified topics
         * @param[in]   topics Vector of topic names to broadcast heads for
         * @return      outcome::success on success, or outcome::failure on error
         */
        outcome::result<void> BroadcastHeadsForTopics( const std::set<std::string> &topics );

        /**
         * @brief Query whether outgoing head broadcasts are enabled.
         * @return true when broadcasts are enabled.
         */
        bool IsBroadcastEnabled() const;

        std::unordered_set<std::string> GetTopicNames() const;

        outcome::result<std::vector<std::pair<std::string, base::Buffer>>> GetLocalDeltaKeyValues(
            const std::string &cid_string );

    protected:
        friend class PubSubBroadcasterExt;
        friend class ::sgns::Blockchain;
        friend class ::sgns::ValidatorRegistry;

        struct RootCIDJob
        {
            std::shared_ptr<IPLDNode> node_;            ///< Current node to process
            std::shared_ptr<IPLDNode> root_node_;       ///< Root node of the Job
            bool                      created_by_self_; ///< True if the root node was created by self
        };

        struct JobIterationResult
        {
            enum class State
            {
                Completed,
                RetryDependency
            };

            State              state = State::Completed;
            std::optional<CID> dependency_cid;
        };

        /** DAG worker structure to keep track of worker threads
        */
        struct DagWorker
        {
            std::future<void> dagWorkerFuture_;                /*> Future for DAG worker thread */
            std::atomic<bool> dagWorkerThreadRunning_ = false; /*> Flag used for keep track of thread cycle */
            std::thread::id   threadId_;                       /*> Worker thread ID */
        };

        /**
         * @brief      Handles when a CID broadcast gets received
         *             If the CID is not known triggers @ref HandleRootCIDBlock
         */
        void HandleCIDBroadcast();
        /**
         * @brief       Handles a root CID block by creating a job to fetch and process its content
         * @param[in]   aCid The root CID to be handled
         * @return      Success if the Root Job was created, or failure otherwise
         */
        outcome::result<void> HandleRootCIDBlock( const CID &aCid );
        /**
         * @brief       Creates a RootCIDJob for the given root CID
         * @param[in]   aRootCID The root CID to create the job for
         * @return      Success if Root Job created, or failure otherwise
         */
        outcome::result<RootCIDJob> CreateRootJob( const CID &aRootCID );
        /**
         * @brief       Gets the links to fetch for a given node in a job
         * @param[in]   job The root job of the current links to fetch
         * @return      List of CIDs to fetch, or failure otherwise
         */
        outcome::result<std::set<CID>> GetLinksToFetch( const RootCIDJob &job );
        /**
         * @brief       Fetches the nodes for the given links and root job
         * @param[in]   aRootJob The root job of the current links to fetch
         * @param[in]   aLinks The links to fetch
         * @return      Success if the nodes were fetched, or failure otherwise
         */
        outcome::result<void> FetchNodes( const RootCIDJob &aRootJob, const std::set<CID> &aLinks );
        /**
         * @brief       Gets the Delta from a given IPLD node, filtering it if it wasn't created by self
         * @param[in]   aNode The IPLD node to get the Delta from
         * @param[in]   created_by_self True if the node was created by self, false otherwise
         * @return      The Delta contained in the node, or failure otherwise
         */
        outcome::result<FilteredDeltaResult> GetDeltaFromNode( const IPLDNode &aNode, bool created_by_self );
        /**
         * @brief       Merges the data from a given Delta into the CRDT set
         * @param[in]   node_cid The CID of the node from which the Delta was obtained
         * @param[in]   aDelta The Delta to be merged
         * @return      Success if the Delta was merged, or failure otherwise
         */
        outcome::result<void> MergeDataFromDelta( const CID &node_cid, const Delta &aDelta );
        /**
         * @brief       Processes A Root CID job
         * @param[in]   job_to_process The job received by either @ref HandleCIDBroadcast or by @ref AddDAGNode
         * @return      Success if the job was processed, or failure otherwise
         */
        outcome::result<JobIterationResult> ProcessJobIteration( const RootCIDJob &job_to_process );

        /** Sync ensures that all the data under the given prefix is flushed to disk in
        * the underlying datastore
        * @return returns outcome::success on success or outcome::failure otherwise
        */
        outcome::result<void> Sync( const HierarchicalKey &aKey );

        /** Helper funtion to print Merkle-DAG records
        * @param aCID CID of DAG record
        * @param aDepth depth used for indenting printed records
        * @param aSet set of CIDs to print
        * @return returns outcome::success on success or outcome::failure otherwise
        */
        outcome::result<void> PrintDAGRec( const CID &aCID, uint64_t aDepth, std::vector<CID> &aSet );

        /** Regularly send out a list of heads that we have not recently seen
        */
        void RebroadcastHeads();

        /**
         * @brief Broadcasts a set of CIDs.
         * Encodes and broadcasts the provided list of CIDs
         * @param[in] cids The list of CIDs to broadcast.
         * @param[in] topic The topic to broadcast to.
         * @param[in] peerInfo Optional peer info to avoid repeated GetPeerInfo calls.
         * @return outcome::success on success, or outcome::failure if an error occurs.
         */
        outcome::result<void> Broadcast( const std::unordered_set<CID>          &cids,
                                         const std::string                      &topic,
                                         boost::optional<libp2p::peer::PeerInfo> peerInfo = boost::none );

        /** EncodeBroadcast encodes list of CIDs to CRDT broadcast data
        * @param heads list of CIDs
        * @return data encoded into Buffer data or outcome::failure on error
        */
        outcome::result<Buffer> EncodeBroadcast( const std::unordered_set<CID> &heads );

        /** EncodeBroadcastStatic encodes list of CIDs to CRDT broadcast data
        * @param heads list of CIDs
        * @return data encoded into Buffer data or outcome::failure on error
        */
        static outcome::result<Buffer> EncodeBroadcastStatic( const std::set<CID> &heads );

        /** CreateIPLDNode add block node to DAGSyncer
        * @param aHeads list of CIDs to add to node as IPLD links
        * @param aDelta Delta to serialize into IPLD node
        * @param topics Topics to add as links
        * @return IPLD node or outcome::failure on error
        */
        outcome::result<std::shared_ptr<IPLDNode>> CreateIPLDNode(
            const std::vector<std::pair<CID, std::string>> &aHeads,
            const std::shared_ptr<Delta>                   &aDelta,
            const std::unordered_set<std::string>          &topics ) const;

        outcome::result<std::shared_ptr<IPLDNode>> CreateDAGNode( const std::shared_ptr<Delta>          &aDelta,
                                                                  const std::unordered_set<std::string> &topics );
        /** AddDAGNode adds node to DAGSyncer and processes new blocks.
         *  @param node   Node to add and process
         *  @return         CID or outcome::failure on error
         */
        outcome::result<CID> AddDAGNode( const std::shared_ptr<CrdtDatastore::IPLDNode> &node );

        /** SyncDatastore sync heads and set datastore
        * @param aKeyList all heads and the set entries related to the given prefix
        * @return returns outcome::success on success or outcome::failure otherwise
        */
        outcome::result<void> SyncDatastore( const std::vector<HierarchicalKey> &aKeyList );

        void PutElementsCallback( const std::string &key, const Buffer &value, const std::string &cid );
        void DeleteElementsCallback( const std::string &key, const std::string &cid );

        void UpdateCRDTHeads( const CID &rootCID, uint64_t rootPriority, bool add_topics_to_broadcast );
        bool EnqueueRootCID( const CID &cid );

        outcome::result<CID> WaitForJob( const CID &cid );

    private:
        friend class CrdtDatastoreReaper;
        friend class CrdtDatastoreLifetimeObserver;

        struct ShutdownControl;

        struct DeferredCrdtDelete
        {
            std::shared_ptr<ShutdownControl> control;
            std::shared_ptr<void>            reaper_registration;

            void operator()( CrdtDatastore *datastore ) const noexcept;
        };

        CrdtDatastore() = delete;

        CrdtDatastore( std::shared_ptr<RocksDB>     aDatastore,
                       const HierarchicalKey       &aKey,
                       std::shared_ptr<DAGSyncer>   aDagSyncer,
                       std::shared_ptr<Broadcaster> aBroadcaster,
                       std::shared_ptr<CrdtOptions> aOptions,
                       std::shared_ptr<ShutdownControl> shutdownControl );

        bool ShouldContinueWorkerThread( DagWorker &dagWorker );
        std::optional<std::chrono::steady_clock::time_point> GetNextWorkerWakeDeadline();
        void NotifyRuntimeWake();
        bool ProcessJobs( std::queue<RootCIDJob> &jobs );
        bool SeedNextExternalRoot();
        void StopWorkerLoops();
        bool IsCurrentThreadInternalWorker() const;
        ShutdownSnapshot SnapshotShutdownStateLocked() const;
        void CompleteCloseOnReaper();
        void WaitForWorkersToExit();
        bool IsRootCIDPendingOrActive( const CID &cid );
        bool IsRootCIDPendingOrActiveLocked( const CID &cid ) const;
        void HandleJobProcessingFailure( const RootCIDJob &job );
        void HandleJobProcessingSuccess( const RootCIDJob &job );
        void CleanupFailedJob( const RootCIDJob &job );
        void ParkJobForDependency( const RootCIDJob &job, const CID &dependency );
        void RemoveParkedRootAfterSuccess( const CID &root );
        void CleanupParkedRoot( const CID &root, std::string_view reason );
        void CleanupRejectedParkedAdmission( const RootCIDJob &job,
                                             const CID        &dependency,
                                             std::string_view  reason );
        void RefreshRetryGateLocked( std::chrono::steady_clock::time_point now );
        std::optional<CID> OldestDueParkedRootLocked( std::chrono::steady_clock::time_point now ) const;
        std::optional<std::chrono::steady_clock::time_point> EarliestParkedDeadlineLocked() const;
        static std::chrono::seconds DependencyRetryDelay( std::size_t attempts );
        void RemoveQueuedRootJobsLocked( const CID &root );
        void RemovePendingRootLocked( const CID &root );

        std::shared_ptr<RocksDB>     dataStore_ = nullptr;
        std::shared_ptr<CrdtOptions> options_   = nullptr;
        std::shared_ptr<ShutdownControl> shutdownControl_;

        HierarchicalKey namespaceKey_;

        std::shared_ptr<CrdtSet>   set_   = nullptr;
        std::shared_ptr<CrdtHeads> heads_ = nullptr;

        std::shared_ptr<Broadcaster> broadcaster_ = nullptr;
        std::shared_ptr<DAGSyncer>   dagSyncer_   = nullptr;
        Logger                       logger_      = base::createLogger( "CrdtDatastore" );

        static constexpr std::chrono::milliseconds threadSleepTimeInMilliseconds_ = std::chrono::milliseconds( 500 );
        static constexpr std::string_view          headsNamespace_                = "h";
        static constexpr std::string_view          setsNamespace_                 = "s";
        int                                        numberOfDagWorkers             = 1;

        std::future<void> handleNextFuture_;
        std::atomic<bool> handleNextThreadRunning_ = false;

        std::future<void> rebroadcastFuture_;
        std::atomic<bool> rebroadcastThreadRunning_ = false;

        std::vector<std::unique_ptr<DagWorker>> dagWorkers_;
        mutable std::mutex                      workerThreadIdsMutex_;
        std::thread::id                         handleNextThreadId_;
        std::thread::id                         rebroadcastThreadId_;

        std::atomic<bool>       closeStarted_                  = false;
        std::atomic<bool>       dagWorkerJobListThreadRunning_ = false;
        mutable std::mutex      dagWorkerMutex_;
        std::condition_variable dagWorkerCv_;
        std::recursive_mutex    callbackDispatchMutex_;

        std::queue<RootCIDJob>                               rootCIDJobList_;     // External jobs
        std::queue<RootCIDJob>                               selfCreatedJobList_; // Self-created jobs (high priority)
        std::map<CID, std::set<std::pair<CID, std::string>>> pendingHeadsByRootCID_;
        mutable std::mutex                                   pendingHeadsMutex_;
        std::queue<CID>                                      pendingRootQueue_;
        std::optional<CID>                                   activeRootCID_;

        struct ParkedRoot
        {
            RootCIDJob                                  job;
            CID                                         dependency_cid;
            std::chrono::steady_clock::time_point       first_stalled;
            std::chrono::steady_clock::time_point       next_retry_deadline;
            std::size_t                                 attempt_count = 0;
            uint64_t                                    insertion_sequence = 0;
            bool                                        expiry_pending = false;
            bool                                        evaluation_in_progress = false;
        };

        struct RetryFairnessGate
        {
            CID         root_cid;
            std::size_t ordinary_roots_ahead = 0;
        };

        std::map<CID, ParkedRoot>      parkedRoots_;
        std::map<CID, std::set<CID>>  parkedRootsByDependency_;
        std::optional<RetryFairnessGate> retryFairnessGate_;
        uint64_t                        parkedInsertionSequence_ = 0;
        std::function<std::chrono::steady_clock::time_point()> monotonicClock_ =
            [] { return std::chrono::steady_clock::now(); };
        DependencyRetryStatistics dependencyRetryStatistics_;

        std::shared_ptr<CRDTWorkJournal> work_journal_;
        CRDTDataFilter                   crdt_filter_;
        bool                             started_               = false;
        bool                             broadcast_enabled_     = false;
        bool                             root_cid_sync_enabled_ = false;

        std::mutex                      rebroadcastMutex_;
        std::mutex                      dagWorkerCvMutex_;
        std::unordered_set<std::string> topicNames_;
        mutable std::mutex              topicNamesMutex_;
        std::unordered_set<std::string> pendingBroadcastTopics_;

        CRDTCallbackManager crdt_cb_manager_;

        std::map<CID, JobStatus> pending_jobs_;
        bool                     has_full_node_topic_;

        void MarkJobPending( const CID &cid );
        void MarkJobFailed( const CID &cid );

        // Cache for CID string representations to avoid repeated base58 encoding
        mutable std::map<CID, std::string> cid_string_cache_;
        mutable std::mutex                 cid_string_cache_mutex_;
    };

}

/**
 * @brief       Macro for declaring error handling in the CrdtDatastore class.
 */
OUTCOME_HPP_DECLARE_ERROR_2( sgns::crdt, CrdtDatastore::Error );

#endif //SUPERGENIUS_CRDT_DATASTORE_HPP
