

#ifndef SUPERGENIUS_STORAGE_TRIE_IMPL_EPHEMERAL_TRIE_BATCH
#define SUPERGENIUS_STORAGE_TRIE_IMPL_EPHEMERAL_TRIE_BATCH

#include "storage/trie/codec.hpp"
#include "storage/trie/supergenius_trie/supergenius_trie.hpp"
#include "storage/trie/trie_batches.hpp"

namespace sgns::storage::trie {

  class EphemeralTrieBatchImpl : public EphemeralTrieBatch {
   public:
    EphemeralTrieBatchImpl(std::shared_ptr<Codec> codec,
                           std::unique_ptr<SuperGeniusTrie> trie);
    ~EphemeralTrieBatchImpl() override = default;

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
    std::unique_ptr<SuperGeniusTrie> trie_;
  };

}  // namespace sgns::storage::trie

#endif  // SUPERGENIUS_STORAGE_TRIE_IMPL_EPHEMERAL_TRIE_BATCH
