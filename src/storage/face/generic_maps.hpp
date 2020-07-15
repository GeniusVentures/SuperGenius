

#ifndef SUPERGENIUS_GENERIC_MAPS_HPP
#define SUPERGENIUS_GENERIC_MAPS_HPP

#include "storage/face/batchable.hpp"
#include "storage/face/iterable.hpp"
#include "storage/face/readable.hpp"
#include "storage/face/writeable.hpp"

namespace sgns::storage::face {

  /**
   * @brief An abstraction over a readable and iterable key-value map.
   * @tparam K key type
   * @tparam V value type
   */
  template <typename K, typename V>
  struct ReadOnlyMap : public Iterable<K, V>, public Readable<K, V> {};

  /**
   * @brief An abstraction over a readable, writeable, iterable key-value map.
   * @tparam K key type
   * @tparam V value type
   */
  template <typename K, typename V>
  struct GenericMap : public ReadOnlyMap<K, V>, public Writeable<K, V> {};

  /**
   * @brief An abstraction over a writeable key-value map with batching support.
   * @tparam K key type
   * @tparam V value type
   */
  template <typename K, typename V>
  struct BatchWriteMap : public Writeable<K, V>, public Batchable<K, V> {};
}  // namespace sgns::storage::face

#endif  // SUPERGENIUS_GENERIC_MAPS_HPP
