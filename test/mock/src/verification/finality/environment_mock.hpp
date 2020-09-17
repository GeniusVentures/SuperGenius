
#ifndef SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_ENVIRONMENT_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_ENVIRONMENT_MOCK_HPP

#include "verification/finality/environment.hpp"

#include <gmock/gmock.h>

namespace sgns::verification::finality {

  class EnvironmentMock : public Environment {
   public:
    MOCK_CONST_METHOD2(getAncestry,
                       outcome::result<std::vector<primitives::BlockHash>>(
                           const primitives::BlockHash &base,
                           const BlockHash &block));

    MOCK_CONST_METHOD1(bestChainContaining,
                       outcome::result<BlockInfo>(const BlockHash &base));

    MOCK_METHOD3(onProposed,
                 outcome::result<void>(RoundNumber round,
                                       MembershipCounter set_id,
                                       const SignedMessage &propose));
    MOCK_METHOD3(onPrevoted,
                 outcome::result<void>(RoundNumber round,
                                       MembershipCounter set_id,
                                       const SignedMessage &prevote));
    MOCK_METHOD3(onPrecommitted,
                 outcome::result<void>(RoundNumber round,
                                       MembershipCounter set_id,
                                       const SignedMessage &precommit));
    MOCK_METHOD3(
        onCommitted,
        outcome::result<void>(RoundNumber round,
                              const BlockInfo &vote,
                              const FinalityJustification &justification));

    MOCK_METHOD1(doOnCompleted, void(const CompleteHandler &));

    MOCK_METHOD1(onCompleted, void(outcome::result<CompletedRound> round));

    MOCK_METHOD2(
        finalize,
        outcome::result<void>(const primitives::BlockHash &block,
                              const FinalityJustification &justification));
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_ENVIRONMENT_MOCK_HPP
