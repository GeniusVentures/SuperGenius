

#ifndef SUPERGENIUS_BASE_ROCKSDB_TEST_HPP
#define SUPERGENIUS_BASE_ROCKSDB_TEST_HPP

#include "testutil/storage/base_fs_test.hpp"

#include "storage/rocksdb/rocksdb.hpp"

namespace test {

  struct BaseRocksDB_Test : public BaseFS_Test {
    using rocksdb = sgns::storage::rocksdb;

    BaseRocksDB_Test(fs::path path);

    void open();

    void SetUp() override;

    void TearDown() override;

    std::shared_ptr<rocksdb> db_;
  };

}  // namespace test

#endif  // SUPERGENIUS_BASE_ROCKSDB_TEST_HPP
