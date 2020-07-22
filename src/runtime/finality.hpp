
#ifndef SUPERGENIUS_SRC_RUNTIME_FINALITY_HPP
#define SUPERGENIUS_SRC_RUNTIME_FINALITY_HPP

#include <boost/optional.hpp>
#include <outcome/outcome.hpp>
#include "base/buffer.hpp"
#include "primitives/authority.hpp"
#include "primitives/block_id.hpp"
#include "primitives/digest.hpp"
#include "primitives/scheduled_change.hpp"
#include "primitives/session_key.hpp"

namespace sgns::runtime {
  
  /**
   * @brief interface for Finality runtime functions
   */
  class Finality {
   protected:
    using Digest = primitives::Digest;
    using ScheduledChange = primitives::ScheduledChange;
    using BlockNumber = primitives::BlockNumber;
    using SessionKey = primitives::SessionKey;
    using WeightedAuthority = primitives::Authority;
    using ForcedChange = primitives::ForcedChange;
    using BlockId = primitives::BlockId;

   public:
    virtual ~Finality() = default;
    /**
     * @brief calls Finality_pending_change runtime api function,
     * which checks a digest for pending changes.
     * @param digest digest to check
     * @return nullopt if there are no pending changes,
     * scheduled change item if exists or error if error occured
     */
    virtual outcome::result<boost::optional<ScheduledChange>> pending_change(
        const Digest &digest) = 0;

    /**
     * @brief calls Finality_forced_change runtime api function
     * which checks a digest for forced changes
     * @param digest digest to check
     * @return nullopt if no forced changes,
     * forced change item if exists or error if error occured
     *
     */
    virtual outcome::result<boost::optional<ForcedChange>> forced_change(
        const Digest &digest) = 0;

    /**
     * @brief calls Finality_authorities runtime api function
     * @return collection of current finality authorities with their weights
     */
    virtual outcome::result<std::vector<WeightedAuthority>> authorities(
        const primitives::BlockId &block_id) = 0;
  };

}  // namespace sgns::runtime

#endif  // SUPERGENIUS_SRC_RUNTIME_FINALITY_HPP
