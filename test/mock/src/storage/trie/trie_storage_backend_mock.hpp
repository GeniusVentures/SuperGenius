

#ifndef SUPERGENIUS_TRIE_STORAGE_BACKEND_MOCK_HPP
#define SUPERGENIUS_TRIE_STORAGE_BACKEND_MOCK_HPP

#include <gmock/gmock.h>

#include "storage/trie/trie_storage_backend.hpp"

namespace sgns::storage::trie {

  class TrieStorageBackendMock : public TrieStorageBackend {
   public:
    MOCK_METHOD0(batch, std::unique_ptr<face::WriteBatch<Buffer, Buffer>>());
    MOCK_METHOD0(cursor, std::unique_ptr<face::MapCursor<Buffer, Buffer>>());
    MOCK_CONST_METHOD1(get, outcome::result<Buffer>(const Buffer &key));
    MOCK_CONST_METHOD1(contains, bool (const Buffer &key));
    MOCK_METHOD2(put, outcome::result<void> (const Buffer &key, const Buffer &value));
    outcome::result<void> put(const base::Buffer &k, base::Buffer &&v) {
      return put_rvalueHack(k, std::move(v));
    }
    MOCK_METHOD2(put_rvalueHack,
                 outcome::result<void>(const base::Buffer &, base::Buffer));
    MOCK_METHOD1(remove, outcome::result<void> (const Buffer &key));
  };

}

#endif  // SUPERGENIUS_TRIE_DB_BACKEND_MOCK_HPP
