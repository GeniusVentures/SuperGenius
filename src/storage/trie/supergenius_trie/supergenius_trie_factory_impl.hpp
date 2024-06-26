

#ifndef SUPERGENIUS_SRC_STORAGE_TRIE_IMPL_SUPERGENIUS_TRIE_FACTORY_IMPL
#define SUPERGENIUS_SRC_STORAGE_TRIE_IMPL_SUPERGENIUS_TRIE_FACTORY_IMPL

#include "storage/trie/supergenius_trie/supergenius_trie_factory.hpp"
#include "storage/trie/supergenius_trie/supergenius_trie_impl.hpp"

namespace sgns::storage::trie {

  class SuperGeniusTrieFactoryImpl : public SuperGeniusTrieFactory {
   public:

    std::unique_ptr<SuperGeniusTrie> createEmpty(
        ChildRetrieveFunctor f) const override;
    std::unique_ptr<SuperGeniusTrie> createFromRoot(
        SuperGeniusTrie::NodePtr root,
        ChildRetrieveFunctor f) const override;

    std::string GetName() override
    {
      return "SuperGeniusTrieFactoryImpl";
    }

   private:
    SuperGeniusTrieImpl::ChildRetrieveFunctor default_child_retrieve_f_;
  };

}  // namespace sgns::storage::trie

#endif  // SUPERGENIUS_SRC_STORAGE_TRIE_IMPL_SUPERGENIUS_TRIE_FACTORY_IMPL
