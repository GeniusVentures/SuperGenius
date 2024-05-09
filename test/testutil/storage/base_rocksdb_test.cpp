

#include "testutil/storage/base_rocksdb_test.hpp"

namespace test {

    void RocksDBFixture::open()
    {
        rocksdb::Options options;
        options.create_if_missing = true;

        auto r = rocksdb::create( getPathString(), options );
        if ( !r )
        {
            throw std::invalid_argument( r.error().message() );
        }

        db_ = std::move( r.value() );
        ASSERT_TRUE( db_ ) << "BaseRocksDB_Test: db is nullptr";
    }

    RocksDBFixture::RocksDBFixture( fs::path path ) : FSFixture( std::move( path ) )
    {
    }

  void RocksDBFixture::SetUp()
  {
      open();
  }

  void RocksDBFixture::TearDown()
  {
      // clear();
  }
}  // namespace test
