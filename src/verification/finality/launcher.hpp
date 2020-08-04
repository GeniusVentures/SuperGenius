

#ifndef SUPERGENIUS_SRC_VERIFICATION_FINALITY_FINALITY_LAUNCHER_HPP
#define SUPERGENIUS_SRC_VERIFICATION_FINALITY_FINALITY_LAUNCHER_HPP

#include "verification/finality/round_observer.hpp"

namespace sgns::verification::finality {

  /**
   * Launches finality voting rounds
   */
  class Launcher : public RoundObserver {
   public:
    ~Launcher() override = default;
    /**
     * Start event loop which executes finality voting rounds
     */
    virtual void start() = 0;
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_SRC_VERIFICATION_FINALITY_FINALITY_LAUNCHER_HPP
