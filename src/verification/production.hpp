

#ifndef SUPERGENIUS_PRODUCTION_HPP
#define SUPERGENIUS_PRODUCTION_HPP

#include <boost/optional.hpp>

#include "verification/production/common.hpp"
#include "verification/production/types/production_meta.hpp"
#include "verification/production/types/epoch.hpp"
#include "network/production_observer.hpp"

namespace sgns::verification {
  /**
   * Production protocol, used for block production in the SuperGenius verification. One of
   * the two parts in that verification; the other is Finality finality
   */
  class Production : public network::ProductionObserver {
   public:
    ~Production() override = default;

    enum class ExecutionStrategy { GENESIS, SYNC_FIRST };

    /**
     * Start production execution
     * @param is_genesis is true when genesis epoch is executed on the current
     * node
     */
    virtual void start(ExecutionStrategy strategy) = 0;

    /**
     * Start a production
     * @param epoch - epoch, which is going to be run
     * @param starting_slot_finish_time - when the slot, from which the Production
     * starts, ends; for example, we start from
     * 5th slot of the some epoch. Then, we need to set time when 5th slot
     * finishes; most probably, that time will be calculated using Median
     * algorithm
     *
     * @note the function will automatically continue launching all further
     * epochs of the Production production
     * @note in fact, it is an implementation of "Invoke-Block-Authoring" from
     * the spec
     */
    virtual void runEpoch(Epoch epoch,
                          ProductionTimePoint starting_slot_finish_time) = 0;
  };
}  // namespace sgns::verification

#endif  // SUPERGENIUS_PRODUCTION_HPP
