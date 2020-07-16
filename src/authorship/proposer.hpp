

#ifndef SUPERGENIUS_SRC_AUTHORSHIP_PROPOSER_TEST_HPP
#define SUPERGENIUS_SRC_AUTHORSHIP_PROPOSER_TEST_HPP

#include "clock/clock.hpp"
#include "primitives/block.hpp"
#include "primitives/block_id.hpp"
#include "primitives/digest.hpp"
#include "primitives/inherent_data.hpp"

namespace sgns::authorship {

  /**
   * Create block to further proposal for consensus
   */
  class Proposer {
   public:
    virtual ~Proposer() = default;

    /**
     * Creates block from provided parameters
     * @param parent_block_id hash or number of parent
     * @param inherent_data additional data on block from unsigned extrinsics
     * @param inherent_digests - chain-specific block auxilary data
     * @return proposed block or error
     */
    virtual outcome::result<primitives::Block> propose(
        const primitives::BlockId &parent_block_id,
        const primitives::InherentData &inherent_data,
        const primitives::Digest &inherent_digest) = 0;
  };

}  // namespace sgns::authorship

#endif  // SUPERGENIUS_SRC_AUTHORSHIP_PROPOSER_TEST_HPP
