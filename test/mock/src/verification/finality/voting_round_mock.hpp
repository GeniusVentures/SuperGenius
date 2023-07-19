#ifndef SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_VOTING_ROUND_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_VOTING_ROUND_MOCK_HPP

#include "verification/finality/voting_round.hpp"

#include <gmock/gmock.h>

namespace sgns::verification::finality {
  struct VotingRoundMock : public VotingRound {
    MOCK_CONST_METHOD0(roundNumber, RoundNumber());
    MOCK_CONST_METHOD0(state, std::shared_ptr<const RoundState>());
    MOCK_METHOD0(play, void());
    MOCK_METHOD0(end, void());
    MOCK_METHOD0(doProposal, bool());
    MOCK_METHOD0(doPrevote, bool());
    MOCK_METHOD0(doPrecommit, bool());
    MOCK_METHOD0(tryFinalize, bool());
    MOCK_METHOD1(onFinalize, void(const Fin&));
    MOCK_METHOD1(onPrimaryPropose, void(const SignedMessage&));
    MOCK_METHOD1(onPrevote, void(const SignedMessage&));
    MOCK_METHOD1(onPrecommit, void(const SignedMessage&));
  };

}  // namespace sgns::verification::finality
#endif  // SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_VOTE_GRAPH_MOCK_HPP
