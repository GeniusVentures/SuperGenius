

#ifndef SUPERGENIUS_PRODUCTION_GOSSIPER_MOCK_HPP
#define SUPERGENIUS_PRODUCTION_GOSSIPER_MOCK_HPP

#include "verification/production/production_gossiper.hpp"

#include <gmock/gmock.h>

namespace sgns::verification {
  struct ProductionGossiperMock : public ProductionGossiper {
    MOCK_METHOD1(blockAnnounce, void(const network::BlockAnnounce &));
  };
}  // namespace sgns::verification

#endif  // SUPERGENIUS_PRODUCTION_GOSSIPER_MOCK_HPP
