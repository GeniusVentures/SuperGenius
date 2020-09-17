
#include "finalization_composite.hpp"

namespace sgns::verification::finality {

  outcome::result<void> FinalizationComposite::onFinalize(
      const primitives::BlockInfo &block) {
    for (auto &observer : observers_) {
      OUTCOME_TRY(observer->onFinalize(block));
    }
    return outcome::success();
  }

}  // namespace sgns::verification::finality
