
#ifndef SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_VOTE_CRYPTO_PROVIDER_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_VOTE_CRYPTO_PROVIDER_MOCK_HPP

#include "verification/finality/vote_crypto_provider.hpp"

#include <gmock/gmock.h>

namespace sgns::verification::finality {

  class VoteCryptoProviderMock : public VoteCryptoProvider {
   public:
    MOCK_CONST_METHOD1(verifyPrimaryPropose,
                       bool(const SignedMessage &primary_propose));
    MOCK_CONST_METHOD1(verifyPrevote, bool(const SignedMessage &prevote));
    MOCK_CONST_METHOD1(verifyPrecommit, bool(const SignedMessage &precommit));

    MOCK_CONST_METHOD1(signPrimaryPropose,
                       SignedMessage(const PrimaryPropose &primary_propose));
    MOCK_CONST_METHOD1(signPrevote, SignedMessage(const Prevote &prevote));
    MOCK_CONST_METHOD1(signPrecommit,
                       SignedMessage(const Precommit &precommit));
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_VOTE_CRYPTO_PROVIDER_MOCK_HPP
