#ifndef SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_VOTING_ROUND_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_VOTING_ROUND_MOCK_HPP

#include "verification/finality/impl/voting_round_impl.hpp"

#include <gmock/gmock.h>

namespace sgns::verification::finality {
  class VotingRoundMock : public VotingRoundImpl {
  public: 	  
    MOCK_METHOD0(play, void());
    MOCK_METHOD0(end, void());
    MOCK_METHOD0(startPrevoteStage, void());
    MOCK_METHOD0(endPrevoteStage, void());
    MOCK_METHOD0(startPrecommitStage, void());
    MOCK_METHOD0(endPrecommitStage, void());
    MOCK_METHOD0(startWaitingStage, void());
    MOCK_METHOD0(endWaitingStage, void());
    MOCK_METHOD0(doProposal, bool());
    MOCK_METHOD0(doPrevote, bool());
    MOCK_METHOD0(doPrecommit, bool());
    MOCK_METHOD1(onFinalize, void(const Fin&));
    MOCK_METHOD1(onPrimaryPropose, void(const SignedMessage&));
    MOCK_METHOD1(onPrevote, void(const SignedMessage&));
    MOCK_METHOD1(onPrecommit, void(const SignedMessage&));
    MOCK_METHOD0(tryFinalize, bool());
    MOCK_CONST_METHOD0(roundNumber, RoundNumber());
    MOCK_CONST_METHOD0(state, std::shared_ptr<const RoundState>());
    MOCK_METHOD1(notify, outcome::result<void>(const RoundState&));
    MOCK_CONST_METHOD1(finalizingPrecommits,
                     boost::optional<FinalityJustification>(
                         const BlockInfo& estimate));
    MOCK_CONST_METHOD1(constructPrevote,
                     outcome::result<SignedMessage>(const RoundState&));
    MOCK_CONST_METHOD1(constructPrecommit,
                     outcome::result<SignedMessage>(const RoundState&));
    MOCK_METHOD2(validate,
               bool(const BlockInfo& vote,
                    const FinalityJustification& justification));
  };

}  // namespace sgns::verification::finality
#endif  // SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_VOTE_GRAPH_MOCK_HPP
