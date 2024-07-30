

#ifndef SUPERGENIUS_WRITEABLE_HPP
#define SUPERGENIUS_WRITEABLE_HPP

#include "outcome/outcome.hpp"

namespace sgns::storage::face {

  /**
   * @brief An mixin for modifiable map.
   * @tparam K key type
   * @tparam V value type
   */
  template <typename K, typename V>
  struct Writeable {
    virtual ~Writeable() = default;

    /**
     * @brief Store value by key
     * @param key key
     * @param value value
     * @return result containing void if put successful, error otherwise
     */
    virtual outcome::result<void> put(const K &key, const V &value) = 0;
    virtual outcome::result<void> put(const K &key, V&& value) = 0;

    /**
     * @brief Remove value by key
     * @param key K
     * @return error code if error happened
     */
    virtual outcome::result<void> remove(const K &key) = 0;
  };

}  // namespace sgns::storage::face

#endif  // SUPERGENIUS_WRITEABLE_HPP
