
#ifndef SUPERGENIUS_SRC_API_SERVICE_EXTRINSIC_REQUEST_PENDING_EXTRINSICS_HPP
#define SUPERGENIUS_SRC_API_SERVICE_EXTRINSIC_REQUEST_PENDING_EXTRINSICS_HPP

#include <jsonrpc-lean/request.h>

#include "api/service/author/author_api.hpp"
#include "primitives/extrinsic.hpp"

namespace sgns::api::author::request {

  class PendingExtrinsics final {
   public:
    explicit PendingExtrinsics(std::shared_ptr<AuthorApi> api)
        : api_(std::move(api)){};

    outcome::result<void> init(const jsonrpc::Request::Parameters &params);

    outcome::result<std::vector<primitives::Extrinsic>> execute();

   private:
    std::shared_ptr<AuthorApi> api_;
  };

}  // namespace sgns::api::author::request

#endif  // SUPERGENIUS_SRC_API_SERVICE_EXTRINSIC_REQUEST_PENDING_EXTRINSICS_HPP
