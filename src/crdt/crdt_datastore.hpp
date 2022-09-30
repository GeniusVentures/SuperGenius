#ifndef SUPERGENIUS_CRDT_DATASTORE_HPP
#define SUPERGENIUS_CRDT_DATASTORE_HPP

#include <boost/asio/steady_timer.hpp>
#include <base/logger.hpp>
#include <primitives/cid/cid.hpp>
#include <crdt/crdt_set.hpp>
#include <crdt/crdt_heads.hpp>
#include <crdt/broadcaster.hpp>
#include <crdt/dagsyncer.hpp>
#include <crdt/crdt_options.hpp>
#include <storage/rocksdb/rocksdb.hpp>
#include <primitives/cid/cid.hpp>
#include <ipfs_lite/ipld/ipld_node.hpp>
#include <shared_mutex>
#include <future>
#include <chrono>
#include <queue>

namespace sgns::crdt
{
  class CrdtSet;
  class CrdtDatastore;
  /** @brief CrdtDataStoreTransaction incapsulated transaction functionality
  * of CRDT datastore
  */
  class CrdtDataStoreTransaction
  {
  public:
      using Buffer = base::Buffer;
      using Logger = base::Logger;
      using Delta = pb::Delta;
      using Element = pb::Element;
      //using Node = ipfs_lite::ipld::IPLDNode;

      CrdtDataStoreTransaction() = delete;

      /** Constructor
      * @param datastore pointer to data storage
      */
      CrdtDataStoreTransaction(std::shared_ptr<CrdtDatastore> datastore);

      /** AddToDelta creates delta with key and value pair and merges it into current delta,
      * caller is responsible for calling PublishDelta()
      * @param aKey HierarchicalKey to add to delta
      * @param aValue Buffer value to add to delta
      * @return returns size of current delta or outcome::failure on error
      */
      outcome::result<int> AddToDelta(const HierarchicalKey& aKey, const Buffer& aValue);

      /** RemoveFromDelta creates delta to remove and merges it into current delta as tombstone,
      * caller is responsible for calling PublishDelta()
      * @param aKey HierarchicalKey to remove from delta
      * @return returns size of current delta or outcome::failure on error
      */
      outcome::result<int> RemoveFromDelta(const HierarchicalKey& aKey);

      /** PublishDelta publishes current delta constructed and broadcast it
      * @return returns outcome::success on success or outcome::failure otherwise
      */
      outcome::result<void> PublishDelta();

  private:
      /** UpdateDeltaWithRemove updates current delta with tomstones
      * To satisfy datastore semantics, we need to remove elements from the current
      * batch if they were added.
      * @param aKey HierarchicalKey for delta
      * @param aDelta pointer to delta to merge
      */
      int UpdateDeltaWithRemove(const HierarchicalKey& aKey, const std::shared_ptr<Delta>& aDelta);

      /** UpdateDelta updates current delta by merging input delta
      * @param aDelta pointer to Delta to merge
      * @return the size of current delta just merged
      *
      */
      int UpdateDelta(const std::shared_ptr<Delta>& aDelta);

      std::shared_ptr<CrdtDatastore> datastore_;

      std::mutex currentDeltaMutex_;
      std::shared_ptr<Delta> currentDelta_;
  };

  /** @brief CrdtDatastore provides a replicated go-datastore (key-value store)
  * implementation using Merkle-CRDTs built with IPLD nodes.
  *
  * This Datastore is agnostic to how new MerkleDAG roots are broadcasted to
  * the rest of replicas (`Broadcaster` component) and to how the IPLD nodes
  * are made discoverable and retrievable to by other replicas (`DAGSyncer`
  * component).
  *
  * The implementation is based on the "Merkle-CRDTs: Merkle-DAGs meet CRDTs"
  * paper by H�ctor Sanju�n, Samuli P�yht�ri and Pedro Teixeira.
  *
  */
  class CrdtDatastore : public std::enable_shared_from_this<CrdtDatastore>
  {
  public:
    using Buffer = base::Buffer;
    using Logger = base::Logger;
    using DataStore = storage::rocksdb;
    using QueryResult = DataStore::QueryResult;
    using Delta = pb::Delta;
    using Element = pb::Element;
    using Node = ipfs_lite::ipld::IPLDNode;

    using PutHookPtr = std::function<void(const std::string& k, const Buffer& v)>;
    using DeleteHookPtr = std::function<void(const std::string& k)>;

    /** Constructor
    * @param aDatastore pointer to data storage
    * @param aKey namespace key
    * @param aDagSyncer pointer to MerkleDAG syncer
    * @param aBroadcaster pointer to broacaster
    * @param aOptions option to construct CrdtDatastore
    * \sa CrdtOptions
    *
    */
    CrdtDatastore(const std::shared_ptr<DataStore>& aDatastore, const HierarchicalKey& aKey,
      const std::shared_ptr<DAGSyncer>& aDagSyncer, const std::shared_ptr<Broadcaster>& aBroadcaster,
      const std::shared_ptr<CrdtOptions>& aOptions);

    /** Destructor
    */
    virtual ~CrdtDatastore();

    /** Static function to merge delta elements and tombstones, use highest priority for the result delta
    * @param aDelta1 Delta to merge
    * @param aDelta2 Delta to merge
    * @return pointer to merged delta
    */
    static std::shared_ptr<Delta> DeltaMerge(const std::shared_ptr<Delta>& aDelta1, const std::shared_ptr<Delta>& aDelta2);

    /** Get the value of an element not tombstoned from the CRDT set by key
    * @param aKey Hierarchical key to get
    * @return value as a Buffer
    */
    outcome::result<Buffer> GetKey(const HierarchicalKey& aKey);

    /** Query CRDT set key-value pairs by prefix, if prefix empty return all elements are not tombstoned
    * @param aPrefix prefix to search, if empty string, return all
    * @return list of key-value pairs matches prefix
    */
    outcome::result<QueryResult> QueryKeyValues(const std::string& aPrefix);

    /** Extracts a unique key part
    * @param key - data item key
    * @return string represenation of a unique key part
    */
    outcome::result<std::string> StripKey(const std::string_view& key) const;

    /** Put stores the object `value` named by `key` as delta and broadcast it
    * @param aKey Hierarchical key to put
    * @param aValue value buffer to publish
    * @return outcome::failure on error or success otherwise
    */
    outcome::result<void> PutKey(const HierarchicalKey& aKey, const Buffer& aValue);

    /** HasKey returns whether the `key` is mapped to a `value` in set
    * @param aKey HierarchicalKey to look for in set
    * @return true if key found or false if not found or outcome::failure on error
    */
    outcome::result<bool> HasKey(const HierarchicalKey& aKey);

    /** Delete removes the value for given `key`.
    * @param aKey HierarchicalKey to delete from set
    * @return outcome::failure on error or success otherwise
    */
    outcome::result<void> DeleteKey(const HierarchicalKey& aKey);

    /** Publish publishes delta and broadcast it
    * @param aDelta Delta to publish
    * @return returns outcome::success on success or outcome::failure otherwise
    */
    outcome::result<void> Publish(const std::shared_ptr<Delta>& aDelta);

    /** PrintDAG pretty prints the current Merkle-DAG using the given printFunc
    * @return returns outcome::success on success or outcome::failure otherwise
    */
    outcome::result<void> PrintDAG();

    /** DecodeBroadcast decodes CRDT broadcast data
    * @param buff Buffer data to decode
    * @return vector of CIDs or outcome::failure on error
    */
    outcome::result<std::vector<CID>> DecodeBroadcast(const Buffer& buff);

    /** Create a transaction object
    * @return new transaction
    */
    std::shared_ptr<CrdtDataStoreTransaction> BeginTransaction();

    /** Returns a new delta-set adding the given key/value.
    * @param key - delta key to add to datastore 
    * @param value - delta value to add to datastore 
    * @return pointer to new delta or outcome::failure on error
    */
    outcome::result<std::shared_ptr<Delta>> CreateDeltaToAdd(const std::string& key, const std::string& value);

    /** Returns a new delta-set removing the given keys with prefix /namespace/s/<key>
    * @param key - delta key to remove from datastore 
    * @return pointer to delta or outcome::failure on error
    */
    outcome::result<std::shared_ptr<Delta>> CreateDeltaToRemove(const std::string& key);

  protected:

    /** DAG jobs structure used by DAG worker threads to send new jobs
    */
    struct DagJob
    {
      CID rootCid_; /*> Root CID */
      uint64_t rootPriority_; /*> root priority */
      std::shared_ptr<Delta> delta_; /*> pointer to delta */
      std::shared_ptr<Node> node_; /*> pointer to node */
    };

    /** DAG worker structure to keep track of worker threads
    */
    struct DagWorker
    {
      std::future<void> dagWorkerFuture_; /*> Future for DAG worker thread */
      std::atomic<bool> dagWorkerThreadRunning_ = false; /*> Flag used for keep track of thread cycle */
    };

    /** Worker thread to handle jobs broadcasted from the network.
    * @param aCrdtDatastore pointer to CRDT datastore
    */
    void HandleNext();

    /** Worker thread to rebroadcast heads
    * @param aCrdtDatastore pointer to CRDT datastore
    */
    void Rebroadcast();

    /** Worker thread to send jobs
    * @param aCrdtDatastore pointer to CRDT datastore
    * @param dagWorker pointer to DAG worker structure
    */
    void SendJobWorker(std::shared_ptr<DagWorker> dagWorker);

    /** SendNewJobs calls getDeltas with the given children and sends each response to the workers.
    * @param aRootCID root CID
    * @param aRootPriority root priority
    * @param aChildren vector of children CIDs
    */
    void SendNewJobs(const CID& aRootCID, const uint64_t& aRootPriority, const std::vector<CID>& aChildren);

    /** Sync ensures that all the data under the given prefix is flushed to disk in
    * the underlying datastore
    * TODO: Need to see if needed
    * @return returns outcome::success on success or outcome::failure otherwise
    */
    outcome::result<void> Sync(const HierarchicalKey& aKey);

    /** Helper funtion to print Merkle-DAG records
    * @param aCID CID of DAG record
    * @param aDepth depth used for indenting printed records
    * @param aSet set of CIDs to print
    * @return returns outcome::success on success or outcome::failure otherwise
    */
    outcome::result<void> PrintDAGRec(const CID& aCID, const uint64_t& aDepth, std::vector<CID>& aSet);

    /** Regularly send out a list of heads that we have not recently seen
    */
    void RebroadcastHeads();

    /** Broadcast CIDs
    * @return returns outcome::success on success or outcome::failure otherwise
    */
    outcome::result<void> Broadcast(const std::vector<CID>& cids);

    /** EncodeBroadcast encodes list of CIDs to CRDT broadcast data
    * @param heads list of CIDs
    * @return data encoded into Buffer data or outcome::failure on error
    */
    outcome::result<Buffer> EncodeBroadcast(const std::vector<CID>& heads);

    /** handleBlock takes care of vetting, retrieving and applying
    * CRDT blocks to the Datastore.
    * @return returns outcome::success on success or outcome::failure otherwise
    */
    outcome::result<void> HandleBlock(const CID& aCid);

    /** ProcessNode processes new block. This makes that every operation applied
    * to this store take effect (delta is merged) before returning.
    * @param aRoot Root CID
    * @param aRootPrio Root priority
    * @param aDelta Pointer to Delta
    * @param aNode Pointer to IPLD node
    * @return list of CIDs or outcome::failure on error
    */
    outcome::result<std::vector<CID>> ProcessNode(const CID& aRoot, const uint64_t& aRootPrio, const std::shared_ptr<Delta>& aDelta, const std::shared_ptr<Node>& aNode);

    /** PutBlock add block node to DAGSyncer
    * @param aHeads list of CIDs to add to node as IPLD links
    * @param aHeight priority set to Delta
    * @param aDelta Delta to serialize into IPLD node
    * @return IPLD node or outcome::failure on error
    */
    outcome::result<std::shared_ptr<Node>> PutBlock(
        const std::vector<CID>& aHeads, const uint64_t& aHeight, const std::shared_ptr<Delta>& aDelta);

    /** AddDAGNode adds node to DAGSyncer and processes new blocks.
    * @param aDelta Pointer to Delta used for generating node and process it
    * @return CID or outcome::failure on error
    * \sa PutBlock, ProcessNode
    */
    outcome::result<CID> AddDAGNode(const std::shared_ptr<Delta>& aDelta);

    /** Close shuts down the CRDT datastore and worker threads. It should not be used afterwards.
    */
    void Close();

    /** SyncDatastore sync heads and set datastore
    * TODO: Need to see if needed, not fully implemented
    * @return returns outcome::success on success or outcome::failure otherwise
    */
    outcome::result<void> SyncDatastore(const std::vector<HierarchicalKey>& aKeyList);

  private:
    CrdtDatastore() = default;

    /** Helper function to log Error messages from threads
    */
    void LogError(std::string message);

    /** Helper function to log Info messages from threads
    */
    void LogInfo(std::string message);

    /** Helper function to log Debug messages from threads
    */
    void LogDebug(std::string message);

    std::shared_ptr<DataStore> dataStore_ = nullptr;
    std::shared_ptr<CrdtOptions> options_ = nullptr;

    HierarchicalKey namespaceKey_;

    std::shared_ptr<CrdtSet> set_ = nullptr;
    std::shared_ptr<CrdtHeads> heads_ = nullptr;

    std::shared_mutex seenHeadsMutex_;
    std::vector<CID> seenHeads_;

    std::shared_ptr<Broadcaster> broadcaster_ = nullptr;
    std::shared_ptr<DAGSyncer> dagSyncer_ = nullptr;
    Logger logger_;

    static const std::chrono::milliseconds threadSleepTimeInMilliseconds_;
    static const std::string headsNamespace_; // "h"
    static const std::string setsNamespace_; // "s"

    PutHookPtr putHookFunc_ = nullptr;
    DeleteHookPtr deleteHookFunc_ = nullptr;

    std::future<void> handleNextFuture_;
    std::atomic<bool> handleNextThreadRunning_ = false;

    std::future<void> rebroadcastFuture_;
    std::atomic<bool> rebroadcastThreadRunning_ = false;

    std::vector<std::shared_ptr<DagWorker>> dagWorkers_;
    std::shared_mutex dagWorkerMutex_;
    std::queue<DagJob> dagWorkerJobList;
  };

} // namespace sgns::crdt

#endif //SUPERGENIUS_CRDT_DATASTORE_HPP
