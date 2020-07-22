#ifndef SUPERGENIUS_SRC_VERIFICATION_FINALITY_VOTING_ROUND_HPP
#define SUPERGENIUS_SRC_VERIFICATION_FINALITY_VOTING_ROUND_HPP

#include "verification/finality/round_observer.hpp"
#include "verification/finality/round_state.hpp"

namespace sgns::verification::finality {

  /**
   * Handles execution of one finality round. For details @see VotingRoundImpl
   */
  struct VotingRound {
    virtual ~VotingRound() = default;

    virtual void onFinalize(const Fin &f) = 0;

    virtual void onPrimaryPropose(
        const SignedMessage &primary_propose) = 0;

    virtual void onPrevote(const SignedMessage &prevote) = 0;

    virtual void onPrecommit(const SignedMessage &precommit) = 0;

    virtual void primaryPropose(const RoundState &last_round_state) = 0;

    virtual void prevote(const RoundState &last_round_state) = 0;

    virtual void precommit(const RoundState &last_round_state) = 0;

    // executes algorithm 4.9 Attempt-To-Finalize-Round(r)
    virtual bool tryFinalize() = 0;

    virtual RoundNumber roundNumber() const = 0;
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_SRC_VERIFICATION_FINALITY_VOTING_ROUND_HPP
