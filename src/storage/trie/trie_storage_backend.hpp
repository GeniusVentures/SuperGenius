

#ifndef SUPERGENIUS_TRIE_DB_BACKEND_HPP
#define SUPERGENIUS_TRIE_DB_BACKEND_HPP

#include "outcome/outcome.hpp"

#include "storage/buffer_map_types.hpp"

namespace sgns::storage::trie {

  /**
   * Adapter for key-value storages that allows to hide keyspace separation
   * along with root hash storing logic from the trie db component
   */
  class TrieStorageBackend : public BufferStorage {
   public:
    ~TrieStorageBackend() override = default;
  };

}  // namespace sgns::storage::trie

#endif  // SUPERGENIUS_TRIE_DB_BACKEND_HPP
