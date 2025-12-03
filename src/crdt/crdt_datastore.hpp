/**
 * @file       crdt_datastore.hpp
 * @brief      CRDT datastore class source file 
 * @date       2025-04-04
 * @author     devcareer0
 * @author     Henrique A. Klein (hklein@gnus.ai)
 */

#ifndef SUPERGENIUS_CRDT_DATASTORE_HPP
#define SUPERGENIUS_CRDT_DATASTORE_HPP

#include <boost/asio/steady_timer.hpp>
#include "base/logger.hpp"
#include <primitives/cid/cid.hpp>
#include "crdt/crdt_set.hpp"
#include "crdt/crdt_heads.hpp"
#include "crdt/broadcaster.hpp"
#include "crdt/dagsyncer.hpp"
#include "crdt/crdt_options.hpp"
#include "crdt/crdt_data_filter.hpp"
#include "crdt/crdt_callback_manager.hpp"
#include <storage/rocksdb/rocksdb.hpp>
#include <ipfs_lite/ipld/ipld_node.hpp>
#include <shared_mutex>
#include <future>
#include <chrono>
#include <queue>
#include <set>
#include <map>
#include <condition_variable>
#include <optional>

namespace sgns::crdt
{
    class CrdtSet; ///< Forward declaration of CRDT Set class

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
        using CRDTNewElementCallback     = CRDTCallbackManager::NewDataCallback;
        using CRDTDeletedElementCallback = CRDTCallbackManager::DeletedDataCallback;

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
        /**
         * @brief       Factory method to create a shared_ptr to a CrdtDatastore
         * @param[in]   aDatastore The underlying database where CRDT is stored
         * @param[in]   aKey The namespace key on the database where CRDT's variables will be stored
         * @param[in]   aDagSyncer The MerkleDAG syncer to request content of CIDs
         * @param[in]   aBroadcaster The broadcaster to publish CIDs
         * @param[in]   aOptions Options to construct the object
         * @param[in]   elem_filter_cb Filter callback to remove or not an element from a Delta
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
        outcome::result<QueryResult> QueryKeyValues( const std::string &aPrefix ) const;

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
        std::string GetValueSuffix() const;

        /**
         * @brief Stores the given value in the CRDT store
         * @param aKey Hierarchical key to put
         * @param aValue Value to be stored
         * @return outcome::success if stored and broadcasted successfully, or outcome::failure otherwise.
         */
        outcome::result<void> PutKey( const HierarchicalKey       &aKey,
                                      const Buffer                &aValue,
                                      const std::set<std::string> &topics );

        /** HasKey returns whether the `key` is mapped to a `value` in set
        * @param aKey HierarchicalKey to look for in set
        * @return true if key found or false if not found or outcome::failure on error
        */
        outcome::result<bool> HasKey( const HierarchicalKey &aKey ) const;

        /** Delete removes the value for given `key`.
        * @param aKey HierarchicalKey to delete from set
        * @return outcome::failure on error or success otherwise
        */
        outcome::result<void> DeleteKey( const HierarchicalKey &aKey, const std::set<std::string> &topics );

        /**
         * @brief Publishes a Delta.
         * Creates a DAG node from the given Delta, merges it into the CRDT, and broadcasts the node.
         * @param aDelta Delta to publish
         * @return returns outcome::success on success or outcome::failure otherwise
         */
        outcome::result<CID> Publish( const std::shared_ptr<Delta> &aDelta, const std::set<std::string> &topics );

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

        /** Returns a new delta-set removing the given keys with prefix /namespace/s/<key>
        * @param key - delta key to remove from datastore
        * @return pointer to delta or outcome::failure on error
        */
        outcome::result<std::shared_ptr<Delta>> CreateDeltaToRemove( const std::string &key ) const;

        void PrintDataStore();

        /** Close shuts down the CRDT datastore and worker threads. It should not be used afterwards.
        */
        void Close();

        bool RegisterElementFilter( const std::string &pattern, CRDTElementFilterCallback filter );
        bool RegisterNewElementCallback( const std::string &pattern, CRDTNewElementCallback callback );
        bool RegisterDeletedElementCallback( const std::string &pattern, CRDTDeletedElementCallback callback );

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
        void AddTopicName( const std::string &topic )
        {
            topicNames_.emplace( topic );
        }

        void SetFullNode( bool full_node )
        {
            isFullNode = std::move( full_node );
        }

        outcome::result<CrdtHeads::CRDTListResult> GetHeadList();
        outcome::result<void>                      RemoveHead( const CID &aCid, const std::string &topic );
        outcome::result<uint64_t>                  GetHeadHeight( const CID &aCid, const std::string &topic );
        outcome::result<void> AddHead( const CID &aCid, const std::string &topic, uint64_t priority );

    protected:
        struct RootCIDJob
        {
            std::shared_ptr<IPLDNode> node_;            ///< Current node to process
            std::shared_ptr<IPLDNode> root_node_;       ///< Root node of the Job
            bool                      created_by_self_; ///< True if the root node was created by self
        };

        /** DAG worker structure to keep track of worker threads
        */
        struct DagWorker
        {
            std::future<void> dagWorkerFuture_;                /*> Future for DAG worker thread */
            std::atomic<bool> dagWorkerThreadRunning_ = false; /*> Flag used for keep track of thread cycle */
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
        outcome::result<Delta> GetDeltaFromNode( const IPLDNode &aNode, bool created_by_self );
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
        outcome::result<void> ProcessJobIteration( const RootCIDJob &job_to_process );

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
        outcome::result<void> Broadcast( const std::set<CID> &cids, const std::string &topic, boost::optional<libp2p::peer::PeerInfo> peerInfo = boost::none );

        /** EncodeBroadcast encodes list of CIDs to CRDT broadcast data
        * @param heads list of CIDs
        * @return data encoded into Buffer data or outcome::failure on error
        */
        outcome::result<Buffer> EncodeBroadcast( const std::set<CID> &heads );

        /** PutBlock add block node to DAGSyncer
        * @param aHeads list of CIDs to add to node as IPLD links
        * @param aDelta Delta to serialize into IPLD node
        * @return IPLD node or outcome::failure on error
        */
        outcome::result<std::shared_ptr<IPLDNode>> PutBlock( const std::vector<std::pair<CID, std::string>> &aHeads,
                                                             const std::shared_ptr<Delta>                   &aDelta,
                                                             const std::set<std::string> &topics ) const;

        /** AddDAGNode adds node to DAGSyncer and processes new blocks.
         *  @param aDelta   Pointer to Delta used for generating node and process it
         *  @param topics   Vector of topic names; the new block will have one link per topic
         *  @return         CID or outcome::failure on error
         */
        outcome::result<CID> AddDAGNode( const std::shared_ptr<Delta> &aDelta, const std::set<std::string> &topics );

        /** SyncDatastore sync heads and set datastore
        * @param: aKeyList all heads and the set entries related to the given prefix
        * @return returns outcome::success on success or outcome::failure otherwise
        */
        outcome::result<void> SyncDatastore( const std::vector<HierarchicalKey> &aKeyList );

        /**
         * @brief           Filter elements on Delta
         * @param[in,out]   delta: The delta to be merged
         */
        void FilterElementsOnDelta( std::shared_ptr<Delta> &delta );

        /**
         * @brief           Filter tombstones on Delta
         * @param[in,out]   delta: The delta to be merged
         */
        void FilterTombstonesOnDelta( std::shared_ptr<Delta> &delta );

        void PutElementsCallback( const std::string &key, const Buffer &value );
        void DeleteElementsCallback( const std::string &key );

        void UpdateCRDTHeads( const CID &rootCID, uint64_t rootPriority );
        bool EnqueueRootCID( const CID &cid );

        outcome::result<CID> WaitForJob( const CID &cid );

    private:
        CrdtDatastore() = default;

        CrdtDatastore( std::shared_ptr<RocksDB>     aDatastore,
                       const HierarchicalKey       &aKey,
                       std::shared_ptr<DAGSyncer>   aDagSyncer,
                       std::shared_ptr<Broadcaster> aBroadcaster,
                       std::shared_ptr<CrdtOptions> aOptions );

        bool ShouldContinueWorkerThread( const std::shared_ptr<DagWorker> &dagWorker );
        bool ProcessSelfCreatedJobs();
        bool ProcessExternalJobs();
        bool SeedNextExternalRoot();
        bool IsRootCIDPendingOrActive( const CID &cid );
        bool IsRootCIDPendingOrActiveLocked( const CID &cid ) const;
        void HandleJobProcessingFailure( const RootCIDJob &job );
        void HandleJobProcessingSuccess( const RootCIDJob &job );
        void CleanupFailedJob( const RootCIDJob &job );

        std::shared_ptr<RocksDB>     dataStore_ = nullptr;
        std::shared_ptr<CrdtOptions> options_   = nullptr;

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

        std::vector<std::shared_ptr<DagWorker>> dagWorkers_;

        std::atomic<bool>       dagWorkerJobListThreadRunning_ = false;
        std::mutex              dagWorkerMutex_;
        std::condition_variable dagWorkerCv_;

        std::queue<RootCIDJob>                               rootCIDJobList_;     // External jobs
        std::queue<RootCIDJob>                               selfCreatedJobList_; // Self-created jobs (high priority)
        std::map<CID, std::set<std::pair<CID, std::string>>> pendingHeadsByRootCID_;
        std::mutex                                           pendingHeadsMutex_;
        std::queue<CID>                                      pendingRootQueue_;
        std::optional<CID>                                   activeRootCID_;

        CRDTDataFilter crdt_filter_;
        bool           started_ = false;

        std::mutex              rebroadcastMutex_;
        std::mutex              dagWorkerCvMutex_;
        std::condition_variable rebroadcastCv_;
        std::set<std::string>   topicNames_;
        bool                    isFullNode = false;

        CRDTCallbackManager crdt_cb_manager_;

        enum class JobStatus
        {
            PENDING,
            COMPLETED,
            FAILED
        };

        std::map<CID, JobStatus> pending_jobs_;

        // Cache for CID string representations to avoid repeated base58 encoding
        mutable std::map<CID, std::string> cid_string_cache_;
        mutable std::mutex                  cid_string_cache_mutex_;
    };

}

/**
 * @brief       Macro for declaring error handling in the CrdtDatastore class.
 */
OUTCOME_HPP_DECLARE_ERROR_2( sgns::crdt, CrdtDatastore::Error );

#endif //SUPERGENIUS_CRDT_DATASTORE_HPP
