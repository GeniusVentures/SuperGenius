#include <crdt/crdt_datastore.hpp>
#include <storage/leveldb/leveldb.hpp>
#include <iostream>
#include <proto/bcast.pb.h>
#include <google/protobuf/unknown_field_set.h>
#include <ipfs_lite/ipld/impl/ipld_node_impl.hpp>

namespace sgns::crdt
{

#define LOG_INFO(msg) \
  if (this->logger_ != nullptr) \
  { \
    std::ostringstream msgStream; \
    msgStream << msg << std::ends; \
    this->logger_->info(msgStream.str()); \
  } 
#define LOG_ERROR(msg) \
  if (this->logger_ != nullptr) \
  { \
    std::ostringstream msgStream; \
    msgStream << msg << std::ends; \
    this->logger_->error(msgStream.str()); \
  } 

  using CRDTBroadcast = pb::CRDTBroadcast;

  const std::string CrdtDatastore::headsNamespace_ = "h";
  const std::string CrdtDatastore::setsNamespace_ = "s";

  CrdtDatastore::CrdtDatastore(const std::shared_ptr<DataStore>& aDatastore, const HierarchicalKey& aKey,
    const std::shared_ptr<DAGSyncer>& aDagSyncer, const std::shared_ptr<Broadcaster>& aBroadcaster,
    const std::shared_ptr<CrdtOptions>& aOptions)
  {
    // <namespace>/s
    auto fullSetNs = aKey.ChildString(setsNamespace_);
    // <namespace>/h
    auto fullHeadsNs = aKey.ChildString(headsNamespace_);

    if (aOptions != nullptr && !aOptions->Verify().has_failure() &&
      aOptions->Verify().value() == CrdtOptions::VerifyErrorCode::Success)
    {
      this->options_ = aOptions;
      this->putHookFunc_ = options_->putHookFunc;
      this->deleteHookFunc_ = options_->deleteHookFunc;
      this->logger_ = options_->logger;
    }

    this->dataStore_ = aDatastore;

    this->dagSyncer_ = aDagSyncer;
    this->broadcaster_ = aBroadcaster;

    this->set_ = std::make_shared<CrdtSet>(CrdtSet(aDatastore, fullSetNs, this->putHookFunc_, this->deleteHookFunc_));
    this->heads_ = std::make_shared<CrdtHeads>(CrdtHeads(aDatastore, fullHeadsNs));

    int numberOfHeads = 0;
    uint64_t maxHeight = 0;
    if (this->heads_ != nullptr)
    {
      std::vector<CID> heads;
      auto getListResult = this->heads_->GetList(heads, maxHeight);
      if (!getListResult.has_failure())
      {
        numberOfHeads = heads.size();
      }
    }

    LOG_INFO("crdt Datastore created. Number of heads: " << numberOfHeads << " Current max-height: " << maxHeight);

    /*
  // sendJobWorker + NumWorkers
  dstore.wg.Add(1 + dstore.opts.NumWorkers)
  go func() {
    defer dstore.wg.Done()
    dstore.sendJobWorker()
  }()
  for i := 0; i < dstore.opts.NumWorkers; i++ {
    go func() {
      defer dstore.wg.Done()
      dstore.dagWorker()
    }()
  }
  dstore.wg.Add(2)
  go func() {
    defer dstore.wg.Done()
    dstore.handleNext()
  }()
  go func() {
    defer dstore.wg.Done()
    dstore.rebroadcast()
  }()
  */

  }

  //static 
  std::shared_ptr<CrdtDatastore::Delta> CrdtDatastore::DeltaMerge(const std::shared_ptr<Delta>& aDelta1, const std::shared_ptr<Delta>& aDelta2)
  {
    auto result = std::make_shared<CrdtDatastore::Delta>();
    if (aDelta1 != nullptr)
    {
      for (const auto& elem : aDelta1->elements())
      {
        auto newElement = result->add_elements();
        newElement->CopyFrom(elem);
      }
      for (const auto& tomb : aDelta1->tombstones())
      {
        auto newTomb = result->add_tombstones();
        newTomb->CopyFrom(tomb);
      }
      result->set_priority(aDelta1->priority());
    }
    if (aDelta2 != nullptr)
    {
      for (const auto& elem : aDelta2->elements())
      {
        auto newElement = result->add_elements();
        newElement->CopyFrom(elem);
      }
      for (const auto& tomb : aDelta2->tombstones())
      {
        auto newTomb = result->add_tombstones();
        newTomb->CopyFrom(tomb);
      }
      auto d2Priority = aDelta2->priority();
      if (d2Priority > result->priority())
      {
        result->set_priority(d2Priority);
      }
    }
    return result;
  }

  void CrdtDatastore::HandleNext()
  {
    if (this->broadcaster_ == nullptr)
    {
      // offline
      return;
    }

    //TODO: revisit this 
    // until store.ctx.Done():
    while (1)
    {
      auto broadcasterNextResult = this->broadcaster_->Next();
      if (broadcasterNextResult.has_failure())
      {
        if (broadcasterNextResult.error().value() == (int)Broadcaster::ErrorCode::ErrNoMoreBroadcast)
        {
          LOG_ERROR("Broadcaster: No more data to broadcast");
          return;
        }
        else
        {
          LOG_ERROR("Failed to get next broadcaster (error code " << broadcasterNextResult.error().value() << ")");
        }
        continue;
      }

      auto decodeResult = this->DecodeBroadcast(broadcasterNextResult.value());
      if (decodeResult.has_failure())
      {
        LOG_ERROR("Broadcaster: Unable to decode broadcast (error code " << broadcasterNextResult.error().value() << ")");
        continue;
      }

      // For each head, we process it.
      for (const auto& bCastHeadCID : decodeResult.value())
      {
        auto handleBlockResult = this->HandleBlock(bCastHeadCID);
        if (handleBlockResult.has_failure())
        {
          LOG_ERROR("Broadcaster: Unable to handle block (error code " << handleBlockResult.error().value() << ")");
          continue;
        }
        std::lock_guard lg(this->seenHeadsMutex_);
        this->seenHeads_.push_back(bCastHeadCID);
      }

      // TODO: We should store trusted-peer signatures associated to
      // each head in a timecache. When we broadcast, attach the
      // signatures (along with our own) to the broadcast.
      // Other peers can use the signatures to verify that the
      // received CIDs have been issued by a trusted peer.
    }
  }

  outcome::result<std::vector<CID>> CrdtDatastore::DecodeBroadcast(const Buffer& buff)
  {
    // Make a list of heads we received
    CRDTBroadcast bcastData;
    bcastData.MergeFromString(buff.toString().data());

    // Compatibility: before we were publishing CIDs directly
    auto msgReflect = bcastData.GetReflection();

    if (msgReflect->GetUnknownFields(bcastData).empty())
    {
      // Backwards compatibility
      //c, err := cid.Cast(msgReflect.GetUnknown())
      //sgns::common::getCidOf(msgReflect->MutableUnknownFields(&bcastData)->);
      return outcome::failure(boost::system::error_code{});
    }

    std::vector<CID> bCastHeads;
    for (const auto& head : bcastData.heads())
    {
      auto cidResult = CID::fromString(head.cid());
      if (cidResult.has_failure())
      {
        LOG_ERROR("DecodeBroadcast: Failed to convert CID from string (error code " << cidResult.error().value() << ")")
      }
      bCastHeads.push_back(cidResult.value());
    }
    return bCastHeads;
  }

  outcome::result<CrdtDatastore::Buffer> CrdtDatastore::EncodeBroadcast(const std::vector<CID>& heads)
  {
    CRDTBroadcast bcastData;
    for (const auto& head : heads)
    {
      auto encodedHead = bcastData.add_heads();
      auto strHeadResult = head.toString();
      if (strHeadResult.has_failure())
      {
        encodedHead->set_cid(strHeadResult.value());
      }
    }

    Buffer outputBuffer;
    outputBuffer.put(bcastData.SerializeAsString());
    return outputBuffer;
  }

  void CrdtDatastore::Rebroadcast()
  {
    // TODO: use timer
    //rebroadcastTimer_
    while (1)
    {
      this->RebroadcastHeads();
    }
  }

  void CrdtDatastore::RebroadcastHeads()
  {
    uint64_t maxHeight = 0;
    std::vector<CID> heads;
    if (this->heads_ != nullptr)
    {
      auto getListResult = this->heads_->GetList(heads, maxHeight);
      if (getListResult.has_failure())
      {
        LOG_ERROR("RebroadcastHeads: Failed to get list of heads (error code " << getListResult.error() << ")");
        return;
      }
    }

    std::vector<CID> headsToBradcast;
    {
      std::lock_guard lg(this->seenHeadsMutex_);
      for (const auto& head : heads)
      {
        if (std::find(seenHeads_.begin(), seenHeads_.end(), head) == seenHeads_.end())
        {
          headsToBradcast.push_back(head);
        }
      }
    }

    auto broadcastResult = this->Broadcast(headsToBradcast);
    if (broadcastResult.has_failure())
    {
      LOG_ERROR("Broadcast failed");
    }

    // Reset the map
    std::lock_guard lg(this->seenHeadsMutex_);
    this->seenHeads_.clear();
  }

  outcome::result<void> CrdtDatastore::HandleBlock(const CID& cid)
  {
    // Ignore already known blocks.
    // This includes the case when the block is a current
    // head.

    //TODO: need to implement it  

    return outcome::failure(boost::system::error_code{});
  }

  outcome::result<CrdtDatastore::Buffer> CrdtDatastore::GetKey(const HierarchicalKey& aKey)
  {
    if (this->set_ == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }
    return this->set_->GetElement(aKey.GetKey());
  }

  outcome::result<bool> CrdtDatastore::HasKey(const HierarchicalKey& aKey)
  {
    if (this->set_ == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }
    return this->set_->IsValueInSet(aKey.GetKey());
  }

  outcome::result<int> CrdtDatastore::GetValueSize(const HierarchicalKey& aKey)
  {
    if (this->dataStore_ == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }

    Buffer keyBuffer;
    keyBuffer.put(aKey.GetKey());
    auto valueResult = this->dataStore_->get(keyBuffer);
    if (valueResult.has_failure())
    {
      return outcome::failure(valueResult.error());
    }

    return static_cast<int>(valueResult.value().size());
  }

  outcome::result<void> CrdtDatastore::PutKey(const HierarchicalKey& aKey, const Buffer& aValue)
  {
    if (this->set_ == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }

    auto deltaResult = this->set_->CreateDeltaToAdd(aKey.GetKey(), aValue.toString().data());
    if (deltaResult.has_failure())
    {
      return outcome::failure(deltaResult.error());
    }
    return this->Publish(deltaResult.value());
  }

  outcome::result<void> CrdtDatastore::PutBatch(const std::unique_ptr<storage::BufferBatch>& aBatchDataStore, const HierarchicalKey& aKey,
    const Buffer& aValueBuffer)
  {
    if (this->options_ == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }

    auto addToDeltaResult = this->AddToDelta(aKey, aValueBuffer);
    if (addToDeltaResult.has_failure())
    {
      return outcome::failure(addToDeltaResult.error());
    }
    if (addToDeltaResult.value() > this->options_->maxBatchDeltaSize)
    {
      LOG_INFO("Delta size over MaxBatchDeltaSize. Commiting.");
      return this->CommitBatch(aBatchDataStore);
    }

    return outcome::success();
  }

  outcome::result<void> CrdtDatastore::DeleteKey(const HierarchicalKey& aKey)
  {
    if (this->set_ == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }

    auto deltaResult = this->set_->CreateDeltaToRemove(aKey.GetKey());
    if (deltaResult.has_failure())
    {
      return outcome::failure(deltaResult.error());
    }

    if (deltaResult.value()->tombstones().empty())
    {
      return outcome::success();
    }

    return this->Publish(deltaResult.value());
  }

  outcome::result<void> CrdtDatastore::DeleteBatch(const std::unique_ptr<storage::BufferBatch>& aBatchDataStore, const HierarchicalKey& aKey)
  {
    if (this->options_ == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }

    auto removeResult = this->RemoveFromDelta(aKey);
    if (removeResult.has_failure())
    {
      return outcome::failure(removeResult.error());
    }

    if (removeResult.value() > this->options_->maxBatchDeltaSize)
    {
      LOG_INFO("Delta size over MaxBatchDeltaSize. Commiting.");
      return this->CommitBatch(aBatchDataStore);
    }

    return outcome::success();
  }

  outcome::result<void> CrdtDatastore::CommitBatch(const std::unique_ptr<storage::BufferBatch>& aBatchDataStore)
  {
    return this->PublishDelta();
  }

  outcome::result<int> CrdtDatastore::AddToDelta(const HierarchicalKey& aKey, const Buffer& aValue)
  {
    if (this->set_ == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }

    auto deltaResult = this->set_->CreateDeltaToAdd(aKey.GetKey(), aValue.toString().data());
    if (deltaResult.has_failure())
    {
      return outcome::failure(deltaResult.error());
    }

    return this->UpdateDelta(deltaResult.value());
  }

  outcome::result<int> CrdtDatastore::RemoveFromDelta(const HierarchicalKey& aKey)
  {
    if (this->set_ == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }

    auto deltaResult = this->set_->CreateDeltaToRemove(aKey.GetKey());
    if (deltaResult.has_failure())
    {
      return outcome::failure(deltaResult.error());
    }
    return this->UpdateDeltaWithRemove(aKey, deltaResult.value());
  }

  int CrdtDatastore::UpdateDeltaWithRemove(const HierarchicalKey& aKey, const std::shared_ptr<Delta>& aDelta)
  {
    if (this->currentDelta_ == nullptr)
    {
      return 0;
    }

    int deltaSize = 0;
    std::vector<Element> elems;
    std::lock_guard lg(this->currentDeltaMutex_);
    for (const auto& elem : this->currentDelta_->elements())
    {
      if (elem.key() != aKey.GetKey())
      {
        elems.push_back(elem);
      }
    }

    auto tombs = this->currentDelta_->tombstones();
    auto priority = this->currentDelta_->priority();

    std::shared_ptr<Delta> deltaToMerge = std::make_shared<Delta>();
    for (const auto& elem : elems)
    {
      auto newElem = deltaToMerge->add_elements();
      newElem->CopyFrom(elem);
    }
    for (const auto& tomb : tombs)
    {
      auto newTomb = deltaToMerge->add_tombstones();
      newTomb->CopyFrom(tomb);
    }
    deltaToMerge->set_priority(priority);

    this->currentDelta_ = DeltaMerge(deltaToMerge, aDelta);
    deltaSize = this->currentDelta_->ByteSizeLong();
    return deltaSize;
  }

  int CrdtDatastore::UpdateDelta(const std::shared_ptr<Delta>& aDelta)
  {
    if (this->currentDelta_ == nullptr)
    {
      return 0;
    }

    int deltaSize = 0;
    std::lock_guard lg(this->currentDeltaMutex_);
    this->currentDelta_ = DeltaMerge(this->currentDelta_, aDelta);
    deltaSize = this->currentDelta_->ByteSizeLong();
    return deltaSize;
  }

  outcome::result<void> CrdtDatastore::PublishDelta()
  {
    std::lock_guard lg(this->currentDeltaMutex_);
    auto publishResult = this->Publish(this->currentDelta_);
    if (publishResult.has_failure())
    {
      return outcome::failure(publishResult.error());
    }
    this->currentDelta_ = nullptr;
    return outcome::success();
  }

  outcome::result<void> CrdtDatastore::Publish(const std::shared_ptr<Delta>& aDelta)
  {
    auto addResult = this->AddDAGNode(aDelta);
    if (addResult.has_failure())
    {
      return outcome::failure(addResult.error());
    }
    std::vector<CID> cids;
    cids.push_back(addResult.value());
    return this->Broadcast(cids);
  }

  outcome::result<void> CrdtDatastore::Broadcast(const std::vector<CID>& cids)
  {
    if (this->broadcaster_ == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }

    auto encodedBufferResult = this->EncodeBroadcast(cids);
    if (encodedBufferResult.has_failure())
    {
      return outcome::failure(encodedBufferResult.error());
    }

    auto bcastResult = this->broadcaster_->Broadcast(encodedBufferResult.value());
    if (bcastResult.has_failure())
    {
      return outcome::failure(bcastResult.error());
    }

    return outcome::success();
  }

  outcome::result<std::unique_ptr<storage::BufferBatch>> CrdtDatastore::Batch()
  {
    if (this->dataStore_ == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }
    return this->dataStore_->batch();
  }

  outcome::result<std::shared_ptr<CrdtDatastore::Node>> CrdtDatastore::PutBlock(const std::vector<CID>& aHeads, const uint64_t& aHeight, const std::shared_ptr<Delta>& aDelta)
  {
    if (aDelta == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }

    aDelta->set_priority(aHeight);
    Buffer deltaBuffer;
    deltaBuffer.put(aDelta->SerializeAsString());

    auto nodeResult = ipfs_lite::ipld::IPLDNodeImpl::createFromRawBytes(deltaBuffer);
    if (nodeResult.has_failure())
    {
      return outcome::failure(nodeResult.error());
    }

    auto node = nodeResult.value();
    for (const auto& head : aHeads)
    {
      auto cidByte = head.toBytes();
      if (cidByte.has_failure())
      {
        continue;
      }
      ipfs_lite::ipld::IPLDLinkImpl link = ipfs_lite::ipld::IPLDLinkImpl(head, "", cidByte.value().size());
      node->addLink(link);
    }

    // TODO: DAGSyncerTimeout?
    //ctx, cancel := context.WithTimeout(store.ctx, store.opts.DAGSyncerTimeout)
    //DAGSyncerTimeout specifies how long to wait for a DAGSyncer
    if (this->dagSyncer_ != nullptr)
    {
      auto dagSyncerResult = this->dagSyncer_->addNode(node);
      if (dagSyncerResult.has_failure())
      {
        LOG_ERROR("DAGSyncer: error writing new block " << node->getCID().toString())
          return outcome::failure(dagSyncerResult.error());
      }
    }

    return node;
  }

  outcome::result<std::vector<CID>> CrdtDatastore::ProcessNode(const CID& aRoot, const uint64_t& aRootPrio, 
    const std::shared_ptr<Delta>& aDelta, const std::shared_ptr<Node>& aNode)
  {
    if (this->set_ == nullptr || this->heads_ == nullptr || this->dagSyncer_ == nullptr || 
      aDelta == nullptr || aNode == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }

    // merge the delta
    auto current = aNode->getCID();
    auto strCidResult = current.toString();
    if (strCidResult.has_failure())
    {
      return outcome::failure(strCidResult.error());
    }
    HierarchicalKey hKey(strCidResult.value());
    auto mergeResult = this->set_->Merge(aDelta, hKey.GetKey());
    if (mergeResult.has_failure())
    {
      LOG_ERROR("ProcessNode: error merging delta from " << hKey.GetKey());
      return outcome::failure(mergeResult.error());
    }

    auto priority = aDelta->priority();
    if (priority % 10 == 0)
    {
      LOG_INFO("ProcessNode: merged delta from " << strCidResult.value() << " (priority: " << priority << ")");
    }
 
    std::vector<CID> children;
    auto links = aNode->getLinks();
    if (links.empty())
    {
      // we reached the bottom, we are a leaf.
      auto addHeadResult = this->heads_->Add(aRoot, aRootPrio);
      if (addHeadResult.has_failure())
      {
        LOG_ERROR("ProcessNode: error adding head " << aRoot.toString().value());
        return outcome::failure(addHeadResult.error());
      }
      return outcome::success(children);
    }

    // walkToChildren
    for (const auto& link : links)
    {
      auto child = link.get().getCID();
      auto isHeadResult = this->heads_->IsHead(child);
      if (isHeadResult.has_failure())
      {
        LOG_ERROR("ProcessNode: error checking if " << child.toString().value() << " is head");
        return outcome::failure(isHeadResult.error());
      }

      if (isHeadResult.value() == true)
      {
        // reached one of the current heads.Replace it with
        // the tip of this branch
        auto replaceResult = this->heads_->Replace(child, aRoot, aRootPrio);
        if (replaceResult.has_failure())
        {
          LOG_ERROR("ProcessNode: error replacing head " << child.toString().value() << " -> " << aRoot.toString().value());
          return outcome::failure(replaceResult.error());
        }

        continue;
      }

      auto knowBlockResult = this->dagSyncer_->HasBlock(child);
      if (knowBlockResult.has_failure())
      {
        LOG_ERROR("ProcessNode: error checking for known block " << child.toString().value());
        return outcome::failure(knowBlockResult.error());
      }

      if (knowBlockResult.value() == true)
      {
        // we reached a non-head node in the known tree.
        // This means our root block is a new head.
        auto addHeadResult = this->heads_->Add(aRoot, aRootPrio);
        if (addHeadResult.has_failure())
        {
          // Don't let this failure prevent us from processing the other links.
          LOG_ERROR("ProcessNode: error adding head " << aRoot.toString().value());
        }
        continue;
      }

      children.push_back(child);
    }

    return children;
  }


  outcome::result<CID> CrdtDatastore::AddDAGNode(const std::shared_ptr<Delta>& aDelta)
  {
    if (this->heads_ == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }

    uint64_t height = 0;
    std::vector<CID> heads;
    auto getListResult = this->heads_->GetList(heads, height);
    if (getListResult.has_failure())
    {
      return outcome::failure(getListResult.error());
    }

    height = height + 1; // This implies our minimum height is 1  
    aDelta->set_priority(height);

    auto putBlockResult = this->PutBlock(heads, height, aDelta);
    if (putBlockResult.has_failure())
    {
      return outcome::failure(putBlockResult.error());
    }

    auto node = putBlockResult.value();

    // Process new block. This makes that every operation applied
    // to this store take effect (delta is merged) before
    // returning. Since our block references current heads, children
    // should be empty
    LOG_INFO("AddDAGNode: Processing generated block " << node->getCID().toString().value());
    
    auto processNodeResult = this->ProcessNode(node->getCID(), height, aDelta, node);
    if (processNodeResult.has_failure())
    {
      LOG_ERROR("AddDAGNode: error processing new block");
      return outcome::failure(processNodeResult.error());
    }

    if (!processNodeResult.value().empty())
    {
      LOG_ERROR("AddDAGNode: bug - created a block to unknown children");
    }

    return node->getCID();
  }

  outcome::result<void> CrdtDatastore::PrintDAG()
  {
    if (this->heads_ == nullptr)
    {
      return outcome::failure(boost::system::error_code{});
    }

    uint64_t height = 0;
    std::vector<CID> heads;
    auto getListResult = this->heads_->GetList(heads, height);
    if (getListResult.has_failure())
    {
      return outcome::failure(getListResult.error());
    }

    std::vector<CID> set;
    for (const auto& head : heads)
    {
      auto printResult = this->PrintDAGRec(head, 0, set);
      if (printResult.has_failure())
      {
        return outcome::failure(printResult.error());
      }
    }
    return outcome::success();
  }

  outcome::result<void> CrdtDatastore::PrintDAGRec(const CID& aCID, const uint64_t& aDepth, const std::vector<CID>& aSet)
  {
    std::string line; 
    for (uint64_t i = 0; i < aDepth; ++i)
    {
      line += " ";
    }

    // TODO: finish it

    return outcome::success();
  }



}