
#ifndef SUPERGENIUS_SRC_VERIFICATION_FINALITY_ROUND_OBSERVER_HPP
#define SUPERGENIUS_SRC_VERIFICATION_FINALITY_ROUND_OBSERVER_HPP

#include "verification/finality/structs.hpp"

namespace sgns::verification::finality {

  /**
   * @class RoundObserver
   * @brief observes incoming messages. Abstraction of a network.
   */
  struct RoundObserver {
    virtual ~RoundObserver() = default;

    /**
     * Handler of finality finalization messages
     * @param f finalization message
     */
    virtual void onFinalize(const Fin &f) = 0;

    /**
     * Handler of finality vote messages
     * @param msg vote message
     */
    virtual void onVoteMessage(const VoteMessage &msg) = 0;
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_SRC_VERIFICATION_FINALITY_ROUND_OBSERVER_HPP
