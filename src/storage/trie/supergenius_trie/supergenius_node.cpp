

#include "storage/trie/supergenius_trie/supergenius_node.hpp"

namespace sgns::storage::trie {

  int BranchNode::getType() const {
    return static_cast<int>(value ? SuperGeniusNode::Type::BranchWithValue
                                  : SuperGeniusNode::Type::BranchEmptyValue);
  }

  uint16_t BranchNode::childrenBitmap() const {
    uint16_t bitmap = 0u;
    for (auto i = 0u; i < kMaxChildren; i++) {
      if (children.at(i)) {
        bitmap = bitmap | 1u << i;
      }
    }
    return bitmap;
  }

  uint8_t BranchNode::childrenNum() const {
    return std::count_if(children.begin(),
                         children.end(),
                         [](auto const &child) { return child; });
    ;
  }

  int LeafNode::getType() const {
    return static_cast<int>(SuperGeniusNode::Type::Leaf);
  }

}  // namespace sgns::storage::trie
