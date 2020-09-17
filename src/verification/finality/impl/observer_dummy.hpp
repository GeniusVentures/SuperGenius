
#ifndef SUPERGENIUS_SRC_VERIFICATION_FINALITY_IMPL_OBSERVER_DUMMY_HPP
#define SUPERGENIUS_SRC_VERIFICATION_FINALITY_IMPL_OBSERVER_DUMMY_HPP

#include "base/logger.hpp"
#include "verification/finality/observer.hpp"

namespace sgns::verification::finality {

  // Will be replaced with real implementation when finality is merged
  class ObserverDummy : public Observer {
   public:
    ~ObserverDummy() override = default;

    ObserverDummy();

    void onPrecommit(Precommit pc) override;

    void onPrevote(Prevote pv) override;

    void onPrimaryPropose(PrimaryPropose pv) override;

   private:
    base::Logger logger_;
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_SRC_VERIFICATION_FINALITY_IMPL_OBSERVER_DUMMY_HPP
