

#ifndef SUPERGENIUS_LEVELDB_CURSOR_HPP
#define SUPERGENIUS_LEVELDB_CURSOR_HPP

#include <leveldb/iterator.h>
#include "storage/leveldb/leveldb.hpp"

namespace sgns::storage {

  /**
   * @brief Instance of cursor can be used as bidirectional iterator over
   * key-value bindings of the Map.
   */
  class LevelDB::Cursor : public BufferMapCursor {
   public:
    ~Cursor() override = default;

    explicit Cursor(std::shared_ptr<leveldb::Iterator> it);

    outcome::result<void> seekToFirst() override;

    outcome::result<void> seek(const Buffer &key) override;

    outcome::result<void> seekToLast() override;

    bool isValid() const override;

    outcome::result<void> next() override;

    outcome::result<void> prev() override;

    outcome::result<Buffer> key() const override;

    outcome::result<Buffer> value() const override;

   private:
    std::shared_ptr<leveldb::Iterator> i_;
  };

}  // namespace sgns::storage

#endif  // SUPERGENIUS_LEVELDB_CURSOR_HPP
