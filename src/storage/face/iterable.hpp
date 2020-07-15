

#ifndef SUPERGENIUS_ITERABLE_HPP
#define SUPERGENIUS_ITERABLE_HPP

#include <memory>

#include "storage/face/map_cursor.hpp"

namespace sgns::storage::face {

  /**
   * @brief A mixin for an iterable map.
   * @tparam K key type
   * @tparam V value type
   */
  template <typename K, typename V>
  struct Iterable {
    virtual ~Iterable() = default;

    /**
     * @brief Returns new key-value iterator.
     * @return kv iterator
     */
    virtual std::unique_ptr<MapCursor<K, V>> cursor() = 0;
  };

}  // namespace sgns::storage::face

#endif  // SUPERGENIUS_ITERABLE_HPP
