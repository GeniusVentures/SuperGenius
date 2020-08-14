

#ifndef SUPERGENIUS_BASE_LEVELDB_TEST_HPP
#define SUPERGENIUS_BASE_LEVELDB_TEST_HPP

#include "testutil/storage/base_fs_test.hpp"

#include "storage/leveldb/leveldb.hpp"

namespace test {

  struct BaseLevelDB_Test : public BaseFS_Test {
    using LevelDB = sgns::storage::LevelDB;

    BaseLevelDB_Test(fs::path path);

    void open();

    void SetUp() override;

    void TearDown() override;

    std::shared_ptr<LevelDB> db_;
  };

}  // namespace test

#endif  // SUPERGENIUS_BASE_LEVELDB_TEST_HPP
