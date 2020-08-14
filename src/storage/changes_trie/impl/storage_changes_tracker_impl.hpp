#ifndef SUPERGENIUS_STORAGE_CHANGES_TRIE_STORAGE_CHANGES_TRACKER_IMPL
#define SUPERGENIUS_STORAGE_CHANGES_TRIE_STORAGE_CHANGES_TRACKER_IMPL

#include "storage/changes_trie/changes_tracker.hpp"

//-------------//
#include <set>
#include <map>

namespace sgns::storage::trie {
  class Codec;
  class SuperGeniusTrieFactory;
}  // namespace sgns::storage::trie

namespace sgns::storage::changes_trie {

  class StorageChangesTrackerImpl : public ChangesTracker {
   public:
    enum class Error {
      EXTRINSIC_IDX_GETTER_UNINITIALIZED = 1,
      INVALID_PARENT_HASH
    };

    StorageChangesTrackerImpl(
        std::shared_ptr<storage::trie::SuperGeniusTrieFactory> trie_factory,
        std::shared_ptr<storage::trie::Codec> codec);

    /**
     * Functor that returns the current extrinsic index, which is supposed to
     * be stored in the state trie
     */
    ~StorageChangesTrackerImpl() override = default;

    void setExtrinsicIdxGetter(GetExtrinsicIndexDelegate f) override;

    outcome::result<void> onBlockChange(
        primitives::BlockHash new_parent_hash,
        primitives::BlockNumber new_parent_number) override;

    outcome::result<void> onPut(const base::Buffer &key,
                                bool new_entry) override;
    outcome::result<void> onRemove(const base::Buffer &key) override;

    outcome::result<base::Hash256> constructChangesTrie(
        const primitives::BlockHash &parent,
        const ChangesTrieConfig &conf) override;

   private:
    std::shared_ptr<storage::trie::SuperGeniusTrieFactory> trie_factory_;
    std::shared_ptr<storage::trie::Codec> codec_;

    std::map<base::Buffer, std::vector<primitives::ExtrinsicIndex>>
        extrinsics_changes_;
    std::set<base::Buffer>
        new_entries_; // entries that do not yet exist in the underlying storage
    primitives::BlockHash parent_hash_;
    primitives::BlockNumber parent_number_;
    GetExtrinsicIndexDelegate get_extrinsic_index_;
  };

}  // namespace sgns::storage::changes_trie

OUTCOME_HPP_DECLARE_ERROR_2(sgns::storage::changes_trie,
                          StorageChangesTrackerImpl::Error);

#endif  // SUPERGENIUS_STORAGE_CHANGES_TRIE_STORAGE_CHANGES_TRACKER_IMPL
