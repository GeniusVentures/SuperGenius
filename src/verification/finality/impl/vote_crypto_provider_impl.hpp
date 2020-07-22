

#ifndef SUPERGENIUS_SRC_VERIFICATION_FINALITY_IMPL_VOTE_CRYPTO_PROVIDER_IMPL_HPP
#define SUPERGENIUS_SRC_VERIFICATION_FINALITY_IMPL_VOTE_CRYPTO_PROVIDER_IMPL_HPP

#include "verification/finality/vote_crypto_provider.hpp"
#include "verification/finality/voter_set.hpp"
#include "crypto/ed25519_provider.hpp"

namespace sgns::verification::finality {

  class VoteCryptoProviderImpl : public VoteCryptoProvider {
   public:
    ~VoteCryptoProviderImpl() override = default;

    VoteCryptoProviderImpl(crypto::ED25519Keypair keypair,
                           std::shared_ptr<crypto::ED25519Provider> ed_provider,
                           RoundNumber round_number,
                           std::shared_ptr<VoterSet> voter_set);

    bool verifyPrimaryPropose(
        const SignedMessage &primary_propose) const override;
    bool verifyPrevote(const SignedMessage &prevote) const override;
    bool verifyPrecommit(const SignedMessage &precommit) const override;

    SignedMessage signPrimaryPropose(
        const PrimaryPropose &primary_propose) const override;
    SignedMessage signPrevote(const Prevote &prevote) const override;
    SignedMessage signPrecommit(const Precommit &precommit) const override;

   private:
    crypto::ED25519Signature voteSignature(const Vote &vote) const;

    crypto::ED25519Keypair keypair_;
    std::shared_ptr<crypto::ED25519Provider> ed_provider_;
    RoundNumber round_number_;
    std::shared_ptr<VoterSet> voter_set_;
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_SRC_VERIFICATION_FINALITY_IMPL_VOTE_CRYPTO_PROVIDER_IMPL_HPP
