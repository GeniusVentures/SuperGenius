
#ifndef SUPERGENIUS_TEST_MOCK_SRC_RUNTIME_TRIE_STORAGE_PROVIDER_MOCK
#define SUPERGENIUS_TEST_MOCK_SRC_RUNTIME_TRIE_STORAGE_PROVIDER_MOCK

#include "runtime/trie_storage_provider.hpp"

#include <gmock/gmock.h>

namespace sgns::runtime {

  class TrieStorageProviderMock: public TrieStorageProvider {
   public:
    MOCK_METHOD0(setToEphemeral, outcome::result<void>());
    MOCK_METHOD1(setToEphemeralAt, outcome::result<void>(const base::Hash256 &));
    MOCK_METHOD0(setToPersistent, outcome::result<void>());
    MOCK_METHOD1(setToPersistentAt, outcome::result<void>(const base::Hash256 &));
    MOCK_CONST_METHOD0(getCurrentBatch, std::shared_ptr<Batch>());
    MOCK_CONST_METHOD0(tryGetPersistentBatch, boost::optional<std::shared_ptr<PersistentBatch>>());
    MOCK_CONST_METHOD0(isCurrentlyPersistent, bool());
    MOCK_METHOD0(forceCommit, outcome::result<base::Buffer>());
  };

}

#endif  // SUPERGENIUS_TEST_MOCK_SRC_RUNTIME_TRIE_STORAGE_PROVIDER_MOCK
