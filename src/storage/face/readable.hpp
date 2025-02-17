

#ifndef SUPERGENIUS_READABLE_HPP
#define SUPERGENIUS_READABLE_HPP

#include "outcome/outcome.hpp"

namespace sgns::storage::face {

  /**
   * @brief A mixin for read-only map.
   * @tparam K key type
   * @tparam V value type
   */
  template <typename K, typename V>
  struct Readable {
    virtual ~Readable() = default;

    /**
     * @brief Get value by key
     * @param key K
     * @return V
     */
    virtual outcome::result<V> get(const K &key) const = 0;

    /**
     * @brief Returns true if given key-value binding exists in the storage.
     * @param key K
     * @return true if key has value, false otherwise.
     */
    virtual bool contains(const K &key) const = 0;

    /**
     * @brief Returns true if the storage is empty.
     */
    virtual bool empty() const = 0;
  };

}  // namespace sgns::storage::face

#endif  // SUPERGENIUS_WRITEABLE_KEY_VALUE_HPP
