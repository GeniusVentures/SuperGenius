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
#include <storage/leveldb/leveldb.hpp>
#include <primitives/cid/cid.hpp>
#include <ipfs_lite/ipld/ipld_node.hpp>


namespace sgns::crdt
{
  /** @brief CrdtDatastore provides a replicated go-datastore (key-value store)
  * implementation using Merkle-CRDTs built with IPLD nodes.
  * 
  * This Datastore is agnostic to how new MerkleDAG roots are broadcasted to
  * the rest of replicas (`Broadcaster` component) and to how the IPLD nodes
  * are made discoverable and retrievable to by other replicas (`DAGSyncer`
  * component).
  *
  * The implementation is based on the "Merkle-CRDTs: Merkle-DAGs meet CRDTs"
  * paper by Héctor Sanjuán, Samuli Pöyhtäri and Pedro Teixeira.
  *
  */
  class CrdtDatastore
  {
  public: 
    using Timer = boost::asio::steady_timer;
    using Duration = Timer::duration;
    using Buffer = base::Buffer;
    using Logger = base::Logger;
    using DataStore = storage::LevelDB;
    using Delta = pb::Delta;
    using Element = pb::Element;
    using Node = ipfs_lite::ipld::IPLDNode;

    using PutHookPtr = std::function<void(const std::string& k, const Buffer& v)>;
    using DeleteHookPtr = std::function<void(const std::string& k)>;

    CrdtDatastore(const std::shared_ptr<DataStore>& aDatastore, const HierarchicalKey& aKey, 
      const std::shared_ptr<DAGSyncer>& aDagSyncer, const std::shared_ptr<Broadcaster>& aBroadcaster,
      const std::shared_ptr<CrdtOptions>& aOptions);

    virtual ~CrdtDatastore() = default;

    static std::shared_ptr<Delta> DeltaMerge(const std::shared_ptr<Delta>& aDelta1, const std::shared_ptr<Delta>& aDelta2);

    outcome::result<Buffer> GetKey(const HierarchicalKey& aKey);

    /** Put stores the object `value` named by `key`.
    */
    outcome::result<void> PutKey(const HierarchicalKey& aKey, const Buffer& aValue);

    /** Delete removes the value for given `key`.
    */
    outcome::result<void> DeleteKey(const HierarchicalKey& aKey);

    /** Has returns whether the `key` is mapped to a `value`.
    */
    outcome::result<bool> HasKey(const HierarchicalKey& aKey);

    /** Returns the size of the `value` named by `key`.
    */
    outcome::result<int> GetValueSize(const HierarchicalKey& aKey);

    /** Sync ensures that all the data under the given prefix is flushed to disk in
    * the underlying datastore
    */
    outcome::result<void> Sync(const HierarchicalKey& aKey);

    /** Close shuts down the CRDT datastore. It should not be used afterwards.
    */
    outcome::result<void> Close();

    /** returns delta size and error
    */
    outcome::result<int> AddToDelta(const HierarchicalKey& aKey, const Buffer& aValue);

    /** returns delta size and error
    */
    outcome::result<int> RemoveFromDelta(const HierarchicalKey& aKey);

    /** to satisfy datastore semantics, we need to remove elements from the current
    * batch if they were added.
    */
    int UpdateDeltaWithRemove(const HierarchicalKey& aKey, const std::shared_ptr<Delta>& aDelta);

    int UpdateDelta(const std::shared_ptr<Delta>& aDelta);

    outcome::result<void> PublishDelta();

    outcome::result<void> Publish(const std::shared_ptr<Delta>& aDelta);

    outcome::result<CID> AddDAGNode(const std::shared_ptr<Delta>& aDelta);

    void HandleNext();

    outcome::result<void> Broadcast(const std::vector<CID>& cids);

    void Rebroadcast();

    outcome::result<std::vector<CID>> DecodeBroadcast(const Buffer& buff);

    outcome::result<Buffer> EncodeBroadcast(const std::vector<CID>& heads);

    /** Regularly send out a list of heads that we have not recently seen
    * 
    */
    void RebroadcastHeads();

    /** handleBlock takes care of vetting, retrieving and applying
    * CRDT blocks to the Datastore.
    */
    outcome::result<void> HandleBlock(const CID& cid);

    void DagWorkerThread();

    void SendJobWorker();

    outcome::result<std::unique_ptr<storage::BufferBatch>> Batch();

    outcome::result<void> PutBatch(const std::unique_ptr<storage::BufferBatch>& aBatchDataStore, const HierarchicalKey& aKey, 
      const Buffer& aValueBuffer);

    outcome::result<void> DeleteBatch(const std::unique_ptr<storage::BufferBatch>&aBatchDataStore, const HierarchicalKey & aKey);

    outcome::result<void> CommitBatch(const std::unique_ptr<storage::BufferBatch>&aBatchDataStore);

    /** PrintDAG pretty prints the current Merkle-DAG using the given printFunc
    */
    outcome::result<void> PrintDAG();

    outcome::result<void> PrintDAGRec(const CID& aCID, const uint64_t& aDepth, const std::vector<CID>& aSet);

    outcome::result<std::shared_ptr<Node>> PutBlock(const std::vector<CID>& aHeads, const uint64_t& aHeight, const std::shared_ptr<Delta>& aDelta);

    outcome::result<std::vector<CID>> ProcessNode(const CID& aRoot, const uint64_t & aRootPrio, const std::shared_ptr<Delta>&aDelta, const std::shared_ptr <Node>& aNode);
    
    // TODO:
    //func (store *Datastore) sendNewJobs(session *sync.WaitGroup, ng *crdtNodeGetter, root cid.Cid, rootPrio uint64, children []cid.Cid) 
    //func (store *Datastore) Query(q query.Query) (query.Results, error) {
 
  private:
    CrdtDatastore() = default; 

    std::shared_ptr<DataStore> dataStore_ = nullptr;
    std::shared_ptr<CrdtOptions> options_ = nullptr;

    HierarchicalKey namespaceKey_;

    std::shared_ptr<CrdtSet> set_ = nullptr;
    std::shared_ptr<CrdtHeads> heads_ = nullptr;

    std::mutex currentDeltaMutex_;
    std::shared_ptr <Delta> currentDelta_ = nullptr;

    std::mutex seenHeadsMutex_;
    std::vector<CID> seenHeads_;

    std::shared_ptr<Broadcaster> broadcaster_ = nullptr;
    std::shared_ptr<DAGSyncer> dagSyncer_ = nullptr;
    Logger logger_;

    //Timer rebroadcastTimer_;

    static const std::string headsNamespace_; // "h" 
    static const std::string setsNamespace_; // "s" 

    PutHookPtr putHookFunc_ = nullptr;
    DeleteHookPtr deleteHookFunc_ = nullptr;

    // TODO: Need to implement these
    //jobQueue chan* dagJob
    //sendJobs chan* dagJob
    // wg sync.WaitGroup
  };

} // namespace sgns::crdt 

#endif SUPERGENIUS_CRDT_DATASTORE_HPP