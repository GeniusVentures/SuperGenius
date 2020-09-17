
#ifndef SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_GOSSIPER_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_GOSSIPER_MOCK_HPP

#include "verification/finality/gossiper.hpp"

#include <gmock/gmock.h>

namespace sgns::verification::finality {

  class GossiperMock : public Gossiper {
   public:
    MOCK_METHOD1(vote, void(const VoteMessage &msg));
    MOCK_METHOD1(finalize, void(const Fin &fin));
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_FINALITY_GOSSIPER_MOCK_HPP
