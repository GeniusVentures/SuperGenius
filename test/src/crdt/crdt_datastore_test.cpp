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

  // NEW TEST: Test filter callback functionality
  TEST_F(CrdtDatastoreTest, FilterCallback)
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

    //const CID cid1 = CID(CID::Version::V1, CID::Multicodec::SHA2_256,
    //  Multihash::create(HashType::sha256, "1123456789ABCDEF0123456789ABCADE"_unhex).value());
    //const CID cid2 = CID(CID::Version::V1, CID::Multicodec::SHA2_256,
    //  Multihash::create(HashType::sha256, "1123456789ABCDEF0123456789ABCAFE"_unhex).value());
    //const CID cid3 = CID(CID::Version::V1, CID::Multicodec::SHA2_256,
    //  Multihash::create(HashType::sha256, "1123456789ABCDEF0123456789ABDECA"_unhex).value());
    //const CID cid4 = CID(CID::Version::V1, CID::Multicodec::SHA2_256,
    //  Multihash::create(HashType::sha256, "1123456789ABCDEF0123456789ABDEAD"_unhex).value());
//
    //std::vector<CID> cids;
    //cids.push_back(cid1);
    //cids.push_back(cid2);
    //cids.push_back(cid3);
    //cids.push_back(cid4);

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

  // NEW TEST: Test atomic transactions with filter
//  TEST_F(CrdtDatastoreTest, AtomicTransactionWithFilter_DISABLE)
//  {
//    // Create a filter that rejects Deltas with specific content
//    const std::string rejectedValue = "RejectThisValue";
//    
//    // Track filter calls
//    std::atomic<int> filter_called_count{0};
//    
//    auto filter_func = [&](Delta delta) {
//      filter_called_count++;
//      
//      // Check each element's value
//      for (const auto& elem : delta.elements())
//      {
//        if (elem.value() == rejectedValue)
//        {
//          return false; // Reject this delta
//        }
//      }
//      return true; // Accept this delta
//    };
//    
//    // Create filter callback
//    auto filter_cb = std::make_shared<CrdtDatastore::FilterCB>(filter_func);
//    
//    // Create a filtered datastore using existing test class members
//    auto filtered_datastore = CrdtDatastore::New(db_, namespaceKey_, dagSyncer_, broadcaster_, 
//                                                CrdtOptions::DefaultOptions(), filter_cb);
//    additional_datastores_.push_back(filtered_datastore); // For cleanup
//    
//    // Create keys and values for transaction
//    auto key1 = HierarchicalKey("TransKey1");
//    CrdtBuffer value1;
//    value1.put("GoodValue");
//    
//    auto key2 = HierarchicalKey("TransKey2");
//    CrdtBuffer value2;
//    value2.put(rejectedValue);
//    
//    auto key3 = HierarchicalKey("TransKey3");
//    CrdtBuffer value3;
//    value3.put("AnotherGoodValue");
//    
//    // Create and execute an atomic transaction with mixed values
//    {
//      AtomicTransaction transaction(filtered_datastore);
//      EXPECT_OUTCOME_TRUE_1(transaction.Put(key1, value1));
//      EXPECT_OUTCOME_TRUE_1(transaction.Put(key2, value2));
//      EXPECT_OUTCOME_TRUE_1(transaction.Put(key3, value3));
//      EXPECT_OUTCOME_TRUE_1(transaction.Commit());
//    }
//    
//    // The filter should reject the entire transaction
//    EXPECT_OUTCOME_EQ(filtered_datastore->HasKey(key1), false);
//    EXPECT_OUTCOME_EQ(filtered_datastore->HasKey(key2), false);
//    EXPECT_OUTCOME_EQ(filtered_datastore->HasKey(key3), false);
//    
//    // Verify filter was called
//    EXPECT_GE(filter_called_count, 1);
//    
//    // Now try a transaction with only acceptable values
//    {
//      AtomicTransaction transaction(filtered_datastore);
//      EXPECT_OUTCOME_TRUE_1(transaction.Put(key1, value1));
//      EXPECT_OUTCOME_TRUE_1(transaction.Put(key3, value3));
//      EXPECT_OUTCOME_TRUE_1(transaction.Commit());
//    }
//    
//    // This transaction should succeed
//    EXPECT_OUTCOME_EQ(filtered_datastore->HasKey(key1), true);
//    EXPECT_OUTCOME_EQ(filtered_datastore->HasKey(key3), true);
//  }
//
//  // NEW TEST: Test worker thread behavior
//  TEST_F(CrdtDatastoreTest, WorkerThreads_DISABLE)
//  {
//    // Configure options for faster testing
//    auto options = CrdtOptions::DefaultOptions();
//    options->rebroadcastIntervalMilliseconds = 100;  // 100ms rebroadcast
//    options->numWorkers = 3;  // Use multiple workers
//    
//    // Create two datastores sharing the same infrastructure
//    auto ds1 = CrdtDatastore::New(db_, namespaceKey_, dagSyncer_, broadcaster_, options);
//    auto ds2 = CrdtDatastore::New(db_, namespaceKey_, dagSyncer_, broadcaster_, options);
//    additional_datastores_.push_back(ds1);
//    additional_datastores_.push_back(ds2);
//    
//    // Add data to the first datastore
//    const int numKeys = 5;
//    std::vector<HierarchicalKey> keys;
//    
//    for (int i = 0; i < numKeys; ++i)
//    {
//      auto key = HierarchicalKey("WorkerKey" + std::to_string(i));
//      keys.push_back(key);
//      CrdtBuffer value;
//      value.put("WorkerValue" + std::to_string(i));
//      
//      EXPECT_OUTCOME_TRUE_1(ds1->PutKey(key, value));
//    }
//    
//    // Wait for the worker threads in the second datastore to process the data
//    bool allSynced = WaitForCondition([&]() {
//      for (const auto& key : keys)
//      {
//        auto result = ds2->HasKey(key);
//        if (!result.has_value() || !result.value())
//        {
//          return false;
//        }
//      }
//      return true;
//    });
//    
//    EXPECT_TRUE(allSynced);
//    
//    // Verify all keys exist in both datastores
//    for (const auto& key : keys)
//    {
//      EXPECT_OUTCOME_EQ(ds1->HasKey(key), true);
//      EXPECT_OUTCOME_EQ(ds2->HasKey(key), true);
//    }
//  }
//
//  // NEW TEST: Test concurrent operations with worker threads
//  TEST_F(CrdtDatastoreTest, ConcurrentOperations_DISABLE)
//  {
//    // Configure options
//    auto options = CrdtOptions::DefaultOptions();
//    options->numWorkers = 4;
//    
//    // Create datastore
//    auto datastore = CrdtDatastore::New(db_, namespaceKey_, dagSyncer_, broadcaster_, options);
//    additional_datastores_.push_back(datastore);
//    
//    // Number of threads and operations per thread
//    const int numThreads = 4;
//    const int opsPerThread = 5;
//    std::atomic<int> successCount{0};
//    
//    // Run concurrent operations
//    std::vector<std::thread> threads;
//    for (int t = 0; t < numThreads; ++t)
//    {
//      threads.emplace_back([&, t]() {
//        for (int i = 0; i < opsPerThread; ++i)
//        {
//          auto key = HierarchicalKey(
//            "ConcurrentKey_" + std::to_string(t) + "_" + std::to_string(i));
//          CrdtBuffer value;
//          value.put("ConcurrentValue_" + std::to_string(t) + "_" + std::to_string(i));
//          
//          auto result = datastore->PutKey(key, value);
//          if (!result.has_failure())
//          {
//            successCount++;
//          }
//        }
//      });
//    }
//    
//    // Wait for all threads to complete
//    for (auto& t : threads)
//    {
//      t.join();
//    }
//    
//    // All operations should have succeeded
//    EXPECT_EQ(successCount, numThreads * opsPerThread);
//    
//    // Check that all keys exist
//    int foundCount = 0;
//    for (int t = 0; t < numThreads; ++t)
//    {
//      for (int i = 0; i < opsPerThread; ++i)
//      {
//        auto key = HierarchicalKey(
//          "ConcurrentKey_" + std::to_string(t) + "_" + std::to_string(i));
//        auto result = datastore->HasKey(key);
//        if (result.has_value() && result.value())
//        {
//          foundCount++;
//        }
//      }
//    }
//    
//    EXPECT_EQ(foundCount, numThreads * opsPerThread);
//  }
//
//  // NEW TEST: Test combined filter and worker thread behavior
//  TEST_F(CrdtDatastoreTest, FilteredReplication_DISABLE)
//  {
//    // Create a filter that rejects deltas with priority > 5
//    std::atomic<int> filter_called_count{0};
//    
//    auto filter_func = [&](Delta delta) {
//      filter_called_count++;
//      return delta.priority() <= 5; // Accept only low-priority deltas
//    };
//    
//    // Create filter callback
//    auto filter_cb = std::make_shared<CrdtDatastore::FilterCB>(filter_func);
//    
//    // Configure options
//    auto options = CrdtOptions::DefaultOptions();
//    options->rebroadcastIntervalMilliseconds = 100;
//    
//    // Create two datastores - one filtered, one unfiltered
//    auto unfiltered = CrdtDatastore::New(db_, namespaceKey_, dagSyncer_, broadcaster_, options);
//    auto filtered = CrdtDatastore::New(db_, namespaceKey_, dagSyncer_, broadcaster_, options, filter_cb);
//    additional_datastores_.push_back(unfiltered);
//    additional_datastores_.push_back(filtered);
//    
//    // Create low-priority delta (should be accepted by filter)
//    auto lowPrioKey = HierarchicalKey("LowPrioKey");
//    CrdtBuffer lowPrioValue;
//    lowPrioValue.put("LowPrioValue");
//    auto lowPrioDelta = CreateTestDelta(lowPrioKey.GetKey(), std::string(lowPrioValue.toString()), 3);
//    
//    // Create high-priority delta (should be rejected by filter)
//    auto highPrioKey = HierarchicalKey("HighPrioKey");
//    CrdtBuffer highPrioValue;
//    highPrioValue.put("HighPrioValue");
//    auto highPrioDelta = CreateTestDelta(highPrioKey.GetKey(), std::string(highPrioValue.toString()), 10);
//    
//    // Add both deltas to the unfiltered datastore
//    EXPECT_OUTCOME_TRUE_1(unfiltered->Publish(lowPrioDelta));
//    EXPECT_OUTCOME_TRUE_1(unfiltered->Publish(highPrioDelta));
//    
//    // Wait for processing to complete
//    std::this_thread::sleep_for(std::chrono::milliseconds(200));
//    
//    // The unfiltered datastore should have both keys
//    EXPECT_OUTCOME_EQ(unfiltered->HasKey(lowPrioKey), true);
//    EXPECT_OUTCOME_EQ(unfiltered->HasKey(highPrioKey), true);
//    
//    // The filtered datastore should only have the low-priority key
//    EXPECT_OUTCOME_EQ(filtered->HasKey(lowPrioKey), true);
//    EXPECT_OUTCOME_EQ(filtered->HasKey(highPrioKey), false);
//    
//    // Verify filter was called
//    EXPECT_GE(filter_called_count, 2);
//  }
}
