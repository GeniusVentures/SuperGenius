

#ifndef SUPERGENIUS_STORAGE_IN_MEMORY_IN_MEMORY_STORAGE_HPP
#define SUPERGENIUS_STORAGE_IN_MEMORY_IN_MEMORY_STORAGE_HPP

#include <memory>

#include "outcome/outcome.hpp"
#include "base/buffer.hpp"
#include "storage/buffer_map_types.hpp"

#include <map>

namespace sgns::storage {

  /**
   * Simple storage that conforms PersistentMap interface
   * Mostly needed to have an in-memory trie in tests to avoid integration with
   * RocksDB
   */
  class InMemoryStorage : public storage::BufferStorage {
   public:
    ~InMemoryStorage() override = default;

    outcome::result<base::Buffer> get(
        const base::Buffer &key) const override;

    outcome::result<void> put(const base::Buffer &key,
                              const base::Buffer &value) override;

    outcome::result<void> put(const base::Buffer &key,
                              base::Buffer &&value) override;

    bool contains(const base::Buffer &key) const override;

    bool empty() const override;

    outcome::result<void> remove(const base::Buffer &key) override;

    std::unique_ptr<
        sgns::storage::face::WriteBatch<base::Buffer, base::Buffer>>
    batch() override;

    std::unique_ptr<
        sgns::storage::face::MapCursor<base::Buffer, base::Buffer>>
    cursor() override;

    std::string GetName() override
    {
      return "InMemoryStorage";
    }

   private:
    std::map<std::string, base::Buffer> storage;
  };

}  // namespace sgns::storage

#endif  // SUPERGENIUS_STORAGE_IN_MEMORY_IN_MEMORY_STORAGE_HPP
