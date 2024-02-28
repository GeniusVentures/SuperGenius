

#ifndef SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_PRODUCTION_SYNCHRONIZER_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_PRODUCTION_SYNCHRONIZER_MOCK_HPP

#include "verification/production/production_synchronizer.hpp"

#include <gmock/gmock.h>

namespace sgns::verification {

  class ProductionSynchronizerMock : public ProductionSynchronizer {
   public:
    MOCK_METHOD4(request,
                 void(const primitives::BlockId &,
                      const primitives::BlockHash &,
                      primitives::AuthorityIndex,
                      const BlocksHandler &));
    MOCK_METHOD0(GetName, std::string());
  };

}  // namespace sgns::verification

#endif  // SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_PRODUCTION_SYNCHRONIZER_MOCK_HPP
