#ifndef SUPERGENIUS_AUTHORITY_UPDATE_OBSERVER
#define SUPERGENIUS_AUTHORITY_UPDATE_OBSERVER

#include "outcome/outcome.hpp"

#include "primitives/common.hpp"
#include "primitives/digest.hpp"
#include "singleton/IComponent.hpp"

namespace sgns::authority {
  class AuthorityUpdateObserver : public IComponent {
   public:
       ~AuthorityUpdateObserver() override = default;

       /**
     * Processes verification message in block digest
     * @param message
     * @return failure or nothing
     */
    virtual outcome::result<void> onVerification(
        const primitives::VerificationEngineId &engine_id,
        const primitives::BlockInfo &block,
        const primitives::Verification &message) = 0;
  };
}  // namespace sgns::authority

#endif  // SUPERGENIUS_AUTHORITY_UPDATE_OBSERVER
