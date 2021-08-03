#include <crdt/crdt_datastore.hpp>
#include <gtest/gtest.h>
#include <storage/rocksdb/rocksdb.hpp>
#include <outcome/outcome.hpp>
#include <testutil/outcome.hpp>
#include <boost/filesystem.hpp>
#include <ipfs_lite/ipfs/merkledag/impl/merkledag_service_impl.hpp>
#include <ipfs_lite/ipfs/impl/in_memory_datastore.hpp>
#include <queue>
#include <string>
#include <boost/asio/error.hpp>
#include <thread>
#include <crdt/proto/bcast.pb.h>

namespace sgns::crdt
{
  using sgns::storage::rocksdb;
  using sgns::base::Buffer;
  using ipfs_lite::ipld::IPLDNode;
  using ipfs_lite::ipfs::IpfsDatastore;
  using ipfs_lite::ipfs::InMemoryDatastore;
  using CrdtBuffer = CrdtDatastore::Buffer;
  using Delta = CrdtDatastore::Delta;

  namespace fs = boost::filesystem;

  class CustomDagSyncer : public DAGSyncer
  {
  public:
    using IpfsDatastore = ipfs_lite::ipfs::IpfsDatastore;
    using MerkleDagServiceImpl = ipfs_lite::ipfs::merkledag::MerkleDagServiceImpl;

    CustomDagSyncer(std::shared_ptr<IpfsDatastore> service)
      : DAGSyncer(service)
    {
    }

    virtual outcome::result<bool> HasBlock(const CID& cid) const override
    {
      auto getNodeResult = this->getNode(cid);
      return getNodeResult.has_value();
    }
  };

  class CustomBroadcaster : public Broadcaster
  {
  public:
    /**
    * Send {@param buff} payload to other replicas.
    * @return outcome::success on success or outcome::failure on error
    */
    virtual outcome::result<void> Broadcast(const base::Buffer& buff) override
    {
      if (!buff.empty())
      {
        const std::string bCastData(buff.toString());
        listOfBroadcasts_.push(bCastData);
      }
      return outcome::success();
    }

    /**
    * Obtain the next {@return} payload received from the network.
    * @return buffer value or outcome::failure on error
    */
    virtual outcome::result<base::Buffer> Next() override
    {
      if (listOfBroadcasts_.empty())
      {
        //Broadcaster::ErrorCode::ErrNoMoreBroadcast
        return outcome::failure(boost::system::error_code{});
      }

      std::string strBuffer = listOfBroadcasts_.front();
      listOfBroadcasts_.pop();

      base::Buffer buffer;
      buffer.put(strBuffer);
      return buffer;
    }

    std::queue<std::string> listOfBroadcasts_;
  };

  class CrdtDatastoreTest : public ::testing::Test
  {
  public:
    void SetUp() override
    {
      // Remove leftover database 
      std::string databasePath = "supergenius_crdt_datastore_test";
      fs::remove_all(databasePath);

      // Create new database
      rocksdb::Options options;
      options.create_if_missing = true;  // intentionally
      auto dataStoreResult = rocksdb::create(databasePath, options);
      auto dataStore = dataStoreResult.value();

      // Create new DAGSyncer
      auto ipfsDataStore = std::make_shared<InMemoryDatastore>();
      auto dagSyncer = std::make_shared<CustomDagSyncer>(ipfsDataStore);

      // Create new Broadcaster
      auto broadcaster = std::make_shared<CustomBroadcaster>();

      // Define test values
      const std::string strNamespace = "/namespace";
      HierarchicalKey namespaceKey(strNamespace);

      // Create crdtDatastore
      crdtDatastore_ = std::make_shared<CrdtDatastore>(dataStore, namespaceKey, dagSyncer, broadcaster, CrdtOptions::DefaultOptions());
    }

    void TearDown() override
    {
      crdtDatastore_ = nullptr;
    }

    std::shared_ptr<CrdtDatastore> crdtDatastore_ = nullptr;
  };

  TEST_F(CrdtDatastoreTest, TestKeyFunctions)
  {
    auto newKey = HierarchicalKey("NewKey");
    CrdtBuffer buffer;
    buffer.put("Data");

    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey(newKey), false);
    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->PutKey(newKey, buffer));
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey(newKey), true);
    EXPECT_OUTCOME_TRUE(valueBuffer, crdtDatastore_->GetKey(newKey));
    EXPECT_TRUE(buffer.toString() == valueBuffer.toString());
    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->DeleteKey(newKey));
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey(newKey), false);

  }

  TEST_F(CrdtDatastoreTest, TestDeltaFunctions)
  {
    auto newKey1 = HierarchicalKey("NewKey1");
    CrdtBuffer buffer1;
    buffer1.put("Data1");

    auto newKey2 = HierarchicalKey("NewKey2");
    CrdtBuffer buffer2;
    buffer2.put("Data2");

    auto newKey3 = HierarchicalKey("NewKey3");
    CrdtBuffer buffer3;
    buffer3.put("Data3");

    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->AddToDelta(newKey1, buffer1));
    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->AddToDelta(newKey2, buffer2));
    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->AddToDelta(newKey3, buffer3));
    EXPECT_OUTCOME_TRUE(deltaSize, crdtDatastore_->RemoveFromDelta(newKey2));
    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->PublishDelta());
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey(newKey1), true);
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey(newKey2), false);

    auto newKey4 = HierarchicalKey("NewKey4");
    CrdtBuffer buffer4;
    buffer4.put("Data4");

    auto newKey5 = HierarchicalKey("NewKey5");
    CrdtBuffer buffer5;
    buffer5.put("Data5");

    auto delta1 = std::make_shared<Delta>();
    auto newElement1 = delta1->add_elements();
    newElement1->set_key(newKey4.GetKey());
    newElement1->set_value(std::string(buffer4.toString()));
    auto newElement2 = delta1->add_tombstones();
    newElement2->set_key(newKey5.GetKey());
    newElement2->set_value(std::string(buffer5.toString()));
    delta1->set_priority(1);

    auto delta2 = std::make_shared<Delta>();
    auto newElement3 = delta2->add_elements();
    newElement3->set_key(newKey3.GetKey());
    newElement3->set_value(std::string(buffer3.toString()));
    delta2->set_priority(2);

    auto mergedDelta = CrdtDatastore::DeltaMerge(delta1, delta2);
    ASSERT_TRUE(mergedDelta != nullptr);
    EXPECT_TRUE(mergedDelta->priority() == 2);

    auto t = mergedDelta->tombstones();
    ASSERT_TRUE(t.size() == 1);
    EXPECT_TRUE(t[0].key() == newKey5.GetKey());
    EXPECT_TRUE(t[0].value() == std::string(buffer5.toString()));

    auto e = mergedDelta->elements();
    ASSERT_TRUE(e.size() == 2);

    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->Publish(mergedDelta));
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey(newKey3), true);
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey(newKey4), true);
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey(newKey5), false);
  }

  TEST_F(CrdtDatastoreTest, TestBatchFunctions)
  {
    auto newKey1 = HierarchicalKey("NewKey1");
    CrdtBuffer buffer1;
    buffer1.put("Data1");

    auto newKey2 = HierarchicalKey("NewKey2");
    CrdtBuffer buffer2;
    buffer2.put("Data2");

    auto newKey3 = HierarchicalKey("NewKey3");
    CrdtBuffer buffer3;
    buffer3.put("Data3");

    EXPECT_OUTCOME_TRUE(batch, crdtDatastore_->GetBatch());
    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->PutBatch(batch, newKey1, buffer1));
    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->PutBatch(batch, newKey2, buffer2));
    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->PutBatch(batch, newKey3, buffer3));
    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->CommitBatch(batch));
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey(newKey1), true);
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey(newKey2), true);
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey(newKey3), true);

    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->DeleteBatch(batch, newKey2));
    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->DeleteBatch(batch, newKey3));
    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->CommitBatch(batch));
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey(newKey1), true);
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey(newKey2), false);
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey(newKey3), false);

  }
}