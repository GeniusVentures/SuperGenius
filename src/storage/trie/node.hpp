

#ifndef SUPERGENIUS_NODE_HPP
#define SUPERGENIUS_NODE_HPP

namespace sgns::storage::trie {

  struct Node {
    virtual ~Node() = default;

    // returns type of a node
    virtual int getType() const = 0;
  };

}  // namespace sgns::storage::trie

#endif  // SUPERGENIUS_NODE_HPP
