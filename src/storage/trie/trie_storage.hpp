

#ifndef SUPERGENIUS_STORAGE_TRIE_TRIE_STORAGE
#define SUPERGENIUS_STORAGE_TRIE_TRIE_STORAGE

#include "base/blob.hpp"
#include "storage/trie/trie_batches.hpp"

namespace sgns::storage::trie {

  /**
   * Grants access to the storage in two ways:
   *  - persistent batch that will
   *    written back to the storage after commit() call
   *  - ephemeral batch, all changes to which are
   *    left in memory and thus the main storage is never changed by it
   */
  class TrieStorage {
   public:
    virtual ~TrieStorage() = default;

    virtual outcome::result<std::unique_ptr<PersistentTrieBatch>>
    getPersistentBatch() = 0;
    virtual outcome::result<std::unique_ptr<EphemeralTrieBatch>>
    getEphemeralBatch() const = 0;

    /**
     * Initializes a batch at the provided state
     * @warning if the batch is committed, the trie will still switch to its
     * state, creating a 'fork'
     */
    virtual outcome::result<std::unique_ptr<PersistentTrieBatch>>
    getPersistentBatchAt(const base::Hash256 &root) = 0;
    virtual outcome::result<std::unique_ptr<EphemeralTrieBatch>>
    getEphemeralBatchAt(const base::Hash256 &root) const = 0;

    /**
     * Root hash of the latest committed trie
     */
    virtual base::Buffer getRootHash() const = 0;
  };

}  // namespace sgns::storage::trie

#endif  // SUPERGENIUS_STORAGE_TRIE_TRIE_STORAGE
