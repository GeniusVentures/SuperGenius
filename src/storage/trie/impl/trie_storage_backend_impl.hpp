

#ifndef SUPERGENIUS_STORAGE_TRIE_IMPL_TRIE_STORAGE_BACKEND
#define SUPERGENIUS_STORAGE_TRIE_IMPL_TRIE_STORAGE_BACKEND

#include "base/buffer.hpp"
#include "outcome/outcome.hpp"
#include "storage/trie/trie_storage_backend.hpp"

namespace sgns::storage::trie {

  class TrieStorageBackendImpl : public TrieStorageBackend {
   public:
    TrieStorageBackendImpl(std::shared_ptr<BufferStorage> storage,
                      base::Buffer node_prefix);

    ~TrieStorageBackendImpl() override = default;

    std::unique_ptr<face::MapCursor<Buffer, Buffer>> cursor() override;
    std::unique_ptr<face::WriteBatch<Buffer, Buffer>> batch() override;

    outcome::result<Buffer> get(const Buffer &key) const override;
    bool contains(const Buffer &key) const override;
    bool empty() const override;

    outcome::result<void> put(const Buffer &key, const Buffer &value) override;
    outcome::result<void> put(const Buffer &key, Buffer &&value) override;
    outcome::result<void> remove(const Buffer &key) override;

   private:
    base::Buffer prefixKey(const base::Buffer &key) const;

    std::shared_ptr<BufferStorage> storage_;
    base::Buffer node_prefix_;
  };

}  // namespace sgns::storage::trie

#endif  // SUPERGENIUS_STORAGE_TRIE_IMPL_TRIE_STORAGE_BACKEND
