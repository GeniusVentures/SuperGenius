#ifndef SUPERGENIUS_STORAGE_CHANGES_TRIE_CHANGES_TRACKER
#define SUPERGENIUS_STORAGE_CHANGES_TRIE_CHANGES_TRACKER

#include "base/buffer.hpp"
#include "primitives/common.hpp"
#include "storage/changes_trie/changes_trie_config.hpp"
#include "singleton/IComponent.hpp"

namespace sgns::storage::changes_trie {

  /**
   * Stores the information about changes to the storage in accordance with the
   * supplied config. Used to build the changes trie.
   * onChange() must be called every time a persistent change to the node
   * storage is made.
   */
  class ChangesTracker : public IComponent {
   public:
    using GetExtrinsicIndexDelegate =
        std::function<outcome::result<base::Buffer>()>;

    ~ChangesTracker() override = default;

    /**
     * @param f is a functor that returns the current extrinsic index
     */
    virtual void setExtrinsicIdxGetter(GetExtrinsicIndexDelegate f) = 0;

    /**
     * Supposed to be called when the processed block changes
     */
    virtual outcome::result<void> onBlockChange(
        primitives::BlockHash new_parent_hash,
        primitives::BlockNumber new_parent_number) = 0;

    /**
     * Supposed to be called when an entry is put into the tracked storage
     * @arg new_entry states whether the entry is new, or just an update of a
     * present value
     */
    virtual outcome::result<void> onPut(const base::Buffer &key,
                                        const base::Buffer &value,
                                        bool new_entry) = 0;
    /**
     * Supposed to be called when an entry is removed from the tracked storage
     */
    virtual outcome::result<void> onRemove(const base::Buffer &key) = 0;

    /**
     * Sinks accumulated changes for the latest registered block to the changes
     * trie and returns its root hash
     */
    virtual outcome::result<base::Hash256> constructChangesTrie(
        const primitives::BlockHash &parent, const ChangesTrieConfig &conf) = 0;
  };

}  // namespace sgns::storage::changes_trie

#endif  // SUPERGENIUS_STORAGE_CHANGES_TRIE_CHANGES_TRACKER
