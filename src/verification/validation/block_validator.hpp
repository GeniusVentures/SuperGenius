

#ifndef SUPERGENIUS_BLOCK_VALIDATOR_HPP
#define SUPERGENIUS_BLOCK_VALIDATOR_HPP

#include "primitives/block.hpp"
#include "singleton/IComponent.hpp"
#include "verification/production/common.hpp"

namespace sgns::verification {
  /**
   * Validator of the blocks
   */
  class BlockValidator : public IComponent {
   public:
       ~BlockValidator() override = default;

       /**
     * Validate the block
     * @param block to be validated
     * @param authority_id authority that sent this block
     * @param threshold is vrf threshold for this epoch
     * @param randomness is randomness used in this epoch
     * @return nothing or validation error
     */
    virtual outcome::result<void> validateBlock(
        const primitives::Block &block,
                                                    const primitives::AuthorityId &authority_id,
        const Threshold &threshold,
        const Randomness &randomness) const = 0;

       /**
     * Validate the block header
     * @param block to be validated
     * @param authority_id authority that sent this block
     * @param threshold is vrf threshold for this epoch
     * @param randomness is randomness used in this epoch
     * @return nothing or validation error
     */
    virtual outcome::result<void> validateHeader(
        const primitives::BlockHeader &block_header,
                                                     const primitives::AuthorityId &authority_id,
        const Threshold &threshold,
        const Randomness &randomness) const = 0;
  };
}  // namespace sgns::verification

#endif  // SUPERGENIUS_BLOCK_VALIDATOR_HPP
