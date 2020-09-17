
#ifndef SUPERGENIUS_SRC_API_STATE_API_HPP
#define SUPERGENIUS_SRC_API_STATE_API_HPP

#include <boost/optional.hpp>

#include "api/service/api_service.hpp"
#include "base/buffer.hpp"
#include "outcome/outcome.hpp"
#include "primitives/common.hpp"
#include "primitives/version.hpp"

namespace sgns::api {

  class StateApi {
   public:
    virtual ~StateApi() = default;

    virtual void setApiService(
        const std::shared_ptr<api::ApiService> &api_service) = 0;

    virtual outcome::result<base::Buffer> getStorage(
        const base::Buffer &key) const = 0;
    virtual outcome::result<base::Buffer> getStorage(
        const base::Buffer &key, const primitives::BlockHash &at) const = 0;
    virtual outcome::result<primitives::Version> getRuntimeVersion(
        const boost::optional<primitives::BlockHash> &at) const = 0;
    virtual outcome::result<uint32_t> subscribeStorage(
        const std::vector<base::Buffer> &keys) = 0;
    virtual outcome::result<void> unsubscribeStorage(
        const std::vector<uint32_t> &subscription_id) = 0;
  };

}  // namespace sgns::api

#endif  // SUPERGENIUS_SRC_API_STATE_API_HPP
