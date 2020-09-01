

#ifndef SUPERGENIUS_STORAGE_TRIE_IMPL_PERSISTENT_TRIE_BATCH
#define SUPERGENIUS_STORAGE_TRIE_IMPL_PERSISTENT_TRIE_BATCH

#include "storage/changes_trie/changes_tracker.hpp"
#include "storage/trie/codec.hpp"
#include "storage/trie/serialization/trie_serializer.hpp"
#include "storage/trie/trie_batches.hpp"

namespace sgns::storage::trie {

  class PersistentTrieBatchImpl: public PersistentTrieBatch {
   public:
    using RootChangedEventHandler = std::function<void(const base::Buffer &)>;

    PersistentTrieBatchImpl(
        std::shared_ptr<Codec> codec,
        std::shared_ptr<TrieSerializer> serializer,
        boost::optional<std::shared_ptr<changes_trie::ChangesTracker>> changes,
        std::unique_ptr<SuperGeniusTrie> trie,
        RootChangedEventHandler &&handler);
    ~PersistentTrieBatchImpl() override = default;

    outcome::result<Buffer> commit() override;
    std::unique_ptr<TopperTrieBatch> batchOnTop() override;

    outcome::result<Buffer> get(const Buffer &key) const override;
    std::unique_ptr<BufferMapCursor> cursor() override;
    bool contains(const Buffer &key) const override;
    bool empty() const override;
    outcome::result<void> clearPrefix(const Buffer &prefix) override;
    outcome::result<void> put(const Buffer &key, const Buffer &value) override;
    outcome::result<void> put(const Buffer &key, Buffer &&value) override;
    outcome::result<void> remove(const Buffer &key) override;

   private:
    std::shared_ptr<Codec> codec_;
    std::shared_ptr<TrieSerializer> serializer_;
    boost::optional<std::shared_ptr<changes_trie::ChangesTracker>> changes_;
    std::unique_ptr<SuperGeniusTrie> trie_;
    RootChangedEventHandler root_changed_handler_;
  };

}  // namespace sgns::storage::trie

#endif  // SUPERGENIUS_STORAGE_TRIE_IMPL_PERSISTENT_TRIE_BATCH
