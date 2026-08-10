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
}
