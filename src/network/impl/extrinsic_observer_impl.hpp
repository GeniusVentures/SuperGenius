
#ifndef SUPERGENIUS_CORE_NETWORK_IMPL_EXTRINSIC_OBSERVER_IMPL_HPP
#define SUPERGENIUS_CORE_NETWORK_IMPL_EXTRINSIC_OBSERVER_IMPL_HPP

#include "network/extrinsic_observer.hpp"

#include "api/service/author/author_api.hpp"
#include "base/logger.hpp"

namespace sgns::network {

  class ExtrinsicObserverImpl : public ExtrinsicObserver {
   public:
    explicit ExtrinsicObserverImpl(std::shared_ptr<api::AuthorApi> api);
    ~ExtrinsicObserverImpl() override = default;

    outcome::result<base::Hash256> onTxMessage(
        const primitives::Extrinsic &extrinsic) override;

   private:
    std::shared_ptr<api::AuthorApi> api_;
    base::Logger logger_;
  };

}  // namespace sgns::network

#endif  // SUPERGENIUS_CORE_NETWORK_IMPL_EXTRINSIC_OBSERVER_IMPL_HPP
