

#include "storage/trie/impl/trie_storage_backend_batch.hpp"

namespace sgns::storage::trie {

  TrieStorageBackendBatch::TrieStorageBackendBatch(
      std::unique_ptr<face::WriteBatch<base::Buffer, base::Buffer>>
          storage_batch,
      base::Buffer node_prefix)
      : storage_batch_{std::move(storage_batch)},
        node_prefix_{std::move(node_prefix)} {
    BOOST_ASSERT(storage_batch_ != nullptr);
  }

  outcome::result<void> TrieStorageBackendBatch::commit() {
    return storage_batch_->commit();
  }

  void TrieStorageBackendBatch::clear() {
    storage_batch_->clear();
  }

  outcome::result<void> TrieStorageBackendBatch::put(
      const base::Buffer &key, const base::Buffer &value) {
    return storage_batch_->put(prefixKey(key), value);
  }

  outcome::result<void> TrieStorageBackendBatch::put(const base::Buffer &key,
                                                     base::Buffer &&value) {
    return storage_batch_->put(prefixKey(key), std::move(value));
  }

  outcome::result<void> TrieStorageBackendBatch::remove(
      const base::Buffer &key) {
    return storage_batch_->remove(prefixKey(key));
  }

  base::Buffer TrieStorageBackendBatch::prefixKey(
      const base::Buffer &key) const {
    auto prefixed_key = node_prefix_;
    prefixed_key.put(key);
    return prefixed_key;
  }

}  // namespace sgns::storage::trie
