

#include "storage/trie/impl/trie_storage_impl.hpp"

#include <gtest/gtest.h>
#include <boost/filesystem.hpp>

#include "storage/trie/impl/trie_storage_backend_impl.hpp"
#include "storage/trie/supergenius_trie/supergenius_trie_factory_impl.hpp"
#include "storage/trie/serialization/supergenius_codec.hpp"
#include "storage/trie/serialization/trie_serializer_impl.hpp"
#include "storage/rocksdb/rocksdb.hpp"
#include "outcome/outcome.hpp"
#include "testutil/outcome.hpp"
#include "testutil/literals.hpp"

namespace sgns::storage
{
  using sgns::base::Buffer;
  using sgns::storage::trie::SuperGeniusTrieFactoryImpl;
  using sgns::storage::trie::SuperGeniusCodec;
  using sgns::storage::trie::TrieSerializerImpl;
  using sgns::storage::trie::TrieStorageBackendImpl;
  using sgns::storage::trie::TrieStorageImpl;

  static Buffer kNodePrefix = "\1"_buf;

  /**
   * @given an empty persistent trie with rocksdb backend
   * @when putting a value into it @and its intance is destroyed @and a new
   * instance initialsed with the same DB
   * @then the new instance contains the same data
   */
  TEST(TriePersistencyTest, CreateDestroyCreate) {
    Buffer root;
    auto factory = std::make_shared<SuperGeniusTrieFactoryImpl>();
    auto codec = std::make_shared<SuperGeniusCodec>();
    {
      rocksdb::Options options;
      options.create_if_missing = true;  // intentionally
      EXPECT_OUTCOME_TRUE(
        rocks_db,
        rocksdb::create("supergenius_rocksdb_persistency_test", options));
      auto serializer = std::make_shared<TrieSerializerImpl>(
        factory,
        codec,
        std::make_shared<TrieStorageBackendImpl>(std::move(rocks_db),
          kNodePrefix));
      auto storage =
        TrieStorageImpl::createEmpty(factory, codec, serializer, boost::none)
        .value();

      auto batch = storage->getPersistentBatch().value();
      EXPECT_OUTCOME_TRUE_1(batch->put("123"_buf, "abc"_buf));
      EXPECT_OUTCOME_TRUE_1(batch->put("345"_buf, "def"_buf));
      EXPECT_OUTCOME_TRUE_1(batch->put("678"_buf, "xyz"_buf));
      EXPECT_OUTCOME_TRUE(root_, batch->commit());
      root = root_;
    }
    EXPECT_OUTCOME_TRUE(new_rocks_db,
      rocksdb::create("supergenius_rocksdb_persistency_test"));
    auto serializer = std::make_shared<TrieSerializerImpl>(
      factory,
      codec,
      std::make_shared<TrieStorageBackendImpl>(std::move(new_rocks_db),
        kNodePrefix));
    auto storage =
      TrieStorageImpl::createFromStorage(root, codec, serializer, boost::none)
      .value();
    auto batch = storage->getPersistentBatch().value();
    EXPECT_OUTCOME_TRUE(v1, batch->get("123"_buf));
    ASSERT_EQ(v1, "abc"_buf);
    EXPECT_OUTCOME_TRUE(v2, batch->get("345"_buf));
    ASSERT_EQ(v2, "def"_buf);
    EXPECT_OUTCOME_TRUE(v3, batch->get("678"_buf));
    ASSERT_EQ(v3, "xyz"_buf);

    //boost::filesystem::remove_all("supergenius_rocksdb_persistency_test");
  }
}