

#ifndef SUPERGENIUS_SRC_VERIFICATION_FINALITY_CHAIN_HPP
#define SUPERGENIUS_SRC_VERIFICATION_FINALITY_CHAIN_HPP

#include <vector>

#include <boost/optional.hpp>
#include "outcome/outcome.hpp"
#include "verification/finality/structs.hpp"

namespace sgns::verification::finality {

  /**
   * Chain context necessary for implementation of the finality gadget.
   */
  struct Chain {
    virtual ~Chain() = default;

    /**
     * @brief Get the ancestry of a {@param block} up to but not including the
     * {@param base} hash. Should be in reverse order from block's parent.
     * @return If the block is not a descendent of base, returns an error.
     */
    virtual outcome::result<std::vector<primitives::BlockHash>> getAncestry(
        const primitives::BlockHash &base,
        const primitives::BlockHash &block) const = 0;

    /**
     * @returns the hash of the best block whose chain contains the given
     * block hash, even if that block is {@param base} itself. If base is
     * unknown, return None.
     */
    virtual outcome::result<BlockInfo> bestChainContaining(
        const primitives::BlockHash &base) const = 0;

    /**
     * @returns true if {@param block} is a descendent of or equal to the
     * given {@param base}.
     */
    bool isEqualOrDescendOf( const primitives::BlockHash &base, const primitives::BlockHash &block ) const
    {
        return base == block ? true : getAncestry( base, block ).has_value();
    }
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_SRC_VERIFICATION_FINALITY_CHAIN_HPP
