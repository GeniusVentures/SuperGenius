

#include "testutil/storage/base_fs_test.hpp"

#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include "storage/rocksdb/rocksdb.hpp"
#include "storage/database_error.hpp"
#include "testutil/outcome.hpp"

namespace fs = boost::filesystem;

namespace sgns::storage
{
    struct rocksdb_Open : public test::FSFixture
    {
        rocksdb_Open() : test::FSFixture( "supergenius_rocksdb_open" )
        {
        }
    };

  /**
   * @given options with disabled option `create_if_missing`
   * @when open database
   * @then database can not be opened (since there is no db already)
   */
  TEST_F(rocksdb_Open, OpenNonExistingDB) {
    rocksdb::Options options;
    options.create_if_missing = false;  // intentionally

    auto r = rocksdb::create(getPathString(), options);
    EXPECT_FALSE(r);
    EXPECT_EQ(r.error(), DatabaseError::INVALID_ARGUMENT);
  }

  /**
   * @given options with enable option `create_if_missing`
   * @when open database
   * @then database is opened
   */
  TEST_F(rocksdb_Open, OpenExistingDB) {
    rocksdb::Options options;
    options.create_if_missing = true;  // intentionally

    EXPECT_OUTCOME_TRUE_2(db, rocksdb::create(getPathString(), options));
    EXPECT_TRUE(db) << "db is nullptr";

    boost::filesystem::path p(getPathString());
    EXPECT_TRUE(fs::exists(p));
  }

  /**
   * @given options with enable option `create_if_missing`
   * @when query prefix database
   * @then return query result
   */
  TEST_F(rocksdb_Open, QueryDB) {
    rocksdb::Options options;
    options.create_if_missing = true;  // intentionally
    EXPECT_OUTCOME_TRUE_2(db, rocksdb::create(getPathString(), options));
    EXPECT_TRUE(db) << "db is nullptr";

    std::string strNamespacePrefix = "/namespace/k/";
    std::string strDataToSearch = "abc";

    std::vector<std::string> strDataList;
    strDataList.push_back("aaa");
    strDataList.push_back(strDataToSearch);
    strDataList.push_back("abb");
    strDataList.push_back("fgd");
    strDataList.push_back("ab");
    strDataList.push_back("bdw");
    strDataList.push_back("bqc");

    // Fill data
    const int numberOfDataset = 1000;
    Buffer key, value;
    for (int i = 0; i < numberOfDataset; ++i)
    {
      for (const auto& str : strDataList)
      {
        key.clear();
        key.put(strNamespacePrefix + str + std::to_string(i));
        value.clear();
        value.put(str + std::to_string(i));
        EXPECT_OUTCOME_TRUE_1(db->put(key, value));
      }
    }

    Buffer prefix;
    prefix.put(strNamespacePrefix + strDataToSearch);
    EXPECT_OUTCOME_TRUE(queryResult, db->query(prefix));

    EXPECT_TRUE(queryResult.size() == numberOfDataset);

  }
}
