
#ifndef SUPERGENIUS_RUNTIME_BLOCK_BUILDER_HPP
#define SUPERGENIUS_RUNTIME_BLOCK_BUILDER_HPP

#include <list>

#include "outcome/outcome.hpp"
#include "primitives/apply_result.hpp"
#include "primitives/block.hpp"
#include "primitives/block_header.hpp"
#include "primitives/check_inherents_result.hpp"
#include "primitives/extrinsic.hpp"
#include "primitives/inherent_data.hpp"

namespace sgns::runtime {
  /**
   * Part of runtime API responsible for building a block for a runtime.
   */
  class BlockBuilder {
   public:
    virtual ~BlockBuilder() = default;

    /**
     * Apply the given extrinsics.
     */
    virtual outcome::result<primitives::ApplyResult> apply_extrinsic(
        const primitives::Extrinsic &extrinsic) = 0;

    /**
     * Finish the current block.
     */
    virtual outcome::result<primitives::BlockHeader> finalise_block() = 0;

    /**
     * Generate inherent extrinsics. The inherent data will vary from chain to
     * chain.
     */
    virtual outcome::result<std::vector<primitives::Extrinsic>>
    inherent_extrinsics(const primitives::InherentData &data) = 0;

    /**
     * Check that the inherents are valid. The inherent data will vary from
     * chain to chain.
     */
    virtual outcome::result<primitives::CheckInherentsResult> check_inherents(
        const primitives::Block &block,
        const primitives::InherentData &data) = 0;

    /**
     * Generate a random seed.
     */
    virtual outcome::result<base::Hash256> random_seed() = 0;
  };
}  // namespace sgns::runtime

#endif  // SUPERGENIUS_RUNTIME_BLOCK_BUILDER_HPP
