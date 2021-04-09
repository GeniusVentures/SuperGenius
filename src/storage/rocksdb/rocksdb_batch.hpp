

#ifndef SUPERGENIUS_rocksdb_BATCH_HPP
#define SUPERGENIUS_rocksdb_BATCH_HPP

#include <rocksdb/write_batch.h>
#include <storage/rocksdb/rocksdb.hpp>

namespace sgns::storage 
{

  /**
   * @brief Class that is used to implement efficient bulk (batch) modifications
   * of the Map.
   */
  class rocksdb::Batch : public BufferBatch 
  {
   public:
    explicit Batch(rocksdb &db);

    outcome::result<void> put(const Buffer &key, const Buffer &value) override;
    outcome::result<void> put(const Buffer &key, Buffer &&value) override;

    outcome::result<void> remove(const Buffer &key) override;

    outcome::result<void> commit() override;

    void clear() override;

   private:
    rocksdb &db_;
    ::ROCKSDB_NAMESPACE::WriteBatch batch_;
  };

}  // namespace sgns::storage

#endif  // SUPERGENIUS_rocksdb_BATCH_HPP
