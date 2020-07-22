
#ifndef SUPERGENIUS_SRC_VERIFICATION_FINALITY_VOTE_CRYPTO_PROVIDER_HPP
#define SUPERGENIUS_SRC_VERIFICATION_FINALITY_VOTE_CRYPTO_PROVIDER_HPP

#include "verification/finality/structs.hpp"

namespace sgns::verification::finality {

  /**
   * Statelessly verifies signatures of votes and signs votes
   */
  class VoteCryptoProvider {
   public:
    virtual ~VoteCryptoProvider() = default;

    virtual bool verifyPrimaryPropose(
        const SignedMessage &primary_propose) const = 0;
    virtual bool verifyPrevote(const SignedMessage &prevote) const = 0;
    virtual bool verifyPrecommit(const SignedMessage &precommit) const = 0;

    virtual SignedMessage signPrimaryPropose(
        const PrimaryPropose &primary_propose) const = 0;
    virtual SignedMessage signPrevote(const Prevote &prevote) const = 0;
    virtual SignedMessage signPrecommit(const Precommit &precommit) const = 0;
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_SRC_VERIFICATION_FINALITY_VOTE_CRYPTO_PROVIDER_HPP
