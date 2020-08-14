

#ifndef SUPERGENIUS_IN_MEMORY_BATCH_HPP
#define SUPERGENIUS_IN_MEMORY_BATCH_HPP

#include "base/buffer.hpp"
#include "storage/in_memory/in_memory_storage.hpp"

namespace sgns::storage {
  using sgns::base::Buffer;

  class InMemoryBatch
      : public sgns::storage::face::WriteBatch<Buffer,
                                                 Buffer> {
   public:
    explicit InMemoryBatch(InMemoryStorage &db) : db{db} {}

    outcome::result<void> put(const Buffer &key,
                              const Buffer &value) override {
      entries[key.toHex()] = value;
      return outcome::success();
    }

    outcome::result<void> put(const Buffer &key,
                              Buffer &&value) override {
      entries[key.toHex()] = std::move(value);
      return outcome::success();
    }

    outcome::result<void> remove(const Buffer &key) override {
      entries.erase(key.toHex());
      return outcome::success();
    }

    outcome::result<void> commit() override {
      for (auto &entry : entries) {
        OUTCOME_TRY(
            db.put(Buffer::fromHex(entry.first).value(), entry.second));
      }
      return outcome::success();
    }

    void clear() override {
      entries.clear();
    }

   private:
    std::map<std::string, Buffer> entries;
    InMemoryStorage &db;
  };
}  // namespace sgns::storage

#endif  // SUPERGENIUS_IN_MEMORY_BATCH_HPP
