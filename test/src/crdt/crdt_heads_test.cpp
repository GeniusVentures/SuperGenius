#include <crdt/crdt_heads.hpp>
#include <gtest/gtest.h>
#include <storage/leveldb/leveldb.hpp>
#include <outcome/outcome.hpp>
#include <testutil/outcome.hpp>
#include <testutil/literals.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/hex.hpp>
#include <libp2p/multi/multihash.hpp>

namespace sgns::crdt
{
  using sgns::storage::LevelDB;
  using sgns::base::Buffer;
  using libp2p::multi::HashType;
  using libp2p::multi::Multihash;
  namespace fs = boost::filesystem;
  

  TEST(CrdtHeadsTest, TestHeadsAdd)
  {
    const std::string strNamespace = "/namespace";
    const auto hKey = HierarchicalKey(strNamespace);
    const uint64_t height1 = 10;
    const uint64_t height2 = 11;

    const CID cid1 = CID(CID::Version::V1, CID::Multicodec::SHA2_256,
      Multihash::create(HashType::sha256, "0123456789ABCDEF0123456789ABCDEF"_unhex).value());

    const CID cid2 = CID(CID::Version::V1, CID::Multicodec::SHA2_256,
      Multihash::create(HashType::sha256, "1123456789ABCDEF0123456789ABCDEF"_unhex).value());

    // Remove leftover database 
    std::string databasePath = "supergenius_crdt_heads_test_set_value";
    fs::remove_all(databasePath);

    // Create new database
    leveldb::Options options;
    options.create_if_missing = true;  // intentionally
    auto dataStoreResult = LevelDB::create(databasePath, options);
    auto dataStore = dataStoreResult.value();

    // Create CrdtHead
    CrdtHeads crdtHeads(dataStore, hKey);

    auto cid1ToStringResult = cid1.toString();
    auto hkeyTest = hKey.ChildString(cid1ToStringResult.value());

    EXPECT_OUTCOME_TRUE(hKeyCID, crdtHeads.GetKey(cid1));
    EXPECT_TRUE(hKeyCID == hkeyTest);

    auto cid2ToStringResult = cid2.toString();
    EXPECT_STRNE(cid1ToStringResult.value().c_str(), cid2ToStringResult.value().c_str());

    // Test Add cid function
    EXPECT_OUTCOME_TRUE_1(crdtHeads.Add(cid1, height1));

    // Test if cis is head 
    EXPECT_OUTCOME_EQ(crdtHeads.IsHead(cid1), true);
    EXPECT_OUTCOME_EQ(crdtHeads.IsHead(cid2), false);
    
    // Check cid height
    EXPECT_OUTCOME_EQ(crdtHeads.GetHeadHeight(cid1), height1);
    EXPECT_OUTCOME_FALSE_1(crdtHeads.GetHeadHeight(cid2));

    EXPECT_OUTCOME_EQ(crdtHeads.GetLenght(), 1);

    // Add same cid again, should remain the same length
    EXPECT_OUTCOME_TRUE_1(crdtHeads.Add(cid1, height1));
    EXPECT_OUTCOME_EQ(crdtHeads.GetLenght(), 1);

    // Add more cid
    EXPECT_OUTCOME_TRUE_1(crdtHeads.Add(cid2, height2));
    EXPECT_OUTCOME_EQ(crdtHeads.GetLenght(), 2);

    // Test getting heads 
    std::vector<CID> heads; 
    uint64_t maxHeight = 0;
    EXPECT_OUTCOME_TRUE_1(crdtHeads.GetList(heads, maxHeight));
    EXPECT_TRUE(heads.size() == 2);
    EXPECT_TRUE(maxHeight == height2);
  }

  TEST(CrdtHeadsTest, TestHeadsReplace)
  {
    const std::string strNamespace = "/namespace";
    const auto hKey = HierarchicalKey(strNamespace);
    const uint64_t height1 = 10;
    const uint64_t height2 = 11;

    const CID cid1 = CID(CID::Version::V1, CID::Multicodec::SHA2_256,
      Multihash::create(HashType::sha256, "0123456789ABCDEF0123456789ABCDEF"_unhex).value());

    const CID cid2 = CID(CID::Version::V1, CID::Multicodec::SHA2_256,
      Multihash::create(HashType::sha256, "1123456789ABCDEF0123456789ABCDEF"_unhex).value());

    const CID cid3 = CID(CID::Version::V1, CID::Multicodec::SHA2_256,
      Multihash::create(HashType::sha256, "2123456789ABCDEF0123456789ABCDEF"_unhex).value());

    // Remove leftover database 
    std::string databasePath = "supergenius_crdt_heads_test_set_value";
    fs::remove_all(databasePath);

    // Create new database
    leveldb::Options options;
    options.create_if_missing = true;  // intentionally
    auto dataStoreResult = LevelDB::create(databasePath, options);
    auto dataStore = dataStoreResult.value();

    // Create CrdtHead
    CrdtHeads crdtHeads(dataStore, hKey);
    auto addResult1 = crdtHeads.Add(cid1, height1);
    auto addResult2 = crdtHeads.Add(cid2, height2);

    EXPECT_OUTCOME_EQ(crdtHeads.GetLenght(), 2);
    EXPECT_OUTCOME_TRUE_1(crdtHeads.Replace(cid2, cid3, height2));
    EXPECT_OUTCOME_EQ(crdtHeads.IsHead(cid2), false);
    EXPECT_OUTCOME_EQ(crdtHeads.IsHead(cid3), true);
    EXPECT_OUTCOME_EQ(crdtHeads.GetHeadHeight(cid3), height2);
    EXPECT_OUTCOME_EQ(crdtHeads.GetLenght(), 2);

    EXPECT_OUTCOME_TRUE_1(crdtHeads.Replace(cid3, cid3, height1));
    EXPECT_OUTCOME_EQ(crdtHeads.GetHeadHeight(cid3), height1);
    EXPECT_OUTCOME_EQ(crdtHeads.GetLenght(), 2);

    // Test getting heads 
    std::vector<CID> heads;
    uint64_t maxHeight = 0;
    EXPECT_OUTCOME_TRUE_1(crdtHeads.GetList(heads, maxHeight));
    EXPECT_TRUE(heads.size() == 2);
    EXPECT_TRUE(maxHeight == height1);

  }
}