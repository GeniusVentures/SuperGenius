

#ifndef SUPERGENIUS_STORAGE_SUPERGENIUS_TRIE_SERIALIZER
#define SUPERGENIUS_STORAGE_SUPERGENIUS_TRIE_SERIALIZER

#include "outcome/outcome.hpp"
#include "storage/trie/supergenius_trie/supergenius_trie.hpp"
#include "singleton/IComponent.hpp"

namespace sgns::storage::trie {

  /**
   * Serializes SuperGeniusTrie and stores it in an external storage
   */
  class TrieSerializer : public IComponent{
   public:
       ~TrieSerializer() override = default;

    /**
     * @return root hash of an empty trie
     */
    virtual base::Buffer getEmptyRootHash() const = 0;

    /**
     * Writes a trie to a storage, recursively storing its
     * nodes.
     */
    virtual outcome::result<base::Buffer> storeTrie(SuperGeniusTrie &trie) = 0;

    /**
     * Fetches a trie from the storage. A nullptr is returned in case that there
     * is no entry for provided key.
     */
    virtual outcome::result<std::unique_ptr<SuperGeniusTrie>> retrieveTrie(
        const base::Buffer &db_key) const = 0;
  };

}  // namespace sgns::storage::trie

#endif  // SUPERGENIUS_STORAGE_SUPERGENIUS_TRIE_SERIALIZER
