

#ifndef SUPERGENIUS_STORAGE_TRIE_IMPL_SUPERGENIUS_TRIE_IMPL
#define SUPERGENIUS_STORAGE_TRIE_IMPL_SUPERGENIUS_TRIE_IMPL

#include "storage/trie/supergenius_trie/supergenius_trie.hpp"

#include "storage/buffer_map_types.hpp"

namespace sgns::storage::trie {

  class SuperGeniusTrieImpl : public SuperGeniusTrie {
    // a child is obtained from the branch list of children as-is.
    // should be used when the trie is completely in memory
    inline static outcome::result<NodePtr> defaultChildRetrieveFunctor(
        const BranchPtr &parent, uint8_t idx) {
      return parent->children.at(idx);
    }

   public:
    using ChildRetrieveFunctor =
        std::function<outcome::result<NodePtr>(BranchPtr, uint8_t)>;

    enum class Error { INVALID_NODE_TYPE = 1 };

    /**
     * Creates an empty Trie
     * @param f a functor that will be used to obtain a child of a branch node
     * by its index. Most useful if Trie grows too big to occupy main memory and
     * is stored on an external storage
     */
    explicit SuperGeniusTrieImpl(
        ChildRetrieveFunctor f = defaultChildRetrieveFunctor);

    explicit SuperGeniusTrieImpl(
        NodePtr root, ChildRetrieveFunctor f = defaultChildRetrieveFunctor);

    NodePtr getRoot() const override;

    outcome::result<NodePtr> getNode(
        NodePtr parent, const KeyNibbles &key_nibbles) const override;

    outcome::result<std::list<std::pair<BranchPtr, uint8_t>>> getPath(
        NodePtr parent, const KeyNibbles &key_nibbles) const override;

    /**
     * Remove all entries, which key starts with the prefix
     */
    outcome::result<void> clearPrefix(const base::Buffer &prefix) override;

    // value will be copied
    outcome::result<void> put(const base::Buffer &key,
                              const base::Buffer &value) override;

    outcome::result<void> put(const base::Buffer &key,
                              base::Buffer &&value) override;

    outcome::result<void> remove(const base::Buffer &key) override;

    outcome::result<base::Buffer> get(
        const base::Buffer &key) const override;

    std::unique_ptr<BufferMapCursor> cursor() override;

    bool contains(const base::Buffer &key) const override;

    bool empty() const override;

   private:
    outcome::result<NodePtr> insert(const NodePtr &parent,
                                    const KeyNibbles &key_nibbles,
                                    NodePtr node);

    outcome::result<NodePtr> updateBranch(BranchPtr parent,
                                          const KeyNibbles &key_nibbles,
                                          const NodePtr &node);

    outcome::result<NodePtr> deleteNode(NodePtr parent,
                                        const KeyNibbles &key_nibbles);
    outcome::result<NodePtr> handleDeletion(const BranchPtr &parent,
                                            NodePtr node,
                                            const KeyNibbles &key_nibbles);
    // remove a node with its children
    outcome::result<NodePtr> detachNode(const NodePtr &parent,
                                        const KeyNibbles &prefix_nibbles);

    static uint32_t getCommonPrefixLength( const KeyNibbles &pref1, const KeyNibbles &pref2 );

    outcome::result<NodePtr> retrieveChild(BranchPtr parent,
                                           uint8_t idx) const override;

    ChildRetrieveFunctor retrieve_child_;
    NodePtr root_;
  };

}  // namespace sgns::storage::trie

OUTCOME_HPP_DECLARE_ERROR_2(sgns::storage::trie, SuperGeniusTrieImpl::Error);

#endif  // SUPERGENIUS_STORAGE_TRIE_IMPL_SUPERGENIUS_TRIE_IMPL
