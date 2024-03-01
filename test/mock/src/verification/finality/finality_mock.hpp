
#ifndef SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_FINALITY_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_FINALITY_MOCK_HPP

#include "verification/finality/finality.hpp"

#include <gmock/gmock.h>

namespace sgns::verification::finality {

  class FinalityMock : public Finality {
   public:
    MOCK_METHOD0(executeNextRound, void());
    MOCK_METHOD1(onFinalize, void(const Fin &));
    MOCK_METHOD1(onVoteMessage, void(const VoteMessage &));
    MOCK_METHOD0(GetName,std::string());
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_FINALITY_MOCK_HPP
