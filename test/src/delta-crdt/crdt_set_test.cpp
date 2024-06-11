#include <crdt/crdt_set.hpp>
#include <gtest/gtest.h>
#include <storage/rocksdb/rocksdb.hpp>
#include <outcome/outcome.hpp>
#include <testutil/outcome.hpp>
#include <boost/filesystem.hpp>
#include <src/delta-crdt/delta-crdts.hpp>
#include <iostream>
#include <storage/database_error.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/system/error_code.hpp>
#include <boost/lexical_cast.hpp>

namespace sgns::crdt
{
  using sgns::storage::rocksdb;
  using sgns::base::Buffer;
  namespace fs = boost::filesystem;



  class Key {
  public:
      Key(const std::string& key) : key_(key) {}

      std::string GetKey() const {
          return key_;
      }

      Key child(const std::string& subKey) const {
          return Key(key_ + "/" + subKey);
      }

  private:
      std::string key_;
  };

  class HierarchicalAWORSet {
  public:
      using Delta = pb::Delta;
      using Element = pb::Element;
      using Buffer = base::Buffer;
      using DataStore = storage::rocksdb;
      using QueryResult = DataStore::QueryResult;
      HierarchicalAWORSet(const std::shared_ptr<DataStore>& dataStore, const std::string& ns)
          : dataStore_(dataStore), prefix(ns) {}

      HierarchicalKey KeyPrefix(const std::string& key) const {
          return prefix.ChildString(key);
      }

      HierarchicalKey ElemsPrefix(const std::string& key) const {
          return prefix.ChildString("s").ChildString(key);
      }

      HierarchicalKey TombsPrefix(const std::string& key) const {
          return prefix.ChildString("t").ChildString(key);
      }

      HierarchicalKey ValueKey(const std::string& key) const {
          return prefix.ChildString("k").ChildString(key).ChildString("v");
      }

      HierarchicalKey PriorityKey(const std::string& key) const {
          return prefix.ChildString("k").ChildString(key).ChildString("p");
      }

      std::set<char> read(const std::string& key) {
          auto it = sets.find(key);
          if (it != sets.end()) {
              return it->second.read();
          }
          return {};
      }
      outcome::result<void> SetValue(const std::unique_ptr<storage::BufferBatch>& aDataStore, const std::string& aKey,
          const std::string& aID, const Buffer& aValue, const uint64_t& aPriority);


      outcome::result<void> SetValue(const std::string& aKey, const std::string& aID, const Buffer& aValue,
          const uint64_t& aPriority);
  private:
      std::shared_ptr<DataStore> dataStore_;
      HierarchicalKey prefix;
      std::map<std::string, crdts::AWORSet<char, std::string>> sets;
  };


  outcome::result<void> HierarchicalAWORSet::SetValue(const std::unique_ptr<storage::BufferBatch>& aDataStore, const std::string& aKey,
      const std::string& aID, const Buffer& aValue, const uint64_t& aPriority)
  {
      //if (aDataStore == nullptr)
      //{
      //    return outcome::failure(boost::system::error_code{});
      //}

      //// If this key was tombstoned already, do not store/update the value at all.
      //auto isDeletedResult = this->InTombsKeyID(aKey, aID);
      //if (isDeletedResult.has_failure() || isDeletedResult.value() == true)
      //{
      //    return outcome::failure(boost::system::error_code{});
      //}

      //auto priorityResult = this->GetPriority(aKey);
      //if (priorityResult.has_failure())
      //{
      //    return outcome::failure(priorityResult.error());
      //}

      //if (aPriority < priorityResult.value())
      //{
      //    return outcome::success();
      //}

      //auto valueK = this->ValueKey(aKey);

      //if (aPriority == priorityResult.value())
      //{
      //    auto valueResult = this->GetValueFromDatastore(valueK);
      //    if (valueResult.has_failure())
      //    {
      //        return outcome::failure(valueResult.error());
      //    }

      //    // if bytes.Compare(valueResult.value(), aValue) >= 0 {
      //    // comparing two data lexicographically,  valueResult >= aValue, no need to store value
      //    if (!boost::lexicographical_compare<std::string, std::string>(valueResult.value(), std::string(aValue.toString())))
      //    {
      //        return outcome::success();
      //    }
      //}

      //// store value
      //Buffer valueKeyBuffer;
      //valueKeyBuffer.put(valueK.GetKey());

      //auto putResult = aDataStore->put(valueKeyBuffer, aValue);
      //if (putResult.has_failure())
      //{
      //    return outcome::failure(putResult.error());
      //}

      //// store priority
      //auto setPriorityResult = this->SetPriority(aKey, aPriority);
      //if (setPriorityResult.has_failure())
      //{
      //    return outcome::failure(setPriorityResult.error());
      //}

      //// trigger add hook
      //if (this->putHookFunc_ != nullptr)
      //{
      //    putHookFunc_(aKey, aValue);
      //}

      return outcome::success();
  }

  outcome::result<void> HierarchicalAWORSet::SetValue(const std::string& aKey, const std::string& aID, const Buffer& aValue,
      const uint64_t& aPriority) {
      if (dataStore_ == nullptr) {
          return outcome::failure(boost::system::error_code{});
      }

      auto batchDatastore = this->dataStore_->batch();
      auto setValueResult = this->SetValue(batchDatastore, aKey, aID, aValue, aPriority);
      if (setValueResult.has_failure())
      {
          return outcome::failure(setValueResult.error());
      }

      auto commitResult = batchDatastore->commit();
      if (commitResult.has_failure()) {
          return outcome::failure(commitResult.error());
      }

      return outcome::success();
  }

  TEST(CrdtSetTest, TestSetKeys)
  {
    std::string strNamespace = "/namespace";
    sgns::crdt::HierarchicalAWORSet crdtSet(nullptr, strNamespace);
    EXPECT_STRCASEEQ((strNamespace + "/key").c_str(), crdtSet.KeyPrefix("key").GetKey().c_str());
    EXPECT_STRCASEEQ((strNamespace + "/s/key").c_str(), crdtSet.ElemsPrefix("key").GetKey().c_str());
    EXPECT_STRCASEEQ((strNamespace + "/t/key").c_str(), crdtSet.TombsPrefix("key").GetKey().c_str());
    EXPECT_STRCASEEQ((strNamespace + "/k/key/v").c_str(), crdtSet.ValueKey("key").GetKey().c_str());
    EXPECT_STRCASEEQ((strNamespace + "/k/key/p").c_str(), crdtSet.PriorityKey("key").GetKey().c_str());
  }

  TEST(CrdtSetTest, TestInvalidDatabase)
  {
    std::string databasePath = "supergenius_crdt_set_invalid_database";
    fs::remove_all(databasePath);

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
    fs::remove_all(databasePath);

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

    // Try to set value with same priority, lexicographically smaller => value should NOT change  
    // comparing two data lexicographically,  currentValue >= newValue, no need to store value
    Buffer newValueLexicographicallySmallerSamePriorityBuffer;
    newValueLexicographicallySmallerSamePriorityBuffer.put(newValueLexicographicallySmaller);
    auto samePriorityResult = crdtSet.GetPriority(strKey);
    const uint64_t samePriority = samePriorityResult.value();
    EXPECT_OUTCOME_TRUE_1(crdtSet.SetValue(strKey, originalTombstoneID, newValueLexicographicallySmallerSamePriorityBuffer, samePriority));
    EXPECT_OUTCOME_TRUE(bufferGetNewValueLexicographicallySmallerSamePriority, crdtSet.GetElement(strKey));
    EXPECT_STRCASEEQ(std::string(bufferGetNewValueLexicographicallySmallerSamePriority.toString()).c_str(), originalValue.c_str());

    // Try to set value with same priority, lexicographically larger => value should change  
    Buffer newValueLexicographicallyLargerSamePriorityBuffer;
    newValueLexicographicallyLargerSamePriorityBuffer.put(newValueLexicographicallyLarger);
    EXPECT_OUTCOME_TRUE_1(crdtSet.SetValue(strKey, originalTombstoneID, newValueLexicographicallyLargerSamePriorityBuffer, samePriority));
    EXPECT_OUTCOME_TRUE(bufferGetNewValueLexicographicallyLargerSamePriority, crdtSet.GetElement(strKey));
    EXPECT_STRCASEEQ(std::string(bufferGetNewValueLexicographicallyLargerSamePriority.toString()).c_str(), newValueLexicographicallyLarger.c_str());
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
    fs::remove_all(databasePath);

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
      auto kNamespace = crdtSet.ElemsPrefix(key).ChildString(id);
      
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
    fs::remove_all(databasePath);

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

    EXPECT_OUTCOME_TRUE_1(crdtSet.PutTombs(elements));

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
    fs::remove_all(databasePath);

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

    EXPECT_OUTCOME_TRUE_1(crdtSet.Merge(delta, id));

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
      EXPECT_OUTCOME_EQ(crdtSet.InTombsKeyID(tomb.key(), tomb.id()), true);
    }

  }
}