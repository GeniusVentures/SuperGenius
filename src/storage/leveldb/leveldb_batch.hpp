

#ifndef SUPERGENIUS_LEVELDB_BATCH_HPP
#define SUPERGENIUS_LEVELDB_BATCH_HPP

#include <leveldb/write_batch.h>
#include "storage/leveldb/leveldb.hpp"

namespace sgns::storage {

  /**
   * @brief Class that is used to implement efficient bulk (batch) modifications
   * of the Map.
   */
  class LevelDB::Batch : public BufferBatch {
   public:
    explicit Batch(LevelDB &db);

    outcome::result<void> put(const Buffer &key, const Buffer &value) override;
    outcome::result<void> put(const Buffer &key, Buffer &&value) override;

    outcome::result<void> remove(const Buffer &key) override;

    outcome::result<void> commit() override;

    void clear() override;

   private:
    LevelDB &db_;
    leveldb::WriteBatch batch_;
  };

}  // namespace sgns::storage

#endif  // SUPERGENIUS_LEVELDB_BATCH_HPP
