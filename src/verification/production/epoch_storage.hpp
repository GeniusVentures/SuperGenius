

#ifndef SUPERGENIUS_EPOCH_STORAGE_HPP
#define SUPERGENIUS_EPOCH_STORAGE_HPP

#include <boost/optional.hpp>

#include "verification/production/common.hpp"
#include "verification/production/types/next_epoch_descriptor.hpp"
#include "singleton/IComponent.hpp"

namespace sgns::verification {
  /**
   * Allows to store epochs
   */
  struct EpochStorage : public IComponent {
    virtual ~EpochStorage() = default;

    /**
     * Stores epoch's information by its number
     * @param epoch_number number of stored epoch
     * @param epoch_descriptor epochs information
     * @return result with success if epoch was added, error otherwise
     */
    virtual outcome::result<void> addEpochDescriptor(
        EpochIndex epoch_number,
        const NextEpochDescriptor &epoch_descriptor) = 0;

    /**
     * Get an epoch by a (\param block_id)
     * @return epoch or nothing, if epoch, in which that block was produced, is
     * unknown to this peer
     */
    virtual outcome::result<NextEpochDescriptor> getEpochDescriptor(
        EpochIndex epoch_number) const = 0;
  };
}  // namespace sgns::verification

#endif  // SUPERGENIUS_EPOCH_STORAGE_HPP
