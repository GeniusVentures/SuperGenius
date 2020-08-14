

#ifndef SUPERGENIUS_STORAGE_TRIE_IMPL_TRIE_STORAGE_IMPL
#define SUPERGENIUS_STORAGE_TRIE_IMPL_TRIE_STORAGE_IMPL

#include "storage/trie/trie_storage.hpp"

#include "base/logger.hpp"
#include "storage/changes_trie/changes_tracker.hpp"
#include "storage/trie/codec.hpp"
#include "storage/trie/supergenius_trie/supergenius_trie_factory.hpp"
#include "storage/trie/serialization/trie_serializer.hpp"

namespace sgns::storage::trie {

  class TrieStorageImpl : public TrieStorage {
   public:
    static outcome::result<std::unique_ptr<TrieStorageImpl>> createEmpty(
        const std::shared_ptr<SuperGeniusTrieFactory> &trie_factory,
        std::shared_ptr<Codec> codec,
        std::shared_ptr<TrieSerializer> serializer,
        boost::optional<std::shared_ptr<changes_trie::ChangesTracker>> changes);

    static outcome::result<std::unique_ptr<TrieStorageImpl>> createFromStorage(
        const base::Buffer &root_hash,
        std::shared_ptr<Codec> codec,
        std::shared_ptr<TrieSerializer> serializer,
        boost::optional<std::shared_ptr<changes_trie::ChangesTracker>> changes);

    TrieStorageImpl(TrieStorageImpl const &) = delete;
    void operator=(const TrieStorageImpl &) = delete;

    TrieStorageImpl(TrieStorageImpl &&) = default;
    TrieStorageImpl& operator=(TrieStorageImpl &&) = default;
    ~TrieStorageImpl() override = default;

    outcome::result<std::unique_ptr<PersistentTrieBatch>> getPersistentBatch()
        override;
    outcome::result<std::unique_ptr<EphemeralTrieBatch>> getEphemeralBatch()
        const override;

    outcome::result<std::unique_ptr<PersistentTrieBatch>> getPersistentBatchAt(
        const base::Hash256 &root) override;
    outcome::result<std::unique_ptr<EphemeralTrieBatch>> getEphemeralBatchAt(
        const base::Hash256 &root) const override;

    base::Buffer getRootHash() const override;

   protected:
    TrieStorageImpl(
        base::Buffer root_hash,
        std::shared_ptr<Codec> codec,
        std::shared_ptr<TrieSerializer> serializer,
        boost::optional<std::shared_ptr<changes_trie::ChangesTracker>> changes);

   private:
    base::Buffer root_hash_;
    std::shared_ptr<Codec> codec_;
    std::shared_ptr<TrieSerializer> serializer_;
    boost::optional<std::shared_ptr<changes_trie::ChangesTracker>> changes_;
    base::Logger logger_;
  };

}  // namespace sgns::storage::trie

#endif  // SUPERGENIUS_STORAGE_TRIE_IMPL_TRIE_STORAGE_IMPL
