

#ifndef SUPERGENIUS_STORAGE_TRIE_SERIALIZER_IMPL
#define SUPERGENIUS_STORAGE_TRIE_SERIALIZER_IMPL

#include "storage/trie/serialization/trie_serializer.hpp"

#include "storage/buffer_map_types.hpp"
#include "storage/trie/codec.hpp"
#include "storage/trie/supergenius_trie/supergenius_trie_factory.hpp"
#include "storage/trie/trie_storage_backend.hpp"

namespace sgns::storage::trie {

  class TrieSerializerImpl : public TrieSerializer {
   public:
    TrieSerializerImpl(std::shared_ptr<SuperGeniusTrieFactory> factory,
                       std::shared_ptr<Codec> codec,
                       std::shared_ptr<TrieStorageBackend> backend);
    ~TrieSerializerImpl() override = default;

    base::Buffer getEmptyRootHash() const override;

    outcome::result<base::Buffer> storeTrie(SuperGeniusTrie &trie) override;

    outcome::result<std::unique_ptr<SuperGeniusTrie>> retrieveTrie(
        const base::Buffer &db_key) const override;

   private:
    /**
     * Writes a node to a persistent storage, recursively storing its
     * descendants as well. Then replaces the node children to dummy nodes to
     * avoid memory waste
     */
    outcome::result<base::Buffer> storeRootNode(SuperGeniusNode &node);
    outcome::result<base::Buffer> storeNode(SuperGeniusNode &node,
                                              BufferBatch &batch);
    outcome::result<void> storeChildren(BranchNode &branch, BufferBatch &batch);
    /**
     * Fetches a node from the storage. A nullptr is returned in case that there
     * is no entry for provided key. Mind that a branch node will have dummy
     * nodes as its children
     */
    outcome::result<SuperGeniusTrie::NodePtr> retrieveNode(
        const base::Buffer &db_key) const;
    /**
     * Retrieves a node child, replacing a dummy node to an actual node if
     * needed
     */
    outcome::result<SuperGeniusTrie::NodePtr> retrieveChild(
        const SuperGeniusTrie::BranchPtr &parent, uint8_t idx) const;

    std::shared_ptr<SuperGeniusTrieFactory> trie_factory_;
    std::shared_ptr<Codec> codec_;
    std::shared_ptr<TrieStorageBackend> backend_;
  };
}  // namespace sgns::storage::trie

#endif // SUPERGENIUS_STORAGE_TRIE_SERIALIZER_IMPL
