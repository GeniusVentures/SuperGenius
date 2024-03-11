
#ifndef SUPERGENIUS_SRC_API_EXTRINSIC_EXTRINSIC_API_HPP
#define SUPERGENIUS_SRC_API_EXTRINSIC_EXTRINSIC_API_HPP

#include "base/blob.hpp"
#include "primitives/extrinsic.hpp"
#include "primitives/author_api_primitives.hpp"
#include "integration/IComponent.hpp"

namespace sgns::api {
  class AuthorApi : public IComponent {
   protected:
    using Hash256 = base::Hash256;
    using Buffer = base::Buffer;
    using Extrinsic = primitives::Extrinsic;
    // using Metadata = primitives::Metadata;
    using Subscriber = primitives::Subscriber;
    using SubscriptionId = primitives::SubscriptionId;
    using ExtrinsicKey = primitives::ExtrinsicKey;

   public:
    virtual ~AuthorApi() = default;
    /**
     * @brief validates and sends extrinsic to transaction pool
     * @param bytes encoded extrinsic
     * @return hash of successfully validated extrinsic
     * or error if state is invalid or unknown
     */
    virtual outcome::result<Hash256> submitExtrinsic(
        const Extrinsic &extrinsic) = 0;

    /**
     * @return collection of pending extrinsics
     */
    virtual outcome::result<std::vector<Extrinsic>> pendingExtrinsics() = 0;

    // TODO(yuraz): will be documented later (no task yet)
    virtual outcome::result<std::vector<Hash256>> removeExtrinsic(
        const std::vector<ExtrinsicKey> &keys) = 0;
  };
}  // namespace sgns::api

#endif  // SUPERGENIUS_SRC_API_EXTRINSIC_EXTRINSIC_API_HPP
