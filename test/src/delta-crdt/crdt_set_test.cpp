#include <crdt/crdt_set.hpp>
#include <gtest/gtest.h>
#include <storage/rocksdb/rocksdb.hpp>
#include <outcome/outcome.hpp>
#include <testutil/outcome.hpp>
#include <boost/filesystem.hpp>
#include <crdts/delta-crdts.h>
#include <iostream>
namespace sgns::crdt
{
  using sgns::storage::rocksdb;
  using sgns::base::Buffer;
  namespace fs = boost::filesystem;
  class Key {
  public:
      Key() : fullPath("/") {}
      Key(const std::string& path) : fullPath(path) {}

      Key child(const std::string& subKey) const {
          return Key(fullPath + "/" + subKey);
      }

      std::string toString() const {
          return fullPath;
      }

      bool operator<(const Key& other) const {
          return fullPath < other.fullPath;
      }

      friend std::ostream& operator<<(std::ostream& os, const Key& key) {
          os << key.toString();
          return os;
      }

  private:
      std::string fullPath;
  };

  class HierarchicalAWORSet {
  public:
      HierarchicalAWORSet(const sgns::crdt::Key& keyPrefix) : prefix(keyPrefix) {}

      void add(const std::string& key, char val) {
          auto fullKey = prefix.child(key);
          auto it = sets.find(fullKey);
          if (it == sets.end()) {
              crdts::AWORSet<char, sgns::crdt::Key> newSet(fullKey);
              newSet = newSet.add(val);
              sets[fullKey] = newSet;
          }
          else {
              it->second = it->second.add(val);
          }
      }

      void remove(const std::string& key, char val) {
          auto fullKey = prefix.child(key);
          auto it = sets.find(fullKey);
          if (it == sets.end()) {
              crdts::AWORSet<char, sgns::crdt::Key> newSet(fullKey);
              newSet = newSet.rmv(val);
              sets[fullKey] = newSet;
          }
          else {
              it->second = it->second.rmv(val);
          }
      }

      std::set<char> read(const std::string& key) {
          auto fullKey = prefix.child(key);
          auto it = sets.find(fullKey);
          if (it != sets.end()) {
              return it->second.read();
          }
          return {};
      }

      void join(const HierarchicalAWORSet& other) {
          for (const auto& [key, set] : other.sets) {
              if (sets.find(key) == sets.end()) {
                  sets[key] = set;
              }
              else {
                  sets[key].join(set);
              }
          }
      }

      friend std::ostream& operator<<(std::ostream& output, const HierarchicalAWORSet& o) {
          for (const auto& [key, set] : o.sets) {
              output << "Key: " << key << "\n" << set << "\n";
          }
          return output;
      }

  private:
      sgns::crdt::Key prefix;
      std::map<sgns::crdt::Key, crdts::AWORSet<char, sgns::crdt::Key>> sets;
  };


  TEST(CrdtSetTest, TestSetKeys)
  {
    
    std::string strNamespace = "/namespace";
    auto hKey(strNamespace);
    std::cout << "hKey: " << hKey << std::endl;
    auto crdtSet = CrdtSet(nullptr, hKey);
    std::string strTest = "test";
    //crdts::AWORSet<char> o1("idx"), o2("idy"), do1, do2;
    //do1.join(o1.add('a'));
    //do1.join(o1.add('b'));

    //do2.join(o2.add('b'));
    //do2.join(o2.add('c'));
    //do2.join(o2.rmv('b'));
    //
    //std::cout << o1 << std::endl;
    //std::cout << o2 << std::endl;
    //crdts::AWORSet<char> o3 = join(o1, o2);
    //crdts::AWORSet<char> o4 = join(join(o1, do1), join(o2, do1));

    //std::cout << o3 << std::endl;
    //std::cout << o4 << std::endl;
    //std::cout << o3.in('c') << o3.in('b') << std::endl;
    //assert(o3.in('c') == true && o3.in('b') == true);
    //for (const auto& e : o1.read()) std::cout << e << " ";
    //std::cout << std::endl;
    //auto set_contents = o3.read();
    //auto search_value = 'c';
    //auto it = set_contents.find(search_value);
    //if (it != set_contents.end()) {
    //    std::cout << "Found: " << *it << std::endl;
    //}
    //else {
    //    std::cout << "Value not found" << std::endl;
    //}
    ////std::cout << o1.read() << std::endl;

    //crdts::AWORSet<string> o5("idz");
    //o5.add("hello");
    //o5.add("world");
    //o5.add("my");
    //cout << o5 << endl;
    //for (const auto& e : o5.read()) std::cout << e << " ";
    // 
    Key prefix("/namespace");
    HierarchicalAWORSet hSet(prefix);
    hSet.add("key", 'a');
    hSet.add("s/key", 'b');
    hSet.add("t/key", 'c');
    std::cout << "Read /namespace/key: ";
    for (const auto& e : hSet.read("key")) {
        std::cout << e << " ";
    }
    std::cout << std::endl;

    std::cout << "Read /namespace/s/key: ";
    for (const auto& e : hSet.read("s/key")) {
        std::cout << e << " ";
    }
    std::cout << std::endl;

    std::cout << "Read /namespace/t/key: ";
    for (const auto& e : hSet.read("t/key")) {
        std::cout << e << " ";
    }
    std::cout << std::endl;

    std::cout << hSet << std::endl;

    //EXPECT_STRCASEEQ((strNamespace + "/key").c_str(), );
    std::cout << "GetKey:" << crdtSet.KeyPrefix("key").GetKey().c_str() << std::endl;
    std::cout << "GetKey:" << crdtSet.ElemsPrefix("key").GetKey().c_str() << std::endl;
    std::cout << "GetKey:" << crdtSet.TombsPrefix("key").GetKey().c_str() << std::endl;
    std::cout << "GetKey:" << crdtSet.ValueKey("key").GetKey().c_str() << std::endl;
    std::cout << "GetKey:" << crdtSet.PriorityKey("key").GetKey().c_str() << std::endl;
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