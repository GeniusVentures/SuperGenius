

#include "storage/trie/supergenius_trie/supergenius_trie_factory_impl.hpp"

namespace sgns::storage::trie {

  std::unique_ptr<SuperGeniusTrie> SuperGeniusTrieFactoryImpl::createEmpty(
      ChildRetrieveFunctor f) const {
    return std::make_unique<SuperGeniusTrieImpl>(f);
  }

  std::unique_ptr<SuperGeniusTrie> SuperGeniusTrieFactoryImpl::createFromRoot(
      SuperGeniusTrie::NodePtr root,
      ChildRetrieveFunctor f) const {
    return std::make_unique<SuperGeniusTrieImpl>(std::move(root), std::move(f));
  }

}  // namespace sgns::storage::trie
