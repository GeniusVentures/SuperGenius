#ifndef SUPERGENIUS_SRC_NETWORK_EXTRINSIC_OBSERVER_HPP
#define SUPERGENIUS_SRC_NETWORK_EXTRINSIC_OBSERVER_HPP

// #include "api/service/author/author_api.hpp"
#include "base/blob.hpp"
#include "outcome/outcome.hpp"
#include "primitives/extrinsic.hpp"
#include "singleton/IComponent.hpp"

namespace sgns::network {
  class ExtrinsicObserver : public IComponent {
   public:
    virtual ~ExtrinsicObserver() = default;

    virtual outcome::result<base::Hash256> onTxMessage(
        const primitives::Extrinsic &extrinsic) = 0;
  };

}

#endif
