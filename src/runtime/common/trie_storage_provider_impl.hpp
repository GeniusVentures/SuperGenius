
#ifndef SUPERGENIUS_SRC_RUNTIME_COMMON_TRIE_STORAGE_PROVIDER_IMPL
#define SUPERGENIUS_SRC_RUNTIME_COMMON_TRIE_STORAGE_PROVIDER_IMPL

#include "base/buffer.hpp"
#include "runtime/trie_storage_provider.hpp"

#include "storage/trie/trie_storage.hpp"

namespace sgns::runtime {

  class TrieStorageProviderImpl : public TrieStorageProvider {
   public:
    explicit TrieStorageProviderImpl(
        std::shared_ptr<storage::trie::TrieStorage> trie_storage);

    ~TrieStorageProviderImpl() override = default;

    outcome::result<void> setToEphemeral() override;
    outcome::result<void> setToEphemeralAt(
        const base::Hash256 &state_root) override;

    outcome::result<void> setToPersistent() override;
    outcome::result<void> setToPersistentAt(
        const base::Hash256 &state_root) override;


    std::shared_ptr<Batch> getCurrentBatch() const override;
    boost::optional<std::shared_ptr<PersistentBatch>> tryGetPersistentBatch()
        const override;
    bool isCurrentlyPersistent() const override;

    outcome::result<base::Buffer> forceCommit() override;

   private:
    std::shared_ptr <storage::trie::TrieStorage> trie_storage_;

    std::shared_ptr<Batch> current_batch_;

    // need to store it because it has to be the same in different runtime calls
    // to keep accumulated changes for commit to the main storage
    std::shared_ptr<PersistentBatch> persistent_batch_;
  };

}  // namespace sgns::runtime

#endif  // SUPERGENIUS_SRC_RUNTIME_COMMON_TRIE_STORAGE_PROVIDER_IMPL
