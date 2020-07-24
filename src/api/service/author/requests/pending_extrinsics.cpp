#include "api/service/author/requests/pending_extrinsics.hpp"

namespace sgns::api::author::request {

  outcome::result<void> PendingExtrinsics::init(
      const jsonrpc::Request::Parameters &params) {
    return outcome::success();
  }

  outcome::result<std::vector<primitives::Extrinsic>>
  PendingExtrinsics::execute() {
    return api_->pendingExtrinsics();
  }

}  // namespace sgns::api::author::request
