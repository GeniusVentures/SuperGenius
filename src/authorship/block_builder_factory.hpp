#ifndef SUPERGENIUS_SRC_AUTHORSHIP_BLOCK_BUILDER_FACTORY_HPP
#define SUPERGENIUS_SRC_AUTHORSHIP_BLOCK_BUILDER_FACTORY_HPP

#include "authorship/block_builder.hpp"
#include "primitives/block_id.hpp"
#include "singleton/IComponent.hpp"

namespace sgns::authorship {

  /**
   * Creates new block builders. Each of them encapsulates the logic for
   * creating a single block from provided block information
   */
  class BlockBuilderFactory : public IComponent {
   public:
       ~BlockBuilderFactory() override = default;

       /**
     * Prepares BlockBuilder for creating block on top of parent block and using
     * provided digests. Also initialises the block created in BlockBuilder
     */
       [[nodiscard]] virtual outcome::result<std::unique_ptr<BlockBuilder>> create(
           const primitives::BlockId &parent_id, primitives::Digest inherent_digest ) const = 0;
  };

}  // namespace sgns::authorship

#endif  // SUPERGENIUS_SRC_AUTHORSHIP_BLOCK_BUILDER_FACTORY_HPP
