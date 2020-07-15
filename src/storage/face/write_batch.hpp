

#ifndef SUPERGENIUS_WRITE_BATCH_HPP
#define SUPERGENIUS_WRITE_BATCH_HPP

#include "storage/face/writeable.hpp"

namespace sgns::storage::face {

  /**
   * @brief An abstraction over a storage, which can be used for batch writes
   * @tparam K key type
   * @tparam V value type
   */
  template <typename K, typename V>
  struct WriteBatch : public Writeable<K, V> {
    /**
     * @brief Writes batch.
     * @return error code in case of error.
     */
    virtual outcome::result<void> commit() = 0;

    /**
     * @brief Clear batch.
     */
    virtual void clear() = 0;
  };

}  // namespace sgns::storage::face

#endif  // SUPERGENIUS_WRITE_BATCH_HPP
