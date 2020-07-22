
#ifndef SUPERGENIUS_SRC_VERIFICATION_FINALITY_IMPL_SYNCING_ROUND_OBSERVER_HPP
#define SUPERGENIUS_SRC_VERIFICATION_FINALITY_IMPL_SYNCING_ROUND_OBSERVER_HPP

#include "verification/finality/round_observer.hpp"

#include "base/logger.hpp"
#include "verification/finality/environment.hpp"

namespace sgns::verification::finality {

  /**
   * Observer of finality messages for syncing nodes
   */
  class SyncingRoundObserver : public RoundObserver {
   public:
    ~SyncingRoundObserver() override = default;
    SyncingRoundObserver(std::shared_ptr<Environment> environment);

    void onFinalize(const Fin &f) override;

    void onVoteMessage(const VoteMessage &msg) override;

   private:
    std::shared_ptr<Environment> environment_;
    base::Logger logger_;
  };

}  // namespace sgns::verification::finality

#endif  // SUPERGENIUS_SRC_VERIFICATION_FINALITY_IMPL_SYNCING_ROUND_OBSERVER_HPP
