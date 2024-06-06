

#ifndef SUPERGENIUS_STORAGE_TRIE_IMPL_SUPERGENIUS_TRIE_FACTORY
#define SUPERGENIUS_STORAGE_TRIE_IMPL_SUPERGENIUS_TRIE_FACTORY

#include "storage/trie/supergenius_trie/supergenius_trie.hpp"
#include "singleton/IComponent.hpp"

namespace sgns::storage::trie {

  class SuperGeniusTrieFactory : public IComponent {
   public:
    using ChildRetrieveFunctor =
        std::function<outcome::result<SuperGeniusTrie::NodePtr>(
            SuperGeniusTrie::BranchPtr, uint8_t)>;

   protected:
    static outcome::result<SuperGeniusTrie::NodePtr> defaultChildRetriever(
        const SuperGeniusTrie::BranchPtr& branch, uint8_t idx) {
      return branch->children.at(idx);
    };

   public:
    /**
     * Creates an empty trie
     * @param f functor that a trie uses to obtain a child of a branch. If
     * optional is none, the default one will be used
     */
    virtual std::unique_ptr<SuperGeniusTrie> createEmpty(
        ChildRetrieveFunctor f = defaultChildRetriever) const = 0;

    /**
     * Creates a trie with the given root
     * @param f functor that a trie uses to obtain a child of a branch. If
     * optional is none, the default one will be used
     */
    virtual std::unique_ptr<SuperGeniusTrie> createFromRoot(
        SuperGeniusTrie::NodePtr root,
        ChildRetrieveFunctor f = defaultChildRetriever) const = 0;

    virtual ~SuperGeniusTrieFactory() = default;
  };

}  // namespace sgns::storage::trie

#endif  // SUPERGENIUS_STORAGE_TRIE_IMPL_SUPERGENIUS_TRIE_FACTORY
