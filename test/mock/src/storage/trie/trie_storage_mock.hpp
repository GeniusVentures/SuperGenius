

#ifndef SUPERGENIUS_TEST_MOCK_SRC_STORAGE_TRIE_TRIE_STORAGE_MOCK
#define SUPERGENIUS_TEST_MOCK_SRC_STORAGE_TRIE_TRIE_STORAGE_MOCK

#include <gmock/gmock.h>

#include "storage/trie/trie_storage.hpp"

namespace sgns::storage::trie {

  class TrieStorageMock : public TrieStorage {
   public:
    MOCK_METHOD0(getPersistentBatch,
                 outcome::result<std::unique_ptr<PersistentTrieBatch>>());
    MOCK_CONST_METHOD0(getEphemeralBatch,
                       outcome::result<std::unique_ptr<EphemeralTrieBatch>>());

    MOCK_METHOD1(getPersistentBatchAt,
                 outcome::result<std::unique_ptr<PersistentTrieBatch>>(
                     const base::Hash256 &root));
    MOCK_CONST_METHOD1(getEphemeralBatchAt,
                       outcome::result<std::unique_ptr<EphemeralTrieBatch>>(
                           const base::Hash256 &root));

    MOCK_CONST_METHOD0(getRootHash, base::Buffer());
  };

}  // namespace sgns::storage::trie

#endif  // SUPERGENIUS_TEST_MOCK_SRC_STORAGE_TRIE_TRIE_DB_MOCK_HPP
