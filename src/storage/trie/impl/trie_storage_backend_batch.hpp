

#ifndef SUPERGENIUS_STORAGE_TRIE_IMPL_TRIE_STORAGE_BACKEND_BATCH
#define SUPERGENIUS_STORAGE_TRIE_IMPL_TRIE_STORAGE_BACKEND_BATCH

#include "base/buffer.hpp"
#include "storage/face/write_batch.hpp"

namespace sgns::storage::trie {

  /**
   * Batch implementation for TrieStorageBackend
   * @see TrieStorageBackend
   */
  class TrieStorageBackendBatch
      : public face::WriteBatch<base::Buffer, base::Buffer> {
   public:
    TrieStorageBackendBatch(
        std::unique_ptr<face::WriteBatch<base::Buffer, base::Buffer>>
            storage_batch,
        base::Buffer node_prefix);
    ~TrieStorageBackendBatch() override = default;

    outcome::result<void> commit() override;

    outcome::result<void> put(const base::Buffer &key,
                              const base::Buffer &value) override;

    outcome::result<void> put(const base::Buffer &key,
                              base::Buffer &&value) override;

    outcome::result<void> remove(const base::Buffer &key) override;
    void clear() override;

   private:
    base::Buffer prefixKey(const base::Buffer &key) const;

    std::unique_ptr<face::WriteBatch<base::Buffer, base::Buffer>>
        storage_batch_;
    base::Buffer node_prefix_;
  };

}  // namespace sgns::storage::trie

#endif  // SUPERGENIUS_STORAGE_TRIE_IMPL_TRIE_STORAGE_BACKEND_BATCH
