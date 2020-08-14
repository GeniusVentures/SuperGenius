

#ifndef SUPERGENIUS_TEST_MOCK_CORE_STORAGE_TRIE_TRIE_BATCHES_MOCK
#define SUPERGENIUS_TEST_MOCK_CORE_STORAGE_TRIE_TRIE_BATCHES_MOCK

#include <gmock/gmock.h>

#include "storage/trie/trie_batches.hpp"

namespace sgns::storage::trie {

  class PersistentTrieBatchMock : public PersistentTrieBatch {
   public:
    MOCK_CONST_METHOD1(get,
                       outcome::result<base::Buffer>(const base::Buffer &));

    MOCK_METHOD0(cursor, std::unique_ptr<BufferMapCursor>());

    MOCK_CONST_METHOD1(contains, bool(const base::Buffer &));

    MOCK_METHOD2(put,
                 outcome::result<void>(const base::Buffer &,
                                       const base::Buffer &));

    outcome::result<void> put(const base::Buffer &k, base::Buffer &&v) {
      return put_rvalueHack(k, std::move(v));
    }
    MOCK_METHOD2(put_rvalueHack,
                 outcome::result<void>(const base::Buffer &, base::Buffer));

    MOCK_METHOD1(remove, outcome::result<void>(const base::Buffer &));

    MOCK_CONST_METHOD0(calculateRoot, outcome::result<base::Buffer>());

    MOCK_METHOD1(clearPrefix, outcome::result<void>(const base::Buffer &buf));

    MOCK_CONST_METHOD0(empty, bool());

    MOCK_METHOD0(commit, outcome::result<Buffer>());

    MOCK_METHOD0(batchOnTop, std::unique_ptr<TopperTrieBatch>());
  };

  class EphemeralTrieBatchMock : public EphemeralTrieBatch {
   public:
    MOCK_CONST_METHOD1(get,
                       outcome::result<base::Buffer>(const base::Buffer &));

    MOCK_METHOD0(cursor, std::unique_ptr<BufferMapCursor>());

    MOCK_CONST_METHOD1(contains, bool(const base::Buffer &));

    MOCK_METHOD2(put,
                 outcome::result<void>(const base::Buffer &,
                                       const base::Buffer &));

    outcome::result<void> put(const base::Buffer &k, base::Buffer &&v) {
      return put_rvalueHack(k, std::move(v));
    }
    MOCK_METHOD2(put_rvalueHack,
                 outcome::result<void>(const base::Buffer &, base::Buffer));

    MOCK_METHOD1(remove, outcome::result<void>(const base::Buffer &));

    MOCK_METHOD1(clearPrefix, outcome::result<void>(const base::Buffer &buf));

    MOCK_CONST_METHOD0(empty, bool());
  };

}  // namespace sgns::storage::trie

#endif  // SUPERGENIUS_TEST_MOCK_CORE_STORAGE_TRIE_TRIE_BATCHES_MOCK
