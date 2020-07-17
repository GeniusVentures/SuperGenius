

#ifndef SUPERGENIUS_PRODUCTION_LOTTERY_MOCK_HPP
#define SUPERGENIUS_PRODUCTION_LOTTERY_MOCK_HPP

#include "verification/production/production_lottery.hpp"

#include <gmock/gmock.h>

namespace sgns::verification {
  struct ProductionLotteryMock : public ProductionLottery {
    MOCK_CONST_METHOD3(slotsLeadership,
                       SlotsLeadership(const Epoch &,
                                       const Threshold &,
                                       const crypto::SR25519Keypair &));

    MOCK_METHOD2(computeRandomness, Randomness(const Randomness &, EpochIndex));

    MOCK_METHOD1(submitVRFValue, void(const crypto::VRFPreOutput &));
  };
}  // namespace sgns::verification

#endif  // SUPERGENIUS_PRODUCTION_LOTTERY_MOCK_HPP
