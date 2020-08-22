

#include <gtest/gtest.h>

#include "storage/changes_trie/impl/storage_changes_tracker_impl.hpp"

#include "mock/src/blockchain/block_header_repository_mock.hpp"
#include "scale/scale.hpp"
#include "storage/in_memory/in_memory_storage.hpp"
#include "storage/trie/impl/persistent_trie_batch_impl.hpp"
#include "storage/trie/impl/trie_storage_backend_impl.hpp"
#include "storage/trie/supergenius_trie/supergenius_trie_factory_impl.hpp"
#include "storage/trie/serialization/supergenius_codec.hpp"
#include "storage/trie/serialization/trie_serializer_impl.hpp"
#include "testutil/literals.hpp"
#include "testutil/outcome.hpp"

using sgns::blockchain::BlockHeaderRepositoryMock;
using sgns::base::Buffer;
using sgns::primitives::ExtrinsicIndex;
using sgns::storage::InMemoryStorage;
using sgns::storage::changes_trie::ChangesTracker;
using sgns::storage::changes_trie::StorageChangesTrackerImpl;
using sgns::storage::trie::PersistentTrieBatch;
using sgns::storage::trie::PersistentTrieBatchImpl;
using sgns::storage::trie::SuperGeniusCodec;
using sgns::storage::trie::SuperGeniusTrieFactoryImpl;
using sgns::storage::trie::TrieSerializerImpl;
using sgns::storage::trie::TrieStorageBackendImpl;
namespace scale = sgns::scale;
using testing::_;
using testing::AnyOf;
using testing::Return;
  std::ostream &operator<<(std::ostream &s,
                           const outcome::result<sgns::primitives::BlockHeader> &data) {
    return s;
  }
  std::ostream &operator<<(std::ostream &s,
                           const outcome::result<sgns::base::Blob<32>> &data) {
    return s;
  }
  std::ostream &operator<<(std::ostream &s,
                           const outcome::result<sgns::blockchain::BlockStatus> &data) {
    return s;
  }
  std::ostream &operator<<(std::ostream &s,
                           const outcome::result<unsigned __int64> &data) {
    return s;
  }
/**
 * @given storage batch with pending changes
 * @when initialize a changes trie with the changes
 * @then changes are passed to the trie successfully
 */
TEST(ChangesTrieTest, IntegrationWithOverlay) {
  // GIVEN
  auto factory = std::make_shared<SuperGeniusTrieFactoryImpl>();
  auto codec = std::make_shared<SuperGeniusCodec>();
  auto backend = std::make_shared<TrieStorageBackendImpl>(
      std::make_shared<InMemoryStorage>(), Buffer{});
  auto serializer =
      std::make_shared<TrieSerializerImpl>(factory, codec, backend);
  std::shared_ptr<ChangesTracker> changes_tracker =
      std::make_shared<StorageChangesTrackerImpl>(factory, codec);
  EXPECT_OUTCOME_TRUE_1(changes_tracker->onBlockChange("aaa"_hash256, 42));
  auto batch = std::make_shared<PersistentTrieBatchImpl>(
      codec,
      serializer,
      boost::make_optional(changes_tracker),
      factory->createEmpty(
          [](auto branch, auto idx) { return branch->children.at(idx); }),
      [](auto &buf) {});

  EXPECT_OUTCOME_TRUE_1(
      batch->put(":extrinsic_index"_buf, Buffer{scale::encode(42).value()}));
  EXPECT_OUTCOME_TRUE_1(batch->put("abc"_buf, "123"_buf));
  EXPECT_OUTCOME_TRUE_1(batch->put("cde"_buf, "345"_buf));

  auto repo = std::make_shared<BlockHeaderRepositoryMock>();
  EXPECT_CALL(*repo, getNumberByHash(_)).WillRepeatedly(Return(42));

//  // WHEN
//  EXPECT_CALL(changes_trie_builder, startNewTrie("aaa"_hash256))
//      .WillOnce(Return(std::ref(changes_trie_builder)));
//  ON_CALL(changes_trie_builder, insertExtrinsicsChange(_, _))
//      .WillByDefault(testing::Invoke([](auto k, auto v) {
//        ;
//        return outcome::success();
//      }));
//  EXPECT_CALL(changes_trie_builder,
//              insertExtrinsicsChange(
//                  AnyOf(":extrinsic_index"_buf, "abc"_buf, "cde"_buf),
//                  AnyOf(std::vector<ExtrinsicIndex>{42},
//                        std::vector<ExtrinsicIndex>{0xffffff})))
//      .WillRepeatedly(Return(outcome::success()));
  EXPECT_OUTCOME_TRUE_1(
      changes_tracker->constructChangesTrie("aaa"_hash256, {}));
  // THEN SUCCESS
}
