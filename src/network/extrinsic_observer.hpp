

#ifndef SUPERGENIUS_SRC_NETWORK_EXTRINSIC_OBSERVER_HPP
#define SUPERGENIUS_SRC_NETWORK_EXTRINSIC_OBSERVER_HPP

#include "api/service/author/author_api.hpp"
#include "base/blob.hpp"
#include "base/outcome.hpp"
#include "primitives/extrinsic.hpp"

namespace sgns::network {

  class ExtrinsicObserver {
   public:
    virtual ~ExtrinsicObserver() = default;

    virtual outcome::result<base::Hash256> onTxMessage(
        const primitives::Extrinsic &extrinsic) = 0;
  };

}  // namespace sgns::network

#endif  // SUPERGENIUS_SRC_NETWORK_EXTRINSIC_OBSERVER_HPP
