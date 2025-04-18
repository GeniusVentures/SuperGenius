

#ifndef SUPERGENIUS_TEST_MOCK_SRC_RUNTIME_PRODUCTION_API_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_SRC_RUNTIME_PRODUCTION_API_MOCK_HPP

#include "runtime/production_api.hpp"

#include <gmock/gmock.h>

namespace sgns::runtime {

  class ProductionApiMock : public ProductionApi {
   public:
    // TODO: Disabled testing, failed to compile 
//    MOCK_METHOD0(configuration,
//                 outcome::result<primitives::ProductionConfiguration>());
  };

}  // namespace sgns::runtime

#endif  // SUPERGENIUS_TEST_MOCK_SRC_RUNTIME_PRODUCTION_API_MOCK_HPP
