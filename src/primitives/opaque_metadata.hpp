

#ifndef SUPERGENIUS_SRC_PRIMITIVES_OPAQUE_METADATA_HPP
#define SUPERGENIUS_SRC_PRIMITIVES_OPAQUE_METADATA_HPP

#include <vector>
#include <cstdint>

namespace sgns::primitives {
  /**
   * SuperGenius primitive, which is opaque representation of RuntimeMetadata
   */
  using OpaqueMetadata = std::vector<uint8_t>;
}

#endif //SUPERGENIUS_SRC_PRIMITIVES_OPAQUE_METADATA_HPP
