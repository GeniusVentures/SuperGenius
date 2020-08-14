

#include "verification/production/impl/syncing_production_observer.hpp"

namespace sgns::verification {

  SyncingProductionObserver::SyncingProductionObserver(
      std::shared_ptr<verification::BlockExecutor> block_executor)
      : block_executor_{std::move(block_executor)} {}

  void SyncingProductionObserver::onBlockAnnounce(
      const network::BlockAnnounce &announce) {
    block_executor_->processNextBlock(announce.header, [](auto &) {});
  }

}  // namespace sgns::verification
