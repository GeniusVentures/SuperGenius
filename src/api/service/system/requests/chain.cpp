#include "api/service/system/requests/chain.hpp"

namespace sgns::api::system::request {

  Chain::Chain(std::shared_ptr<SystemApi> api) : api_(std::move(api)) {
    BOOST_ASSERT(api_ != nullptr);
  }

  outcome::result<void> Chain::init(
      const jsonrpc::Request::Parameters &params) {
    if (!params.empty()) {
      throw jsonrpc::InvalidParametersFault("Method should not have params");
    }
    return outcome::success();
  }

  outcome::result<std::string> Chain::execute() {
    return api_->getConfig()->chainType();
  }

}  // namespace sgns::api::system::request
