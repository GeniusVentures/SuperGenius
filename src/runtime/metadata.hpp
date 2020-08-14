
#ifndef SUPERGENIUS_SRC_RUNTIME_METADATA_HPP
#define SUPERGENIUS_SRC_RUNTIME_METADATA_HPP

#include <outcome/outcome.hpp>
#include "primitives/opaque_metadata.hpp"

namespace sgns::runtime {

  class Metadata {
   protected:
    using OpaqueMetadata = primitives::OpaqueMetadata;

   public:
    virtual ~Metadata() = default;

    /**
     * @brief calls metadata method of Metadata runtime api
     * @return opaque metadata object or error
     */
    virtual outcome::result<OpaqueMetadata> metadata() = 0;
  };

}  // namespace sgns::runtime

#endif  // SUPERGENIUS_SRC_RUNTIME_METADATA_HPP
