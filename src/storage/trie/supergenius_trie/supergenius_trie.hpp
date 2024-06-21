

#ifndef SUPERGENIUS_STORAGE_TRIE_SUPERGENIUS_TRIE_HPP
#define SUPERGENIUS_STORAGE_TRIE_SUPERGENIUS_TRIE_HPP

#include "storage/face/generic_maps.hpp"
#include "storage/trie/supergenius_trie/supergenius_node.hpp"
#include <list>

namespace sgns::storage::trie {

  /**
   * For specification see SuperGenius Runtime Environment Protocol Specification
   * '2.1.2 The General Tree Structure' and further
   */
  class SuperGeniusTrie : public face::GenericMap<base::Buffer, base::Buffer> {
   public:
    using NodePtr = std::shared_ptr<SuperGeniusNode>;
    using BranchPtr = std::shared_ptr<BranchNode>;

    /**
     * Remove all trie entries which key begins with the supplied prefix
     */
    virtual outcome::result<void> clearPrefix(const base::Buffer &prefix) = 0;

    /**
     * @return the root node of the trie
     */
    virtual NodePtr getRoot() const = 0;

    /**
     * @returns a child node pointer of a provided \arg parent node
     * at the index \idx
     */
    virtual outcome::result<NodePtr> retrieveChild(BranchPtr parent,
                                                   uint8_t idx) const = 0;

    // TODO(Harrm) Make key nibbles type distinguishable with just key
    /**
     * @returns a node which is a descendant of \arg parent found by following
     * \arg key_nibbles
     */
    virtual outcome::result<NodePtr> getNode(
        NodePtr parent, const KeyNibbles &key_nibbles) const = 0;

    /**
     * @returns a sequence of nodes in between \arg parent and the node found by
     * following \arg key_nibbles. The parent is included, the end node isn't.
     */
    virtual outcome::result<std::list<std::pair<BranchPtr, uint8_t>>> getPath(
        NodePtr parent, const KeyNibbles &key_nibbles) const = 0;
  };

}  // namespace sgns::storage::trie

#endif  // SUPERGENIUS_STORAGE_TRIE_SUPERGENIUS_TRIE_HPP
