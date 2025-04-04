

#ifndef SUPERGENIUS_STORAGE_TRIE_IMPL_TRIE_BATCH
#define SUPERGENIUS_STORAGE_TRIE_IMPL_TRIE_BATCH

#include "storage/buffer_map_types.hpp"

namespace sgns::storage::trie {

  class TrieBatch : public face::Readable<Buffer, Buffer>,
                    public face::Writeable<Buffer, Buffer>,
                    public face::Iterable<Buffer, Buffer> {
   public:
    ~TrieBatch() override = default;

    /**
     * Remove all trie entries which key begins with the supplied prefix
     */
    virtual outcome::result<void> clearPrefix(const Buffer &prefix) = 0;
  };

  class TopperTrieBatch;

  /**
   * A batch that grants access to the persistent trie storage.
   * All changes are contained in memory until commit() is called.
   */
  class PersistentTrieBatch
      : public TrieBatch,
        public std::enable_shared_from_this<PersistentTrieBatch> {
   public:
    /**
     * Commits changes to a persistent storage
     * @returns the root of the committed trie
     */
    virtual outcome::result<Buffer> commit() = 0;

    /**
     * Creates a batch on top of this batch
     */
    virtual std::unique_ptr<TopperTrieBatch> batchOnTop() = 0;
  };

  /**
   * A temporary in-memory trie built on top of a persistent one
   * All changes to it are simply discarded when the batch is destroyed
   */
  class EphemeralTrieBatch : public TrieBatch {};

  /**
   * A batch on top of another batch
   * Used for small amount of atomic changes, like applying an extrinsic
   */
  class TopperTrieBatch : public TrieBatch {
   public:
    /**
     * Writes changes to the parent batch
     */
    virtual outcome::result<void> writeBack() = 0;
  };

}  // namespace sgns::storage::trie

#endif  // SUPERGENIUS_STORAGE_TRIE_IMPL_TRIE_BATCH
