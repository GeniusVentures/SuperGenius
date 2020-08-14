
#include "network/impl/extrinsic_observer_impl.hpp"

namespace sgns::network {

  ExtrinsicObserverImpl::ExtrinsicObserverImpl(
      std::shared_ptr<api::AuthorApi> api)
      : api_(std::move(api)) {
    BOOST_ASSERT(api_);
  }

  outcome::result<base::Hash256> ExtrinsicObserverImpl::onTxMessage(
      const primitives::Extrinsic &extrinsic) {
    return api_->submitExtrinsic(extrinsic);
  }

}  // namespace sgns::network
