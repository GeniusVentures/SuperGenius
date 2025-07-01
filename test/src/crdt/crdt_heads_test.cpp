#include "crdt/crdt_heads.hpp"
#include <gtest/gtest.h>
#include <storage/rocksdb/rocksdb.hpp>
#include "outcome/outcome.hpp"
#include <testutil/outcome.hpp>
#include <testutil/literals.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/hex.hpp>
#include <libp2p/multi/multihash.hpp>

namespace sgns::crdt
{
  using sgns::storage::rocksdb;
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
    std::string databasePath = "supergenius_crdt_heads_test_set_value_add";
    fs::remove_all(databasePath);

    // Create new database
    rocksdb::Options options;
    options.create_if_missing = true;  // intentionally

    auto dataStoreResult = rocksdb::create(databasePath, options);
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
    EXPECT_OUTCOME_TRUE_1(crdtHeads.Add(cid1, height1, ""));

    // Test if cis is head
    EXPECT_EQ(crdtHeads.IsHead(cid1), true);
    EXPECT_EQ(crdtHeads.IsHead(cid2), false);

    // Check cid height
    EXPECT_OUTCOME_EQ(crdtHeads.GetHeadHeight(cid1), height1);
    EXPECT_OUTCOME_EQ(crdtHeads.GetHeadHeight(cid2), 0);

    EXPECT_OUTCOME_EQ(crdtHeads.GetLength(), 1);

    // Add same cid again, should remain the same length
    EXPECT_OUTCOME_TRUE_1(crdtHeads.Add(cid1, height1, ""));
    EXPECT_OUTCOME_EQ(crdtHeads.GetLength(), 1);

    // Add more cid
    EXPECT_OUTCOME_TRUE_1(crdtHeads.Add(cid2, height2, ""));
    EXPECT_OUTCOME_EQ(crdtHeads.GetLength(), 2);

    // Test getting heads
    auto getListResult = crdtHeads.GetList();
    EXPECT_OUTCOME_TRUE_1(getListResult);

    auto [head_map, maxHeight] = getListResult.value();
    uint64_t heads_size = 0;
    for ( const auto &[topic_name, cid_set] : head_map )
    {
        heads_size += cid_set.size();
    }
    
    EXPECT_TRUE(heads_size == 2);
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
    std::string databasePath = "supergenius_crdt_heads_test_set_value_replace";
    fs::remove_all(databasePath);

    // Create new database
    rocksdb::Options options;
    options.create_if_missing = true;  // intentionally

    auto dataStoreResult = rocksdb::create(databasePath, options);
    auto dataStore = dataStoreResult.value();

    // Create CrdtHead
    CrdtHeads crdtHeads(dataStore, hKey);
    auto addResult1 = crdtHeads.Add(cid1, height1, "");
    auto addResult2 = crdtHeads.Add(cid2, height2, "");

    EXPECT_OUTCOME_EQ(crdtHeads.GetLength(), 2);
    EXPECT_OUTCOME_TRUE_1(crdtHeads.Replace(cid2, cid3, height2, ""));
    EXPECT_EQ(crdtHeads.IsHead(cid2), false);
    EXPECT_EQ(crdtHeads.IsHead(cid3), true);
    EXPECT_OUTCOME_EQ(crdtHeads.GetHeadHeight(cid3), height2);
    EXPECT_OUTCOME_EQ(crdtHeads.GetLength(), 2);

    EXPECT_OUTCOME_TRUE_1(crdtHeads.Replace(cid3, cid3, height1, ""));
    EXPECT_OUTCOME_EQ(crdtHeads.GetHeadHeight(cid3), height1);
    EXPECT_OUTCOME_EQ(crdtHeads.GetLength(), 2);

    // Test getting heads
    auto getListResult = crdtHeads.GetList();
    EXPECT_OUTCOME_TRUE_1(getListResult);

    auto [head_map, maxHeight] = getListResult.value();
    uint64_t heads_size = 0;
    for ( const auto &[topic_name, cid_set] : head_map )
    {
        heads_size += cid_set.size();
    }
    
    EXPECT_TRUE(heads_size == 2);
    EXPECT_TRUE(maxHeight == height1);

  }
}
