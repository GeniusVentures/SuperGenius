
#ifndef SUPERGENIUS_AUTHORITY_UPDATE_OBSERVER_MOCK
#define SUPERGENIUS_AUTHORITY_UPDATE_OBSERVER_MOCK

#include "verification/authority/authority_update_observer.hpp"

#include <gmock/gmock.h>

namespace sgns::authority {
  struct AuthorityUpdateObserverMock
      : public AuthorityUpdateObserver {
    MOCK_METHOD3(onVerification,
                 outcome::result<void>(const primitives::VerificationEngineId &,
                                       const primitives::BlockInfo &,
                                       const primitives::Verification &));

    MOCK_METHOD0(GetName, std::string());
  };
}  // namespace sgns::authority

#endif  // SUPERGENIUS_AUTHORITY_UPDATE_OBSERVER_MOCK
