

#ifndef SUPERGENIUS_rocksdb_CURSOR_HPP
#define SUPERGENIUS_rocksdb_CURSOR_HPP

#include <rocksdb/iterator.h>
#include "storage/rocksdb/rocksdb.hpp"

namespace sgns::storage 
{

  /**
   * @brief Instance of cursor can be used as bidirectional iterator over
   * key-value bindings of the Map.
   */
  class rocksdb::Cursor : public BufferMapCursor {
   public:
    ~Cursor() override = default;

    explicit Cursor(std::shared_ptr<Iterator> it);

    outcome::result<void> seekToFirst() override;

    outcome::result<void> seek(const Buffer &key) override;

    outcome::result<void> seekToLast() override;

    bool isValid() const override;

    outcome::result<void> next() override;

    outcome::result<void> prev() override;

    outcome::result<Buffer> key() const override;

    outcome::result<Buffer> value() const override;

   private:
    std::shared_ptr<Iterator> i_;
  };

}  // namespace sgns::storage

#endif  // SUPERGENIUS_rocksdb_CURSOR_HPP
