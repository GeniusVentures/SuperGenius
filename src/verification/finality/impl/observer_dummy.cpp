
#include "verification/finality/impl/observer_dummy.hpp"

namespace sgns::verification::finality {

  ObserverDummy::ObserverDummy()
      : logger_{base::createLogger("ObserverDummy")} {}

  void ObserverDummy::onPrecommit(Precommit) {
    logger_->error("onPrecommit is not implemented");
  }

  void ObserverDummy::onPrevote(Prevote) {
    logger_->error("onPrevote is not implemented");
  }

  void ObserverDummy::onPrimaryPropose(PrimaryPropose) {
    logger_->error("onPrimaryPropose is not implemented");
  }

}  // namespace sgns::verification::finality
