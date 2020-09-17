
#ifndef SUPERGENIUS_FINALITY_FINALIZATION_OBSERVER
#define SUPERGENIUS_FINALITY_FINALIZATION_OBSERVER

#include "outcome/outcome.hpp"
#include "primitives/common.hpp"

namespace sgns::verification::finality {
  class FinalizationObserver {
   public:
    virtual ~FinalizationObserver() = default;

    /**
     * Doing something at block finalized
     * @param message
     * @return failure or nothing
     */
    virtual outcome::result<void> onFinalize(
        const primitives::BlockInfo &block) = 0;
  };
}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_FINALITY_FINALIZATION_OBSERVER
