
#ifndef SUPERGENIUS_SRC_VERIFICATION_FINALITY
#define SUPERGENIUS_SRC_VERIFICATION_FINALITY

#include "verification/finality/round_observer.hpp"

namespace sgns::verification::finality {

  /**
   * Launches finality voting rounds
   */
  class Finality : public RoundObserver {
   public:
    ~Finality() override = default;

    virtual void executeNextRound() = 0;
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_SRC_VERIFICATION_FINALITY
