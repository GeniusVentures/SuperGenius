

#ifndef SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_PRODUCTION_EPOCH_STORAGE_MOCK_HPP
#define SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_PRODUCTION_EPOCH_STORAGE_MOCK_HPP

#include "verification/production/epoch_storage.hpp"

#include <gmock/gmock.h>

namespace sgns::verification {

  class EpochStorageMock : public EpochStorage {
   public:
    MOCK_METHOD2(addEpochDescriptor,
                 outcome::result<void>(EpochIndex,
                                       const NextEpochDescriptor &));
    MOCK_CONST_METHOD1(
        getEpochDescriptor,
        outcome::result<NextEpochDescriptor>(EpochIndex epoch_number));
  };

}  // namespace sgns::verification

#endif  // SUPERGENIUS_TEST_MOCK_SRC_VERIFICATION_PRODUCTION_EPOCH_STORAGE_MOCK_HPP
