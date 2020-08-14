
#include "verification/finality/impl/syncing_round_observer.hpp"

namespace sgns::verification::finality {

  SyncingRoundObserver::SyncingRoundObserver(
      std::shared_ptr<verification::finality::Environment> environment)
      : environment_{std::move(environment)},
        logger_{base::createLogger("SyncingRoundObserver")} {}

  void SyncingRoundObserver::onVoteMessage(const VoteMessage &msg) {
    // do nothing as syncing node does not care about vote messages
  }

  void SyncingRoundObserver::onFinalize(const Fin &f) {
    if (auto fin_res =
            environment_->finalize(f.vote.block_hash, f.justification);
        ! fin_res) {
      logger_->error("Could not finalize block with hash {}. Reason: {}",
                     f.vote.block_hash.toHex(),
                     fin_res.error().message());
      return;
    }
  }

}  // namespace sgns::verification::finality
