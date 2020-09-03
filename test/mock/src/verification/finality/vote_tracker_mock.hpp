
#ifndef SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_VOTE_TRACKER_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_VOTE_TRACKER_MOCK_HPP

#include "verification/finality/vote_tracker.hpp"

#include <gmock/gmock.h>

namespace sgns::verification::finality {

  class VoteTrackerMock : public VoteTracker {
   public:
    MOCK_METHOD2(push, PushResult(const SignedPrevote &, size_t));
    MOCK_METHOD2(push, PushResult(const SignedPrecommit &, size_t));

    MOCK_CONST_METHOD0(getPrevotes, std::vector<SignedPrevote>());
    MOCK_CONST_METHOD0(getPrecommits, std::vector<SignedPrecommit>());

    MOCK_CONST_METHOD0(prevoteWeight, size_t());
    MOCK_CONST_METHOD0(precommitWeight, size_t());

    MOCK_CONST_METHOD1(getJustification, Justification(const BlockInfo &));
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_VOTE_TRACKER_MOCK_HPP
