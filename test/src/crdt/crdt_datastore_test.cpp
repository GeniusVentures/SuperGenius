#include "crdt/crdt_datastore.hpp"
#include "crdt/atomic_transaction.hpp"
#include <gtest/gtest.h>
#include <storage/rocksdb/rocksdb.hpp>
#include "outcome/outcome.hpp"
#include <testutil/outcome.hpp>
#include <testutil/literals.hpp>
#include "testutil/wait_condition.hpp"

#include <boost/filesystem.hpp>
#include <boost/algorithm/hex.hpp>
#include <libp2p/multi/multihash.hpp>
#include <ipfs_lite/ipfs/impl/in_memory_datastore.hpp>
#include <queue>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include "crdt/proto/bcast.pb.h"
#include "crdt_custom_broadcaster.hpp"
#include "crdt_custom_dagsyncer.hpp"

namespace sgns::crdt
{
  using storage::rocksdb;
  using base::Buffer;
  using ipfs_lite::ipld::IPLDNode;
  using ipfs_lite::ipfs::IpfsDatastore;
  using ipfs_lite::ipfs::InMemoryDatastore;
  using CrdtBuffer = CrdtDatastore::Buffer;
  using Delta = CrdtDatastore::Delta;
  using Element = CrdtDatastore::Element;
  using libp2p::multi::HashType;
  using libp2p::multi::Multihash;

  namespace fs = boost::filesystem;

  class CrdtDatastoreTest : public ::testing::Test
  {
  public:
    // Remove leftover database if any
    const std::string databasePath = "supergenius_crdt_datastore_test";

    CrdtDatastoreTest()
    {
        fs::remove_all(databasePath);
    }

    void SetUp() override
    {
        // Create new database
        rocksdb::Options options;
        options.create_if_missing = true;  // intentionally
        auto result = rocksdb::create(databasePath, options);
        if ( !result )
        {
            throw std::invalid_argument( result.error().message() );
        }
        db_  = std::move( result.value() );

        // Create new DAGSyncer
        auto ipfsDataStore = std::make_shared<InMemoryDatastore>();
        dagSyncer_ = std::make_shared<CustomDagSyncer>(ipfsDataStore);

        // Create new Broadcaster
        broadcaster_ = std::make_shared<CustomBroadcaster>();

        // Define test values
        const std::string strNamespace = "/namespace";
        namespaceKey_ = HierarchicalKey(strNamespace);

        // Create crdtDatastore
        crdtDatastore_ = CrdtDatastore::New(db_, namespaceKey_, dagSyncer_, broadcaster_, CrdtOptions::DefaultOptions());
    }

    void TearDown() override
    {
      if (crdtDatastore_) {
        crdtDatastore_->Close();
        crdtDatastore_ = nullptr;
      }
      
      // Clean up any additional datastores
      
      db_ = nullptr;
      dagSyncer_ = nullptr;
      broadcaster_ = nullptr;
      
      // Remove leftover database
      fs::remove_all(databasePath);
    }

    // Helper to create a test delta
    std::shared_ptr<Delta> CreateTestDelta(const std::string& key, const std::string& value, uint64_t priority = 1)
    {
      auto delta = std::make_shared<Delta>();
      auto element = delta->add_elements();
      element->set_key(key);
      element->set_value(value);
      delta->set_priority(priority);
      return delta;
    }

    std::shared_ptr<rocksdb> db_;
    std::shared_ptr<CustomDagSyncer> dagSyncer_;
    std::shared_ptr<CustomBroadcaster> broadcaster_;
    std::shared_ptr<CrdtDatastore> crdtDatastore_ = nullptr;
    HierarchicalKey namespaceKey_;
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

    AtomicTransaction transaction = AtomicTransaction(crdtDatastore_);
    EXPECT_OUTCOME_TRUE_1(transaction.Put(newKey1, buffer1));
    EXPECT_OUTCOME_TRUE_1(transaction.Put(newKey2, buffer2));
    EXPECT_OUTCOME_TRUE_1(transaction.Put(newKey3, buffer3));
    // this won't work as part of the same atomic transaction, because the Remove looks for the existing key
    // to create the delta, and since it's queued in the atomic transaction, it doesn't find key2
    //EXPECT_OUTCOME_TRUE_1(transaction.Remove(newKey2));
    EXPECT_OUTCOME_TRUE_1(transaction.Commit());
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey(newKey1), true);
    AtomicTransaction transactionRemoveKey2 = AtomicTransaction(crdtDatastore_);
    EXPECT_OUTCOME_TRUE_1(transactionRemoveKey2.Remove(newKey2));
    EXPECT_OUTCOME_TRUE_1(transactionRemoveKey2.Commit());
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey(newKey1), true);
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey(newKey2), false);
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey(newKey3), true);

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

  TEST_F(CrdtDatastoreTest, FilterCallbackOneInvalid)
  {
    // Create a filter that rejects Deltas containing a specific key
    const std::string rejectedKey = "RejectMe";
    const std::string acceptedKey = "AcceptMe";
    
    // Track filter calls
    std::atomic<int> filter_called_count{0};
    
    auto filter_func = [&](Element element) {
      filter_called_count++;
      
      // Check if any element has the rejected key

        if (element.value() == rejectedKey)
        {
          return false; // Reject this delta
        }
      return true; // Accept this delta
    };
    
    // Create filter callback
    auto filter_cb = std::make_shared<CrdtDatastore::FilterCB>(filter_func);

    EXPECT_TRUE(crdtDatastore_->SetFilterCallback(filter_cb));

    std::shared_ptr<Delta> delta = std::make_shared<Delta>();
    auto element1 = delta->add_elements();
    element1->set_key( "Key1" );
    element1->set_value( acceptedKey );
    auto element2 = delta->add_elements();
    element2->set_key( "Key2" );
    element2->set_value( rejectedKey );
    auto element3 = delta->add_elements();
    element3->set_key( "Key3");
    element3->set_value( acceptedKey );
    auto element4 = delta->add_elements();
    element4->set_key( "Key4");
    element4->set_value( acceptedKey );

    delta->set_priority(1);


    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->Publish(delta));

    std::chrono::milliseconds resultTime;

    test::assertWaitForCondition(
      [&filter_called_count]() {
          return filter_called_count == 4;
      },
      std::chrono::milliseconds(15000),
      "NO FILTER RAN",
      &resultTime
  );

    
    // Test with accepted key (should be stored)
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey({"Key1"}), true);
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey({"Key2"}), false);
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey({"Key3"}), true);
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey({"Key4"}), true);
    
    // Verify filter was called
    EXPECT_GE(filter_called_count, 1);
  }

  TEST_F(CrdtDatastoreTest, FilterCallbackAllInvalid)
  {
    // Create a filter that rejects Deltas containing a specific key
    const std::string rejectedKey = "RejectMe";
    const std::string acceptedKey = "AcceptMe";
    
    // Track filter calls
    std::atomic<int> filter_called_count{0};
    
    auto filter_func = [&](Element element) {
      filter_called_count++;
      
      // Check if any element has the rejected key

        if (element.value() == rejectedKey)
        {
          return false; // Reject this delta
        }
      return true; // Accept this delta
    };
    
    // Create filter callback
    auto filter_cb = std::make_shared<CrdtDatastore::FilterCB>(filter_func);

    EXPECT_TRUE(crdtDatastore_->SetFilterCallback(filter_cb));

    std::shared_ptr<Delta> delta = std::make_shared<Delta>();
    auto element1 = delta->add_elements();
    element1->set_key( "Key1" );
    element1->set_value( rejectedKey );
    auto element2 = delta->add_elements();
    element2->set_key( "Key2" );
    element2->set_value( rejectedKey );
    auto element3 = delta->add_elements();
    element3->set_key( "Key3");
    element3->set_value( rejectedKey );
    auto element4 = delta->add_elements();
    element4->set_key( "Key4");
    element4->set_value( rejectedKey );

    delta->set_priority(1);


    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->Publish(delta));

    std::chrono::milliseconds resultTime;

    test::assertWaitForCondition(
      [&filter_called_count]() {
          return filter_called_count == 4;
      },
      std::chrono::milliseconds(15000),
      "NO FILTER RAN",
      &resultTime
  );

    
    // Test with accepted key (should be stored)
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey({"Key1"}), false);
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey({"Key2"}), false);
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey({"Key3"}), false);
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey({"Key4"}), false);
    
    // Verify filter was called
    EXPECT_GE(filter_called_count, 1);
  }

  TEST_F(CrdtDatastoreTest, FilterCallbackMultipleDeltas)
  {
    // Create a filter that rejects Deltas containing a specific key
    const std::string rejectedKey = "RejectMe";
    const std::string acceptedKey = "AcceptMe";
    
    // Track filter calls
    std::atomic<int> filter_called_count{0};
    
    auto filter_func = [&](Element element) {
      filter_called_count++;
      
      // Check if any element has the rejected key

        if (element.value() == rejectedKey)
        {
          return false; // Reject this delta
        }
      return true; // Accept this delta
    };
    
    // Create filter callback
    auto filter_cb = std::make_shared<CrdtDatastore::FilterCB>(filter_func);

    EXPECT_TRUE(crdtDatastore_->SetFilterCallback(filter_cb));

    std::shared_ptr<Delta> delta1 = std::make_shared<Delta>();
    auto element1 = delta1->add_elements();
    element1->set_key( "Key1" );
    element1->set_value( acceptedKey );
    std::shared_ptr<Delta> delta2 = std::make_shared<Delta>();
    auto element2 = delta2->add_elements();
    element2->set_key( "Key2" );
    element2->set_value( rejectedKey );
    std::shared_ptr<Delta> delta3 = std::make_shared<Delta>();
    auto element3 = delta3->add_elements();
    element3->set_key( "Key3");
    element3->set_value( acceptedKey );
    std::shared_ptr<Delta> delta4 = std::make_shared<Delta>();
    auto element4 = delta4->add_elements();
    element4->set_key( "Key4");
    element4->set_value( acceptedKey );

    delta1->set_priority(1);
    delta2->set_priority(2);
    delta3->set_priority(3);
    delta4->set_priority(4);


    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->Publish(delta1));
    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->Publish(delta2));
    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->Publish(delta3));
    EXPECT_OUTCOME_TRUE_1(crdtDatastore_->Publish(delta4));

    std::chrono::milliseconds resultTime;

    test::assertWaitForCondition(
      [&filter_called_count]() {
          return filter_called_count == 4;
      },
      std::chrono::milliseconds(15000),
      "NO FILTER RAN",
      &resultTime
  );

    
    // Test with accepted key (should be stored)
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey({"Key1"}), true);
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey({"Key2"}), false);
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey({"Key3"}), true);
    EXPECT_OUTCOME_EQ(crdtDatastore_->HasKey({"Key4"}), true);
    
    // Verify filter was called
    EXPECT_GE(filter_called_count, 1);
  }
  
}
