
#ifndef SUPERGENIUS_SRC_API_SERVICE_EXTRINSIC_REQUEST_SUBMIT_EXTRINSIC_HPP
#define SUPERGENIUS_SRC_API_SERVICE_EXTRINSIC_REQUEST_SUBMIT_EXTRINSIC_HPP

#include <jsonrpc-lean/request.h>

#include "api/service/author/author_api.hpp"
#include "primitives/extrinsic.hpp"

namespace sgns::api::author::request {

  class SubmitExtrinsic final {
   public:
    explicit SubmitExtrinsic(std::shared_ptr<AuthorApi> api)
        : api_(std::move(api)){};

    outcome::result<void> init(const jsonrpc::Request::Parameters &params);

    outcome::result<base::Hash256> execute();

   private:
    std::shared_ptr<AuthorApi> api_;
    primitives::Extrinsic extrinsic_;
  };

}  // namespace sgns::api::author::request

#endif  // SUPERGENIUS_SRC_API_SERVICE_EXTRINSIC_REQUEST_SUBMIT_EXTRINSIC_HPP
