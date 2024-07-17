

#ifndef SUPERGENIUS_GENERIC_STORAGE_HPP
#define SUPERGENIUS_GENERIC_STORAGE_HPP

#include "storage/face/generic_maps.hpp"
#include "singleton/IComponent.hpp"

namespace sgns::storage::face {

  /**
   * @brief An abstraction over readable, writeable, iterable key-value storage
   * that supports write batches
   * @tparam K key type
   * @tparam V value type
   */
  template <typename K, typename V>
  struct GenericStorage : public ReadOnlyMap<K, V>,
                          public BatchWriteMap<K, V>,
                          public IComponent {};  

}  // namespace sgns::storage::face

#endif  // SUPERGENIUS_GENERIC_STORAGE_HPP
