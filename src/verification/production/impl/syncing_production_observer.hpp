

#ifndef SUPERGENIUS_SRC_VERIFICATION_PRODUCTION_IMPL_SYNCING_PRODUCTION_OBSERVER_HPP
#define SUPERGENIUS_SRC_VERIFICATION_PRODUCTION_IMPL_SYNCING_PRODUCTION_OBSERVER_HPP

#include "network/production_observer.hpp"

#include "verification/production/impl/block_executor.hpp"

namespace sgns::verification {

  // Production observer for syncing node
  class SyncingProductionObserver : public network::ProductionObserver {
   public:
    ~SyncingProductionObserver() override = default;

    SyncingProductionObserver(
        std::shared_ptr<consensus::BlockExecutor> block_executor);

    void onBlockAnnounce(const network::BlockAnnounce &announce) override;

   private:
    std::shared_ptr<consensus::BlockExecutor> block_executor_;
  };

}  // namespace sgns::verification

#endif  // SUPERGENIUS_SRC_VERIFICATION_PRODUCTION_IMPL_SYNCING_PRODUCTION_OBSERVER_HPP
