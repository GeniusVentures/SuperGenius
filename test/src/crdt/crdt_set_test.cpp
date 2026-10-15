#include "crdt/crdt_set.hpp"
#include <gtest/gtest.h>
#include <storage/rocksdb/rocksdb.hpp>
#include "outcome/outcome.hpp"
#include <testutil/outcome.hpp>
#include <testutil/remove_all.hpp>
#include <boost/filesystem.hpp>


namespace sgns::crdt
{
  using sgns::storage::rocksdb;
  using sgns::base::Buffer;
  namespace fs = boost::filesystem;

  TEST(CrdtSetTest, TestSetKeys)
  {
    std::string strNamespace = "/namespace";
    auto hKey(strNamespace);
    auto crdtSet = CrdtSet(nullptr, hKey);

    EXPECT_STRCASEEQ((strNamespace + "/key").c_str(), crdtSet.KeyPrefix("key").GetKey().c_str());
    EXPECT_STRCASEEQ((strNamespace + "/s/key").c_str(), crdtSet.ElemsPrefix("key").GetKey().c_str());
    EXPECT_STRCASEEQ((strNamespace + "/t/key").c_str(), crdtSet.TombsPrefix("key").GetKey().c_str());
    EXPECT_STRCASEEQ((strNamespace + "/k/key/v").c_str(), crdtSet.ValueKey("key").GetKey().c_str());
    EXPECT_STRCASEEQ((strNamespace + "/k/key/p").c_str(), crdtSet.PriorityKey("key").GetKey().c_str());
  }

  TEST(CrdtSetTest, TestInvalidDatabase)
  {
    std::string databasePath = "supergenius_crdt_set_invalid_database";
    test::removeAllWithRetry( databasePath );

    rocksdb::Options options;
    options.create_if_missing = true;  // intentionally
    auto dataStoreResult = rocksdb::create(databasePath, options);
    auto dataStore = dataStoreResult.value();

    CrdtSet crdtSetInvalid = CrdtSet(nullptr, HierarchicalKey("/namespace"));

    Buffer valueBuffer;
    valueBuffer.put("V456");
    EXPECT_OUTCOME_FALSE_1(crdtSetInvalid.SetValue("key", "ID123", valueBuffer, 11));
  }


  TEST(CrdtSetTest, TestSetValue)
  {
    // Define test values
    const std::string strNamespace = "/namespace";
    const std::string strKey = "key";
    const uint64_t lowerPriority = 11;
    const uint64_t higherPriority = 12;
    const std::string originalValue = "V456";
    const std::string newValue = "V789";
    const std::string newValueLexicographicallyLarger = "V457";
    const std::string newValueLexicographicallySmaller = "V455";
    const std::string originalTombstoneID = "ID123";

    // Remove leftover database
    std::string databasePath = "supergenius_crdt_set_test_set_value";
    test::removeAllWithRetry( databasePath );

    // Create new database
    rocksdb::Options options;
    options.create_if_missing = true;  // intentionally
    auto dataStoreResult = rocksdb::create(databasePath, options);
    auto dataStore = dataStoreResult.value();

    // Create CrdtSet
    auto hKey(strNamespace);
    auto crdtSet = CrdtSet(dataStore, hKey);

    // Empty CrdtSet should not have namespace defined
    EXPECT_OUTCOME_FALSE(valueFromDatastoreResult, crdtSet.GetValueFromDatastore(hKey));

    EXPECT_OUTCOME_EQ(crdtSet.IsValueInSet(strKey), false);

    // Test SetValue
    Buffer valueBuffer;
    valueBuffer.put(originalValue);
    EXPECT_OUTCOME_TRUE_1(crdtSet.SetValue(strKey, originalTombstoneID, valueBuffer, lowerPriority));
    EXPECT_OUTCOME_EQ(crdtSet.IsValueInSet(strKey), true);
    EXPECT_OUTCOME_TRUE(bufferGetValue, crdtSet.GetElement(strKey));
    EXPECT_STRCASEEQ(std::string(bufferGetValue.toString()).c_str(), originalValue.c_str());

    // Change priority
    EXPECT_OUTCOME_EQ(crdtSet.GetPriority(strKey), lowerPriority);
    EXPECT_OUTCOME_TRUE_1(crdtSet.SetPriority(strKey, higherPriority));
    EXPECT_OUTCOME_EQ(crdtSet.GetPriority(strKey), higherPriority);

    // Try to set value with lower priority, data should not change
    Buffer newValueLowerPriorityBuffer;
    newValueLowerPriorityBuffer.put(newValue);
    EXPECT_OUTCOME_TRUE_1(crdtSet.SetValue(strKey, originalTombstoneID, newValueLowerPriorityBuffer, lowerPriority));
    EXPECT_OUTCOME_EQ(crdtSet.GetPriority(strKey), higherPriority);
    EXPECT_OUTCOME_TRUE(bufferGetNewValueLowerPriority, crdtSet.GetElement(strKey));
    EXPECT_STRCASEEQ(std::string(bufferGetNewValueLowerPriority.toString()).c_str(), originalValue.c_str());

  }

  TEST(CrdtSetTest, TestDelta)
  {
    const std::string strNamespace = "/namespace";
    const std::string deltaKey1 = "abc";
    const std::string deltaValue1 = "cba";
    const uint64_t lowerPriority = 11;
    const uint64_t higherPriority = 12;
    const std::string id = "ID123";

    // Remove leftover database
    std::string databasePath = "supergenius_crdt_set_test_delta";
    test::removeAllWithRetry( databasePath );

    // Create new database
    rocksdb::Options options;
    options.create_if_missing = true;  // intentionally

    auto dataStoreResult = rocksdb::create(databasePath, options);
    auto dataStore = dataStoreResult.value();

    // Create CrdtSet
    auto crdtSet = CrdtSet(dataStore, HierarchicalKey(strNamespace));

    // Testing CreateDelta function
    EXPECT_OUTCOME_TRUE(deltaToAdd, crdtSet.CreateDeltaToAdd(deltaKey1, deltaValue1));
    ASSERT_TRUE(deltaToAdd != nullptr);
    ASSERT_EQ(1, deltaToAdd->elements_size());
    auto elements1 = deltaToAdd->elements();
    EXPECT_STRCASEEQ(elements1.Get(0).key().c_str(), deltaKey1.c_str());
    EXPECT_STRCASEEQ(elements1.Get(0).value().c_str(), deltaValue1.c_str());

    // Add Delta to buffer
    std::vector<CrdtSet::Element> elements(deltaToAdd->elements().begin(), deltaToAdd->elements().end());
    EXPECT_OUTCOME_TRUE_1(crdtSet.PutElems(elements, id, lowerPriority));

    for (auto& elem : elements)
    {
      auto key = elem.key();
      auto value = elem.value();

      // /namespace/s/<key>/<id>
      auto kNamespace = crdtSet.ElemsPrefix(key).ChildString( id );

      Buffer keyBuffer;
      keyBuffer.put(kNamespace.GetKey());
      EXPECT_TRUE(dataStore->contains(keyBuffer));

      // See if we get the same value from the buffer
      EXPECT_OUTCOME_TRUE(getElementResult, crdtSet.GetElement(key));
      EXPECT_STRCASEEQ(std::string(getElementResult.toString()).c_str(), value.c_str());

      EXPECT_OUTCOME_EQ(crdtSet.GetPriority(key), lowerPriority);
    }

    EXPECT_OUTCOME_TRUE(deltaToRemove, crdtSet.CreateDeltaToRemove(deltaKey1));
  }

  TEST(CrdtSetTest, TestTombstone)
  {
    const std::string strNamespace = "/namespace";
    const std::string deltaKey1 = "abc";
    const std::string deltaValue1 = "cba";
    const uint64_t lowerPriority = 11;
    const std::string id = "ID123";

    // Remove leftover database
    std::string databasePath = "supergenius_crdt_set_test_tombstone";
    test::removeAllWithRetry( databasePath );

    // Create new database
    rocksdb::Options options;
    options.create_if_missing = true;  // intentionally

    auto dataStoreResult = rocksdb::create(databasePath, options);
    auto dataStore = dataStoreResult.value();

    // Create CrdtSet
    auto crdtSet = CrdtSet(dataStore, HierarchicalKey(strNamespace));

    // Create delta to add
    auto deltaToAddResult = crdtSet.CreateDeltaToAdd(deltaKey1, deltaValue1);
    auto deltaElements = deltaToAddResult.value()->elements();

    // Add delta to buffer
    std::vector<CrdtSet::Element> elements(deltaElements.begin(), deltaElements.end());
    auto putElementsResult = crdtSet.PutElems(elements, id, lowerPriority);
    ASSERT_FALSE(putElementsResult.has_failure());

    EXPECT_OUTCOME_TRUE_1(crdtSet.PutTombs(elements, ""));

    for (auto& elem : elements)
    {
      EXPECT_OUTCOME_EQ(crdtSet.IsValueInSet(elem.key()), false);
      EXPECT_OUTCOME_EQ(crdtSet.InTombsKeyID(elem.key(), elem.id()), true);
    }

  }

  TEST(CrdtSetTest, TestMerge)
  {
    const std::string strNamespace = "/namespace";
    const std::string deltaKey1 = "abc";
    const std::string deltaValue1 = "cba";
    const uint64_t lowerPriority = 11;
    const uint64_t higherPriority = 12;
    const std::string id = "ID123";

    // Remove leftover database
    std::string databasePath = "supergenius_crdt_set_test_merge";
    test::removeAllWithRetry( databasePath );

    // Create new database
    rocksdb::Options options;
    options.create_if_missing = true;  // intentionally

    auto dataStoreResult = rocksdb::create(databasePath, options);
    auto dataStore = dataStoreResult.value();

    // Create CrdtSet
    auto crdtSet = CrdtSet(dataStore, HierarchicalKey(strNamespace));

    // Create delta to add
    auto delta = std::make_shared<CrdtSet::Delta>();
    delta->set_priority(lowerPriority);
    auto element1 = delta->add_elements();
    element1->set_key("k1");
    element1->set_value("v1");
    auto element2 = delta->add_elements();
    element2->set_key("k2");
    element2->set_value("v2");
    auto tombstone1 = delta->add_tombstones();
    tombstone1->set_key("t_k1");
    tombstone1->set_value("t_v1");

    auto deltaElements = delta->elements();
    auto deltaTombstones = delta->tombstones();

    EXPECT_OUTCOME_TRUE_1(crdtSet.Merge( *delta, id ) );

    std::vector<CrdtSet::Element> elements(deltaElements.begin(), deltaElements.end());
    for (auto& elem : elements)
    {
      EXPECT_OUTCOME_TRUE(getElementResult, crdtSet.GetElement(elem.key()));
      EXPECT_STRCASEEQ(std::string(getElementResult.toString()).c_str(), elem.value().c_str());
      EXPECT_OUTCOME_EQ(crdtSet.InTombsKeyID(elem.key(), elem.id()), false);
    }

    std::vector<CrdtSet::Element> tombstones(deltaTombstones.begin(), deltaTombstones.end());
    for (auto& tomb : tombstones)
    {
      EXPECT_OUTCOME_EQ(crdtSet.IsValueInSet(tomb.key()), false);
      EXPECT_OUTCOME_EQ(crdtSet.InTombsKeyID(tomb.key(), id), true);
    }

  }

  TEST(CrdtSetTest, TestTombstoneAcrossDifferentCIDsAndQuery)
  {
    const std::string strNamespace = "/namespace";
    const std::string key = "claimable/task_x";
    const std::string value = "task_x";
    const uint64_t priority = 1;
    const std::string cidA = "CID_A";
    const std::string cidB = "CID_B";

    std::string databasePath = "supergenius_crdt_set_test_tombstone_diff_cids";
    test::removeAllWithRetry( databasePath );

    rocksdb::Options options;
    options.create_if_missing = true;

    auto dataStoreResult = rocksdb::create(databasePath, options);
    auto dataStore = dataStoreResult.value();

    auto crdtSet = CrdtSet(dataStore, HierarchicalKey(strNamespace));

    auto addDelta = std::make_shared<CrdtSet::Delta>();
    addDelta->set_priority(priority);
    auto addElement = addDelta->add_elements();
    addElement->set_key(key);
    addElement->set_value(value);

    // Simulate creation being merged under CID_A.
    EXPECT_OUTCOME_TRUE_1(crdtSet.Merge(*addDelta, cidA));
    EXPECT_OUTCOME_EQ(crdtSet.IsValueInSet(key), true);

    EXPECT_OUTCOME_TRUE(removeDelta, crdtSet.CreateDeltaToRemove(key));
    ASSERT_EQ(removeDelta->tombstones_size(), 1);
    const auto tombId = removeDelta->tombstones(0).id();
    EXPECT_FALSE(tombId.empty());

    // Simulate deletion being published under CID_B.
    EXPECT_OUTCOME_TRUE_1(crdtSet.Merge(*removeDelta, cidB));
    EXPECT_OUTCOME_EQ(crdtSet.IsValueInSet(key), false);
    EXPECT_OUTCOME_EQ(crdtSet.InTombsKeyID(key, tombId), true);

    // Prefix query should not return the tombstoned key anymore.
    EXPECT_OUTCOME_TRUE(queryAfterDelete, crdtSet.QueryElements(key, CrdtSet::QuerySuffix::QUERY_VALUESUFFIX));
    EXPECT_TRUE(queryAfterDelete.empty());
  }

  // Fresh temp DB per test for the caller-owned-batch erase primitive
  // (StageEraseElement). Every test asserts present-before and absent-after
  // against the same store (D-C6).
  class EraseElementStagingTest : public ::testing::Test
  {
  protected:
    void SetUp() override
    {
      test::removeAllWithRetry(databasePath);

      rocksdb::Options options;
      options.create_if_missing = true;  // intentionally
      auto dataStoreResult = rocksdb::create(databasePath, options);
      dataStore = dataStoreResult.value();

      crdtSet = std::make_shared<CrdtSet>(dataStore, HierarchicalKey("/namespace"));
    }

    // Raw count of datastore keys starting with aPrefix (bare prefix, so it
    // catches anything the erase could have missed).
    size_t countKeysWithPrefix(const std::string& prefix)
    {
      Buffer prefixBuffer;
      prefixBuffer.put(prefix);
      auto queryResult = dataStore->query(prefixBuffer);
      EXPECT_TRUE(queryResult);
      return queryResult.value().size();
    }

    bool datastoreContains(const std::string& key)
    {
      Buffer keyBuffer;
      keyBuffer.put(key);
      return dataStore->contains(keyBuffer);
    }

    const std::string databasePath = "supergenius_crdt_set_test_erase_element_staging";
    std::shared_ptr<rocksdb> dataStore;
    std::shared_ptr<CrdtSet> crdtSet;
  };

  TEST_F(EraseElementStagingTest, StagedEraseRemovesAllFamilies)
  {
    const std::string targetKey = "target";
    const std::string siblingKey = "sibling";
    const std::string firstId = "ID123";
    const std::string secondId = "ID456";
    const uint64_t firstPriority = 11;
    const uint64_t secondPriority = 12;
    const std::string targetValue = "TARGET_VALUE";
    const std::string siblingValue = "SIBLING_VALUE";

    // Seed the target through the public set path: element entry
    // (/namespace/s/<key>/<id>) plus value and priority.
    auto firstDeltaResult = crdtSet->CreateDeltaToAdd(targetKey, targetValue);
    std::vector<CrdtSet::Element> firstElements(firstDeltaResult.value()->elements().begin(),
                                                firstDeltaResult.value()->elements().end());
    EXPECT_OUTCOME_TRUE_1(crdtSet->PutElems(firstElements, firstId, firstPriority));

    // Seed a tombstone entry (/namespace/t/<key>/<id>) through the public
    // tombstone path. PutTombs itself fires the delete hook, so it must run
    // BEFORE the hook is registered below.
    EXPECT_OUTCOME_TRUE_1(crdtSet->PutTombs(firstElements, firstId));

    // Re-add under a second id so the key is live again while the tombstone
    // from the first id stays present (both must be erased).
    auto secondDeltaResult = crdtSet->CreateDeltaToAdd(targetKey, targetValue);
    std::vector<CrdtSet::Element> secondElements(secondDeltaResult.value()->elements().begin(),
                                                 secondDeltaResult.value()->elements().end());
    EXPECT_OUTCOME_TRUE_1(crdtSet->PutElems(secondElements, secondId, secondPriority));

    // Seed the untouched sibling (control) and snapshot its value bytes.
    Buffer siblingValueBuffer;
    siblingValueBuffer.put(siblingValue);
    EXPECT_OUTCOME_TRUE_1(crdtSet->SetValue(siblingKey, "ID_SIB", siblingValueBuffer, 5));
    EXPECT_OUTCOME_TRUE(siblingValueBefore, crdtSet->GetElement(siblingKey));
    const std::string siblingBytesBefore(siblingValueBefore.toString());

    // Present-before asserts.
    EXPECT_EQ(2, countKeysWithPrefix(crdtSet->ElemsPrefix(targetKey).GetKey()));
    EXPECT_EQ(1, countKeysWithPrefix(crdtSet->TombsPrefix(targetKey).GetKey()));
    EXPECT_TRUE(datastoreContains(crdtSet->ValueKey(targetKey).GetKey()));
    EXPECT_TRUE(datastoreContains(crdtSet->PriorityKey(targetKey).GetKey()));
    EXPECT_OUTCOME_TRUE(targetValueBefore, crdtSet->GetElement(targetKey));
    EXPECT_STRCASEEQ(std::string(targetValueBefore.toString()).c_str(), targetValue.c_str());
    EXPECT_OUTCOME_EQ(crdtSet->GetPriority(targetKey), secondPriority);

    // The erase primitive must never fire the delete hook.
    bool deleteHookFired = false;
    crdtSet->SetDeleteHook(
        [&deleteHookFired](const std::string& /*k*/, const std::string& /*cid*/) { deleteHookFired = true; });

    // Stage into a caller-owned batch, then commit it ourselves: the caller
    // owns the single commit.
    auto batch = dataStore->batch();
    EXPECT_OUTCOME_TRUE_1(crdtSet->StageEraseElement(batch, targetKey));
    EXPECT_OUTCOME_TRUE_1(batch->commit());

    // Absent-after asserts: all four families gone.
    EXPECT_EQ(0, countKeysWithPrefix(crdtSet->ElemsPrefix(targetKey).GetKey()));
    EXPECT_EQ(0, countKeysWithPrefix(crdtSet->TombsPrefix(targetKey).GetKey()));
    EXPECT_FALSE(datastoreContains(crdtSet->ValueKey(targetKey).GetKey()));
    EXPECT_FALSE(datastoreContains(crdtSet->PriorityKey(targetKey).GetKey()));
    EXPECT_OUTCOME_FALSE_1(crdtSet->GetElement(targetKey));
    EXPECT_OUTCOME_EQ(crdtSet->GetPriority(targetKey), 0);

    // No hook fired and the sibling is byte-identical.
    EXPECT_FALSE(deleteHookFired);
    EXPECT_TRUE(datastoreContains(crdtSet->ValueKey(siblingKey).GetKey()));
    EXPECT_OUTCOME_TRUE(siblingValueAfter, crdtSet->GetElement(siblingKey));
    EXPECT_STREQ(siblingBytesBefore.c_str(), std::string(siblingValueAfter.toString()).c_str());
  }

  TEST_F(EraseElementStagingTest, NullBatchIsRejected)
  {
    const std::string key = "key";
    const std::string value = "V1";

    Buffer valueBuffer;
    valueBuffer.put(value);
    EXPECT_OUTCOME_TRUE_1(crdtSet->SetValue(key, "ID123", valueBuffer, 11));

    EXPECT_OUTCOME_FALSE_1(crdtSet->StageEraseElement(nullptr, key));

    // Nothing was staged or committed: the value is still readable.
    EXPECT_OUTCOME_TRUE(getElementResult, crdtSet->GetElement(key));
    EXPECT_STRCASEEQ(std::string(getElementResult.toString()).c_str(), value.c_str());
  }

  TEST_F(EraseElementStagingTest, UncommittedBatchChangesNothing)
  {
    const std::string key = "key";
    const std::string value = "V1";

    Buffer valueBuffer;
    valueBuffer.put(value);
    EXPECT_OUTCOME_TRUE_1(crdtSet->SetValue(key, "ID123", valueBuffer, 11));

    {
      auto batch = dataStore->batch();
      EXPECT_OUTCOME_TRUE_1(crdtSet->StageEraseElement(batch, key));
      // The batch is deliberately destroyed without commit.
    }

    // The staged removes died with the batch: the value is still readable.
    EXPECT_OUTCOME_TRUE(getElementResult, crdtSet->GetElement(key));
    EXPECT_STRCASEEQ(std::string(getElementResult.toString()).c_str(), value.c_str());
  }
}
